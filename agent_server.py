#!/usr/bin/env python3
"""
Voice Assistant Server for ESP32-P4
STT -> LLM -> TTS Pipeline
STT: whisper.cpp server | LLM: MiniMax-M2.7 | TTS: Edge

FIX: Session history artık WebSocket object ID yerine client IP adresine
     bağlıdır. ESP32 reconnect etse bile aynı IP'den geldiği için
     konuşma geçmişi korunur.
"""

import asyncio
import os
import json
import wave
import struct
import array
import tempfile
import httpx
import websockets
from typing import AsyncGenerator

MINIMAX_API_KEY  = os.environ.get("MINIMAX_API_KEY", "")
MINIMAX_GROUP_ID = os.environ.get("MINIMAX_GROUP_ID", "")
GEMINI_API_KEY   = os.environ.get("GEMINI_API_KEY", "")

TTS_BACKEND   = os.environ.get("TTS_BACKEND", "edge")
WHISPER_SERVER = "http://localhost:8080"

# ZeroClaw Gateway (opsiyonel)
ZEROCLAW_HOST  = os.environ.get("ZEROCLAW_HOST", "localhost")
ZEROCLAW_PORT  = int(os.environ.get("ZEROCLAW_PORT", "42617"))
ZEROCLAW_TOKEN = os.environ.get("ZEROCLAW_TOKEN", "zc_4795d5f23d70f2e908e7afc1d08df9700487a33d26820b99dcd728805097669f")
ZEROCLAW_WS_URL = f"ws://{ZEROCLAW_HOST}:{ZEROCLAW_PORT}/ws/chat?token={ZEROCLAW_TOKEN}"
USE_ZEROCLAW   = os.environ.get("USE_ZEROCLAW", "false").lower() == "true"

SYSTEM_PROMPT = """Sen Turkce konusan yardimci bir sesli asistansin.
SADECE Turkce cevap ver, asla baska dil kullanma.
Kisa ve dogal cevaplar ver, 1-2 cumle yeterli."""

# FIX: session key = client IP (str), reconnect sonrası history korunur
conversation_histories: dict[str, list] = {}
active_pipelines:       dict[str, bool]  = {}

# History kaç mesaj sonra eskiler temizlensin (token tasarrufu)
MAX_HISTORY_TURNS = 20  # kullanıcı + asistan mesaj sayısı


# ─── STT ──────────────────────────────────────────────────────────────────────

async def transcribe_audio(pcm_bytes: bytes) -> str:
    try:
        samples = struct.unpack(f"<{len(pcm_bytes)//2}h", pcm_bytes)
        mono_samples = array.array('h', [
            (samples[i] + samples[i+1]) // 2
            for i in range(0, len(samples) - 1, 2)
        ])

        if mono_samples:
            max_amp  = max(abs(s) for s in mono_samples)
            avg_amp  = sum(abs(s) for s in mono_samples) / len(mono_samples)
            duration = len(mono_samples) / 16000.0
            print(f"[STT] Audio: {len(mono_samples)} samples "
                  f"({duration:.2f}s), max={max_amp}, avg={avg_amp:.1f}")

        if len(mono_samples) < 4800:
            print(f"[STT] Audio too short: {len(mono_samples)/16000:.2f}s")
            return ""

        with tempfile.NamedTemporaryFile(suffix='.wav', delete=False) as f:
            temp_path = f.name

        with wave.open(temp_path, 'wb') as wf:
            wf.setnchannels(1)
            wf.setsampwidth(2)
            wf.setframerate(16000)
            wf.writeframes(mono_samples.tobytes())

        try:
            print("[STT] Transcribing with whisper.cpp server...")
            with open(temp_path, 'rb') as f:
                files = {'file': ('audio.wav', f, 'audio/wav')}
                async with httpx.AsyncClient(timeout=30) as client:
                    response = await client.post(
                        f"{WHISPER_SERVER}/inference",
                        files=files,
                        params={"output_format": "json"}
                    )
            if response.status_code == 200:
                result = response.json()
                text = (result.get("text", "") or result.get("output", "")).strip()
                print(f"[STT] Whisper: '{text}'")
                return text
            else:
                print(f"[STT] Error {response.status_code}: {response.text[:300]}")
                return ""
        finally:
            os.unlink(temp_path)

    except Exception as e:
        print(f"[STT] Error: {e}")
        import traceback; traceback.print_exc()
        return ""


