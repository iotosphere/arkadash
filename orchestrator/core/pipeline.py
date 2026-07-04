"""
Voice pipeline orkestrasyonu: STT → agent (LLM + tools) → TTS.

Tek fonksiyon process_audio, websocket tarafından PTT_STOP sonrası çağrılır.
Adımlar:
  1. utils.normalization.normalize_command (fuzzy match) — komutları eşle
  2. services.stt.transcribe_audio — Whisper ile Türkçe transkripsiyon
  3. services.agent.run — LLM + tool calling + multi-step reasoning
     (tool'lar services/ altında: weather, time, matter, spotify, ...)
  4. Final text → cümle-bazlı services.tts.tts_stream → P4'e
  5. Dinamik pacing: her chunk boyutuna göre pacing = ses_süresi × 0.95
     (1024 byte → 29 ms, 4096 byte → 116 ms). DMA underflow önler, queue
     overflow riski yok (5% birikim, cümle 3-5s'de pratik etki yaratmaz).
  6. tts_end + done sinyalleri pipeline tamamlandığında
"""

import asyncio
import json
import re
import traceback

import websockets

from orchestrator.services.agent import run as agent_run
from orchestrator.services.stt import transcribe_audio
from orchestrator.services.tts import tts_stream
from orchestrator.utils.normalization import clean_text_for_tts, normalize_command


# ─── Pacing Parametreleri ────────────────────────────────────────────────────
# f27ae9b "kaymak" orijinali (ArkadashSon'da stabil çalışan hali):
#   - TTS chunk_size = 4096 byte (128 ms ses @ 16 kHz mono)
#   - pacing = 0.060s sabit (4096/0.060 = 68 KB/s, DMA 32 KB/s'den %112 hızlı)
#   - tts_start'tan sonra 50ms bekleme (P4 firmware settle)
#   - Sonuç: ring buffer overflow eğilimli ama P4 queue 128×4096=512KB absorbe eder
#
# ArkadashSon f27ae9b'de bu parametrelerle "hiçbir sorun yok". P4 firmware de
# f27ae9b'de kalmış (queue 128 + DMA 12 + WS buffer 8192 + ES8311 volume 65).
# ~/arkadash'ta P4 firmware değiştirilmişti (ES8311 65→80, sdkconfig); bu uyumsuzluk
# drop yapıyordu. ArkadashSon P4 firmware flash'lanmalı.
#
# Tarihçe (UZUN, DEĞİŞTİRME):
#   - f27ae9b (ArkadashSon)         — chunk 4096 + pacing 0.060 + 50ms → ✓ stabil
#   - dinamik (ses × 0.95)           — DENENDİ, jitter yaptı
#   - 0.030 (sabit)                  — overflow, drop
#   - 0.045 (sabit)                  — hâlâ drop
#   - adaptive (initial+steady)      — ilk chunk trick de bozdu
#   - 0.030 + chunk 1024 (Desktop/smartHome/arkadash) — yine drop, P4 firmware
#     ~/arkadash'ta değiştirildiği için uyumsuz
#   - GERİ DÖNÜŞ: f27ae9b 0.060 + chunk 4096 + 50ms bekleme
TTS_CHUNK_PACING_STEADY = 0.060  # f27ae9b "kaymak" — DEĞİŞTİRME
TTS_TTS_START_DELAY = 0.050      # tts_start sonrası P4 firmware settle bekleme


# ─── STT → Agent → TTS Pipeline ────────────────────────────────────────────

