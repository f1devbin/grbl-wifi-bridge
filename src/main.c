// GRBL Wi-Fi Bridge: ESP32-S3 USB host <-> raw TCP (port 23) + web panel.
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "lwip/sockets.h"
#include "mdns.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "driver/uart.h"
#include "usb/cdc_acm_host.h"
#include "usb/usb_host.h"
#include "usb/vcp_ch34x.h"
#include "usb/vcp_cp210x.h"
#include "usb/vcp_ftdi.h"

#define FW_VERSION "1.5.0"
#define FW_NAME "GRBL Wi-Fi Bridge"
#define HOSTNAME "grbl-bridge"
#define AP_SSID "GRBL-Bridge-setup"
#define AP_PASS "grblbridge"
#define AP_IP_U32 0x0104A8C0  // 192.168.4.1 in network byte order
#define AP_FALLBACK_US (30LL * 1000000)
#define STA_RETRY_US (5LL * 1000000)
#define LED_GPIO 48
#define LED_LEVEL 24
#define LOG_SIZE 8192
#define RX_STREAM_SIZE 16384
#define DEFAULT_BAUD 115200
#define DEFAULT_PORT 23
#define STREAMING_WINDOW_US (10LL * 1000000)
#define STATUS_POLL_US (2LL * 1000000)  // '?' rate while no sender is connected
#define WIFI_TX_POWER_QDBM 60  // 15 dBm: avoids brownout resets on weak USB power, plenty for a home network

#define EV_USB_NEW BIT0
#define EV_USB_GONE BIT1

static const char *TAG = "bridge";

typedef struct {
  uint32_t baud;
  uint16_t port;
  bool safety;
} config_t;

static config_t g_cfg = {DEFAULT_BAUD, DEFAULT_PORT, true};
static char g_ssid[33];
static char g_pass[65];
static bool g_has_creds;

static volatile bool g_wifi_ok;
static volatile bool g_wifi_started;
static volatile bool g_ap_active;
static volatile bool g_drop_client;
static volatile bool g_restart;
static char g_ip[16] = "-";
static esp_timer_handle_t g_sta_retry_timer;
static esp_timer_handle_t g_ap_fallback_timer;

static SemaphoreHandle_t g_usb_mutex;
static TaskHandle_t g_usb_task;
static StreamBufferHandle_t g_rx_stream;
static cdc_acm_dev_hdl_t g_cdc;
static volatile bool g_usb_ready;
static volatile uint16_t g_usb_vid, g_usb_pid;
static uint16_t g_new_vid, g_new_pid;
static const char *g_usb_driver = "-";

static volatile int g_client = -1;
static char g_client_ip[16] = "-";
static volatile uint32_t g_tx_bytes, g_rx_bytes;
static volatile int64_t g_last_gcode_us = -STREAMING_WINDOW_US;
static volatile int64_t g_last_poll_us;
static volatile bool g_safety_tripped;
static volatile bool g_show_status_once;

static portMUX_TYPE g_lock = portMUX_INITIALIZER_UNLOCKED;
static char g_log[LOG_SIZE];
static uint32_t g_log_total;
static char g_state[16] = "-";

// ---------------------------------------------------------------- log & state

static void log_append(const char *s, size_t n) {
  portENTER_CRITICAL(&g_lock);
  for (size_t i = 0; i < n; i++) g_log[g_log_total++ % LOG_SIZE] = s[i];
  portEXIT_CRITICAL(&g_lock);
}

static void log_msg(const char *fmt, ...) {
  char buf[200];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
  va_end(ap);
  if (n < 0) return;
  if (n > (int)sizeof(buf) - 2) n = sizeof(buf) - 2;
  ESP_LOGI(TAG, "%s", buf);
  buf[n++] = '\n';
  log_append("# ", 2);
  log_append(buf, n);
}

static void get_state(char *out, size_t size) {
  portENTER_CRITICAL(&g_lock);
  strlcpy(out, g_state, size);
  portEXIT_CRITICAL(&g_lock);
}

static void set_state(const char *s) {
  portENTER_CRITICAL(&g_lock);
  strlcpy(g_state, s, sizeof(g_state));
  portEXIT_CRITICAL(&g_lock);
}

// Splits GRBL output into lines: status reports update the state, the rest goes to the console log.
static void parse_rx(const uint8_t *data, size_t len) {
  static char line[160];
  static size_t n;
  for (size_t i = 0; i < len; i++) {
    const char c = (char)data[i];
    if (c == '\r') continue;
    if (c != '\n') {
      if (n < sizeof(line) - 1) line[n++] = c;
      continue;
    }
    line[n] = '\0';
    if (line[0] == '<') {
      char st[16];
      size_t k = 0;
      for (const char *p = line + 1; *p && *p != '|' && *p != '>' && *p != ':' && k < sizeof(st) - 1; p++) st[k++] = *p;
      st[k] = '\0';
      set_state(st);
      // A reset after an emergency stop ends with Idle; the panel no longer shows the stop as active.
      if (g_safety_tripped && strcmp(st, "Idle") == 0) g_safety_tripped = false;
      if (g_show_status_once) {
        g_show_status_once = false;
        log_append(line, n);
        log_append("\n", 1);
      }
    } else if (n > 0 && !(g_client >= 0 && strcmp(line, "ok") == 0)) {
      if (strncmp(line, "ALARM", 5) == 0) set_state("Alarm");
      log_append(line, n);
      log_append("\n", 1);
    }
    n = 0;
  }
}

// ---------------------------------------------------------------- config

