"""
ESP32-P4 WebSocket protokolü + bağlı client state'i.

Tek giriş: handle_client(websocket). P4 bağlandığında websockets.serve
tarafından çağrılır. Mesaj döngüsünde:
  - "PTT_START" / "PTT_STOP": ses kaydı başlat/durdur, STT başlat
  - "SPOTIFY_*": playlist/control komutları
  - "LED_*": TBR (ESP32-S3) üzerinden Matter LED kontrolü
  - bytes mesaj: aynı komutlar (LED_TOGGLE bytes formu) + ses verisi

Process audio (STT→LLM→TTS) core/pipeline.py'de tutulur; buradan çağrılır.

Shared state:
  wss_clients:   {ip: {connected_at, last_msg_at, last_msg}} — health.py okur
  p4_telemetry:  {ip: {heap, rssi, ...}}                  — health.py okur
  active_pipelines: {ip: bool}                            — re-entry guard
"""

import asyncio
import json
import struct
import time
import traceback

import websockets

from orchestrator.core import pipeline as pipeline_svc
from orchestrator.services import matter as matter_svc
from orchestrator.services import spotify as spotify_svc


# ─── Global State (health.py ile paylaşılır) ─────────────────────────────────

wss_clients: dict[str, dict] = {}
p4_telemetry: dict[str, dict] = {}
active_pipelines: dict[str, bool] = {}


# ─── P4 Mesaj Protokolü ──────────────────────────────────────────────────────

_PTT_START = ("PTT_START", "PTT_STAR", "PTT_STA")
_PTT_STOP  = ("PTT_STOP",  "PTT_STO",  "PTT_ST")
_PTT_START_BYTES = (b"PTT_START", b"PTT_STAR", b"PTT_STA")
_PTT_STOP_BYTES  = (b"PTT_STOP",  b"PTT_STO",  b"PTT_ST")
_MIN_AUDIO_BYTES = 4000  # PTT_STOP sonrası minimum audio, daha az → atla


# ─── Yardımcılar ────────────────────────────────────────────────────────────

def _track_msg(client_ip: str, message) -> None:
    """wss_clients[ip]['last_msg_*'] alanlarını güncelle (health için)."""
    if client_ip not in wss_clients:
        return
    wss_clients[client_ip]["last_msg_at"] = time.time()
    if isinstance(message, str):
        wss_clients[client_ip]["last_msg"] = repr(message)[:50]
    elif isinstance(message, (bytes, bytearray)):
        wss_clients[client_ip]["last_msg"] = f"audio {len(message)}B"
    else:
        wss_clients[client_ip]["last_msg"] = type(message).__name__


async def _send_json(websocket, payload: dict) -> None:
    await websocket.send(json.dumps(payload))


# ─── Spotify / LED Komut Handler'ları (str form) ───────────────────────────

async def _handle_spotify(websocket, msg: str) -> None:
    if msg == "SPOTIFY_LIST":
        playlists = await spotify_svc.get_playlists()
        await _send_json(websocket, {"type": "spotify_playlists", "playlists": playlists})
    elif msg.startswith("SPOTIFY_PLAY:"):
        playlist_id = msg.split(":", 1)[1]
        success = await spotify_svc.play_random_from_playlist(playlist_id)
        await _send_json(websocket, {"type": "spotify_play_result", "success": success})
    elif msg == "SPOTIFY_STATUS":
        track = await spotify_svc.get_current_track()
        await _send_json(websocket, {
            "type": "spotify_status",
            "title": track["title"],
            "artist": track["artist"],
            "is_playing": track["is_playing"],
        })
    elif msg == "SPOTIFY_PAUSE":
        success = await spotify_svc.pause()
        await _send_json(websocket, {"type": "spotify_pause_result", "success": success})
    elif msg == "SPOTIFY_RESUME":
        success = await spotify_svc.resume()
        await _send_json(websocket, {"type": "spotify_resume_result", "success": success})
    elif msg == "SPOTIFY_SKIP":
        success = await spotify_svc.skip()
        await _send_json(websocket, {"type": "spotify_skip_result", "success": success})


