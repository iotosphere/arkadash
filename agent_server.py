#!/usr/bin/env python3
"""
Voice Assistant Server for ESP32-P4
STT -> LLM -> TTS Pipeline
STT: whisper.cpp server | LLM: MiniMax-M2.7 | TTS: Edge

FIX: Session history artık WebSocket object ID yerine client IP adresine
     bağlıdır. ESP32 reconnect etse bile aynı IP'den geldiği için
     konuşma geçmişi korunur.

Spotify Remote: SPOTIFY_* komutları ile playlist browse ve playback control
"""

import sys
sys.path.insert(0, '/Users/serhatsaday/arkadash/orch/lib/python3.14/site-packages')

import asyncio
import os
import json
import wave
import struct
import array
import socket
import tempfile
import subprocess
import threading
import httpx
import websockets
from typing import AsyncGenerator
import random
import time
from http.server import BaseHTTPRequestHandler, HTTPServer

# ─── Global State (Health monitoring için) ──────────────────────────────────
START_TIME = time.time()

# Aktif WebSocket client'ları takip et (health app için):
#   ip -> {connected_at, last_msg_at, last_msg, ip}
# Yeni bağlantı handle_client'da eklenir, finally'de çıkarılır.
wss_clients: dict[str, dict] = {}

# Phase 2A (2026-06-29): P4 firmware her 10s'de POST /api/telemetry gönderir.
# Key = client IP (P4 WiFi STA IP, ws_connections ile aynı olur).
# Value: {heap_free, rssi, ssid, mac, ip_local, uptime_s, matter_count, last_post_at}
p4_telemetry: dict[str, dict] = {}

MINIMAX_API_KEY  = os.environ.get("MINIMAX_API_KEY", "")
MINIMAX_GROUP_ID = os.environ.get("MINIMAX_GROUP_ID", "")
GEMINI_API_KEY   = os.environ.get("GEMINI_API_KEY", "")

TTS_BACKEND   = os.environ.get("TTS_BACKEND", "minimax")
WHISPER_SERVER = "http://localhost:8080"

# Health server portu — Arkadash panel/mobile health app'inin sorgulayacağı
# REST endpoint'i (FletApp/P4 health monitor / etc.). 8088 default.
HEALTH_HTTP_PORT = int(os.environ.get("HEALTH_PORT", "8088"))

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

# ─── LED Control (ESP32-S3 HTTP API → Matter → ESP32-H2) ──────────────────────

ESP32_S3_HTTP_BASE = "http://192.168.1.15:8766"

async def led_toggle():
    """Toggle living room LED via ESP32-S3."""
    try:
        async with httpx.AsyncClient(timeout=5) as client:
            response = await client.get(f"{ESP32_S3_HTTP_BASE}/led/toggle")
        return response.status_code == 200
    except Exception as e:
        print(f"[LED] Toggle error: {e}")
        return False

async def led_set_color(color_x: int, color_y: int):
    """Set living room LED color (CIE xyY) via ESP32-S3."""
    try:
        async with httpx.AsyncClient(timeout=5) as client:
            response = await client.get(
                f"{ESP32_S3_HTTP_BASE}/led/color",
                params={"x": color_x, "y": color_y}
            )
        return response.status_code == 200
    except Exception as e:
        print(f"[LED] Color error: {e}")
        return False

async def led_set_brightness(level: int):
    """Set living room LED brightness (0-254) via ESP32-S3."""
    try:
        async with httpx.AsyncClient(timeout=5) as client:
            response = await client.get(
                f"{ESP32_S3_HTTP_BASE}/led/brightness",
                params={"level": level}
            )
        return response.status_code == 200
    except Exception as e:
        print(f"[LED] Brightness error: {e}")
        return False

async def led_get_status():
    """Get living room LED status from ESP32-S3."""
    try:
        async with httpx.AsyncClient(timeout=5) as client:
            response = await client.get(f"{ESP32_S3_HTTP_BASE}/led/status")
        if response.status_code == 200:
            return response.json()
        return None
    except Exception as e:
        print(f"[LED] Status error: {e}")
        return None


# ─── Spotify Control (Web API) ───────────────────────────────────────────────

