idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    EMBED_TXTFILES "web/index.html"
    PRIV_REQUIRES esp_wifi esp_netif esp_event nvs_flash esp_http_server lwip app_update esp_timer esp_driver_uart
)