# ─── ZeroClaw Client ─────────────────────────────────────────────────────────

async def zeroclaw_chat_stream(user_text: str) -> AsyncGenerator[str, None]:
    """ZeroClaw gateway'den streaming yanit al."""
    try:
        print(f"[ZeroClaw] Streaming: '{user_text[:50]}...'")
        
        async with websockets.connect(ZEROCLAW_WS_URL) as ws:
            await ws.send(json.dumps({"message": user_text}))
            
            async for message in ws:
                if isinstance(message, str):
                    try:
                        data = json.loads(message)
                        content = data.get("response", "") or data.get("content", "")
                        if content:
                            yield content
                    except json.JSONDecodeError:
                        continue
                elif isinstance(message, bytes):
                    continue  # Binary audio data
                    
    except Exception as e:
        print(f"[ZeroClaw] Error: {e}")


# ─── LLM ──────────────────────────────────────────────────────────────────────

async def chat_stream(session_id: str, user_text: str, use_zeroclaw: bool = False) -> AsyncGenerator[str, None]:
    """Stream chat response. session_id = client IP.
    
    use_zeroclaw=True → ZeroClaw gateway (Agent mode - multi-step tasks)
    use_zeroclaw=False → MiniMax API (Chat mode - low latency)
    """

    if session_id not in conversation_histories:
        conversation_histories[session_id] = []

    history = conversation_histories[session_id]
    history.append({"role": "user", "content": user_text})

    # ZeroClaw kullan (Agent mode - multi-step)
    if use_zeroclaw:
        full_response = ""
        async for token in zeroclaw_chat_stream(user_text):
            full_response += token
            yield token
        
        if full_response:
            full_response = full_response.strip()
            print(f"[ZeroClaw] Full: '{full_response}'")
            history.append({"role": "assistant", "content": full_response})
        else:
            if history and history[-1]["role"] == "user":
                history.pop()
        return

    # MiniMax API kullan (Chat mode - düsük gecikme)
    url = f"https://api.minimax.io/v1/text/chatcompletion_v2?GroupId={MINIMAX_GROUP_ID}"
    headers = {
        "Authorization": f"Bearer {MINIMAX_API_KEY}",
        "Content-Type": "application/json"
    }

    messages = [{"role": "system", "content": SYSTEM_PROMPT}]
    messages.extend(history[-MAX_HISTORY_TURNS:])

    payload = {
        "model": "MiniMax-M2.7",
        "messages": messages,
        "stream": True,
        "max_tokens": 300,
        "temperature": 0.7,
    }

    full_response = ""
    debug_count   = 0

    try:
        async with httpx.AsyncClient(timeout=60) as client:
            async with client.stream("POST", url, headers=headers, json=payload) as resp:
                if resp.status_code != 200:
                    error_text = await resp.aread()
                    print(f"[LLM] HTTP {resp.status_code}: {error_text[:300]}")
                    if resp.status_code == 529:
                        print("[LLM] Server overloaded, waiting 5s...")
                        await asyncio.sleep(5)
                    return

                async for line in resp.aiter_lines():
                    line = line.strip()
                    if not line or line == "data: ":
                        continue
                    if line.startswith("data:"):
                        line = line[5:].strip()
                    if not line or line == "[DONE]" or line == "{}":
                        continue

                    debug_count += 1
                    if debug_count <= 3:
                        print(f"[LLM-DEBUG] Line #{debug_count}: {repr(line[:150])}")

                    try:
                        data = json.loads(line)

                        if data.get("type") == "error":
                            print(f"[LLM] Stream error: "
                                  f"{data.get('error', {}).get('message', 'Unknown')}")
                            break

                        content = None
                        if "choices" in data and data["choices"]:
                            choice = data["choices"][0]
                            if "message" in choice and "content" in choice["message"]:
                                content = choice["message"]["content"]
                            elif "delta" in choice and "content" in choice["delta"]:
                                content = choice["delta"]["content"]
                            elif "text" in choice:
                                content = choice["text"]
                            if choice.get("finish_reason") and not content:
                                break

                        if content:
                            if full_response.endswith(content):
                                continue
                            full_response += content
                            yield content

                    except json.JSONDecodeError:
                        continue
                    except (KeyError, IndexError, TypeError) as e:
                        print(f"[LLM] Parse error: {e}")
                        continue

    except httpx.RequestError as e:
        print(f"[LLM] Network error: {e}")
    except Exception as e:
        print(f"[LLM] Unexpected error: {type(e).__name__}: {e}")
        import traceback; traceback.print_exc()

    if full_response:
        full_response = full_response.strip()
        print(f"[LLM] Full response: '{full_response}' ({len(full_response)} chars)")
        history.append({"role": "assistant", "content": full_response})
        print(f"[LLM] History: {len(history)} messages (ip={session_id})")
    else:
        print("[LLM] Empty response")
        # Boş yanıtta user mesajını geri al — bağlam bozulmasın
        if history and history[-1]["role"] == "user":
            history.pop()