SPOTIFY_CLIENT_ID = os.environ.get("SPOTIFY_CLIENT_ID", "")
SPOTIFY_CLIENT_SECRET = os.environ.get("SPOTIFY_CLIENT_SECRET", "")
SPOTIFY_CACHE_PATH = os.path.expanduser("~/.cache/spotify_token")

spotify_token = None
spotify_token_expiry = 0

async def refresh_spotify_token():
    """OAuth2 token yenile veya al."""
    global spotify_token, spotify_token_expiry
    
    if not SPOTIFY_CLIENT_ID or not SPOTIFY_CLIENT_SECRET:
        print("[Spotify] Credentials not set (SPOTIFY_CLIENT_ID, SPOTIFY_CLIENT_SECRET)")
        return False
    
    try:
        import spotipy
        from spotipy.oauth2 import SpotifyOAuth
        
        oauth = SpotifyOAuth(
            client_id=SPOTIFY_CLIENT_ID,
            client_secret=SPOTIFY_CLIENT_SECRET,
            redirect_uri="http://127.0.0.1:8080/callback",
            scope="user-read-playback-state user-modify-playback-state playlist-read-private",
            cache_path=SPOTIFY_CACHE_PATH
        )
        
        token_info = oauth.get_cached_token()
        if not token_info:
            print("[Spotify] No cached token - need to authenticate")
            return False
        
        spotify_token = token_info["access_token"]
        spotify_token_expiry = asyncio.get_event_loop().time() + token_info["expires_in"]
        print("[Spotify] Token refreshed, expires in", token_info["expires_in"])
        return True
    except Exception as e:
        print(f"[Spotify] Token refresh error: {e}")
        return False

def get_spotify_headers():
    global spotify_token, spotify_token_expiry
    if not spotify_token or asyncio.get_event_loop().time() > spotify_token_expiry:
        return None
    return {"Authorization": f"Bearer {spotify_token}"}

async def spotify_get_playlists():
    """Kullanıcının playlistlerini döndür."""
    headers = get_spotify_headers()
    if not headers:
        return []
    try:
        async with httpx.AsyncClient() as client:
            response = await client.get(
                "https://api.spotify.com/v1/me/playlists",
                headers=headers,
                params={"limit": 50}
            )
        if response.status_code == 200:
            data = response.json()
            playlists = [{"id": p["id"], "name": p["name"]} for p in data.get("items", [])]
            return playlists
        else:
            print(f"[Spotify] Playlists error: {response.status_code}")
            return []
    except Exception as e:
        print(f"[Spotify] Get playlists error: {e}")
        return []

async def spotify_play_random_from_playlist(playlist_id: str):
    """Playlist'ten rastgele şarkı çal."""
    headers = get_spotify_headers()
    if not headers:
        return False
    try:
        async with httpx.AsyncClient() as client:
            response = await client.get(
                f"https://api.spotify.com/v1/playlists/{playlist_id}/tracks",
                headers=headers,
                params={"fields": "items(track(id,name,artists(name)))"}
            )
        
        if response.status_code != 200:
            print(f"[Spotify] Playlist tracks error: {response.status_code}")
            return False
        
        data = response.json()
        tracks = data.get("items", [])
        if not tracks:
            print("[Spotify] Empty playlist")
            return False
        
        track = random.choice(tracks)
        if not track.get("track") or not track["track"].get("id"):
            print("[Spotify] Invalid track")
            return False
        
        track_id = track["track"]["id"]
        
        async with httpx.AsyncClient() as client:
            devices_resp = await client.get(
                "https://api.spotify.com/v1/me/player/devices",
                headers=headers
            )
        
        device_id = None
        if devices_resp.status_code == 200:
            devices = devices_resp.json().get("devices", [])
            for d in devices:
                if d["is_active"]:
                    device_id = d["id"]
                    break
            if not device_id and devices:
                device_id = devices[0]["id"]
        
        if not device_id:
            print("[Spotify] No active device")
            return False
        
        async with httpx.AsyncClient() as client:
            play_resp = await client.put(
                "https://api.spotify.com/v1/me/player/play",
                headers=headers,
                json={
                    "context_uri": f"spotify:playlist:{playlist_id}",
                    "offset": {"position": random.randint(0, len(tracks)-1)},
                    "device_id": device_id
                }
            )
        
        if play_resp.status_code in (200, 204):
            print(f"[Spotify] Playing random track from playlist {playlist_id}")
            return True
        else:
            print(f"[Spotify] Play error: {play_resp.status_code}")
            return False
            
    except Exception as e:
        print(f"[Spotify] Play error: {e}")
        return False

