"""
TTS (Text-to-Speech) backend'leri.

Desteklenen backend'ler (config.TTS_BACKEND ile seçilir):
  - "minimax":      MiniMax speech-2.8-hd, Turkish_CalmWoman (default)
  - "edge":         Microsoft Edge TTS (tr-TR-AhmetNeural)
  - "google_cloud": Google Cloud Text-to-Speech (tr-TR-Wavenet-A)
  - "gemini":       Gemini gemini-2.5-flash-tts-preview

Akış (minimax):
  1. POST /t2a_v2 sync (cumulative streaming bug var, sync kullanıyoruz)
  2. mp3 hex decode → tempfile mp3 yaz
  3. ffmpeg subprocess: mp3 → 24kHz s16le mono
  4. utils.audio.soft_limit (DAC clipping önleme)
  5. utils.audio.resample_24k_to_16k (P4 I2S uyumlu)
  6. 4096-byte chunk'larda yield et (pacing agent tarafında)

Hata durumunda log yazar, yield etmez → process_audio boş PCM alır → sessizlik.
"""

import asyncio
import os
import subprocess
import tempfile
from typing import AsyncGenerator

import httpx
import numpy as np

from orchestrator.config import GEMINI_API_KEY, MINIMAX_API_KEY, MINIMAX_GROUP_ID
from orchestrator.utils.audio import resample_24k_to_16k, soft_limit


# ─── Backend Wrapper'ları ───────────────────────────────────────────────────

async def tts_minimax_stream(text: str) -> AsyncGenerator[bytes, None]:
    """MiniMax speech-hd TTS — sync çağrı + mp3 decode + resample.

    Neden sync PCM streaming DEĞİL: MiniMax T2A stream cumulative hex yolluyor
    (her chunk önceki tüm sesin üstüne eklenmiş hali) — P4'e aynı sesin üst üste
    binen kopyalarını oynatıyor ("cümle iki kere" + "cazırtı"). Sync çağrı + mp3
    decode bu sorunu kökünden çözer.
    """
    try:
        url = f"https://api.minimax.io/v1/t2a_v2?GroupId={MINIMAX_GROUP_ID}"
        headers = {
            "Authorization": f"Bearer {MINIMAX_API_KEY}",
            "Content-Type": "application/json",
        }
        payload = {
            "model": "speech-2.8-hd",
            "text": text,
            "stream": False,
            "voice_setting": {
                "voice_id": "Turkish_CalmWoman",
            },
            "audio_setting": {
                "sample_rate": 24000,
                "bitrate": 128000,
                "format": "mp3",
            },
            "language_boost": "Turkish",
        }

        print(f"[TTS-MiniMax] Requesting (sync mp3): '{text[:50]}...'")

        async with httpx.AsyncClient(timeout=120) as client:
            response = await client.post(url, headers=headers, json=payload)

        if response.status_code != 200:
            print(f"[TTS-MiniMax] HTTP {response.status_code}: {response.text[:300]}")
            return

        try:
            json_data = response.json()
        except Exception:
            print(f"[TTS-MiniMax] Failed to parse JSON: {response.text[:200]}")
            return

        audio_hex = json_data.get("data", {}).get("audio", "")
        if not audio_hex:
            print(f"[TTS-MiniMax] No audio in response: {list(json_data.keys())}")
            return

        mp3_data = bytes.fromhex(audio_hex)
        print(f"[TTS-MiniMax] MP3: {len(mp3_data)} bytes")

        # Geçici dosyalara yaz, ffmpeg ile decode et
        with tempfile.NamedTemporaryFile(suffix=".mp3", delete=False) as f_in:
            f_in.write(mp3_data)
            mp3_path = f_in.name
        pcm_path = mp3_path + ".pcm"

        try:
            # Async subprocess: ffmpeg çalışırken event loop bloklanmaz,
            # WebSocket/health/telemetry frame'leri kaçırılmaz.
            process = await asyncio.create_subprocess_exec(
                "ffmpeg", "-y", "-v", "error", "-f", "mp3", "-i", mp3_path,
                "-f", "s16le", "-acodec", "pcm_s16le",
                "-ar", "24000", "-ac", "1", pcm_path,
                stdout=asyncio.subprocess.DEVNULL,
                stderr=asyncio.subprocess.PIPE,
            )
            try:
                _, stderr = await asyncio.wait_for(process.communicate(), timeout=60)
            except asyncio.TimeoutError:
                process.kill()
                print("[TTS-MiniMax] ffmpeg timeout, killed")
                return
            if process.returncode != 0:
                err = stderr.decode("utf-8", errors="replace")[:500]
                print(f"[TTS-MiniMax] ffmpeg error: {err}")
                return

            with open(pcm_path, "rb") as f:
                pcm_data = f.read()

            # 24kHz mono int16 → soft limit (clipping önleme) → 16kHz mono
            # drive=1.3, gain=0.65: max sample ~12190 (%74 full scale).
            # ES8311 hardware volume 100'de — speaker max güçte cızırtı yaptı.
            # 0.65 + ES8311 80 dengeli; pre-DAC clipping yok, hardware boost
            # yeterli.
            samples_24k = np.frombuffer(pcm_data, dtype=np.int16)
            limited = soft_limit(samples_24k, drive=1.3, gain=0.65)
            samples_16k = resample_24k_to_16k(limited)
            pcm_mono = samples_16k.tobytes()

            duration = len(pcm_mono) / (16000 * 2)
            print(f"[TTS-MiniMax] PCM: {len(pcm_mono)} bytes, "
                  f"{duration:.2f}s @ 16kHz mono")

            chunk_size = 4096
            for i in range(0, len(pcm_mono), chunk_size):
                yield pcm_mono[i:i + chunk_size]

            print("[TTS-MiniMax] Complete")

        finally:
            os.unlink(mp3_path)
            if os.path.exists(pcm_path):
                os.unlink(pcm_path)

    except Exception as e:
        print(f"[TTS-MiniMax] Error: {type(e).__name__}: {e}")
        import traceback
        traceback.print_exc()