static void load_config(void) {
  nvs_handle_t h;
  if (nvs_open("wifi", NVS_READONLY, &h) == ESP_OK) {
    size_t a = sizeof(g_ssid), b = sizeof(g_pass);
    g_has_creds = nvs_get_str(h, "ssid", g_ssid, &a) == ESP_OK && nvs_get_str(h, "pass", g_pass, &b) == ESP_OK &&
                  g_ssid[0] != '\0';
    nvs_close(h);
  }
  if (nvs_open("cfg", NVS_READONLY, &h) == ESP_OK) {
    uint8_t safety;
    nvs_get_u32(h, "baud", &g_cfg.baud);
    nvs_get_u16(h, "port", &g_cfg.port);
    if (nvs_get_u8(h, "safety", &safety) == ESP_OK) g_cfg.safety = safety != 0;
    nvs_close(h);
  }
}

static esp_err_t save_config(void) {
  nvs_handle_t h;
  esp_err_t err = nvs_open("cfg", NVS_READWRITE, &h);
  if (err != ESP_OK) return err;
  err = nvs_set_u32(h, "baud", g_cfg.baud);
  if (err == ESP_OK) err = nvs_set_u16(h, "port", g_cfg.port);
  if (err == ESP_OK) err = nvs_set_u8(h, "safety", g_cfg.safety ? 1 : 0);
  if (err == ESP_OK) err = nvs_commit(h);
  nvs_close(h);
  return err;
}

static esp_err_t save_wifi(const char *ssid, const char *pass) {
  nvs_handle_t h;
  esp_err_t err = nvs_open("wifi", NVS_READWRITE, &h);
  if (err != ESP_OK) return err;
  err = nvs_set_str(h, "ssid", ssid);
  if (err == ESP_OK) err = nvs_set_str(h, "pass", pass);
  if (err == ESP_OK) err = nvs_commit(h);
  nvs_close(h);
  return err;
}

// ---------------------------------------------------------------- USB

static esp_err_t usb_write(const void *data, size_t len) {
  if (xSemaphoreTake(g_usb_mutex, pdMS_TO_TICKS(3000)) != pdTRUE) return ESP_ERR_TIMEOUT;
  esp_err_t err = ESP_ERR_INVALID_STATE;
  if (g_cdc) err = cdc_acm_host_data_tx_blocking(g_cdc, (const uint8_t *)data, len, 2000);
  xSemaphoreGive(g_usb_mutex);
  return err;
}

static void apply_line_coding(void) {
  cdc_acm_line_coding_t lc = {.dwDTERate = g_cfg.baud, .bCharFormat = 0, .bParityType = 0, .bDataBits = 8};
  if (xSemaphoreTake(g_usb_mutex, pdMS_TO_TICKS(3000)) != pdTRUE) return;
  if (g_cdc) {
    esp_err_t err = cdc_acm_host_line_coding_set(g_cdc, &lc);
    if (err != ESP_OK) log_msg("Baud rate not applied: %s", esp_err_to_name(err));
  }
  xSemaphoreGive(g_usb_mutex);
}

// Feed hold first so the axes decelerate, then soft reset, which also switches the laser or spindle off.
static void emergency_stop(const char *reason) {
  if (!g_usb_ready) return;
  const uint8_t hold = '!';
  const uint8_t reset = 0x18;
  usb_write(&hold, 1);
  vTaskDelay(pdMS_TO_TICKS(300));
  usb_write(&reset, 1);
  g_safety_tripped = true;
  log_msg("STOP: %s", reason);
}

static bool on_cdc_data(const uint8_t *data, size_t len, void *arg) {
  g_rx_bytes += len;
  if (g_client >= 0) xStreamBufferSend(g_rx_stream, data, len, pdMS_TO_TICKS(200));
  parse_rx(data, len);
  return true;
}

static void on_cdc_event(const cdc_acm_host_dev_event_data_t *event, void *ctx) {
  if (event->type == CDC_ACM_HOST_DEVICE_DISCONNECTED) {
    xTaskNotify(g_usb_task, EV_USB_GONE, eSetBits);
  } else if (event->type == CDC_ACM_HOST_ERROR) {
    ESP_LOGW(TAG, "CDC error %d", event->data.error);
  }
}

static void on_new_usb_device(usb_device_handle_t dev) {
  const usb_device_desc_t *desc;
  if (usb_host_get_device_descriptor(dev, &desc) != ESP_OK) return;
  g_new_vid = desc->idVendor;
  g_new_pid = desc->idProduct;
  xTaskNotify(g_usb_task, EV_USB_NEW, eSetBits);
}

static void open_usb_device(uint16_t vid, uint16_t pid) {
  const cdc_acm_host_device_config_t cfg = {
      .connection_timeout_ms = 3000,
      .out_buffer_size = 512,
      .in_buffer_size = 512,
      .event_cb = on_cdc_event,
      .data_cb = on_cdc_data,
      .user_arg = NULL,
  };
  cdc_acm_dev_hdl_t hdl = NULL;
  esp_err_t err;
  const char *driver;

  if (vid == NANJING_QINHENG_MICROE_VID && (pid == CH340_PID || pid == CH340_PID_1 || pid == CH341_PID)) {
    driver = "CH34x";
    err = ch34x_vcp_open(pid, 0, &cfg, &hdl);
  } else if (vid == SILICON_LABS_VID) {
    driver = "CP210x";
    err = cp210x_vcp_open(pid, 0, &cfg, &hdl);
  } else if (vid == FTDI_VID) {
    driver = "FTDI";
    err = ftdi_vcp_open(pid, 0, &cfg, &hdl);
  } else {
    driver = "CDC-ACM";
    err = cdc_acm_host_open(vid, pid, 0, &cfg, &hdl);
  }
  if (err != ESP_OK) {
    log_msg("USB %04x:%04x: %s open failed: %s", vid, pid, driver, esp_err_to_name(err));
    return;
  }

  xSemaphoreTake(g_usb_mutex, portMAX_DELAY);
  g_cdc = hdl;
  xSemaphoreGive(g_usb_mutex);
  apply_line_coding();
  // Most GRBL boards reset on DTR like on a PC connection; the welcome banner then appears in the console.
  cdc_acm_host_set_control_line_state(hdl, true, false);

  g_usb_vid = vid;
  g_usb_pid = pid;
  g_usb_driver = driver;
  g_usb_ready = true;
  set_state("-");
  log_msg("Machine connected: USB %04x:%04x, %s, %lu baud", vid, pid, driver, (unsigned long)g_cfg.baud);
}