async def spotify_get_current_track():
    """Çalan şarkı bilgisini döndür."""
    headers = get_spotify_headers()
    if not headers:
        return {"title": "", "artist": "", "is_playing": False}
    try:
        async with httpx.AsyncClient() as client:
            response = await client.get(
                "https://api.spotify.com/v1/me/player/currently-playing",
                headers=headers
            )
        
        if response.status_code == 200:
            data = response.json()
            if data and data.get("item"):
                track = data["item"]
                return {
                    "title": track.get("name", ""),
                    "artist": ", ".join(a["name"] for a in track.get("artists", [])),
                    "is_playing": data.get("is_playing", False)
                }
        return {"title": "", "artist": "", "is_playing": False}
    except Exception as e:
        print(f"[Spotify] Get current track error: {e}")
        return {"title": "", "artist": "", "is_playing": False}

async def spotify_pause():
    """Pause playback."""
    headers = get_spotify_headers()
    if not headers:
        return False
    try:
        async with httpx.AsyncClient() as client:
            response = await client.put(
                "https://api.spotify.com/v1/me/player/pause",
                headers=headers
            )
        return response.status_code in (200, 204)
    except Exception as e:
        print(f"[Spotify] Pause error: {e}")
        return False

async def spotify_resume():
    """Resume playback."""
    headers = get_spotify_headers()
    if not headers:
        return False
    try:
        async with httpx.AsyncClient() as client:
            response = await client.put(
                "https://api.spotify.com/v1/me/player/play",
                headers=headers
            )
        return response.status_code in (200, 204)
    except Exception as e:
        print(f"[Spotify] Resume error: {e}")
        return False

async def spotify_skip():
    """Skip to next track."""
    headers = get_spotify_headers()
    if not headers:
        return False
    try:
        async with httpx.AsyncClient() as client:
            response = await client.post(
                "https://api.spotify.com/v1/me/player/next",
                headers=headers
            )
        return response.status_code in (200, 204)
    except Exception as e:
        print(f"[Spotify] Skip error: {e}")
        return False


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

