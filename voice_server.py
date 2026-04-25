#!/usr/bin/env python3
"""
Voice Assistant Server for ESP32-P4
STT -> LLM -> TTS Pipeline
STT: whisper.cpp server | LLM: MiniMax-M2.7 | TTS: Edge
"""

import asyncio
import os
import json
import wave
import io
import struct
import array
import tempfile
import httpx
import websockets
from typing import AsyncGenerator

# Debug: print all incoming messages

# Konfigurasyon
MINIMAX_API_KEY  = os.environ.get("MINIMAX_API_KEY", "")
MINIMAX_GROUP_ID = os.environ.get("MINIMAX_GROUP_ID", "")
GEMINI_API_KEY    = os.environ.get("GEMINI_API_KEY", "")

# TTS Backend: "gemini", "edge", or "google_cloud"
TTS_BACKEND = os.environ.get("TTS_BACKEND", "edge")  # Default to edge TTS

# Google Cloud TTS credentials
GOOGLE_APPLICATION_CREDENTIALS = os.environ.get("GOOGLE_APPLICATION_CREDENTIALS", "")

# Whisper.cpp server endpoint
WHISPER_SERVER = "http://localhost:8080"

SYSTEM_PROMPT = """Sen Turkce konusan yardimci bir sesli asistansin.
SADECE Turkce cevap ver, asla baska dil kullanma.
Kisa ve dogal cevaplar ver, 1-2 cumle yeterli."""

conversation_histories = {}

# Gemini types import
from google.genai import types

async def transcribe_audio(pcm_bytes: bytes) -> str:
    """Transcribe using whisper.cpp server"""
    try:
        # Convert stereo to mono 16kHz
        samples = struct.unpack(f"<{len(pcm_bytes)//2}h", pcm_bytes)
        mono_samples = array.array('h', [
            (samples[i] + samples[i+1]) // 2
            for i in range(0, len(samples) - 1, 2)
        ])
        
        # Debug: Check audio amplitude
        if len(mono_samples) > 0:
            max_amp = max(abs(s) for s in mono_samples)
            avg_amp = sum(abs(s) for s in mono_samples) / len(mono_samples)
            duration_sec = len(mono_samples) / 16000.0
            print(f"[STT] Audio: {len(mono_samples)} samples ({duration_sec:.2f}s), max={max_amp}, avg={avg_amp:.1f}")
        
        # Check if audio is long enough (at least 0.3 seconds)
        if len(mono_samples) < 4800:  # 0.3 seconds at 16kHz
            print(f"[STT] Audio too short: {duration_sec:.2f}s < 0.3s")
            return ""
        
        # Save to temp WAV file
        with tempfile.NamedTemporaryFile(suffix='.wav', delete=False) as f:
            temp_path = f.name
            with wave.open(f, 'wb') as wf:
                wf.setnchannels(1)
                wf.setsampwidth(2)
                wf.setframerate(16000)
                wf.writeframes(mono_samples.tobytes())
        
        try:
            print(f"[STT] Transcribing with whisper.cpp server...")
            
            # Send to whisper.cpp server
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
                text = result.get("text", "") or result.get("output", "")
                text = text.strip()
                print(f"[STT] Whisper: '{text}'")
                return text
            else:
                print(f"[STT] Error {response.status_code}: {response.text[:300]}")
                return ""
            
        finally:
            os.unlink(temp_path)
        
    except Exception as e:
        print(f"[STT] Error: {e}")
        import traceback
        traceback.print_exc()
        return ""