static void usb_device_task(void *arg) {
  while (true) {
    uint32_t ev = 0;
    xTaskNotifyWait(0, UINT32_MAX, &ev, portMAX_DELAY);
    if (ev & EV_USB_GONE) {
      xSemaphoreTake(g_usb_mutex, portMAX_DELAY);
      if (g_cdc) cdc_acm_host_close(g_cdc);
      g_cdc = NULL;
      xSemaphoreGive(g_usb_mutex);
      g_usb_ready = false;
      g_drop_client = true;
      g_usb_driver = "-";
      set_state("-");
      log_msg("Machine disconnected");
    }
    if ((ev & EV_USB_NEW) && !g_usb_ready) open_usb_device(g_new_vid, g_new_pid);
  }
}

static void usb_lib_task(void *arg) {
  while (true) {
    uint32_t flags;
    usb_host_lib_handle_events(portMAX_DELAY, &flags);
    if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) usb_host_device_free_all();
  }
}

static void start_usb(void) {
  const usb_host_config_t host_cfg = {.skip_phy_setup = false, .intr_flags = ESP_INTR_FLAG_LEVEL1};
  ESP_ERROR_CHECK(usb_host_install(&host_cfg));
  xTaskCreate(usb_lib_task, "usb_lib", 4096, NULL, 10, NULL);
  xTaskCreate(usb_device_task, "usb_dev", 4096, NULL, 6, &g_usb_task);

  const cdc_acm_host_driver_config_t drv_cfg = {
      .driver_task_stack_size = 4096,
      .driver_task_priority = 9,
      .xCoreID = 0,
      .new_dev_cb = on_new_usb_device,
  };
  ESP_ERROR_CHECK(cdc_acm_host_install(&drv_cfg));
}

// ---------------------------------------------------------------- TCP bridge

static bool sock_is_ap(int fd) {
  struct sockaddr_storage addr;
  socklen_t len = sizeof(addr);
  if (getsockname(fd, (struct sockaddr *)&addr, &len) != 0) return false;
  if (addr.ss_family == AF_INET) return ((struct sockaddr_in *)&addr)->sin_addr.s_addr == AP_IP_U32;
#if LWIP_IPV6
  if (addr.ss_family == AF_INET6) {
    const uint32_t *w = ((struct sockaddr_in6 *)&addr)->sin6_addr.un.u32_addr;
    return w[3] == AP_IP_U32;
  }
#endif
  return false;
}

static bool grbl_running(void) {
  char st[16];
  get_state(st, sizeof(st));
  return strcmp(st, "Run") == 0 || strcmp(st, "Jog") == 0 || strcmp(st, "Hold") == 0;
}

static bool machine_busy(void) {
  return grbl_running() || esp_timer_get_time() - g_last_gcode_us < STREAMING_WINDOW_US;
}

static void close_client(const char *reason) {
  if (g_client < 0) return;
  close(g_client);
  g_client = -1;
  log_msg("Client %s disconnected: %s", g_client_ip, reason);
  if (g_cfg.safety && machine_busy()) emergency_stop("client lost during a job");
}

static bool send_all(int fd, const uint8_t *buf, size_t len) {
  while (len > 0) {
    const int n = send(fd, buf, len, 0);
    if (n <= 0) return false;
    buf += n;
    len -= (size_t)n;
  }
  return true;
}

static void accept_client(int listen_fd) {
  struct sockaddr_storage addr;
  socklen_t len = sizeof(addr);
  const int fd = accept(listen_fd, (struct sockaddr *)&addr, &len);
  if (fd < 0) return;

  if (g_client >= 0 || sock_is_ap(fd)) {
    // One sender at a time; no machine control from the setup access point.
    close(fd);
    return;
  }

  const int on = 1, idle = 3, intvl = 1, cnt = 3;
  setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof(on));
  setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
  setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl));
  setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt));
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
  const struct timeval snd_to = {.tv_sec = 3};
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &snd_to, sizeof(snd_to));

  char ip[46] = "?";
  if (addr.ss_family == AF_INET) {
    inet_ntop(AF_INET, &((struct sockaddr_in *)&addr)->sin_addr, ip, sizeof(ip));
#if LWIP_IPV6
  } else if (addr.ss_family == AF_INET6) {
    const uint32_t *w = ((struct sockaddr_in6 *)&addr)->sin6_addr.un.u32_addr;
    inet_ntop(AF_INET, &w[3], ip, sizeof(ip));
#endif
  }
  strlcpy(g_client_ip, ip, sizeof(g_client_ip));

  uint8_t junk[256];
  while (xStreamBufferReceive(g_rx_stream, junk, sizeof(junk), 0) > 0) {
  }
  g_safety_tripped = false;
  g_drop_client = false;
  g_client = fd;
  log_msg("Client %s connected", g_client_ip);
}