async def tts_minimax_stream(text: str) -> AsyncGenerator[bytes, None]:
    """MiniMax speech-hd TTS (synchronous - tüm audio bir seferde gelir)."""
    try:
        import numpy as np
        from scipy.signal import resample_poly

        url = f"https://api.minimax.io/v1/t2a_v2?GroupId={MINIMAX_GROUP_ID}"
        headers = {
            "Authorization": f"Bearer {MINIMAX_API_KEY}",
            "Content-Type": "application/json"
        }
        payload = {
            "model": "speech-2.8-hd",
            "text": text,
            "stream": False,
            "voice_setting": {
                "voice_id": "Turkish_CalmWoman"
            },
            "audio_setting": {
                "sample_rate": 24000,
                "bitrate": 128000,
                "format": "mp3"
            }
        }

        print(f"[TTS-MiniMax] Requesting (sync): '{text[:50]}...'")

        async with httpx.AsyncClient(timeout=120) as client:
            response = await client.post(url, headers=headers, json=payload)

        if response.status_code != 200:
            print(f"[TTS-MiniMax] HTTP {response.status_code}: {response.text[:300]}")
            return

        try:
            json_data = response.json()
        except Exception:
            print(f"[TTS-MiniMax] Failed to parse JSON response: {response.text[:200]}")
            return

        audio_hex = json_data.get("data", {}).get("audio", "")
        if not audio_hex:
            print(f"[TTS-MiniMax] No audio in response: {list(json_data.keys())}")
            return

        mp3_data = bytes.fromhex(audio_hex)
        print(f"[TTS-MiniMax] MP3 decoded from hex: {len(mp3_data)} bytes")

        with tempfile.NamedTemporaryFile(suffix='.mp3', delete=False) as f_in:
            f_in.write(mp3_data)
            mp3_path = f_in.name
        pcm_path = mp3_path + '.pcm'

        print(f"[TTS-MiniMax] MP3 file written to {mp3_path}")

        try:
            result = subprocess.run([
                'ffmpeg', '-y', '-v', 'debug', '-f', 'mp3', '-i', mp3_path,
                '-f', 's16le', '-acodec', 'pcm_s16le',
                '-ar', '24000', '-ac', '1', pcm_path
            ], capture_output=True, timeout=60)

            stderr = result.stderr.decode('utf-8', errors='replace')
            stdout = result.stdout.decode('utf-8', errors='replace')
            
            print(f"[TTS-MiniMax] ffmpeg rc={result.returncode}")
            if result.returncode != 0:
                print(f"[TTS-MiniMax] ffmpeg STDOUT (first 300): {stdout[:300]}")
                print(f"[TTS-MiniMax] ffmpeg STDERR (first 500): {stderr[:500]}")
                return

            with open(pcm_path, 'rb') as f:
                pcm_data = f.read()

            samples_24k = np.frombuffer(pcm_data, dtype=np.int16)
            samples_16k = resample_poly(samples_24k, 2, 3).astype(np.int16)
            pcm_mono = samples_16k.tobytes()

            duration = len(pcm_mono) / (16000 * 2)
            print(f"[TTS-MiniMax] PCM: {len(pcm_mono)} bytes, {duration:.2f}s @ 16kHz mono")

            chunk_size = 1024
            for i in range(0, len(pcm_mono), chunk_size):
                yield pcm_mono[i:i+chunk_size]

            print("[TTS-MiniMax] Complete")

        finally:
            os.unlink(mp3_path)
            if os.path.exists(pcm_path):
                os.unlink(pcm_path)

    except Exception as e:
        print(f"[TTS-MiniMax] Error: {type(e).__name__}: {e}")
        import traceback; traceback.print_exc()


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
    elif TTS_BACKEND == "minimax":
        async for chunk in tts_minimax_stream(text):
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

        playback_duration = total_pcm_bytes / (16000 * 2)
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

    # Health tracking: yeni bağlantıyı wss_clients'a kaydet
    wss_clients[client_ip] = {
        "ip":          client_ip,
        "connected_at": time.time(),
        "last_msg_at": time.time(),
        "last_msg":    "WS connected",
    }

    audio_buffer      = bytearray()
    recording         = False
    expected_audio_len = 0
    use_zeroclaw      = False  # Chat=False (MiniMax), Agent=True (ZeroClaw)

    try:
        async for message in websocket:
            msg_len  = len(message) if isinstance(message, (bytes, bytearray, str)) else 0
            msg_repr = repr(message)[:50] if isinstance(message, str) else "N/A"
            print(f"[DEBUG] type={type(message).__name__} len={msg_len} repr={msg_repr}")

            # Health tracking: son mesaj zamanı + özeti
            # Her mesajda güncelle — health app staleness gösterir.
            if client_ip in wss_clients:
                wss_clients[client_ip]["last_msg_at"] = time.time()
                if isinstance(message, str):
                    wss_clients[client_ip]["last_msg"] = msg_repr
                elif isinstance(message, (bytes, bytearray)):
                    wss_clients[client_ip]["last_msg"] = f"audio {msg_len}B"
                else:
                    wss_clients[client_ip]["last_msg"] = f"{type(message).__name__}"

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

                # ── Spotify Control ──────────────────────────────────────────────
                elif msg_stripped == "SPOTIFY_LIST":
                    playlists = await spotify_get_playlists()
                    await websocket.send(json.dumps({
                        "type": "spotify_playlists",
                        "playlists": playlists
                    }))
                elif msg_stripped.startswith("SPOTIFY_PLAY:"):
                    playlist_id = msg_stripped.split(":", 1)[1]
                    success = await spotify_play_random_from_playlist(playlist_id)
                    await websocket.send(json.dumps({
                        "type": "spotify_play_result",
                        "success": success
                    }))
                elif msg_stripped == "SPOTIFY_STATUS":
                    track = await spotify_get_current_track()
                    await websocket.send(json.dumps({
                        "type": "spotify_status",
                        "title": track["title"],
                        "artist": track["artist"],
                        "is_playing": track["is_playing"]
                    }))
                elif msg_stripped == "SPOTIFY_PAUSE":
                    success = await spotify_pause()
                    await websocket.send(json.dumps({
                        "type": "spotify_pause_result",
                        "success": success
                    }))
                elif msg_stripped == "SPOTIFY_RESUME":
                    success = await spotify_resume()
                    await websocket.send(json.dumps({
                        "type": "spotify_resume_result",
                        "success": success
                    }))
                elif msg_stripped == "SPOTIFY_SKIP":
                    success = await spotify_skip()
                    await websocket.send(json.dumps({
                        "type": "spotify_skip_result",
                        "success": success
                    }))

                # ── LED Control (Living Room) ──────────────────────────────────────
                elif msg_stripped == "LED_TOGGLE":
                    success = await led_toggle()
                    status = await led_get_status()
                    await websocket.send(json.dumps({
                        "type": "led_toggle_result",
                        "success": success,
                        "state": status
                    }))
                elif msg_stripped.startswith("LED_COLOR:"):
                    try:
                        parts = msg_stripped.split(":")
                        color_x = int(parts[1])
                        color_y = int(parts[2])
                        success = await led_set_color(color_x, color_y)
                        status = await led_get_status()
                        await websocket.send(json.dumps({
                            "type": "led_color_result",
                            "success": success,
                            "color_x": color_x,
                            "color_y": color_y,
                            "state": status
                        }))
                    except (IndexError, ValueError):
                        await websocket.send(json.dumps({
                            "type": "led_color_result",
                            "success": False,
                            "error": "Invalid format. Use LED_COLOR:x:y"
                        }))
                elif msg_stripped.startswith("LED_BRIGHTNESS:"):
                    try:
                        level = int(msg_stripped.split(":")[1])
                        success = await led_set_brightness(level)
                        status = await led_get_status()
                        await websocket.send(json.dumps({
                            "type": "led_brightness_result",
                            "success": success,
                            "brightness": level,
                            "state": status
                        }))
                    except (IndexError, ValueError):
                        await websocket.send(json.dumps({
                            "type": "led_brightness_result",
                            "success": False,
                            "error": "Invalid format. Use LED_BRIGHTNESS:level"
                        }))
                elif msg_stripped == "LED_STATUS":
                    status = await led_get_status()
                    await websocket.send(json.dumps({
                        "type": "led_status",
                        "state": status
                    }))

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

                elif msg_bytes.startswith(b"LED_TOGGLE"):
                    asyncio.ensure_future(led_toggle())
                elif msg_bytes.startswith(b"LED_COLOR:"):
                    parts = msg_bytes.decode().split(":")
                    if len(parts) >= 3:
                        try:
                            color_x = int(parts[1])
                            color_y = int(parts[2])
                            asyncio.ensure_future(led_set_color(color_x, color_y))
                        except ValueError:
                            pass
                elif msg_bytes.startswith(b"LED_BRIGHTNESS:"):
                    parts = msg_bytes.decode().split(":")
                    if len(parts) >= 2:
                        try:
                            level = int(parts[1])
                            asyncio.ensure_future(led_set_brightness(level))
                        except ValueError:
                            pass

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
        # Health tracking: client bağlantısı koptu
        if client_ip in wss_clients:
            uptime = int(time.time() - wss_clients[client_ip]["connected_at"])
            del wss_clients[client_ip]
            print(f"[-] Health: client {client_ip} düştü (uptime: {uptime}s)")
        # FIX: history'yi silme! Aynı IP tekrar bağlandığında korunacak.
        # Sadece yeni bağlantı günlüğünü yaz.
        print(f"[-] Session {session_id} disconnected "
              f"(history={len(conversation_histories.get(session_id, []))} msgs, preserved)")


