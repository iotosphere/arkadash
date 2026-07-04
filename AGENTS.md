# Arkadash P4 Panel — Project Memory

## Status (2026-07-04)

End-to-end pipeline **stabil** in `~/arkadash/` (`agent_server.py` SİLİNDİ → `orchestrator/`
modüler refactor):

```
P4 button → ws_client (PTT) → orchestrator (orchestrator/main.py)
  → STT (whisper.cpp) → agent (LangGraph + tool calling) → TTS (MiniMax Turkish)
  → WebSocket PCM → P4 ring buffer → DMA → ES8311 → speaker ✅
```

**Voice pipeline tuned** (nihai karar 2026-07-04):
- Tek TTS çağrısı (cümle bazlı değil — cümle arası 2.5s latency cümle drop yaratıyordu)
- pacing = 0.060s sabit (chunk 4096, 68 KB/s, DMA 32 KB/s'nin %112 üstünde)
- tts_start sonrası 50ms bekleme (P4 firmware settle)
- Verified: 22.58s tool call cevabı (weather.get_current) 722462 byte TAM oynandı,
  drop yok. Hafif jitter WS frame overhead'inden.

**P4 firmware stable** (ArkadashSon f27ae9b merged):
- queue 128 (chunk_queue), DMA 12 desc x 240 frame
- WS buffer 8192, ES8311 voice_volume 65 (init), slider override → 0-100
- Slider callbacks: audio.c `audio_set_volume()`, ui/eez_wrapper.cpp
  `ui_slider_volume_cb` + `ui_slider_brightness_cb`

## Hardware

- **P4**: Waveshare ESP32-P4-WIFI6, chip rev v1.0, 8 MB flash, 32 MB PSRAM, IP `192.168.1.16`
- **TBR (S3)**: `~/esp/controller/`, port `/dev/cu.usbmodem14301`, IP **`192.168.1.15`** (NOT 1.19)
- **H2 light**: `~/esp/smarthome/`, port `/dev/cu.usbmodem14101`, node_id=0x1, endpoint=1, fabric 3F594493A97781BC
- **Mac agent**: `ws://0.0.0.0:8765`, TBR HTTP `http://192.168.1.15:8766`
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
| `audio`         | `components/audio/`               | I2S + ES8311 + `audio_set_volume()` (slider) |
| `battery_monitor` | `components/battery_monitor/`   | NEW — ADC oneshot battery gauge            |
| `clock_weather` | `components/clock_weather/`       | SNTP, weather fetch, **periodic battery task** |
| `display`       | `components/display/`             | ST7789 SPI                                 |
| `encoder`       | `components/encoder/`             | EC11 + KEY0                                |
| `ui`            | `components/ui/`                  | LVGL/EEZ flow, slider callbacks, image assets |
| `websocket_client` | `components/websocket_client/` | esp_websocket_client wrapper               |
| `wifi_station`  | `components/wifi_station/`        | STA connect                                |

## Orchestrator (Mac side, `orchestrator/` directory)

**Layered + atomic design** (refactored from monolithic `agent_server.py`,
2026-07-03):

```
orchestrator/
├── main.py               # Entry: banner, validate, health thread, UDP broadcast, WS server
├── config.py             # Env vars, TBR IP, SYSTEM_PROMPT, MAX_HISTORY_TURNS
├── core/
│   └── pipeline.py       # STT → fuzzy → agent → TTS, **tek TTS çağrısı**
├── services/             # Atomic, birbirinden bağımsız
│   ├── stt.py            # Whisper HTTP client (energy gate, vocab bias)
│   ├── llm.py            # chat_stream, chat_with_tools, chat_with_tool_results
│   ├── tts.py            # 4 backend (minimax/edge/google_cloud/gemini), **chunk 4096**
│   ├── agent.py          # **LangGraph StateGraph** (call_llm, dispatch, should_continue)
│   ├── spotify.py        # OAuth + Web API + 6 TOOL_SCHEMA
│   ├── matter.py         # TBR LED wrapper + 4 TOOL_SCHEMA
│   ├── weather.py        # open-meteo HTTP + TOOL_SCHEMA_GET_CURRENT
│   └── time.py           # now()/today() + TOOL_SCHEMA
├── network/              # Network I/O katmanı
│   ├── websocket.py      # handle_client, _Recorder, state
│   ├── health.py         # REST endpoints (lazy import circular safe)
│   └── discovery.py      # UDP broadcaster (mDNS yerine)
└── utils/                # Saf DSP
    ├── audio.py          # stereo_to_mono, normalize, resample, **soft_limit**
    └── normalization.py  # fuzzy match, clean_text_for_tts (°C → "derece")
```

**Atomic tasarım prensibi:** Her modül tek concern. Yeni tool ekle →
`services/yeni_tool.py` yaz, `_TOOL_MODULES`'a ekle, agent otomatik keşfeder.

**LangGraph StateGraph** (`services/agent.py`):
```
START → call_llm → [tool_calls?] → dispatch → call_llm → ... → END
                ↓
            (max 3 iter guard)
```
MAX_ITERATIONS=3 — "yarın hava yağmurluysa müziği durdur" gibi zincirler için.

**Run command** (canonical):
```bash
cd ~/arkadash
source orch/bin/activate
PYTHONDONTWRITEBYTECCODE=1 python -m orchestrator.main
```

## Voice Pipeline Tuning (NİHAİ 2026-07-04)

**Sorun:** Uzun TTS cevaplarında (15s+) cümle drop, P4 log'da "Buffer underflow".

**Denenen ve BAŞARISIZ olan pacing değerleri:**
- 0.005/0.015/0.030/0.060/0.045 (sabit pacing) — ya drop ya jitter
- dinamik pacing (ses × 0.95) — chunk boyutuna göre değişken, jitter
- adaptive (initial burst + steady) — initial chunk trick da bozdu
- Cümle bazlı TTS — her cümlede 2.5s latency → 2. cümle drop

**ÇÖZÜM (nihai):**
1. **Tek TTS çağrısı** — tüm yanıt tek ses sentezi, cümle bazlı YOK
2. **pacing = 0.060s sabit** + **chunk_size = 4096** (TTS backend)
3. **tts_start sonrası 50ms bekleme** (P4 firmware settle)
4. **P4 firmware stabil:** queue 128 + DMA 12 + WS buffer 8192

**Pacing matematiği:**
- 4096 byte / 0.060s = 68 KB/s gönderim
- DMA consume 32 KB/s → net 36 KB/s birikim
- 5 saniyelik cümle = 180 KB birikim, queue 512 KB kapasiteli → OK
- Hafif jitter WS frame overhead'inden (~1-5ms), tolere edilebilir

**Neden cümle bazlı TEMSİL OLMAZ:**
- Her cümle = ayrı TTS API + ffmpeg decode = 2-2.5s latency
- Bu latency boyunca P4 ring buffer tükenir (2.5s × 32 KB/s = 80 KB)
- Sonraki cümle ilk chunk geldiğinde ring buffer boş → drop
- Tek TTS çağrısı = tek 2.5s latency (cevap başında, ring buffer zaten boş, sorun yok)

**Kanıt:** 2026-07-04 16:46 verified 22.58s tool call cevabı 722462 byte tam oynandı.

**Yapılmayacaklar** (denenmiş, bozmuş):
- Pacing 0.030'a düşürme → drop (queue underflow)
- Pacing 0.090'a yükseltme → jitter (queue overflow)
- chunk_size 1024'e indirme → pacing tutarsız, jitter
- "Buffer underflow" log'unu Pacing ile çözme — bu cümle drop'u işareti, gerçek çözüm P4 firmware'de

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

## LangGraph StateGraph (DONE 2026-07-03)

**Durum:** ✅ Tamamlandı — `services/agent.py` gerçek LangGraph StateGraph kullanıyor.

**Mimari:**
```python
from langgraph.graph import StateGraph, END, START
class AgentState(TypedDict, total=False):
    session_id: str; user_text: str; tool_calls: list
    tool_results: list; final_text: str; iteration: int

# Nodes: call_llm, dispatch
# Conditional: should_continue → "dispatch" | "end"
# MAX_ITERATIONS = 3
```

**Tool keşif** (`services/agent.py:_discover_tools`):
- `_TOOL_MODULES = [weather, time_svc, matter, spotify]`
- Her modülün `TOOL_SCHEMA_X` dict'leri otomatik toplanır
- Schema `name = "module.func"` → dispatch `func = getattr(module, "func")`
- Yeni tool ekle: `services/yeni.py` + `_TOOL_MODULES`'a ekle, otomatik çalışır

**Test edilmiş tool'lar** (MiniMax M2.7 OpenAI function_calling format):
- `time.now` / `time.today` — Türkçe haftanın günü
- `weather.get_current` — open-meteo (API key gereksiz)
- `led.toggle` — TBR HTTP wrapper
- `spotify.*` — OAuth + Web API (6 tool, test edilmedi)

**Eklenmesi planlanan tool'lar:**
- `bitget` — kripto trading (`services/bitget.py` + `core/trade_safety.py` confirmation gate)
- `volume.set` / `brightness.set` — P4 firmware control via WebSocket reverse channel

## P4 Audio Buffer Tuning (ÇÖZÜLDÜ 2026-07-04)

**Durum:** ✅ Çözüldü — yukarıdaki "Voice Pipeline Tuning (NİHAİ)" bölümüne bak.

**Sorun:** Uzun TTS cevaplarında cümle drop, "Buffer underflow, continuing..."

**Çözüm kombinasyonu** (Python + firmware):
1. **Tek TTS çağrısı** (`orchestrator/core/pipeline.py`) — cümle bazlı değil
2. **pacing 0.060s + chunk 4096** (`services/tts.py`)
3. **tts_start sonrası 50ms bekleme** (P4 firmware settle)
4. **P4 firmware stabil hali:** `components/websocket_client/websocket_client.c`
   queue 128 + WS buffer 8192, `components/audio/audio.c` DMA 12 desc + ES8311 volume 65

**ASLA YAPMA** (denenmiş, drop yaratıyor):
- Pacing 0.030 veya daha hızlı → ring buffer underflow
- Cümle bazlı TTS → her cümlede 2.5s latency, sonraki cümle drop
- chunk_size 1024 → pacing tutarsız
- "Buffer underflow" log'unu pacing ile çözme — bu gerçek cümle drop işareti

