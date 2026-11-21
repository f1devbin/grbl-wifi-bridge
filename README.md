# GRBL Wi-Fi Bridge: wireless GRBL for laser engravers and CNC machines (ESP32-S3)

[![Build](https://github.com/f1devbin/grbl-wifi-bridge/actions/workflows/build.yml/badge.svg)](https://github.com/f1devbin/grbl-wifi-bridge/actions/workflows/build.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Web flasher](https://img.shields.io/badge/flash-in%20your%20browser-blue)](https://f1devbin.github.io/grbl-wifi-bridge/)

**English** | [Русский](README.ru.md)

GRBL Wi-Fi Bridge turns an ESP32-S3 board into a **Wi-Fi adapter for any GRBL machine with a USB port**: diode laser engravers, 3018-style CNC routers, pen plotters. Plug the ESP32-S3 into the machine's USB port in place of the computer, and control it over Wi-Fi from **LightBurn**, **Universal Gcode Sender** or any GRBL sender that supports a TCP / telnet connection. No changes to the machine, no soldering inside it, no GRBL firmware update.

**[Flash it from your browser →](https://f1devbin.github.io/grbl-wifi-bridge/)** (Chrome or Edge on a computer). The flasher also sets up Wi-Fi.

## Features

- **USB host on the ESP32-S3**: talks to the machine's USB serial chip directly. Drivers for CH340/CH341, CP210x, FTDI and any CDC-ACM device (CH343, native USB controllers).
- **Raw TCP bridge on port 23**: bytes pass through unchanged, so G-code streaming, real-time commands (`?`, `!`, `~`, `Ctrl-X`) and GRBL settings work exactly as over a USB cable.
- **Web control panel** at `http://grbl-bridge.local/`: status, big STOP button, pause and resume, GRBL console with quick commands (`$I`, `$$`, `$X`, `$H`), settings, Wi-Fi, firmware update. English and Russian, adaptive layout for phones, high-contrast large text.
- **Live GRBL state**: when no sender is connected the bridge asks GRBL for its status every 2 s, so the panel and the LED show Idle, Alarm or Run before you open a program.
- **Self-recovery**: if the web panel stops answering (for example after an unusual USB disconnect), the bridge restarts itself within about 90 s. The panel links to this repository.
- **Safety**: if the connection to the sender drops during a job (Wi-Fi lost, laptop asleep, program crashed), the bridge sends feed hold and a GRBL reset, which stops motion and switches the laser or spindle off.
- **Wi-Fi setup three ways**: from the web flasher (Improv), from the setup access point `GRBL-Bridge-setup`, or from the web panel.
- **Over-the-air updates** with automatic rollback if a new image fails to start.
- **Status LED** (WS2812 on GPIO48): colour shows Wi-Fi, machine and job state.
- **mDNS** name `grbl-bridge.local`, advertised `_http._tcp` and `_telnet._tcp` services.

## What you need

- An ESP32-S3 board with **native USB OTG** (GPIO19/GPIO20) and **at least 8 MB of flash**. Boards in the ESP32-S3-DevKitC-1 style with two USB-C ports are the easiest: one port goes through a USB-UART chip for power and flashing, the other is the native USB port for the machine.
- 5 V on the native USB port, so the machine's USB chip is powered. On many dual-USB-C boards this needs a solder jumper (often marked `USB-OTG`) that connects the VBUS lines of both ports. **First check whether your machine already drives 5 V on its own USB port**: if it does, do not close that jumper.
- A USB power adapter for the ESP32-S3, plugged into its USB-UART port.

## Quick start

1. Open the **[web flasher](https://f1devbin.github.io/grbl-wifi-bridge/)**, connect the ESP32-S3 to your computer through its USB-UART port and press **Install**.
2. When the flasher asks, choose your Wi-Fi network and enter the password. It shows a link to the bridge when it is online.
3. Unplug the board from the computer, power it from a USB adapter and connect the machine to its native USB port.
4. Open `http://grbl-bridge.local/` (or the IP address from your router) and check that the machine is shown as connected and GRBL answers `$I`.
5. Connect your sender:
   - **LightBurn**: Devices → Create Manually → GRBL → Ethernet/TCP → address of the bridge, port 23.
   - **Universal Gcode Sender**: Tools → Options → Sender Options → Connection driver: TCP; then connect to the bridge address, port 23.
   - **Programs that only open serial ports** (Linux, macOS):

     ```bash
     socat pty,link=$HOME/ttyGRBL,raw,echo=0 tcp:grbl-bridge.local:23
     ```

     and choose `~/ttyGRBL` as the port.

If the flasher could not set up Wi-Fi, or the saved network is not reachable for 30 s after power-on, the bridge starts the access point **`GRBL-Bridge-setup`** (password `grblbridge`). Connect to it, open `http://192.168.4.1` and enter your network. Machine control is disabled on this access point.

## Status LED

| Colour | Meaning |
|---|---|
| Purple, blinking | Setup access point active, no Wi-Fi |
| Red, blinking | No Wi-Fi connection |
| Yellow | Wi-Fi OK, machine not connected |
| Green | Ready |
| Blue | Sender connected over TCP |
| White, blinking | Job running (`Run` or `Jog`) |
| Red, fast blinking | GRBL `Alarm` or emergency stop triggered |

On some boards the LED only works after closing a solder jumper marked `RGB`.

## Safety notes

- The emergency stop triggers when the TCP sender disconnects while GRBL reports `Run`, `Jog` or `Hold`, or a G-code line was sent during the last 10 s. Read-only queries such as `$I`, `$$` and `?` do not arm it, so checking the machine and disconnecting does not reset an idle machine. Broken connections are detected by TCP keepalive in about 6 s. After the reset GRBL loses its position, so the job has to be restarted. The option can be turned off in the panel.
- The bridge never asserts DTR, so it does not reset the controller when it connects or reboots. Reset the machine from its own panel or sender when needed.
- Wi-Fi is less reliable than a cable. For laser jobs prefer dynamic power (`M4`), which turns the beam off when motion stops, and never leave a running laser or CNC unattended.
- The bridge refuses to restart, change Wi-Fi or change the TCP port or baud rate while GRBL reports `Run`, `Jog` or `Hold`, and refuses a firmware update while a sender is connected or G-code was sent during the last 10 s. Press STOP first: restarting the bridge mid-job would leave the machine running without a sender.
- The web panel has no login: anyone on your local network can open it. Keep the bridge on a trusted network.

## Will it work with my machine?

The bridge works with anything that speaks GRBL (or grblHAL) over a USB serial chip: most hobby diode laser engravers, 3018-style CNC routers and GRBL pen plotters. The TCP bridge itself is transparent, so other USB serial devices (for example 3D printers) can be reached too, but the status display and the emergency stop expect GRBL replies. Default baud rate is 115200 and can be changed in the panel.

Controllers that already have Wi-Fi (FluidNC, Grbl_ESP32) do not need this bridge.

## Build from source

The firmware is built with ESP-IDF 6.1 through PlatformIO; USB host drivers, the WS2812 driver and mDNS come from the ESP Component Registry (`src/idf_component.yml`).

```bash
pip install platformio
pio run
```

Update over Wi-Fi without touching settings:

```bash
curl --data-binary @.pio/build/esp32s3/firmware.bin http://grbl-bridge.local/api/update
```

or upload `firmware.bin` in the web panel. A new image is confirmed after it runs for 30 s; if it crashes before that, the bootloader rolls back to the previous one.

`tools/release.sh` merges the bootloader, partition table and application into the factory image used by the web flasher and regenerates the flasher site with `tools/build_site.py`.

### Flash layout

| Offset | Content |
|---|---|
| `0x0` | Bootloader |
| `0x8000` | Partition table |
| `0x9000` | NVS (Wi-Fi and bridge settings), 24 KB |
| `0xf000` | OTA data |
| `0x20000` | Application slot 0, 3 MB |
| `0x320000` | Application slot 1, 3 MB |

## HTTP API

| Method | Path | Description |
|---|---|---|
| GET | `/api/status` | JSON status: Wi-Fi, machine, GRBL state, client, traffic, settings |
| GET | `/api/log?since=N` | Console log after position `N`; first line is the new position |
| POST | `/api/cmd` | Send one GRBL line (body); `!`, `~`, `?` are sent as real-time bytes |
| POST | `/api/stop` | Feed hold, then GRBL reset |
| POST | `/api/settings` | Form fields `baud`, `port`, `safety` |
| POST | `/api/wifi` | Form fields `ssid`, `pass`; the bridge restarts |
| POST | `/api/update` | Firmware image as the request body |
| POST | `/api/reboot` | Restart the bridge |

Errors are returned as short keys (`err_no_usb`, `err_busy`, ...) that the web panel translates.

## Project status

Beta. Wi-Fi, the web panel, Improv provisioning, OTA updates and rollback are tested on hardware. The USB host and TCP bridge were verified with a real CH340-based GRBL 1.0.2 controller (Elidor Z6): status polling, `$I` and the console work. Motion, laser jobs and the emergency stop during a running job still have to be tested on a machine. The USB host uses Espressif's official drivers; reports about specific machines and USB chips are welcome in [issues](https://github.com/f1devbin/grbl-wifi-bridge/issues).

## License

[MIT](LICENSE)