# ─── Main ─────────────────────────────────────────────────────────────────────

def _get_local_ip() -> str:
    """Routing interface üzerinden kendi IP'mizi bul (192.168.1.X gibi).
    Broadcast için doğru IP'yi bulmak kritik — socket.gethostbyname()
    bazen 127.0.0.1 döner."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.connect(("8.8.8.8", 80))  # routing olmayan hedef, bağlantı kurmaz
        return sock.getsockname()[0]
    finally:
        sock.close()


def _udp_broadcaster(stop_event: threading.Event):
    """Her 5 saniyede "ARKADASH:<ip>" UDP broadcast gönder.
    P4 firmware UDP 53000'de dinler, agent_server'ın IP'sini
    otomatik öğrenir (mDNS/Bonjour alternatifi).
    Background thread, daemon=True → process kapanınca ölür."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.settimeout(1.0)  # recv yok ama future-proof

    while not stop_event.is_set():
        try:
            ip = _get_local_ip()
            msg = f"ARKADASH:{ip}:8765".encode("utf-8")
            sock.sendto(msg, ("255.255.255.255", 53000))
            print(f"[DISC] broadcast gönderildi: {msg.decode()} -> 255.255.255.255:53000")
        except Exception as e:
            print(f"[DISC] broadcast hatası: {e}")
        # 5 saniye bekle (stop_event check ile)
        stop_event.wait(5.0)

    sock.close()
    print("[DISC] broadcaster durdu")