# LLM - MiniMax MiniMax-M2.7 (ROBUST STREAMING PARSER)
async def chat_stream(session_id: str, user_text: str) -> AsyncGenerator[str, None]:
    """Stream chat response from MiniMax with robust multi-format parsing"""
    if session_id not in conversation_histories:
        conversation_histories[session_id] = []

    history = conversation_histories[session_id]
    history.append({"role": "user", "content": user_text})

    url = f"https://api.minimax.io/v1/text/chatcompletion_v2?GroupId={MINIMAX_GROUP_ID}"
    headers = {
        "Authorization": f"Bearer {MINIMAX_API_KEY}",
        "Content-Type": "application/json"
    }
    
    messages = [{"role": "system", "content": SYSTEM_PROMPT}]
    messages.extend(history[-10:])

    payload = {
        "model": "MiniMax-M2.7",
        "messages": messages,
        "stream": True,
        "max_tokens": 300,
        "temperature": 0.7,
    }

    full_response = ""
    debug_count = 0
    
    try:
        async with httpx.AsyncClient(timeout=60) as client:
            async with client.stream("POST", url, headers=headers, json=payload) as resp:
                if resp.status_code != 200:
                    error_text = await resp.text()
                    print(f"[LLM] HTTP {resp.status_code}: {error_text[:300]}")
                    if "overloaded" in error_text.lower() or resp.status_code == 529:
                        print("[LLM] Server overloaded, waiting 5s before retry...")
                        await asyncio.sleep(5)
                    return
                
                async for line in resp.aiter_lines():
                    line = line.strip()
                    if not line or line == "data: ":
                        continue
                    
                    # Handle SSE "data: " prefix
                    if line.startswith("data:"):
                        line = line[5:].strip()
                    
                    if not line or line == "[DONE]" or line == "{}":
                        continue
                    
                    # Debug: log first 3 lines
                    debug_count += 1
                    if debug_count <= 3:
                        print(f"[LLM-DEBUG] Line #{debug_count}: {repr(line[:150])}")
                    
                    try:
                        data = json.loads(line)
                        
                        if data.get("type") == "error":
                            err = data.get("error", {})
                            print(f"[LLM] Stream error: {err.get('message', 'Unknown')}")
                            break
                        
                        content = None
                        if "choices" in data and data["choices"]:
                            choice = data["choices"][0]
                            
                            # Format 1: message.content (MiniMax streaming style)
                            if "message" in choice and "content" in choice["message"]:
                                content = choice["message"]["content"]
                            
                            # Format 2: delta.content (OpenAI-style)
                            elif "delta" in choice and "content" in choice["delta"]:
                                content = choice["delta"]["content"]
                            
                            # Format 3: direct text field
                            elif "text" in choice:
                                content = choice["text"]
                            
                            # Format 4: finish_reason without content = end of stream
                            if choice.get("finish_reason") and not content:
                                break
                        
                        if content:
                            # Skip if this content is a duplicate of the last appended content
                            if full_response.endswith(content):
                                continue
                            # Skip if content is already in the last 20 chars (duplicate chunk)
                            if len(full_response) >= 20 and full_response[-20:] == content:
                                continue
                            full_response += content
                            yield content
                            
                    except json.JSONDecodeError:
                        continue
                    except (KeyError, IndexError, TypeError) as e:
                        print(f"[LLM] Parse error: {e} | data: {str(data)[:100]}")
                        continue
                        
    except httpx.RequestError as e:
        print(f"[LLM] Network error: {e}")
    except Exception as e:
        print(f"[LLM] Unexpected error: {type(e).__name__}: {e}")
        import traceback
        traceback.print_exc()

    # Log final result
    if full_response:
        print(f"[LLM] Response: '{full_response[:100]}...' ({len(full_response)} chars)")
        history.append({"role": "assistant", "content": full_response})
    else:
        print(f"[LLM] Empty response - API may be overloaded")

