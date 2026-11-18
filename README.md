# ESP32 Wi‑Fi CSI Radar

ESP32 DevKit V1 + W5500 Ethernet. W5500 has a fixed IP address, so any PC on the same LAN can connect to the radar.

## Network

- W5500 / ESP32 IP: `192.168.1.23`
- Gateway: `192.168.1.1`
- Subnet mask: `255.255.255.0`
- Radar TCP server port: `9999`
- PC app connects to: `192.168.1.23:9999`

No PC-side IP configuration is required: the PC app connects directly to the W5500 address.

## Build

GitHub Actions builds firmware automatically on push to `main`.

## Flash

```bat
idf.py -p COMx flash monitor
```

## PC app

Run `pc_app\install.bat` once, then `pc_app\start_radar.bat`.

## Wiring W5500

| W5500 | ESP32 |
|---|---|
| MOSI | GPIO23 |
| MISO | GPIO19 |
| SCLK | GPIO18 |
| CS | GPIO5 |
| INT | GPIO4 |
| RST | GPIO33 |

## Important

For motion detection, a second Wi‑Fi transmitter is required: router, phone hotspot, or another ESP32. The ESP32 analyzes Wi‑Fi CSI changes caused by movement.