# ─── TTS Helpers ───────────────────────────────────────────────────────────────

def clean_text_for_tts(text: str) -> str:
    """Remove emojis and problematic characters before TTS."""
    import re
    # Remove unicode emojis
    emoji_pattern = re.compile(
        "["
        "\U0001F600-\U0001F64F"  # emoticons
        "\U0001F300-\U0001F5FF"  # symbols & pictographs
        "\U0001F680-\U0001F6FF"  # transport & map symbols
        "\U0001F1E0-\U0001F1FF"  # flags
        "\U00002702-\U000027B0"
        "\U000024C2-\U0001F251"
        "\U0001F900-\U0001F9FF"  # supplemental symbols
        "\U0001FA00-\U0001FA6F"  # chess symbols
        "\U0001FA70-\U0001FAFF"  # symbols extended
        "\U00002600-\U000026FF"  # misc symbols
        "\U00002700-\U000027BF"  # dingbats
        "]+", flags=re.UNICODE
    )
    text = emoji_pattern.sub('', text)
    # Remove :emoji: text format
    text = re.sub(r':[a-zA-Z_]+:', '', text)
    # Collapse multiple spaces
    text = re.sub(r'\s+', ' ', text)
    return text.strip()

# ─── TTS ──────────────────────────────────────────────────────────────────────

async def tts_edge_stream(text: str) -> AsyncGenerator[bytes, None]:
    try:
        import edge_tts
        import subprocess

        print(f"[TTS-Edge] Requesting: '{text[:50]}...'")

        communicate = edge_tts.Communicate(text, voice="tr-TR-AhmetNeural")
        mp3_chunks = []
        async for chunk in communicate.stream():
            if chunk["type"] == "audio":
                mp3_chunks.append(chunk["data"])

        if not mp3_chunks:
            print("[TTS-Edge] No audio received")
            return

        mp3_data = b''.join(mp3_chunks)
        print(f"[TTS-Edge] MP3: {len(mp3_data)} bytes")

        with tempfile.NamedTemporaryFile(suffix='.mp3', delete=False) as f_in:
            f_in.write(mp3_data)
            mp3_path = f_in.name
        pcm_path = mp3_path + '.pcm'

        try:
            result = subprocess.run([
                'ffmpeg', '-y', '-f', 'mp3', '-i', mp3_path,
                '-f', 's16le', '-acodec', 'pcm_s16le',
                '-ar', '16000', '-ac', '2', pcm_path
            ], capture_output=True, timeout=30)

            if result.returncode != 0:
                print(f"[TTS-Edge] ffmpeg error: {result.stderr.decode()[:200]}")
                return

            with open(pcm_path, 'rb') as f:
                pcm_data = f.read()

            duration = len(pcm_data) / (16000 * 2 * 2)
            print(f"[TTS-Edge] PCM: {len(pcm_data)} bytes, {duration:.2f}s @ 16kHz stereo")

            # Küçük chunk'lar - ESP daha hızlı alır, TCP tamponı dolmaz
            chunk_size = 1024
            for i in range(0, len(pcm_data), chunk_size):
                yield pcm_data[i:i+chunk_size]

            print("[TTS-Edge] Complete")

        finally:
            os.unlink(mp3_path)
            if os.path.exists(pcm_path):
                os.unlink(pcm_path)

    except Exception as e:
        print(f"[TTS-Edge] Error: {type(e).__name__}: {e}")
        import traceback; traceback.print_exc()