# TTS - Edge TTS (Microsoft)
async def tts_edge_stream(text: str) -> AsyncGenerator[bytes, None]:
    """Generate TTS audio using Edge TTS with ffmpeg for conversion"""
    try:
        import edge_tts
        import subprocess
        import tempfile
        import os
        
        print(f"[TTS-Edge] Requesting: '{text[:50]}...'")
        
        # Collect audio chunks first
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
        
        # Use ffmpeg to convert MP3 to 16kHz stereo PCM
        with tempfile.NamedTemporaryFile(suffix='.mp3', delete=False) as f_in:
            f_in.write(mp3_data)
            mp3_path = f_in.name
        
        pcm_path = mp3_path + '.pcm'
        
        try:
            result = subprocess.run([
                'ffmpeg', '-y', '-f', 'mp3', '-i', mp3_path,
                '-f', 's16le', '-acodec', 'pcm_s16le',
                '-ar', '16000', '-ac', '2',
                pcm_path
            ], capture_output=True, timeout=30)
            
            if result.returncode != 0:
                print(f"[TTS-Edge] ffmpeg error: {result.stderr.decode()[:200]}")
                return
            
            with open(pcm_path, 'rb') as f:
                pcm_data = f.read()
            
            duration = len(pcm_data) / (16000 * 2 * 2)
            print(f"[TTS-Edge] PCM: {len(pcm_data)} bytes, {duration:.2f}s @ 16kHz stereo")
            
            # Stream in smaller chunks for ESP32 flow control
            chunk_size = 2048
            for i in range(0, len(pcm_data), chunk_size):
                yield pcm_data[i:i+chunk_size]
            
            print(f"[TTS-Edge] Complete")
            
        finally:
            os.unlink(mp3_path)
            if os.path.exists(pcm_path):
                os.unlink(pcm_path)
        
    except Exception as e:
        print(f"[TTS-Edge] Error: {type(e).__name__}: {e}")
        import traceback
        traceback.print_exc()

# TTS - Google Gemini (original)
async def tts_gemini_stream(text: str) -> AsyncGenerator[bytes, None]:
    """Generate TTS audio from Gemini with timeout and error handling"""
    try:
        from google import genai
        from google.genai import types
        import numpy as np
        from scipy.signal import resample_poly
        
        if not GEMINI_API_KEY:
            print("[TTS-Gemini] ERROR: GEMINI_API_KEY not set")
            return
        
        client = genai.Client(api_key=GEMINI_API_KEY)
        print(f"[TTS-Gemini] Requesting: '{text[:50]}...'")
        
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
        print(f"[TTS-Gemini] Generated: {len(audio_data)} bytes raw PCM (24kHz mono)")
        
        # Convert: 24kHz mono → 16kHz stereo
        samples_24k = np.frombuffer(audio_data, dtype=np.int16)
        samples_16k = resample_poly(samples_24k, 2, 3)
        stereo = np.empty(len(samples_16k) * 2, dtype=np.int16)
        stereo[0::2] = samples_16k
        stereo[1::2] = samples_16k
        pcm_output = stereo.tobytes()
        
        duration = len(pcm_output) / (16000 * 2 * 2)
        print(f"[TTS-Gemini] Output: {len(pcm_output)} bytes, {duration:.2f}s @ 16kHz stereo")
        
        # Stream in chunks
        chunk_size = 4096
        sent = 0
        for i in range(0, len(pcm_output), chunk_size):
            chunk = pcm_output[i:i+chunk_size]
            yield chunk
            sent += len(chunk)
        
        print(f"[TTS-Gemini] Complete: {sent} bytes sent")
        
    except Exception as e:
        print(f"[TTS-Gemini] Error: {type(e).__name__}: {e}")
        import traceback
        traceback.print_exc()

