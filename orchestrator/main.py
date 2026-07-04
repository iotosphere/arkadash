"""
Voice Assistant Server v2.3 — entry point.

Başlangıç sırası:
  1. Health HTTP server (daemon thread, port 8088) — FletApp / health app
  2. Spotify OAuth token refresh (async, ana event loop'ta)
  3. UDP broadcast başlat (daemon thread) — P4 keşif
  4. WebSocket server başlat (async, blocking, port 8765) — ana iş

Graceful shutdown: SIGINT/SIGTERM → stop_event.set() + ws_server.close().

Gereksinimler:
  - env: MINIMAX_API_KEY, MINIMAX_GROUP_ID, [GEMINI_API_KEY, SPOTIFY_*]
  - system: ffmpeg (PATH'te), whisper.cpp server (http://localhost:8080)
"""

import asyncio
import signal
import threading

from orchestrator.config import (
    GEMINI_API_KEY,
    MINIMAX_API_KEY,
    MINIMAX_GROUP_ID,
    TTS_BACKEND,
)
from orchestrator.network.discovery import run_broadcaster
from orchestrator.network.health import start as start_health_server
from orchestrator.network.websocket import start_server
from orchestrator.services.spotify import refresh_token


def _print_banner() -> None:
    print("=" * 60)
    print("  Voice Assistant Server v2.3 (MiniMax Mode)")
    print("=" * 60)
    print("  STT: whisper.cpp server (local)")
    print("  LLM: MiniMax-M2.7")
    print(f"  TTS: {TTS_BACKEND} "
          f"({'Turkish_CalmWoman' if TTS_BACKEND == 'minimax' else TTS_BACKEND})")
    print("  Session: IP-based (reconnect-safe)")
    print("  ws://0.0.0.0:8765")
    print("=" * 60)


def _validate_credentials() -> None:
    """Eksik credentials varsa uyar, gerekli olanlar için çık."""
    if not MINIMAX_API_KEY:
        print("ERROR: MINIMAX_API_KEY not set!")
        raise SystemExit(1)
    if not MINIMAX_GROUP_ID:
        print("ERROR: MINIMAX_GROUP_ID not set!")
        raise SystemExit(1)
    if TTS_BACKEND not in ("edge",) and not GEMINI_API_KEY:
        # minimax + google_cloud + gemini → GEMINI key gerekli değil
        # (sadece gemini backend için)
        if TTS_BACKEND == "gemini" and not GEMINI_API_KEY:
            print(f"ERROR: GEMINI_API_KEY not set (required for TTS_BACKEND={TTS_BACKEND})!")
            raise SystemExit(1)


async def main() -> None:
    _print_banner()
    _validate_credentials()

    # 1) Health HTTP server (daemon thread, asyncio loop'tan ÖNCE başlatılır)
    start_health_server()

    # 2) Spotify token refresh (ana event loop'ta)
    await refresh_token()

    # 3) UDP broadcast (daemon thread) — P4 keşif
    stop_event = threading.Event()
    threading.Thread(
        target=run_broadcaster,
        args=(stop_event,),
        daemon=True,
        name="udp-broadcaster",
    ).start()

    # 4) WebSocket server (blocking, ana event loop'ta)
    ws_server = await start_server()

    # Graceful shutdown: SIGINT/SIGTERM
    loop = asyncio.get_event_loop()
    def _shutdown() -> None:
        print("\n[MAIN] Shutdown signal received")
        stop_event.set()
        ws_server.close()
    try:
        loop.add_signal_handler(signal.SIGINT, _shutdown)
        loop.add_signal_handler(signal.SIGTERM, _shutdown)
    except (NotImplementedError, RuntimeError):
        # Windows / non-unix fallback
        pass

    # Run forever
    await asyncio.Future()  # never completes, cancelled by signal


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\n[MAIN] KeyboardInterrupt, exiting")