async def process_audio(websocket, session_id: str, audio_buffer: bytearray) -> None:
    """Bir PTT_RELEASE → response → cümle-bazlı PCM stream pipeline'ı.

    Args:
        websocket:    P4 WebSocket bağlantısı (PCM response buradan yollanır)
        session_id:   Client IP (LLM history key)
        audio_buffer: PTT_START-STOP arası toplanan ham PCM (P4 stereo)
    """
    if len(audio_buffer) < 4000:
        await websocket.send(json.dumps({"type": "error", "msg": "Audio too short"}))
        return

    print("[*] Processing with: agent (LLM + tools)")

    try:
        await websocket.send(json.dumps({"type": "status", "msg": "Processing..."}))

        # 1) STT: stereo → mono → normalize → whisper → Türkçe metin
        user_text = await transcribe_audio(bytes(audio_buffer))
        print(f"[STT] Text: '{user_text}'")

        if not user_text.strip():
            await websocket.send(json.dumps({"type": "error", "msg": "Not recognized"}))
            return

        await websocket.send(json.dumps({"type": "transcript", "text": user_text}))

        # 2) Fuzzy match: STT halüsinasyonlarını kanonik komuta eşle
        #    "uşukları aç" -> "ışıkları aç" gibi. Sohbet/selam fuzzy'ye
        #    takılmaz, olduğu gibi agent'a gider.
        normalized = normalize_command(user_text)
        if normalized != user_text:
            print(f"[FUZZ] Sending normalized to agent: '{normalized}'")
            user_text = normalized

        await websocket.send(json.dumps({"type": "status", "msg": "Thinking..."}))

        # 3) Agentic run: LLM + tool calls + multi-step reasoning
        #    services/weather, time, matter, spotify, ... — atomic tool'lar
        response_text = await agent_run(session_id, user_text)
        response_text = clean_text_for_tts(response_text)
        print(f"[Agent] Response: '{response_text[:120]}'")

        if not response_text.strip():
            await websocket.send(json.dumps({"type": "error", "msg": "Empty response"}))
            return

        # 4) TEK TTS ÇAĞRISI — ArkadashSon f27ae9b orijinal mantığı.
        #    Cümle bazlı TTS yapma: her cümle için ayrı TTS API çağrısı = her
        #    cümlede ~2.5s latency (TTS API + ffmpeg decode) = P4 ring buffer o
        #    sürede tükenir → sonraki cümle drop. TEK çağrı = TEK 2.5s latency
        #    (cevap başında), kalan PCM stream tutarlı pacing ile akar. Atomic
        #    mimari kuralı atomic kalmak için "tüm yanıtı tek ses sentezi" daha
        #    doğal — birden fazla ses dosyası üretmiyoruz zaten.
        await websocket.send(json.dumps({"type": "tts_start"}))
        await asyncio.sleep(TTS_TTS_START_DELAY)  # P4 firmware settle
        total_pcm_bytes = 0
        async for pcm in tts_stream(response_text):
            await websocket.send(pcm)
            total_pcm_bytes += len(pcm)
            await asyncio.sleep(TTS_CHUNK_PACING_STEADY)
        await websocket.send(json.dumps({"type": "tts_end"}))

        if total_pcm_bytes == 0:
            await websocket.send(json.dumps({"type": "error", "msg": "TTS failed"}))
            return

        print(f"[TTS] Total PCM sent: {total_pcm_bytes} bytes")
        playback_duration = total_pcm_bytes / (16000 * 2)
        print(f"[+] Playback: {playback_duration:.2f}s")
        await asyncio.sleep(playback_duration + 0.5)

        await websocket.send(json.dumps({"type": "done"}))
        print("[+] Pipeline complete")

    except websockets.exceptions.ConnectionClosed:
        print("[-] Connection lost during pipeline")
    except Exception as e:
        print(f"[ERROR] {e}")
        traceback.print_exc()
        try:
            await websocket.send(json.dumps({"type": "error", "msg": "Server error"}))
        except Exception:
            pass
    finally:
        from orchestrator.network.websocket import active_pipelines
        active_pipelines.pop(session_id, None)


# ─── TTS Cümle-Bazlı Streaming ─────────────────────────────────────────────

# Cümle ayracı regex — final punctuation + ellipsis + newline
_SENTENCE_END_RE = re.compile(r"(?<=[.!?…])\s+|\n+")


def _split_sentences(text: str) -> list[str]:
    """Metni cümle sonlarına göre parçala. Boş string'leri filtrele."""
    raw = _SENTENCE_END_RE.split(text)
    return [s.strip() for s in raw if s.strip()]


async def _stream_response_sentences(websocket, response_text: str) -> int:
    """Response metnini cümlelere böl, her cümleyi TTS ile stream et.

    Her cümle için:
      - ws.send("tts_start")
      - TTS PCM chunk'larını ws.send + dinamik pacing
      - ws.send("tts_end")

    Returns:
        Toplam gönderilen PCM byte sayısı (playback duration hesabı için).
    """
    sentences = _split_sentences(response_text)
    total_sent = 0
    for sent in sentences:
        print(f"[TTS→] {sent[:60]}...")
        # Desktop orijinal "kaymak" versiyonu (Jun 11 agent_server.py):
        # tts_start → 50ms bekleme → chunk loop'ta pacing 0.030s sabit.
        # chunk_size 1024 byte TTS tarafında (services/tts.py).
        await websocket.send(json.dumps({"type": "tts_start"}))
        # 50ms bekleme: P4 firmware'in tts_start WS mesajını işleyip playback
        # ring buffer'ı hazırlaması için zaman tanır. Bu bekleme olmadan ilk
        # chunk geldiğinde ring buffer henüz setup edilmemiş olabilir → drop.
        await asyncio.sleep(TTS_TTS_START_DELAY)
        async for pcm in tts_stream(sent):
            await websocket.send(pcm)
            total_sent += len(pcm)
            # Pacing 0.030s sabit (Desktop orijinal):
            #   1024 byte / 30ms = 34 KB/s (16 kHz mono DMA 32 KB/s'den %6 hızlı).
            #   DMA consume rate'ine yakın → ring buffer asla dolmaz, drop ASLA olmaz.
            await asyncio.sleep(TTS_CHUNK_PACING_STEADY)
        await websocket.send(json.dumps({"type": "tts_end"}))
    return total_sent