static void bridge_task(void *arg) {
  const int listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  const int on = 1;
  setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
  struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(g_cfg.port), .sin_addr.s_addr = htonl(INADDR_ANY)};
  if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(listen_fd, 2) != 0) {
    log_msg("TCP port %u unavailable", g_cfg.port);
    vTaskDelete(NULL);
    return;
  }

  static uint8_t buf[1024];
  bool warned_no_usb = false;
  while (true) {
    if (g_drop_client) {
      g_drop_client = false;
      close_client(g_wifi_ok ? "machine unplugged" : "Wi-Fi lost");
    }

    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(listen_fd, &rfds);
    int maxfd = listen_fd;
    const int client = g_client;
    if (client >= 0) {
      FD_SET(client, &rfds);
      if (client > maxfd) maxfd = client;
    }
    struct timeval tv = {.tv_sec = 0, .tv_usec = 20000};
    const int ready = select(maxfd + 1, &rfds, NULL, NULL, &tv);

    if (ready > 0 && FD_ISSET(listen_fd, &rfds)) accept_client(listen_fd);

    if (ready > 0 && client >= 0 && FD_ISSET(client, &rfds)) {
      const int n = recv(client, buf, sizeof(buf), 0);
      if (n <= 0) {
        close_client(n == 0 ? "closed by client" : "connection error");
      } else if (!g_usb_ready) {
        if (!warned_no_usb) log_msg("Data from client dropped: machine not connected");
        warned_no_usb = true;
      } else {
        warned_no_usb = false;
        for (int i = 0; i < n; i++) {
          if (buf[i] != '?' && buf[i] != '\r' && buf[i] != '\n') {
            g_last_gcode_us = esp_timer_get_time();
            break;
          }
        }
        if (usb_write(buf, (size_t)n) == ESP_OK) {
          g_tx_bytes += n;
        } else {
          log_msg("USB write failed");
        }
      }
    }

    size_t got;
    while ((got = xStreamBufferReceive(g_rx_stream, buf, sizeof(buf), 0)) > 0) {
      if (g_client >= 0 && !send_all(g_client, buf, got)) {
        close_client("send failed");
        break;
      }
    }

    // Without a sender nothing asks GRBL for its status, so the panel and the LED would stay blank.
    // Status reports are filtered out of the console, and polling stops as soon as a sender connects.
    if (g_usb_ready && g_client < 0 && esp_timer_get_time() - g_last_poll_us >= STATUS_POLL_US) {
      static const uint8_t query = '?';
      g_last_poll_us = esp_timer_get_time();
      usb_write(&query, 1);
    }
  }
}

// ---------------------------------------------------------------- Wi-Fi

static void start_ap(void) {
  if (g_ap_active) return;
  wifi_config_t ap = {0};
  strlcpy((char *)ap.ap.ssid, AP_SSID, sizeof(ap.ap.ssid));
  ap.ap.ssid_len = strlen(AP_SSID);
  strlcpy((char *)ap.ap.password, AP_PASS, sizeof(ap.ap.password));
  ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
  ap.ap.max_connection = 4;
  ap.ap.channel = 1;
  esp_wifi_set_mode(WIFI_MODE_APSTA);
  esp_wifi_set_config(WIFI_IF_AP, &ap);
  g_ap_active = true;
  log_msg("Setup access point %s started, http://192.168.4.1", AP_SSID);
}

static int ap_client_count(void) {
  wifi_sta_list_t list = {0};
  return esp_wifi_ap_get_sta_list(&list) == ESP_OK ? list.num : 0;
}

static void stop_ap_if_idle(void) {
  if (!g_ap_active || !g_wifi_ok || ap_client_count() > 0) return;
  esp_wifi_set_mode(WIFI_MODE_STA);
  g_ap_active = false;
  log_msg("Setup access point stopped");
}

static void on_sta_retry(void *arg) { esp_wifi_connect(); }

static void on_ap_fallback(void *arg) {
  if (!g_wifi_ok) start_ap();
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
    if (g_has_creds) esp_wifi_connect();
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    if (g_wifi_ok) log_msg("Wi-Fi lost");
    g_wifi_ok = false;
    g_drop_client = true;
    strlcpy(g_ip, "-", sizeof(g_ip));
    if (g_has_creds) esp_timer_start_once(g_sta_retry_timer, STA_RETRY_US);
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    const ip_event_got_ip_t *ev = (const ip_event_got_ip_t *)data;
    snprintf(g_ip, sizeof(g_ip), IPSTR, IP2STR(&ev->ip_info.ip));
    g_wifi_ok = true;
    log_msg("Wi-Fi %s: http://%s/ (http://%s.local/), TCP port %u", g_ssid, g_ip, HOSTNAME, g_cfg.port);
    stop_ap_if_idle();
  }
}

static void start_wifi(void) {
  esp_netif_t *sta = esp_netif_create_default_wifi_sta();
  esp_netif_create_default_wifi_ap();
  esp_netif_set_hostname(sta, HOSTNAME);

  const wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&init));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi_event, NULL));

  const esp_timer_create_args_t retry = {.callback = on_sta_retry, .name = "sta_retry"};
  const esp_timer_create_args_t fallback = {.callback = on_ap_fallback, .name = "ap_fallback"};
  ESP_ERROR_CHECK(esp_timer_create(&retry, &g_sta_retry_timer));
  ESP_ERROR_CHECK(esp_timer_create(&fallback, &g_ap_fallback_timer));

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  if (g_has_creds) {
    wifi_config_t sta_cfg = {0};
    strlcpy((char *)sta_cfg.sta.ssid, g_ssid, sizeof(sta_cfg.sta.ssid));
    strlcpy((char *)sta_cfg.sta.password, g_pass, sizeof(sta_cfg.sta.password));
    sta_cfg.sta.threshold.authmode = g_pass[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
  }
  ESP_ERROR_CHECK(esp_wifi_start());
  g_wifi_started = true;
  esp_wifi_set_max_tx_power(WIFI_TX_POWER_QDBM);
  esp_wifi_set_ps(WIFI_PS_NONE);  // Lower latency for streaming G-code.

  if (g_has_creds) {
    esp_timer_start_once(g_ap_fallback_timer, AP_FALLBACK_US);
  } else {
    start_ap();
  }
}