async def _handle_led(websocket, msg: str) -> None:
    if msg == "LED_TOGGLE":
        success = await matter_svc.toggle()
        state = await matter_svc.get_status()
        await _send_json(websocket, {"type": "led_toggle_result", "success": success, "state": state})
    elif msg.startswith("LED_COLOR:"):
        try:
            parts = msg.split(":")
            color_x, color_y = int(parts[1]), int(parts[2])
            success = await matter_svc.set_color(color_x, color_y)
            state = await matter_svc.get_status()
            await _send_json(websocket, {
                "type": "led_color_result", "success": success,
                "color_x": color_x, "color_y": color_y, "state": state,
            })
        except (IndexError, ValueError):
            await _send_json(websocket, {
                "type": "led_color_result", "success": False,
                "error": "Invalid format. Use LED_COLOR:x:y",
            })
    elif msg.startswith("LED_BRIGHTNESS:"):
        try:
            level = int(msg.split(":")[1])
            success = await matter_svc.set_brightness(level)
            state = await matter_svc.get_status()
            await _send_json(websocket, {
                "type": "led_brightness_result", "success": success,
                "brightness": level, "state": state,
            })
        except (IndexError, ValueError):
            await _send_json(websocket, {
                "type": "led_brightness_result", "success": False,
                "error": "Invalid format. Use LED_BRIGHTNESS:level",
            })
    elif msg == "LED_STATUS":
        state = await matter_svc.get_status()
        await _send_json(websocket, {"type": "led_status", "state": state})


# ─── Ses Kayıt Yönetimi ─────────────────────────────────────────────────────

class _Recorder:
    """Per-connection state: audio buffer, recording flag, expected length."""

    def __init__(self) -> None:
        self.buffer = bytearray()
        self.recording = False
        self.expected_len = 0

    def reset(self) -> None:
        self.buffer.clear()
        self.expected_len = 0
        self.recording = True

    def stop(self) -> int:
        self.recording = False
        return len(self.buffer)

    def feed(self, data: bytes) -> None:
        if self.recording:
            self.buffer.extend(data)


# ─── Ana Handler ────────────────────────────────────────────────────────────