async def tts_gemini_stream(text: str) -> AsyncGenerator[bytes, None]:
    try:
        from google import genai
        from google.genai import types
        import numpy as np
        from scipy.signal import resample_poly

        client = genai.Client(api_key=GEMINI_API_KEY)
        response = client.models.generate_content(
            model="gemini-3.1-flash-tts-preview",
            contents=text,
            config=types.GenerateContentConfig(
                response_modalities=["AUDIO"],
                speech_config=types.SpeechConfig(
                    voice_config=types.VoiceConfig(
                        prebuilt_voice_config=types.PrebuiltVoiceConfig(voice_name='Kore')
                    )
                ),
            )
        )
        audio_data = response.candidates[0].content.parts[0].inline_data.data
        samples_24k = np.frombuffer(audio_data, dtype=np.int16)
        samples_16k = resample_poly(samples_24k, 2, 3)
        stereo = np.empty(len(samples_16k) * 2, dtype=np.int16)
        stereo[0::2] = samples_16k
        stereo[1::2] = samples_16k
        pcm_output = stereo.tobytes()
        for i in range(0, len(pcm_output), 4096):
            yield pcm_output[i:i+4096]
    except Exception as e:
        print(f"[TTS-Gemini] Error: {type(e).__name__}: {e}")


async def tts_google_cloud_stream(text: str) -> AsyncGenerator[bytes, None]:
    try:
        import base64
        import numpy as np
        from scipy.signal import resample_poly

        url = "https://texttospeech.googleapis.com/v1/text:synthesize?key=" + GEMINI_API_KEY
        payload = {
            "input": {"text": text},
            "voice": {"languageCode": "tr-TR", "name": "tr-TR-Wavenet-A", "ssmlGender": "FEMALE"},
            "audioConfig": {"audioEncoding": "LINEAR16", "sampleRateHertz": 24000,
                            "effectsProfileId": ["headphone-class-device"]}
        }
        async with httpx.AsyncClient(timeout=30) as client:
            response = await client.post(url, json=payload)
        if response.status_code != 200:
            print(f"[TTS-GCloud] Error {response.status_code}")
            return
        audio_data = base64.b64decode(response.json()["audioContent"])
        samples_24k = np.frombuffer(audio_data, dtype=np.int16)
        samples_16k = resample_poly(samples_24k, 2, 3).astype(np.int16)
        stereo = np.empty(len(samples_16k) * 2, dtype=np.int16)
        stereo[0::2] = samples_16k
        stereo[1::2] = samples_16k
        pcm_output = stereo.tobytes()
        for i in range(0, len(pcm_output), 4096):
            yield pcm_output[i:i+4096]
    except Exception as e:
        print(f"[TTS-GCloud] Error: {type(e).__name__}: {e}")


async def tts_stream(text: str) -> AsyncGenerator[bytes, None]:
    if TTS_BACKEND == "edge":
        async for chunk in tts_edge_stream(text):
            yield chunk
    elif TTS_BACKEND == "google_cloud":
        async for chunk in tts_google_cloud_stream(text):
            yield chunk
    else:
        async for chunk in tts_gemini_stream(text):
            yield chunk


# ─── Pipeline ─────────────────────────────────────────────────────────────────