static void start_mdns(void) {
  if (mdns_init() != ESP_OK) return;
  mdns_hostname_set(HOSTNAME);
  mdns_instance_name_set(FW_NAME);
  mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
  mdns_service_add(NULL, "_telnet", "_tcp", g_cfg.port, NULL, 0);
}

// ---------------------------------------------------------------- web panel

// Web panel, embedded from src/web/index.html.
extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");

static int hex_value(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Extracts and URL-decodes one field of an application/x-www-form-urlencoded body.
static bool form_field(const char *body, const char *name, char *out, size_t out_size) {
  const size_t name_len = strlen(name);
  const char *p = body;
  while (p && *p) {
    if (strncmp(p, name, name_len) == 0 && p[name_len] == '=') {
      p += name_len + 1;
      size_t n = 0;
      while (*p && *p != '&') {
        if (n + 1 >= out_size) return false;
        char c = *p++;
        if (c == '+') {
          c = ' ';
        } else if (c == '%') {
          const int hi = p[0] ? hex_value(p[0]) : -1;
          const int lo = hi >= 0 && p[1] ? hex_value(p[1]) : -1;
          if (lo < 0) return false;
          c = (char)(hi * 16 + lo);
          p += 2;
        }
        out[n++] = c;
      }
      out[n] = '\0';
      return true;
    }
    p = strchr(p, '&');
    if (p) p++;
  }
  return false;
}

static int read_body(httpd_req_t *req, char *buf, size_t size) {
  if (req->content_len >= size) return -1;
  size_t got = 0;
  while (got < req->content_len) {
    const int n = httpd_req_recv(req, buf + got, req->content_len - got);
    if (n <= 0) return -1;
    got += (size_t)n;
  }
  buf[got] = '\0';
  return (int)got;
}

static void json_escape(const char *in, char *out, size_t size) {
  size_t n = 0;
  for (; *in && n + 7 < size; in++) {
    const unsigned char c = (unsigned char)*in;
    if (c == '"' || c == '\\') {
      out[n++] = '\\';
      out[n++] = (char)c;
    } else if (c < 0x20) {
      n += snprintf(out + n, size - n, "\\u%04x", c);
    } else {
      out[n++] = (char)c;
    }
  }
  out[n] = '\0';
}

static esp_err_t reply(httpd_req_t *req, const char *status, const char *text) {
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, "text/plain; charset=utf-8");
  return httpd_resp_sendstr(req, text);
}

static bool deny_on_ap(httpd_req_t *req) {
  if (!sock_is_ap(httpd_req_to_sockfd(req))) return false;
  reply(req, "403 Forbidden", "err_ap");
  return true;
}

static esp_err_t page_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  // EMBED_TXTFILES adds a terminating NUL that must not be sent.
  return httpd_resp_send(req, index_html_start, index_html_end - index_html_start - 1);
}

static esp_err_t status_handler(httpd_req_t *req) {
  char ssid[70], state[16], state_esc[40], json[768];
  json_escape(g_ssid, ssid, sizeof(ssid));
  get_state(state, sizeof(state));
  json_escape(state, state_esc, sizeof(state_esc));
  int rssi = 0;
  wifi_ap_record_t ap;
  if (g_wifi_ok && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) rssi = ap.rssi;
  snprintf(json, sizeof(json),
           "{\"ap\":%s,\"wifi\":%s,\"ssid\":\"%s\",\"rssi\":%d,\"ip\":\"%s\",\"usb\":%s,\"vid\":\"%04x\",\"pid\":\"%04x\","
           "\"driver\":\"%s\",\"state\":\"%s\",\"tripped\":%s,\"client\":%s,\"client_ip\":\"%s\",\"tx\":%lu,\"rx\":%lu,"
           "\"uptime\":%lld,\"version\":\"%s\",\"baud\":%lu,\"port\":%u,\"safety\":%s,\"host\":\"%s\",\"ap_ssid\":\"%s\"}",
           sock_is_ap(httpd_req_to_sockfd(req)) ? "true" : "false", g_wifi_ok ? "true" : "false", ssid, rssi, g_ip,
           g_usb_ready ? "true" : "false", g_usb_vid, g_usb_pid, g_usb_driver, state_esc,
           g_safety_tripped ? "true" : "false", g_client >= 0 ? "true" : "false", g_client_ip,
           (unsigned long)g_tx_bytes, (unsigned long)g_rx_bytes, esp_timer_get_time() / 1000000, FW_VERSION,
           (unsigned long)g_cfg.baud, g_cfg.port, g_cfg.safety ? "true" : "false", HOSTNAME, AP_SSID);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_sendstr(req, json);
}

static esp_err_t log_handler(httpd_req_t *req) {
  if (deny_on_ap(req)) return ESP_OK;
  char query[48], value[16];
  uint32_t since = 0;
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
      httpd_query_key_value(query, "since", value, sizeof(value)) == ESP_OK) {
    since = strtoul(value, NULL, 10);
  }

  char *out = malloc(LOG_SIZE + 16);
  if (!out) return reply(req, "500 Internal Server Error", "err_no_mem");
  portENTER_CRITICAL(&g_lock);
  const uint32_t total = g_log_total;
  uint32_t start = since > total ? 0 : since;
  if (total - start > LOG_SIZE) start = total - LOG_SIZE;
  int n = snprintf(out, 16, "%lu\n", (unsigned long)total);
  for (uint32_t i = start; i < total; i++) out[n++] = g_log[i % LOG_SIZE];
  portEXIT_CRITICAL(&g_lock);

  httpd_resp_set_type(req, "text/plain; charset=utf-8");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  const esp_err_t err = httpd_resp_send(req, out, n);
  free(out);
  return err;
}