# ─── Health HTTP server (port 8088) ───────────────────────────────────────────
#
# Sağlık monitoring için: FletApp veya başka bir tool JSON status alabilsin
# diye minimal REST endpoint sağlar. ws_sessions'ı (bağlı client'lar) ve voice
# pipeline istatistiklerini döner. asyncio event loop'u bloklamaz (thread'de).
#
# Endpoint'ler:
#   GET /api/health → JSON: uptime, clients, spotify, voice
#   GET /api/clients → JSON: list of client IPs

class _HealthHandler(BaseHTTPRequestHandler):
    def _send_json(self, payload: dict, status: int = 200):
        body = json.dumps(payload).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        # CORS — FletApp'in browser tabanlı runtime'ından gerekebilir
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)

    def _collect(self) -> dict:
        now = time.time()
        clients = []
        for ip, info in wss_clients.items():
            uptime_s = int(now - info["connected_at"])
            last_msg_age = int(now - info.get("last_msg_at", info["connected_at"]))
            clients.append({
                "ip":            ip,
                "uptime_s":      uptime_s,
                "last_msg_age_s": last_msg_age,
                "last_msg":      info.get("last_msg", ""),
            })
        # Spotify durumu (token refresh aktifken aktif)
        spotify_enabled = bool(SPOTIFY_CLIENT_ID and SPOTIFY_CLIENT_ID != "your-spotify-client-id")
        spotify_authed = bool(spotify_token)
        return {
            "ts":           int(now),
            "agent":        "arkadash-voice-server",
            "version":      "v2.3-health",
            "uptime_s":     int(now - START_TIME),
            "ws_server":    "0.0.0.0:8765",
            "clients":      clients,
            "p4_telemetry": _telemetry_snapshot(now),
            "spotify":      {
                "enabled":   spotify_enabled,
                "authed":    spotify_authed,
            },
            "whisper_stt":  WHISPER_SERVER,
            "voice":        {
                "active_sessions": len(wss_clients),
                "history_msgs":    sum(len(h) for h in conversation_histories.values()),
            },
        }

    def do_POST(self):
        """P4 firmware buraya periyodik telemetry POST eder (her 10s).
        Body JSON: {heap_free, rssi, ssid, mac, ip_local, uptime_s, matter_count}
        Sender IP = key (ws_clients ile aynı IP olur genellikle)."""
        if self.path not in ("/api/telemetry", "/telemetry"):
            self.send_response(404)
            self.end_headers()
            return
        try:
            length = int(self.headers.get("Content-Length", 0))
            body   = self.rfile.read(length) if length else b"{}"
            data   = json.loads(body) if body else {}
        except (ValueError, json.JSONDecodeError) as e:
            self._send_json({"ok": False, "err": f"invalid json: {e}"}, status=400)
            return

        sender_ip = self.client_address[0]
        # P4 hem IP hem mac'i body'de gönderebilir — IP'yi öncelikli kullan.
        if not data.get("ip_local"):
            data["ip_local"] = sender_ip
        data["last_post_at"] = time.time()
        data["last_post_age_s"] = 0
        p4_telemetry[sender_ip] = data

        # Stale entry'leri de drop et (30s'den eski P4'ler)
        _gc_stale_telemetry(now=time.time())

        self._send_json({"ok": True, "stored_for": sender_ip})

    def do_GET(self):
        if self.path == "/api/health" or self.path == "/health":
            self._send_json(self._collect())
        elif self.path == "/api/clients" or self.path == "/clients":
            self._send_json({
                "ts": int(time.time()),
                "clients": [
                    {"ip": ip, "uptime_s": int(time.time() - v["connected_at"])}
                    for ip, v in wss_clients.items()
                ],
            })
        else:
            self.send_response(404)
            self.end_headers()

    def log_message(self, format, *args):  # access log bastır
        pass


