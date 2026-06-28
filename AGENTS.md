# Arkadash P4 Panel — Project Memory

## Status (2026-06-11)

End-to-end pipeline **works** in `~/arkadash/` (not `~/esp/arkadashsmart/` which is the older backup):

```
P4 button → ws_client: LED_TOGGLE → agent_server.py → TBR HTTP /led/toggle
  → Matter invoke → H2 LED physical toggle ✅
```

Verified by user: "h2 rgb led toggle olarak yanıp sönüyor" on 2026-06-11 ~02:11.

## Hardware

- **P4**: Waveshare ESP32-P4-WIFI6, chip rev v1.0, 8 MB flash, 32 MB PSRAM, IP `192.168.1.18`
- **TBR (S3)**: `~/esp/controller/`, port `/dev/cu.usbmodem14301`, IP **`192.168.1.19`** (NOT 1.45, NOT 1.34)
- **H2 light**: `~/esp/smarthome/`, port `/dev/cu.usbmodem14101`, node_id=0x1, endpoint=1, fabric 3F594493A97781BC
- **Mac agent**: `ws://0.0.0.0:8765`, TBR HTTP `http://192.168.1.19:8766`
- **WiFi**: `SUPERONLINE_WiFi_5292`

### Battery divider (PCB rev needed)
Schematic wires battery divider tap to **GPIO 48** — but GPIO 48 has NO ADC on P4.
**Fix in code**: `BATTERY_ADC_PIN 50` (`app_config/include/app_config.h`).
**PCB fix pending**: user will re-route the divider trace from GPIO 48 to GPIO 50 in KiCad.

## Key Fixes Applied This Project (do not regress)

1. **PSRAM enable** in menuconfig — without it `xTaskCreate` silently fails after large allocations
2. **Flash size 8 MB** — must re-set after every `set-target`
3. **`MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM`** on the 160 KB audio rec buffer (`main/main.c:108`) — DMA-only fails because PSRAM is the only place big enough
4. **TBR IP** in `agent_server.py:57` = `http://192.168.1.19:8766`
5. **Living Room button** in `screens.c:200` already calls `ws_send_led_toggle()` (do not re-add)
6. **TBR HTTP server** uses `ScheduleLambda` for Matter calls (workaround for `AssertChipStackLockedByCurrentThread`)

## Components

| Component       | Path                              | Purpose                                    |
|-----------------|-----------------------------------|--------------------------------------------|
| `app_config`    | `components/app_config/`          | Pin definitions, includes `BATTERY_ADC_PIN` |
| `audio`         | `components/audio/`               | I2S + ES8311                               |
| `battery_monitor` | `components/battery_monitor/`   | NEW — ADC oneshot battery gauge            |
| `clock_weather` | `components/clock_weather/`       | SNTP, weather fetch, **periodic battery task** |
| `display`       | `components/display/`             | ST7789 SPI                                 |
| `encoder`       | `components/encoder/`             | EC11 + KEY0                                |
| `ui`            | `components/ui/`                  | LVGL/EEZ flow, image assets                |
| `websocket_client` | `components/websocket_client/` | esp_websocket_client wrapper               |
| `wifi_station`  | `components/wifi_station/`        | STA connect                                |

## Known Issues / TODOs (user requested 2026-06-11)

**Refactoring + code standard needed.** Specifics:

- `main/main.c` (610 lines) — split into `chat_task.c`, `display_task.c`, `encoder_read.c`
- `components/ui/eez_wrapper.cpp` (132+ lines) — split by concern:
  `ui_clock.c/h`, `ui_weather.c/h`, `ui_battery.c/h`, `ui_footer.c/h`
- `components/ui/screens.c` (1900+ lines) — one file per screen
- Naming inconsistency: `ui_set_X(v)` vs `ui_set_X_with_state(s, v)` — pick one convention
- Magic numbers in `battery_monitor.c` (`SMOOTH_N=8`, `CHARGING_DV_RAW=4`) → config struct
- `extern` declarations scattered in `screens.c` → move to component headers
- Comments mixed Turkish/English — decide a policy

## Image Naming Convention (corrected 2026-06-11)

EEZ-generated images in `components/ui/ui_image_*.c`:
- `img_full_battery` → %100 green
- `img_high_battery` → %80 green (NOT high-battery.png which is "80" green)
- `img_80` → %80 green
- `img_50` → %50 yellow
- `img_charging` → %20 red (mis-named; intended as low-battery)
- `img_plus` → charging overlay (bolt + plus)

Level mapping in `ui_set_battery_state()`:
- charging → `img_plus`
- pct < 20  → `img_charging` (red)
- pct < 50  → `img_50` (yellow)
- pct < 80  → `img_80` (green)
- else      → `img_full_battery`

## Agent Server TBR IP

**Always update both** `~/arkadash/agent_server.py:57` AND re-mention the IP when hardware changes.

If TBR reboots, check new IP via `idf.py monitor` on `/dev/cu.usbmodem14301` — search for
`IPv4 address changed on WiFi station interface: 192.168.1.X`.