async def handle_client(websocket) -> None:
    """Bir P4 bağlantısı için mesaj döngüsü. Bağlantı kapanana kadar çalışır.

    session_id = client IP. Reconnect sonrası aynı IP → aynı history (LLM
    services/llm.py'de tutulur).
    """
    client_ip = websocket.remote_address[0]
    session_id = client_ip
    print(f"[+] Client connected: {websocket.remote_address} (session={session_id})")

    wss_clients[client_ip] = {
        "ip": client_ip,
        "connected_at": time.time(),
        "last_msg_at": time.time(),
        "last_msg": "WS connected",
    }
    recorder = _Recorder()

    try:
        async for message in websocket:
            _track_msg(client_ip, message)

            # ─── String Mesajlar: Kontrol Komutları ────────────────────────
            if isinstance(message, str):
                msg = message.strip()

                if msg in _PTT_START:
                    if session_id in active_pipelines:
                        print("[!] Pipeline already running, ignoring PTT_START")
                        continue
                    recorder.reset()
                    print(f"[*] Recording started (msg: {repr(message)})")

                elif msg in _PTT_STOP:
                    recorder.stop()
                    n = len(recorder.buffer)
                    print(f"[*] Recording stopped - {n} bytes")
                    if n >= _MIN_AUDIO_BYTES:
                        active_pipelines[session_id] = True
                        snapshot = bytearray(recorder.buffer)
                        asyncio.ensure_future(
                            pipeline_svc.process_audio(websocket, session_id, snapshot)
                        )
                    else:
                        print(f"[!] Audio too short: {n} bytes")

                elif msg.startswith("SPOTIFY_"):
                    await _handle_spotify(websocket, msg)

                elif msg.startswith("LED_"):
                    await _handle_led(websocket, msg)

                # Bilinmeyen string mesaj → yoksay (gelecek protokol eklemeleri)

            # ─── Bytes Mesajlar: Audio Stream + Basit Komutlar ──────────────
            elif isinstance(message, (bytes, bytearray)):
                msg_bytes = message.strip()

                if msg_bytes in _PTT_START_BYTES:
                    if session_id in active_pipelines:
                        continue
                    recorder.reset()
                    print("[*] Recording started (bytes)")

                elif msg_bytes in _PTT_STOP_BYTES:
                    recorder.stop()
                    n = len(recorder.buffer)
                    print(f"[*] Recording stopped (bytes) - {n} bytes")
                    if n >= _MIN_AUDIO_BYTES:
                        active_pipelines[session_id] = True
                        snapshot = bytearray(recorder.buffer)
                        asyncio.ensure_future(
                            pipeline_svc.process_audio(websocket, session_id, snapshot)
                        )

                # 4-byte size header (audio length, big-endian uint32)
                elif len(message) == 4 and recorder.expected_len == 0:
                    size = struct.unpack(">I", message)[0]
                    if 1000 < size < 1_000_000:
                        recorder.expected_len = size
                        print(f"[*] Audio size header: {size} bytes")
                    # else: spurious 4-byte message, ignore

                # Audio data
                elif recorder.recording:
                    recorder.feed(message)
                    if (recorder.expected_len and
                            len(recorder.buffer) % 8192 == 0) or \
                            len(recorder.buffer) == recorder.expected_len:
                        print(f"[*] Buffer: {len(recorder.buffer)}"
                              f"/{recorder.expected_len} bytes")

                # LED komutlar (bytes form)
                elif msg_bytes.startswith(b"LED_TOGGLE"):
                    asyncio.ensure_future(matter_svc.toggle())
                elif msg_bytes.startswith(b"LED_COLOR:"):
                    parts = msg_bytes.decode().split(":")
                    if len(parts) >= 3:
                        try:
                            x, y = int(parts[1]), int(parts[2])
                            asyncio.ensure_future(matter_svc.set_color(x, y))
                        except ValueError:
                            pass
                elif msg_bytes.startswith(b"LED_BRIGHTNESS:"):
                    parts = msg_bytes.decode().split(":")
                    if len(parts) >= 2:
                        try:
                            level = int(parts[1])
                            asyncio.ensure_future(matter_svc.set_brightness(level))
                        except ValueError:
                            pass

    except websockets.exceptions.ConnectionClosed:
        print(f"[-] Connection closed ({session_id})")
    except Exception as e:
        print(f"[WS] {session_id} error: {type(e).__name__}: {e}")
        traceback.print_exc()
    finally:
        active_pipelines.pop(session_id, None)
        if client_ip in wss_clients:
            uptime = int(time.time() - wss_clients[client_ip]["connected_at"])
            del wss_clients[client_ip]
            print(f"[-] Health: client {client_ip} düştü (uptime: {uptime}s)")
        print(f"[-] Session {session_id} disconnected")


# ─── Server Başlatma ─────────────────────────────────────────────────────────

WS_PORT = 8765


async def start_server(host: str = "0.0.0.0", port: int = WS_PORT):
    """WebSocket server'ı başlat (blocking). main.py içinde await edilir."""
    print(f"[WS] Starting WebSocket server on {host}:{port} ...")
    try:
        server = await websockets.serve(handle_client, host, port)
        sockname = server.sockets[0].getsockname() if server.sockets else "no sockets"
        print(f"[WS] WebSocket server STARTED: {sockname}")
        return server
    except Exception as e:
        print(f"[WS] ERROR starting server: {type(e).__name__}: {e}")
        raise