# TTS - Google Cloud TTS
async def tts_google_cloud_stream(text: str) -> AsyncGenerator[bytes, None]:
    """Generate TTS audio using Google Cloud TTS API"""
    try:
        import base64
        import numpy as np
        from scipy.signal import resample_poly
        
        print(f"[TTS-GCloud] Requesting: '{text[:50]}...'")
        
        url = "https://texttospeech.googleapis.com/v1/text:synthesize?key=" + GEMINI_API_KEY
        
        payload = {
            "input": {"text": text},
            "voice": {"languageCode": "tr-TR", "name": "tr-TR-Wavenet-A", "ssmlGender": "FEMALE"},
            "audioConfig": {"audioEncoding": "LINEAR16", "sampleRateHertz": 24000, "effectsProfileId": ["headphone-class-device"]}
        }
        
        async with httpx.AsyncClient(timeout=30) as client:
            response = await client.post(url, json=payload)
        
        if response.status_code != 200:
            print(f"[TTS-GCloud] Error {response.status_code}: {response.text[:300]}")
            return
        
        result = response.json()
        audio_data = base64.b64decode(result["audioContent"])
        print(f"[TTS-GCloud] Generated: {len(audio_data)} bytes raw PCM (24kHz mono)")
        
        # Convert: 24kHz mono → 16kHz stereo
        samples_24k = np.frombuffer(audio_data, dtype=np.int16)
        samples_16k = resample_poly(samples_24k, 2, 3).astype(np.int16)
        stereo = np.empty(len(samples_16k) * 2, dtype=np.int16)
        stereo[0::2] = samples_16k
        stereo[1::2] = samples_16k
        pcm_output = stereo.tobytes()
        
        duration = len(pcm_output) / (16000 * 2 * 2)
        print(f"[TTS-GCloud] Output: {len(pcm_output)} bytes, {duration:.2f}s @ 16kHz stereo")
        
        # Stream in chunks
        chunk_size = 4096
        for i in range(0, len(pcm_output), chunk_size):
            yield pcm_output[i:i+chunk_size]
        
        print(f"[TTS-GCloud] Complete")
        
    except Exception as e:
        print(f"[TTS-GCloud] Error: {type(e).__name__}: {e}")
        import traceback
        traceback.print_exc()

# TTS dispatch
async def tts_stream(text: str) -> AsyncGenerator[bytes, None]:
    """Route to appropriate TTS backend"""
    if TTS_BACKEND == "edge":
        async for chunk in tts_edge_stream(text):
            yield chunk
    elif TTS_BACKEND == "google_cloud":
        async for chunk in tts_google_cloud_stream(text):
            yield chunk
    else:
        async for chunk in tts_gemini_stream(text):
            yield chunk

# Pipeline
async def process_audio(websocket, session_id: str, audio_buffer: bytearray):
    if len(audio_buffer) < 4000:
        await websocket.send(json.dumps({"type": "error", "msg": "Audio too short"}))
        print(f"[!] Audio too short: {len(audio_buffer)} bytes")
        return

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
        total_pcm_bytes = 0

        async for token in chat_stream(session_id, user_text):
            sentence_buf += token
        
        print(f"[LLM] Full response: '{sentence_buf}'")
        
        # Process any remaining text
        if sentence_buf.strip():
            print(f"[TTS] Synthesizing: {sentence_buf.strip()[:50]}...")
            await websocket.send(json.dumps({"type": "tts_start"}))
            await asyncio.sleep(0.05)  # Small delay for ESP32 to prepare
            
            async for pcm in tts_stream(sentence_buf.strip()):
                await websocket.send(pcm)
                total_pcm_bytes += len(pcm)
                # Pace chunks to match ESP32 playback rate (~64KB/s = 2KB/31ms)
                await asyncio.sleep(0.03)

            # Brief pause to ensure last chunk is transmitted before tts_end
            await asyncio.sleep(0.2)
            print(f"[TTS] All PCM sent ({total_pcm_bytes} bytes), now sending tts_end with size")
            await websocket.send(json.dumps({"type": "tts_end", "size": total_pcm_bytes}))

        playback_duration = total_pcm_bytes / (16000 * 2 * 2)
        print(f"[+] Playback: {playback_duration:.2f}s")
        await asyncio.sleep(playback_duration + 0.5)
        await websocket.send(json.dumps({"type": "done"}))
        print(f"[+] Pipeline complete")

    except Exception as e:
        print(f"[ERROR] {e}")
        import traceback
        traceback.print_exc()
        await websocket.send(json.dumps({"type": "error", "msg": "Server error"}))

