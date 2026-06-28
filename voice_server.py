#!/usr/bin/env python3
"""
Voice Assistant Server v4.0
==========================
STT → LangGraph (MiniMax) → TTS
STT: whisper.cpp server (local) | LLM: MiniMax | TTS: Edge

Environment Variables:
    MINIMAX_API_KEY      - MiniMax API key
    MINIMAX_GROUP_ID     - MiniMax group ID
    GEMINI_API_KEY       - Google Gemini API key (alternative TTS)
    TTS_BACKEND          - TTS backend: "edge" (default), "gemini", "google_cloud"
    WHISPER_SERVER       - whisper.cpp server URL (default: http://localhost:8080)
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

# ─── Configuration ──────────────────────────────────────────────────────────────

MINIMAX_API_KEY   = os.environ.get("MINIMAX_API_KEY", "")
MINIMAX_GROUP_ID  = os.environ.get("MINIMAX_GROUP_ID", "")
GEMINI_API_KEY    = os.environ.get("GEMINI_API_KEY", "")

TTS_BACKEND       = os.environ.get("TTS_BACKEND", "edge")
WHISPER_SERVER    = os.environ.get("WHISPER_SERVER", "http://localhost:8080")

# System prompt
SYSTEM_PROMPT = """Sen Turkce konusan yardimci bir sesli asistansin.
SADECE Turkce cevap ver, asla baska dil kullanma.
Kisa ve dogal cevaplar ver, 1-2 cumle yeterli."""

# ─── Session Management ──────────────────────────────────────────────────────

conversation_histories: dict[str, list] = {}
active_pipelines:       dict[str, bool]  = {}
MAX_HISTORY_TURNS = 20


# ─── LangGraph State ─────────────────────────────────────────────────────────

class AssistantState:
    """Simple state for LangGraph-like flow.
    
    Future extensions:
    - Tool calling (calculator, file creation, web search)
    - Multi-step reasoning
    - Memory management
    """
    def __init__(self, user_text: str, session_id: str):
        self.user_text = user_text
        self.session_id = session_id
        self.messages = conversation_histories.get(session_id, [])
        self.response = ""
        self.needs_tool = False
        self.tool_name = None
        self.tool_result = None


# ─── STT ──────────────────────────────────────────────────────────────────────

async def transcribe_audio(pcm_bytes: bytes) -> str:
    """Convert PCM audio to text using whisper.cpp server."""
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


# ─── LLM ──────────────────────────────────────────────────────────────────────

async def chat_stream(state: AssistantState) -> AsyncGenerator[str, None]:
    """Stream chat response using MiniMax.
    
    LangGraph-like flow:
    1. Router: Analyze user intent (future: tool selection)
    2. LLM: Generate response
    3. Output: Return to user
    
    For now, it's a simple pass-through to MiniMax.
    Future: Add tool nodes for calculator, file creation, etc.
    """
    
    url = f"https://api.minimax.io/v1/text/chatcompletion_v2?GroupId={MINIMAX_GROUP_ID}"
    headers = {
        "Authorization": f"Bearer {MINIMAX_API_KEY}",
        "Content-Type": "application/json"
    }

    messages = [{"role": "system", "content": SYSTEM_PROMPT}]
    messages.extend(state.messages[-MAX_HISTORY_TURNS:])
    messages.append({"role": "user", "content": state.user_text})

    payload = {
        "model": "MiniMax-M2.7",
        "messages": messages,
        "stream": True,
        "max_tokens": 300,
        "temperature": 0.7,
    }

    full_response = ""

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

    # Update state
    state.response = full_response.strip()
    state.messages.append({"role": "user", "content": state.user_text})
    if state.response:
        state.messages.append({"role": "assistant", "content": state.response})
        conversation_histories[state.session_id] = state.messages


# ─── TTS Helpers ────────────────────────────────────────────────────────────────

def clean_text_for_tts(text: str) -> str:
    """Remove emojis and problematic characters before TTS."""
    import re
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
    text = re.sub(r':[a-zA-Z_]+:', '', text)
    text = re.sub(r'\s+', ' ', text)
    return text.strip()


# ─── TTS ───────────────────────────────────────────────────────────────────────

async def tts_edge_stream(text: str) -> AsyncGenerator[bytes, None]:
    """Edge TTS streaming."""
    try:
        import edge_tts
        import subprocess

        print(f"[TTS-Edge] Requesting: '{text[:50]}...'")

        communicate = edge_tts.Communicate(text, voice="tr-TR-AhmetNeural", rate="-20%")
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
                '-ar', '16000', '-ac', '1', pcm_path
            ], capture_output=True, timeout=30)

            if result.returncode != 0:
                print(f"[TTS-Edge] ffmpeg error: {result.stderr.decode()[:200]}")
                return

            with open(pcm_path, 'rb') as f:
                pcm_data = f.read()

            print(f"[TTS-Edge] PCM: {len(pcm_data)} bytes")

            chunk_size = 4096
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
    """Gemini TTS streaming."""
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
    """Google Cloud TTS streaming."""
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
    """Route TTS request to appropriate backend."""
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

async def process_audio(websocket, session_id: str, audio_buffer: bytearray):
    """STT → LLM → TTS pipeline.
    
    LangGraph flow:
    1. STT node: Audio → Text
    2. Router node: Analyze intent (future: tool selection)
    3. LLM node: Generate response (MiniMax)
    4. Output node: TTS synthesis
    
    Currently simplified - direct MiniMax → TTS
    Future: Add tool nodes between LLM and output
    """
    if len(audio_buffer) < 4000:
        await websocket.send(json.dumps({"type": "error", "msg": "Audio too short"}))
        return

    print(f"[*] Processing audio: {len(audio_buffer)} bytes")

    try:
        await websocket.send(json.dumps({"type": "status", "msg": "Processing..."}))

        # STT node
        user_text = await transcribe_audio(bytes(audio_buffer))
        print(f"[STT] Text: '{user_text}'")

        if not user_text.strip():
            await websocket.send(json.dumps({"type": "error", "msg": "Not recognized"}))
            return

        await websocket.send(json.dumps({"type": "transcript", "text": user_text}))
        await websocket.send(json.dumps({"type": "status", "msg": "Thinking..."}))

        # LLM node (with LangGraph state)
        state = AssistantState(user_text, session_id)
        sentence_buf = ""
        async for token in chat_stream(state):
            sentence_buf += token

        sentence_buf = sentence_buf.strip()
        if not sentence_buf:
            await websocket.send(json.dumps({"type": "error", "msg": "LLM empty response"}))
            return

        # Output node - TTS
        tts_text = clean_text_for_tts(sentence_buf)
        print(f"[TTS] Synthesizing: {tts_text[:60]}...")
        await websocket.send(json.dumps({"type": "tts_start"}))
        await asyncio.sleep(0.05)

        total_pcm_bytes = 0
        async for pcm in tts_stream(tts_text):
            await websocket.send(pcm)
            total_pcm_bytes += len(pcm)
            await asyncio.sleep(0.10)

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


# ─── WebSocket handler ─────────────────────────────────────────────────────────

async def handle_client(websocket):
    """Handle ESP32 client connection.
    
    Protocol (simplified from v3.0):
      - "PTT_START" → Start recording audio
      - [4-byte size] → Audio size header
      - [binary audio data] → Audio chunks
      - "PTT_STOP" → Stop recording and process
    
    Responses:
      - {"type": "transcript", "text": "..."} → STT result
      - {"type": "status", "msg": "..."} → Processing status
      - {"type": "tts_start"} → TTS playback begins
      - [binary PCM chunks] → Audio data
      - {"type": "tts_end", "size": N} → TTS playback ends
      - {"type": "done"} → Pipeline complete
    
    Note: CHAT_START/AGENT_START removed - always use MiniMax
    """
    try:
        client_ip  = websocket.remote_address[0]
        session_id = client_ip
        print(f"[+] Client connected: {websocket.remote_address} (session={session_id})")

        audio_buffer       = bytearray()
        recording          = False
        expected_audio_len = 0

        async for message in websocket:
            msg_len  = len(message) if isinstance(message, (bytes, bytearray, str)) else 0
            msg_repr = repr(message)[:50] if isinstance(message, str) else "N/A"
            print(f"[DEBUG] type={type(message).__name__} len={msg_len} repr={msg_repr}")

            if isinstance(message, str):
                msg_stripped = message.strip()

                if msg_stripped in ("PTT_START", "PTT_STAR", "PTT_STA"):
                    if session_id in active_pipelines:
                        print("[!] Pipeline already running, ignoring PTT_START")
                        continue
                    audio_buffer.clear()
                    recording           = True
                    expected_audio_len = 0
                    print(f"[*] Recording started")

                elif msg_stripped in ("PTT_STOP", "PTT_STO", "PTT_ST"):
                    recording = False
                    print(f"[*] Recording stopped - {len(audio_buffer)} bytes")
                    if len(audio_buffer) >= 4000:
                        active_pipelines[session_id] = True
                        snapshot = bytearray(audio_buffer)
                        asyncio.ensure_future(
                            process_audio(websocket, session_id, snapshot)
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
                    recording           = True
                    expected_audio_len = 0
                    print(f"[*] Recording started (bytes)")

                elif msg_bytes in (b"PTT_STOP", b"PTT_STO", b"PTT_ST"):
                    recording = False
                    print(f"[*] Recording stopped (bytes) - {len(audio_buffer)} bytes")
                    if len(audio_buffer) >= 4000:
                        active_pipelines[session_id] = True
                        snapshot = bytearray(audio_buffer)
                        asyncio.ensure_future(
                            process_audio(websocket, session_id, snapshot)
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
    except Exception as e:
        print(f"[FATAL] handle_client crashed: {e}")
        import traceback; traceback.print_exc()
    finally:
        active_pipelines.pop(session_id, None)
        print(f"[-] Session {session_id} disconnected "
              f"(history={len(conversation_histories.get(session_id, []))} msgs, preserved)")


# ─── Main ──────────────────────────────────────────────────────────────────────

async def main():
    """Start the voice assistant server v4.0 with auto-restart on failure."""
    while True:
        try:
            print("=" * 60)
            print("  Voice Assistant Server v4.0")
            print("  LangGraph + MiniMax + Edge TTS")
            print("=" * 60)
            
            if not MINIMAX_API_KEY:
                print("ERROR: MINIMAX_API_KEY not set!"); return
            if not MINIMAX_GROUP_ID:
                print("ERROR: MINIMAX_GROUP_ID not set!"); return
            
            print(f"  TTS Backend: {TTS_BACKEND}")
            print(f"  Whisper Server: {WHISPER_SERVER}")
            print(f"  Session: IP-based (reconnect-safe)")
            print("  WebSocket: ws://0.0.0.0:8765")
            print("=" * 60)
            
            if TTS_BACKEND != "edge" and not GEMINI_API_KEY:
                print(f"WARNING: GEMINI_API_KEY not set (required for TTS_BACKEND={TTS_BACKEND})")
            
            async with websockets.serve(handle_client, "0.0.0.0", 8765):
                print("[+] WebSocket server listening on ws://0.0.0.0:8765")
                await asyncio.Future()  # run forever
        except Exception as e:
            print(f"[!] Server error: {e} – restarting in 5 seconds...")
            await asyncio.sleep(5)


if __name__ == "__main__":
    asyncio.run(main())
