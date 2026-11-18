# ESP32 Wi‑Fi CSI Radar

ESP32 DevKit V1 + W5500 Ethernet: Wi‑Fi CSI telemetry, TinyML-ready anomaly detection, UDP stream to PC, Windows control app.

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

Set PC IP in `main/main.c` (`RADAR_PC_IP`) and UDP port 9999.