# WebSocket Handler
async def handle_client(websocket):
    session_id = str(id(websocket))
    print(f"[+] Client connected: {websocket.remote_address}")
    audio_buffer = bytearray()
    recording = False
    expected_audio_len = 0

    try:
        async for message in websocket:
            msg_len = len(message) if isinstance(message, (bytes, bytearray, str)) else 0
            msg_repr = repr(message)[:50] if isinstance(message, str) else "N/A"
            print(f"[DEBUG] type={type(message).__name__} len={msg_len} repr={msg_repr}")
            
            if isinstance(message, str):
                # Check for PTT commands (handle typos like "PTT_STAR" vs "PTT_START")
                msg_stripped = message.strip()
                if msg_stripped in ("PTT_START", "PTT_STAR", "PTT_STA"):
                    audio_buffer.clear()
                    recording = True
                    expected_audio_len = 0
                    print(f"[*] Recording started (msg: {repr(message)})")
                elif msg_stripped in ("PTT_STOP", "PTT_STO", "PTT_ST"):
                    recording = False
                    print(f"[*] Recording stopped - {len(audio_buffer)} bytes")
                    if len(audio_buffer) >= 4000:
                        await process_audio(websocket, session_id, audio_buffer)
                    else:
                        print(f"[!] Audio too short: {len(audio_buffer)} bytes")

            elif isinstance(message, bytes):
                # Check bytes directly (handle null-padded strings)
                msg_bytes = message.strip()
                if msg_bytes in (b"PTT_START", b"PTT_STAR", b"PTT_STA"):
                    audio_buffer.clear()
                    recording = True
                    expected_audio_len = 0
                    print(f"[*] Recording started (bytes: {repr(message)})")
                elif msg_bytes in (b"PTT_STOP", b"PTT_STO", b"PTT_ST"):
                    recording = False
                    print(f"[*] Recording stopped (bytes) - {len(audio_buffer)} bytes")
                    if len(audio_buffer) >= 4000:
                        await process_audio(websocket, session_id, audio_buffer)
                elif len(message) == 4 and expected_audio_len == 0:
                    # This might be the audio size header (4 bytes, big-endian uint32)
                    import struct
                    size = struct.unpack('>I', message)[0]
                    if 1000 < size < 1000000:  # Reasonable audio size (1KB to 1MB)
                        expected_audio_len = size
                        print(f"[*] Audio size header: {expected_audio_len} bytes")
                elif recording:
                    audio_buffer.extend(message)
                    if len(audio_buffer) % 8192 == 0 or len(audio_buffer) == expected_audio_len:
                        print(f"[*] Buffer: {len(audio_buffer)}/{expected_audio_len} bytes")

    except websockets.exceptions.ConnectionClosed:
        print(f"[-] Connection closed")
    finally:
        conversation_histories.pop(session_id, None)

# Main
async def main():
    if not MINIMAX_API_KEY:
        print("ERROR: MINIMAX_API_KEY not set!")
        return
    if not MINIMAX_GROUP_ID:
        print("ERROR: MINIMAX_GROUP_ID not set!")
        return
    if not GEMINI_API_KEY:
        print("ERROR: GEMINI_API_KEY not set!")
        return

    print("=" * 60)
    print("  Voice Assistant Server v2.0")
    print("=" * 60)
    print("  STT: whisper.cpp server (local)")
    print("  LLM: MiniMax-M2.7")
    print(f"  TTS: {TTS_BACKEND}")
    print(f"  Whisper Server: {WHISPER_SERVER}")
    print("  ws://0.0.0.0:8765")
    print("=" * 60)

    async with websockets.serve(handle_client, "0.0.0.0", 8765):
        await asyncio.Future()

if __name__ == "__main__":
    asyncio.run(main())