def _telemetry_snapshot(now: float) -> list:
    """Phase 2A: P4 telemetry dict'ini list[dict] olarak döner.
    Her entry'ye 'stale' flag ekler (last_post > 30s = stale=True)."""
    out = []
    for ip, info in p4_telemetry.items():
        entry = dict(info)
        entry["ip"]     = ip
        entry["stale"]  = bool(now - info.get("last_post_at", 0) > 30)
        out.append(entry)
    return out


def _gc_stale_telemetry(now: float, ttl_s: int = 60) -> None:
    """60 saniye post etmeyen P4'ü düşür — sonsuz dict büyümez."""
    stale = [ip for ip, info in p4_telemetry.items()
             if now - info.get("last_post_at", 0) > ttl_s]
    for ip in stale:
        p4_telemetry.pop(ip, None)


def _start_health_server(port: int = HEALTH_HTTP_PORT):
    """Health HTTP server'ı daemon thread'de başlat. asyncio loop'u bloklamaz."""
    try:
        server = HTTPServer(("0.0.0.0", port), _HealthHandler)
        t = threading.Thread(
            target=server.serve_forever,
            daemon=True,
            name="health-http",
        )
        t.start()
        print(f"[HEALTH] HTTP server :{port} başlatıldı (FletApp / health app için)")
        print(f"[HEALTH] Endpoints: GET /api/health, /api/clients · POST /api/telemetry")
        return server
    except OSError as e:
        print(f"[HEALTH] Port {port} kullanımda: {e} — health server başlatılamadı")
        return None


async def main():
    # Health HTTP server'ı başlat (FletApp / mobile health app için).
    # asyncio loop'tan ÖNCE yapılır — thread daemon olarak çalışır.
    _start_health_server()

    # Spotify token'ı başlat
    await refresh_spotify_token()

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
        print("  Voice Assistant Server v2.3 (MiniMax Mode)")
        print("=" * 60)
        print("  STT: whisper.cpp server (local)")
        print("  LLM: MiniMax-M2.7")
        print("  TTS: MiniMax speech-2.8-hd (Turkish_CalmWoman)")
        print("  Session: IP-based (reconnect-safe)")
        print("  ws://0.0.0.0:8765")
        print("=" * 60)

    print("[WS] Starting WebSocket server on 0.0.0.0:8765 ...")
    try:
        server = await websockets.serve(handle_client, "0.0.0.0", 8765)
        print(f"[WS] WebSocket server STARTED: {server.sockets[0].getsockname() if server.sockets else 'no sockets'}")
    except Exception as e:
        print(f"[WS] ERROR starting server: {type(e).__name__}: {e}")
        raise

    # === UDP broadcast başlat (P4 keşif için) ===
    # Daemon thread: process kapanınca otomatik ölür. asyncio event loop'u
    # bloklamaz çünkü ayrı thread'de koşar.
    disc_stop = threading.Event()
    disc_thread = threading.Thread(
        target=_udp_broadcaster,
        args=(disc_stop,),
        daemon=True,
        name="udp-broadcaster"
    )
    disc_thread.start()
    print(f"[DISC] UDP broadcaster başlatıldı (5s interval, local IP: {_get_local_ip()})")

    async with server:
        print("[WS] Server is now accepting connections")
        try:
            await asyncio.Future()
        finally:
            disc_stop.set()


if __name__ == "__main__":
    asyncio.run(main())