async def tts_edge_stream(text: str) -> AsyncGenerator[bytes, None]:
    """Microsoft Edge TTS (ücretsiz, online)."""
    try:
        import edge_tts

        print(f"[TTS-Edge] Requesting: '{text[:50]}...'")

        communicate = edge_tts.Communicate(text, voice="tr-TR-AhmetNeural")
        mp3_chunks: list[bytes] = []
        async for chunk in communicate.stream():
            if chunk["type"] == "audio":
                mp3_chunks.append(chunk["data"])

        if not mp3_chunks:
            print("[TTS-Edge] No audio received")
            return

        mp3_data = b"".join(mp3_chunks)
        print(f"[TTS-Edge] MP3: {len(mp3_data)} bytes")

        with tempfile.NamedTemporaryFile(suffix=".mp3", delete=False) as f_in:
            f_in.write(mp3_data)
            mp3_path = f_in.name
        pcm_path = mp3_path + ".pcm"

        try:
            # Async subprocess: ffmpeg çalışırken event loop bloklanmaz.
            process = await asyncio.create_subprocess_exec(
                "ffmpeg", "-y", "-v", "error", "-f", "mp3", "-i", mp3_path,
                "-f", "s16le", "-acodec", "pcm_s16le",
                "-ar", "16000", "-ac", "1", pcm_path,  # P4 I2S_SLOT_MODE_MONO
                stdout=asyncio.subprocess.DEVNULL,
                stderr=asyncio.subprocess.PIPE,
            )
            try:
                _, stderr = await asyncio.wait_for(process.communicate(), timeout=30)
            except asyncio.TimeoutError:
                process.kill()
                print("[TTS-Edge] ffmpeg timeout, killed")
                return
            if process.returncode != 0:
                err = stderr.decode("utf-8", errors="replace")[:200]
                print(f"[TTS-Edge] ffmpeg error: {err}")
                return

            with open(pcm_path, "rb") as f:
                pcm_data = f.read()

            duration = len(pcm_data) / (16000 * 2 * 2)
            print(f"[TTS-Edge] PCM: {len(pcm_data)} bytes, "
                  f"{duration:.2f}s @ 16kHz stereo")

            chunk_size = 4096
            for i in range(0, len(pcm_data), chunk_size):
                yield pcm_data[i:i + chunk_size]

            print("[TTS-Edge] Complete")

        finally:
            os.unlink(mp3_path)
            if os.path.exists(pcm_path):
                os.unlink(pcm_path)

    except Exception as e:
        print(f"[TTS-Edge] Error: {type(e).__name__}: {e}")
        import traceback
        traceback.print_exc()