async def process_audio(websocket, session_id: str, audio_buffer: bytearray, use_zeroclaw: bool = False):
    """STT → LLM → TTS. session_id = client IP adresi.
    
    use_zeroclaw=True → ZeroClaw (Agent mode)
    use_zeroclaw=False → MiniMax (Chat mode)
    """

    if len(audio_buffer) < 4000:
        await websocket.send(json.dumps({"type": "error", "msg": "Audio too short"}))
        return

    mode_str = "ZeroClaw (Agent)" if use_zeroclaw else "MiniMax (Chat)"
    print(f"[*] Processing with: {mode_str}")

    try:
        await websocket.send(json.dumps({"type": "status", "msg": "Processing..."}))

        user_text = await transcribe_audio(bytes(audio_buffer))
        print(f"[STT] Text: '{user_text}'")

        if not user_text.strip():
            await websocket.send(json.dumps({"type": "error", "msg": "Not recognized"}))
            return

        await websocket.send(json.dumps({"type": "transcript", "text": user_text}))
        await websocket.send(json.dumps({"type": "status", "msg": "Thinking..."}))

        sentence_buf = ""
        async for token in chat_stream(session_id, user_text, use_zeroclaw):
            sentence_buf += token

        sentence_buf = sentence_buf.strip()
        if not sentence_buf:
            await websocket.send(json.dumps({"type": "error", "msg": "LLM empty response"}))
            return

        # TTS'den önce emojileri temizle - ses patlamasını önler
        tts_text = clean_text_for_tts(sentence_buf)
        print(f"[TTS] Synthesizing: {tts_text[:60]}...")
        await websocket.send(json.dumps({"type": "tts_start"}))
        await asyncio.sleep(0.05)

        total_pcm_bytes = 0
        async for pcm in tts_stream(tts_text):
            await websocket.send(pcm)
            total_pcm_bytes += len(pcm)
            await asyncio.sleep(0.03)

        await asyncio.sleep(0.2)
        print(f"[TTS] All PCM sent ({total_pcm_bytes} bytes), sending tts_end")
        await websocket.send(json.dumps({"type": "tts_end", "size": total_pcm_bytes}))

        playback_duration = total_pcm_bytes / (16000 * 2 * 2)
        print(f"[+] Playback: {playback_duration:.2f}s")
        await asyncio.sleep(playback_duration + 0.5)

        await websocket.send(json.dumps({"type": "done"}))
        print("[+] Pipeline complete")

    except websockets.exceptions.ConnectionClosed:
        print("[-] Connection lost during pipeline")
    except Exception as e:
        print(f"[ERROR] {e}")
        import traceback; traceback.print_exc()
        try:
            await websocket.send(json.dumps({"type": "error", "msg": "Server error"}))
        except Exception:
            pass
    finally:
        active_pipelines.pop(session_id, None)


# ─── WebSocket handler ────────────────────────────────────────────────────────