static esp_err_t cmd_handler(httpd_req_t *req) {
  if (deny_on_ap(req)) return ESP_OK;
  char body[256];
  const int len = read_body(req, body, sizeof(body) - 1);
  if (len <= 0) return reply(req, "400 Bad Request", "err_empty");
  if (!g_usb_ready) return reply(req, "503 Service Unavailable", "err_no_usb");

  const bool feed_hold = len == 1 && body[0] == '!';
  if (g_client >= 0 && !feed_hold) {
    return reply(req, "409 Conflict", "err_busy");
  }

  esp_err_t err;
  if (len == 1 && (body[0] == '!' || body[0] == '~' || body[0] == '?')) {
    if (body[0] == '?') g_show_status_once = true;
    err = usb_write(body, 1);  // Realtime commands go without a newline.
  } else {
    body[len] = '\n';
    err = usb_write(body, (size_t)len + 1);
  }
  log_append("> ", 2);
  log_append(body, body[0] == '!' || body[0] == '~' || body[0] == '?' ? 1 : (size_t)len);
  log_append("\n", 1);
  return err == ESP_OK ? reply(req, "200 OK", "ok") : reply(req, "500 Internal Server Error", "err_write");
}

static esp_err_t stop_handler(httpd_req_t *req) {
  if (!g_usb_ready) return reply(req, "503 Service Unavailable", "err_no_usb");
  emergency_stop("STOP button in the web panel");
  return reply(req, "200 OK", "ok");
}

static esp_err_t settings_handler(httpd_req_t *req) {
  if (deny_on_ap(req)) return ESP_OK;
  char body[200], v[16];
  if (read_body(req, body, sizeof(body)) < 0) return reply(req, "400 Bad Request", "err_too_long");

  config_t cfg = g_cfg;
  if (form_field(body, "baud", v, sizeof(v))) cfg.baud = strtoul(v, NULL, 10);
  if (form_field(body, "port", v, sizeof(v))) cfg.port = (uint16_t)strtoul(v, NULL, 10);
  cfg.safety = form_field(body, "safety", v, sizeof(v));
  if (cfg.baud < 1200 || cfg.baud > 2000000 || cfg.port == 0 || cfg.port == 80) {
    return reply(req, "400 Bad Request", "err_bad_settings");
  }
  // A restart or a baud change in the middle of a job would leave the machine running unattended.
  if (grbl_running()) return reply(req, "409 Conflict", "err_job_running");
  if (g_client >= 0 && (cfg.baud != g_cfg.baud || cfg.port != g_cfg.port)) {
    return reply(req, "409 Conflict", "err_busy");
  }

  const bool port_changed = cfg.port != g_cfg.port;
  const bool baud_changed = cfg.baud != g_cfg.baud;
  g_cfg = cfg;
  if (save_config() != ESP_OK) return reply(req, "500 Internal Server Error", "err_save");
  if (baud_changed) apply_line_coding();
  log_msg("Settings saved: %lu baud, port %u, safety %s", (unsigned long)cfg.baud, cfg.port, cfg.safety ? "on" : "off");
  if (port_changed) {
    g_restart = true;
    return reply(req, "200 OK", "port_changed");
  }
  return reply(req, "200 OK", "ok");
}

static esp_err_t wifi_handler(httpd_req_t *req) {
  char body[300], ssid[33], pass[65] = "";
  if (read_body(req, body, sizeof(body)) < 0) return reply(req, "400 Bad Request", "err_too_long");
  const bool ok = form_field(body, "ssid", ssid, sizeof(ssid)) && ssid[0] && form_field(body, "pass", pass, sizeof(pass));
  const size_t plen = strlen(pass);
  if (!ok || (plen > 0 && plen < 8)) return reply(req, "400 Bad Request", "err_bad_wifi");
  if (grbl_running()) return reply(req, "409 Conflict", "err_job_running");
  if (save_wifi(ssid, pass) != ESP_OK) return reply(req, "500 Internal Server Error", "err_save");
  log_msg("Wi-Fi settings changed to %s, restarting", ssid);
  g_restart = true;
  return reply(req, "200 OK", "ok");
}

static esp_err_t update_handler(httpd_req_t *req) {
  if (deny_on_ap(req)) return ESP_OK;
  if (g_client >= 0 || machine_busy()) return reply(req, "409 Conflict", "err_job_running");

  const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
  esp_ota_handle_t ota;
  if (!part || esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &ota) != ESP_OK) {
    return reply(req, "500 Internal Server Error", "err_update_start");
  }
  char *buf = malloc(4096);
  if (!buf) {
    esp_ota_abort(ota);
    return reply(req, "500 Internal Server Error", "err_no_mem");
  }
  size_t left = req->content_len;
  esp_err_t err = left > 0 ? ESP_OK : ESP_ERR_INVALID_SIZE;
  while (left > 0 && err == ESP_OK) {
    const int n = httpd_req_recv(req, buf, left < 4096 ? left : 4096);
    if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
    if (n <= 0) {
      err = ESP_FAIL;
      break;
    }
    err = esp_ota_write(ota, buf, n);
    left -= (size_t)n;
  }
  free(buf);
  if (err == ESP_OK) err = esp_ota_end(ota);
  else esp_ota_abort(ota);
  if (err == ESP_OK) err = esp_ota_set_boot_partition(part);
  if (err != ESP_OK) {
    log_msg("Firmware update failed: %s", esp_err_to_name(err));
    return reply(req, "500 Internal Server Error", "err_update");
  }
  log_msg("Firmware updated, restarting");
  g_restart = true;
  return reply(req, "200 OK", "ok");
}

static esp_err_t reboot_handler(httpd_req_t *req) {
  if (deny_on_ap(req)) return ESP_OK;
  if (grbl_running()) return reply(req, "409 Conflict", "err_job_running");
  g_restart = true;
  return reply(req, "200 OK", "ok");
}