async def tts_gemini_stream(text: str) -> AsyncGenerator[bytes, None]:
    """Google Gemini TTS (gemini-2.5-flash-tts-preview)."""
    try:
        from google import genai
        from google.genai import types

        client = genai.Client(api_key=GEMINI_API_KEY)
        response = client.models.generate_content(
            model="gemini-2.5-flash-tts-preview",
            contents=text,
            config=types.GenerateContentConfig(
                response_modalities=["AUDIO"],
                speech_config=types.SpeechConfig(
                    voice_config=types.VoiceConfig(
                        prebuilt_voice_config=types.PrebuiltVoiceConfig(voice_name="Kore")
                    )
                ),
            ),
        )
        audio_data = response.candidates[0].content.parts[0].inline_data.data
        samples_24k = np.frombuffer(audio_data, dtype=np.int16)
        samples_16k = resample_24k_to_16k(samples_24k)
        pcm_output = samples_16k.tobytes()  # P4 I2S_SLOT_MODE_MONO
        for i in range(0, len(pcm_output), 4096):
            yield pcm_output[i:i + 4096]
    except Exception as e:
        print(f"[TTS-Gemini] Error: {type(e).__name__}: {e}")


async def tts_google_cloud_stream(text: str) -> AsyncGenerator[bytes, None]:
    """Google Cloud Text-to-Speech (tr-TR-Wavenet-A)."""
    try:
        import base64
        from scipy.signal import resample_poly

        url = "https://texttospeech.googleapis.com/v1/text:synthesize?key=" + GEMINI_API_KEY
        payload = {
            "input": {"text": text},
            "voice": {
                "languageCode": "tr-TR",
                "name": "tr-TR-Wavenet-A",
                "ssmlGender": "FEMALE",
            },
            "audioConfig": {
                "audioEncoding": "LINEAR16",
                "sampleRateHertz": 24000,
                "effectsProfileId": ["headphone-class-device"],
            },
        }
        async with httpx.AsyncClient(timeout=30) as client:
            response = await client.post(url, json=payload)
        if response.status_code != 200:
            print(f"[TTS-GCloud] Error {response.status_code}")
            return
        audio_data = base64.b64decode(response.json()["audioContent"])
        samples_24k = np.frombuffer(audio_data, dtype=np.int16)
        samples_16k = resample_poly(samples_24k, 2, 3).astype(np.int16)
        pcm_output = samples_16k.tobytes()  # P4 I2S_SLOT_MODE_MONO
        for i in range(0, len(pcm_output), 4096):
            yield pcm_output[i:i + 4096]
    except Exception as e:
        print(f"[TTS-GCloud] Error: {type(e).__name__}: {e}")


# ─── TTS Router ─────────────────────────────────────────────────────────────

async def tts_stream(text: str) -> AsyncGenerator[bytes, None]:
    """TTS_BACKEND'e göre doğru backend'e yönlendir. Tüm akış için tek giriş."""
    from orchestrator.config import TTS_BACKEND

    if TTS_BACKEND == "edge":
        async for chunk in tts_edge_stream(text):
            yield chunk
    elif TTS_BACKEND == "google_cloud":
        async for chunk in tts_google_cloud_stream(text):
            yield chunk
    elif TTS_BACKEND == "gemini":
        async for chunk in tts_gemini_stream(text):
            yield chunk
    else:  # "minimax" (default)
        async for chunk in tts_minimax_stream(text):
            yield chunk