async def handle_client(websocket):
    # FIX: session_id = IP adresi. Reconnect sonrası aynı IP → aynı history.
    client_ip  = websocket.remote_address[0]
    session_id = client_ip
    print(f"[+] Client connected: {websocket.remote_address} (session={session_id})")

    audio_buffer      = bytearray()
    recording         = False
    expected_audio_len = 0
    use_zeroclaw      = False  # Chat=False (MiniMax), Agent=True (ZeroClaw)

    try:
        async for message in websocket:
            msg_len  = len(message) if isinstance(message, (bytes, bytearray, str)) else 0
            msg_repr = repr(message)[:50] if isinstance(message, str) else "N/A"
            print(f"[DEBUG] type={type(message).__name__} len={msg_len} repr={msg_repr}")

            if isinstance(message, str):
                msg_stripped = message.strip()

                # Mod degistirme mesajlari
                if msg_stripped == "CHAT_START":
                    use_zeroclaw = False
                    print("[*] Mode: Chat (MiniMax - düsük gecikme)")
                    continue
                elif msg_stripped == "AGENT_START":
                    use_zeroclaw = True
                    print("[*] Mode: Agent (ZeroClaw - çok adımlı görevler)")

                elif msg_stripped in ("PTT_START", "PTT_STAR", "PTT_STA"):
                    if session_id in active_pipelines:
                        print("[!] Pipeline already running, ignoring PTT_START")
                        continue
                    audio_buffer.clear()
                    recording          = True
                    expected_audio_len = 0
                    print(f"[*] Recording started (msg: {repr(message)})")

                elif msg_stripped in ("PTT_STOP", "PTT_STO", "PTT_ST"):
                    recording = False
                    print(f"[*] Recording stopped - {len(audio_buffer)} bytes")
                    if len(audio_buffer) >= 4000:
                        active_pipelines[session_id] = True
                        snapshot = bytearray(audio_buffer)
                        asyncio.ensure_future(
                            process_audio(websocket, session_id, snapshot, use_zeroclaw)
                        )
                    else:
                        print(f"[!] Audio too short: {len(audio_buffer)} bytes")

            elif isinstance(message, bytes):
                msg_bytes = message.strip()

                if msg_bytes in (b"PTT_START", b"PTT_STAR", b"PTT_STA"):
                    if session_id in active_pipelines:
                        print("[!] Pipeline already running, ignoring PTT_START")
                        continue
                    audio_buffer.clear()
                    recording          = True
                    expected_audio_len = 0
                    print(f"[*] Recording started (bytes)")

                elif msg_bytes in (b"PTT_STOP", b"PTT_STO", b"PTT_ST"):
                    recording = False
                    print(f"[*] Recording stopped (bytes) - {len(audio_buffer)} bytes")
                    if len(audio_buffer) >= 4000:
                        active_pipelines[session_id] = True
                        snapshot = bytearray(audio_buffer)
                        asyncio.ensure_future(
                            process_audio(websocket, session_id, snapshot, use_zeroclaw)
                        )

                elif len(message) == 4 and expected_audio_len == 0:
                    size = struct.unpack('>I', message)[0]
                    if 1000 < size < 1_000_000:
                        expected_audio_len = size
                        print(f"[*] Audio size header: {expected_audio_len} bytes")

                elif recording:
                    audio_buffer.extend(message)
                    if len(audio_buffer) % 8192 == 0 or len(audio_buffer) == expected_audio_len:
                        print(f"[*] Buffer: {len(audio_buffer)}/{expected_audio_len} bytes")

    except websockets.exceptions.ConnectionClosed:
        print(f"[-] Connection closed ({session_id})")
    finally:
        active_pipelines.pop(session_id, None)
        # FIX: history'yi silme! Aynı IP tekrar bağlandığında korunacak.
        # Sadece yeni bağlantı günlüğünü yaz.
        print(f"[-] Session {session_id} disconnected "
              f"(history={len(conversation_histories.get(session_id, []))} msgs, preserved)")


# ─── Main ─────────────────────────────────────────────────────────────────────

async def main():
    # ZeroClaw modu kullaniliyorsa MiniMax gerekli degil
    if USE_ZEROCLAW:
        print("=" * 60)
        print("  Voice Assistant Server v2.3 (ZeroClaw Mode)")
        print("=" * 60)
        print(f"  ZeroClaw: {ZEROCLAW_WS_URL[:50]}...")
        print("  STT: whisper.cpp server (local)")
        print(f"  TTS: {TTS_BACKEND}")
        print("  Session: IP-based (reconnect-safe)")
        print("  ws://0.0.0.0:8765")
        print("=" * 60)
    else:
        if not MINIMAX_API_KEY:
            print("ERROR: MINIMAX_API_KEY not set!"); return
        if not MINIMAX_GROUP_ID:
            print("ERROR: MINIMAX_GROUP_ID not set!"); return
        if TTS_BACKEND != "edge" and not GEMINI_API_KEY:
            print(f"ERROR: GEMINI_API_KEY not set (required for TTS_BACKEND={TTS_BACKEND})!"); return

        print("=" * 60)
        print("  Voice Assistant Server v2.2 (MiniMax Mode)")
        print("=" * 60)
        print("  STT: whisper.cpp server (local)")
        print("  LLM: MiniMax-M2.7")
        print(f"  TTS: {TTS_BACKEND}")
        print("  Session: IP-based (reconnect-safe)")
        print("  ws://0.0.0.0:8765")
        print("=" * 60)

    async with websockets.serve(handle_client, "0.0.0.0", 8765):
        await asyncio.Future()


if __name__ == "__main__":
    asyncio.run(main())