static void start_http(void) {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.stack_size = 8192;
  config.max_uri_handlers = 12;
  config.lru_purge_enable = true;
  config.recv_wait_timeout = 15;
  httpd_handle_t server = NULL;
  ESP_ERROR_CHECK(httpd_start(&server, &config));

  const httpd_uri_t uris[] = {
      {.uri = "/", .method = HTTP_GET, .handler = page_handler},
      {.uri = "/api/status", .method = HTTP_GET, .handler = status_handler},
      {.uri = "/api/log", .method = HTTP_GET, .handler = log_handler},
      {.uri = "/api/cmd", .method = HTTP_POST, .handler = cmd_handler},
      {.uri = "/api/stop", .method = HTTP_POST, .handler = stop_handler},
      {.uri = "/api/settings", .method = HTTP_POST, .handler = settings_handler},
      {.uri = "/api/wifi", .method = HTTP_POST, .handler = wifi_handler},
      {.uri = "/api/update", .method = HTTP_POST, .handler = update_handler},
      {.uri = "/api/reboot", .method = HTTP_POST, .handler = reboot_handler},
  };
  for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) httpd_register_uri_handler(server, &uris[i]);
}

// ---------------------------------------------------------------- status LED

typedef struct {
  uint8_t r, g, b;
  uint16_t period_ms;  // 0 = steady
} led_mode_t;

static led_mode_t pick_led_mode(void) {
  char st[16];
  get_state(st, sizeof(st));
  if (g_ap_active && !g_wifi_ok) return (led_mode_t){LED_LEVEL, 0, LED_LEVEL, 1000};  // purple blink: setup AP
  if (!g_wifi_ok) return (led_mode_t){LED_LEVEL, 0, 0, 1000};                         // red blink: no Wi-Fi
  if (!g_usb_ready) return (led_mode_t){LED_LEVEL, LED_LEVEL, 0, 0};                  // yellow: no machine
  if (strcmp(st, "Alarm") == 0 || g_safety_tripped) return (led_mode_t){LED_LEVEL, 0, 0, 250};  // fast red
  if (strcmp(st, "Run") == 0 || strcmp(st, "Jog") == 0) return (led_mode_t){LED_LEVEL, LED_LEVEL, LED_LEVEL, 500};
  if (g_client >= 0) return (led_mode_t){0, 0, LED_LEVEL, 0};  // blue: program connected
  return (led_mode_t){0, LED_LEVEL, 0, 0};                     // green: ready
}

static void monitor_task(void *arg) {
  led_strip_handle_t led = NULL;
  const led_strip_config_t strip_cfg = {
      .strip_gpio_num = LED_GPIO,
      .max_leds = 1,
      .led_model = LED_MODEL_WS2812,
      .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
  };
  const led_strip_rmt_config_t rmt_cfg = {.clk_src = RMT_CLK_SRC_DEFAULT, .resolution_hz = 10 * 1000 * 1000};
  if (led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &led) != ESP_OK) led = NULL;

  bool marked_valid = false;
  uint32_t tick = 0;
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(50));
    tick += 50;

    if (g_restart) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      esp_restart();
    }
    // Keep an OTA image only after it has run for a while; a crash before that rolls back.
    if (!marked_valid && tick >= 30000) {
      esp_ota_mark_app_valid_cancel_rollback();
      marked_valid = true;
    }
    if (tick % 10000 == 0) stop_ap_if_idle();

    if (led) {
      const led_mode_t m = pick_led_mode();
      const bool on = m.period_ms == 0 || (tick % m.period_ms) < m.period_ms / 2;
      led_strip_set_pixel(led, 0, on ? m.r : 0, on ? m.g : 0, on ? m.b : 0);
      led_strip_refresh(led);
    }
  }
}

// ---------------------------------------------------------------- Improv serial (Wi-Fi setup from the web flasher)
// https://www.improv-wifi.com/serial/ - packets share the console UART with log output.

#define IMPROV_UART UART_NUM_0
#define IMPROV_STATE_READY 0x02
#define IMPROV_STATE_PROVISIONING 0x03
#define IMPROV_STATE_PROVISIONED 0x04
#define IMPROV_ERR_INVALID 0x01
#define IMPROV_ERR_UNKNOWN_CMD 0x02
#define IMPROV_ERR_CONNECT 0x03

static void improv_send(uint8_t type, const uint8_t *data, uint8_t len) {
  uint8_t pkt[6 + 3 + 255 + 2];
  memcpy(pkt, "IMPROV", 6);
  pkt[6] = 1;
  pkt[7] = type;
  pkt[8] = len;
  memcpy(pkt + 9, data, len);
  uint8_t sum = 0;
  for (int i = 0; i < 9 + len; i++) sum += pkt[i];
  pkt[9 + len] = sum;
  pkt[10 + len] = '\n';
  uart_write_bytes(IMPROV_UART, pkt, 11 + len);
}

static void improv_state(uint8_t state) { improv_send(0x01, &state, 1); }
static void improv_error(uint8_t err) { improv_send(0x02, &err, 1); }

static void improv_result(uint8_t cmd, const char *const *strs, int n) {
  uint8_t d[250];
  size_t p = 2;
  for (int i = 0; i < n; i++) {
    const size_t l = strlen(strs[i]);
    if (p + 1 + l > sizeof(d)) break;
    d[p++] = (uint8_t)l;
    memcpy(d + p, strs[i], l);
    p += l;
  }
  d[0] = cmd;
  d[1] = (uint8_t)(p - 2);
  improv_send(0x04, d, (uint8_t)p);
}

static void improv_send_url(uint8_t cmd) {
  char url[40];
  snprintf(url, sizeof(url), "http://%s/", g_ip);
  const char *strs[] = {url};
  improv_result(cmd, strs, 1);
}

