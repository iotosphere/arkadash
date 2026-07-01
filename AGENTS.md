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
7. **Whisper STT** in `agent_server.py:transcribe_audio()` uses `language=tr`, Türkçe komut `prompt`, threshold'lar (no_speech/compression_ratio/logprob), `max_amp<800` energy gate, +600ms trailing silence — DO NOT regress to bare `output_format=json` only

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

## Whisper STT Configuration (2026-07-01)

Voice pipeline: P4 (PTT) → WebSocket → `agent_server.py:transcribe_audio()` →
whisper.cpp server (`:8080`).

**Server command** (run on Mac, `~/whisper.cpp/build`):
```bash
./bin/whisper-server \
  -m ../models/ggml-tiny.bin \
  -l tr \
  --port 8080 \
  --host 0.0.0.0 \
  --threads 2
```
`--threads 2` zorunlu — Mac i5-6360U 2C/4T 15W TDP, 3-4 thread thermal throttle eder.
`tiny` yeterli (39M params, ~390MB RAM). Quantized Q4 default.

**`transcribe_audio()` parametreleri** (`agent_server.py`):
- `language=tr` — dil tespitini kapat, halüsinasyon azaltır
- `prompt=<Türkçe komut vocabulary>` — vocabulary bias, en kritik iyileştirme
- `temperature=0.0`, `temperature_inc=0.0` — greedy, fallback yok
- `beam_size=1`, `best_of=1` — en hızlı
- `no_speech_threshold=0.6`, `compression_ratio_threshold=2.4`, `logprob_threshold=-1.0`
- Client-side: `max_amp < 800` ise skip (sessizlikte Whisper çağrısı yapılmaz)
- Client-side: 600ms trailing silence eklenir (PTT_STOP sonrası son heceyi kaçırmamak için)

**Energy threshold tuning:** 800 çok yüksekse `ENERGY_THRESHOLD=600` veya `400`'e düşür.
Logda `Silent audio, skipping` çok sık çıkıyorsa eşik fazla yüksek.

**voice_server.py**: AYNI dizinde ama artık kullanılmıyor (`agent_server.py` canonical).
Eski parametreler korundu, dokunma.

## LangGraph Migration Plan (2026-07-01)

**Hedef:** `agent_server.py`'yi gerçek LangGraph orchestrator'a çevirmek.
Şu an basit asyncio pipeline, `AssistantState` placeholder sınıf. Yorumlarda
"LangGraph-like flow" yazıyor ama gerçek LangGraph değil.

**Neden:**
- LangGraph öğrenme amaçlı (kullanıcı bildirdi)
- İleride multi-step agent, tool calling, memory orchestration

**Aşamalar (önerilen sıra):**
1. ✅ **STT/LLM/TTS pipeline iyileştirmesi** — orchestrator'dan bağımsız, şu an yapılıyor
2. ⏳ **LLM tool calling** — `led_toggle`, `led_set_color`, `led_set_brightness`, Spotify, hava durumu
   - Bağımlılık: MiniMax API'nin tool/function calling desteği henüz doğrulanmadı
3. ⏳ **Gerçek StateGraph** — `StateGraph` typed state, nodes (stt/llm/tools/tts), edges, conditional routing
4. ⏳ **Persistent state** — Redis/SQLite checkpoint, conversation recovery
5. ⏳ **Human-in-the-loop** — interrupt, approve before tool call

**Refactor notu:** `agent_server.py` 1375 satır, LangGraph geçişinde büyük ihtimalle
yeniden yapılandırılacak. Şu anki "tek dosya her şey" yapısı orchestrator için uygun değil.

## P4 Audio Buffer Tuning (TODO 2026-07-01)

**Sorun:** Uzun TTS cevaplarında (15s+) ses karışıyor veya kesiliyor, P4 log:
```
W ws_client: Ring buffer full, dropped N bytes
```

**Sebep:** P4'ün iki buffer'ı yetmiyor:
- `ws_client` ring buffer (default ~küçük) — TTS PCM'i düşürmeden depolayamıyor
- `i2s_common` DMA buffers (`6 desc x 240 frame` log'da) — PCM'i DMA'ya koymadan yetiştiremiyor

**2026-07-01 bulgu (kaynak: github.com/iotosphere/arkadash):**
Çözüm P4 firmware `components/websocket_client/websocket_client.c`'de **3 değişiklik**:
1. **64 KB static ring buffer** (TTS PCM için):
   ```c
   static uint8_t s_rb_storage[65536];
   xRingbufferCreateStatic(sizeof(s_rb_storage), RINGBUF_TYPE_BYTEBUF, ...);
   ```
2. **ESP-IDF WebSocket client `buffer_size = 8192`**:
   ```c
   esp_websocket_client_config_t ws_cfg = {
       .uri         = uri,
       .buffer_size = 8192,
       .keep_alive_enable = true,
       .reconnect_timeout_ms = 5000,
       ...
   };
   ```
3. **Ses gönderirken chunk_size 4096 + vTaskDelay 5ms**:
   ```c
   size_t chunk_size = 4096;
   while (sent < audio_len) {
       int ret = esp_websocket_client_send_bin(..., len, pdMS_TO_TICKS(5000));
       sent += len;
       vTaskDelay(pdMS_TO_TICKS(5));
   }
   ```

**2026-07-01 pacing denemeleri (agent_server.py):** 0.005/0.015/0.030/0.10 denendi,
hiçbiri sorunu kökünden çözmedi. **P4 firmware tarafında düzeltme şart**.

**TODO (yarın):**
1. `components/websocket_client/websocket_client.c` — yukarıdaki 3 değişikliği uygula
2. (Opsiyonel) `i2s_channel_new()` DMA buffer count/len — eğer cümle tamamlanma hâlâ bozuksa
3. Build + flash + test (uzun TTS cümlesi ile dene)

**Trade-off:** 64 KB static ring buffer → ~64 KB RAM kullanımı. P4'te 32 MB PSRAM var,
yeterli headroom. Static allocation tercih edilir (heap fragmentation önler).