static void improv_scan(void) {
  // A scan is refused while the station is still trying to join a network.
  if (!g_wifi_ok) {
    esp_timer_stop(g_sta_retry_timer);
    esp_wifi_disconnect();
  }
  uint16_t count = 20;
  wifi_ap_record_t recs[20];
  if (esp_wifi_scan_start(NULL, true) != ESP_OK || esp_wifi_scan_get_ap_records(&count, recs) != ESP_OK) count = 0;
  for (uint16_t i = 0; i < count; i++) {
    if (recs[i].ssid[0] == '\0') continue;
    char rssi[8];
    snprintf(rssi, sizeof(rssi), "%d", recs[i].rssi);
    const char *strs[] = {(const char *)recs[i].ssid, rssi, recs[i].authmode == WIFI_AUTH_OPEN ? "NO" : "YES"};
    improv_result(0x04, strs, 3);
  }
  improv_result(0x04, NULL, 0);
  if (!g_wifi_ok && g_has_creds) esp_wifi_connect();
}

static void improv_provision(const uint8_t *args, uint8_t len) {
  if (len < 2 || args[0] > 32 || 1 + args[0] + 1 > len) {
    improv_error(IMPROV_ERR_INVALID);
    return;
  }
  const uint8_t slen = args[0];
  const uint8_t plen = args[1 + slen];
  if (slen == 0 || plen > 63 || 2 + slen + plen > len) {
    improv_error(IMPROV_ERR_INVALID);
    return;
  }
  char ssid[33], pass[65];
  memcpy(ssid, args + 1, slen);
  ssid[slen] = '\0';
  memcpy(pass, args + 2 + slen, plen);
  pass[plen] = '\0';

  improv_state(IMPROV_STATE_PROVISIONING);
  strlcpy(g_ssid, ssid, sizeof(g_ssid));
  strlcpy(g_pass, pass, sizeof(g_pass));
  g_has_creds = true;
  g_wifi_ok = false;
  esp_timer_stop(g_sta_retry_timer);
  wifi_config_t cfg = {0};
  strlcpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid));
  strlcpy((char *)cfg.sta.password, pass, sizeof(cfg.sta.password));
  cfg.sta.threshold.authmode = pass[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
  esp_wifi_disconnect();
  esp_wifi_set_config(WIFI_IF_STA, &cfg);
  esp_wifi_connect();

  for (int i = 0; i < 200 && !g_wifi_ok; i++) vTaskDelay(pdMS_TO_TICKS(100));
  if (!g_wifi_ok) {
    log_msg("Improv: could not join %s", ssid);
    improv_error(IMPROV_ERR_CONNECT);
    improv_state(IMPROV_STATE_READY);
    return;
  }
  if (save_wifi(ssid, pass) != ESP_OK) log_msg("Improv: Wi-Fi settings not saved");
  improv_state(IMPROV_STATE_PROVISIONED);
  improv_send_url(0x01);
}

static void improv_handle(uint8_t type, const uint8_t *data, uint8_t len) {
  if (type != 0x03 || len < 2 || data[1] + 2 > len) {
    improv_error(IMPROV_ERR_INVALID);
    return;
  }
  const uint8_t cmd = data[0];
  if (cmd != 0x02 && cmd != 0x03) {
    for (int i = 0; i < 100 && !g_wifi_started; i++) vTaskDelay(pdMS_TO_TICKS(50));
  }
  switch (cmd) {
    case 0x01:
      improv_provision(data + 2, data[1]);
      break;
    case 0x02:
      if (g_wifi_ok) {
        improv_state(IMPROV_STATE_PROVISIONED);
        improv_send_url(0x02);
      } else {
        improv_state(IMPROV_STATE_READY);
      }
      break;
    case 0x03: {
      const char *strs[] = {FW_NAME, FW_VERSION, "ESP32-S3", HOSTNAME};
      improv_result(0x03, strs, 4);
      break;
    }
    case 0x04:
      improv_scan();
      break;
    default:
      improv_error(IMPROV_ERR_UNKNOWN_CMD);
  }
}

static void improv_task(void *arg) {
  static const char hdr[] = "IMPROV";
  uint8_t buf[6 + 3 + 255 + 1];
  size_t n = 0;
  while (true) {
    uint8_t c;
    if (uart_read_bytes(IMPROV_UART, &c, 1, portMAX_DELAY) != 1) continue;
    if (n < 6 && c != (uint8_t)hdr[n]) {
      n = c == 'I' ? 1 : 0;
      if (n) buf[0] = c;
      continue;
    }
    buf[n++] = c;
    if (n < 9) continue;
    if (buf[6] != 1) {
      n = 0;
      continue;
    }
    const size_t total = 9 + buf[8] + 1;
    if (n < total) continue;
    uint8_t sum = 0;
    for (size_t i = 0; i < total - 1; i++) sum += buf[i];
    if (sum == buf[total - 1]) {
      improv_handle(buf[7], buf + 9, buf[8]);
    } else {
      improv_error(IMPROV_ERR_INVALID);
    }
    n = 0;
  }
}

static void start_improv(void) {
  if (uart_driver_install(IMPROV_UART, 512, 0, 0, NULL, 0) != ESP_OK) return;
  xTaskCreate(improv_task, "improv", 6144, NULL, 4, NULL);
}

// ---------------------------------------------------------------- main

void app_main(void) {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  g_usb_mutex = xSemaphoreCreateMutex();
  g_rx_stream = xStreamBufferCreate(RX_STREAM_SIZE, 1);
  load_config();
  log_msg(FW_NAME " %s, Wi-Fi %s", FW_VERSION, g_has_creds ? g_ssid : "not configured");

  xTaskCreate(monitor_task, "monitor", 4096, NULL, 3, NULL);
  start_improv();
  start_wifi();
  start_mdns();
  start_usb();
  start_http();
  xTaskCreate(bridge_task, "bridge", 6144, NULL, 8, NULL);
}
