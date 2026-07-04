"""
FletApp / health monitor için REST HTTP server.

Endpoint'ler:
  GET  /api/health     → JSON: uptime, bağlı client'lar, p4 telemetry, spotify, voice
  GET  /api/clients    → JSON: bağlı WebSocket client listesi
  POST /api/telemetry  → P4 firmware periyodik telemetry alır (heap, rssi, ssid, vb.)

Threading:
  BaseHTTPRequestHandler blocking — daemon thread'de çalıştırılır, asyncio
  event loop'u etkilemez. CORS header'ları FletApp'in browser tabanlı runtime'ı
  için gerekli (Access-Control-Allow-Origin: *).

Shared state (network/websocket.py'den okunur):
  wss_clients:   {ip: {connected_at, last_msg_at, last_msg}}
  p4_telemetry:  {ip: {heap_free, rssi, ssid, mac, ip_local, uptime_s, ...}}
"""

import json
import threading
import time
from http.server import BaseHTTPRequestHandler, HTTPServer

from orchestrator.config import (
    HEALTH_HTTP_PORT,
    SPOTIFY_CLIENT_ID,
    WHISPER_SERVER,
)
from orchestrator.services import spotify as spotify_svc
from orchestrator.services import llm as llm_svc


# ─── Başlangıç Zamanı (uptime hesabı) ──────────────────────────────────────

_START_TIME = time.time()


# ─── State Import'ları (lazy, circular import önleme) ──────────────────────
# wss_clients ve p4_telemetry network/websocket.py tarafından doldurulur.
# Doğrudan import etmek circular'a yol açar; bu yüzden modül seviyesinde
# değil, handler içinde import edilir.

def _get_websocket_state():
    """Lazy import — handler runtime'ında çağrılır."""
    from orchestrator.network import websocket as ws
    return ws.wss_clients


def _get_telemetry_state():
    """Lazy import — handler runtime'ında çağrılır."""
    from orchestrator.network import websocket as ws
    return ws.p4_telemetry


# ─── Telemetry Snapshot Yardımcıları ─────────────────────────────────────────

_TELEMETRY_TTL_S = 60


def _gc_stale_telemetry(now: float) -> None:
    """60 saniye post etmeyen P4'ü düşür — sonsuz dict büyümesin."""
    p4_telemetry = _get_telemetry_state()
    stale = [ip for ip, info in p4_telemetry.items()
             if now - info.get("last_post_at", 0) > _TELEMETRY_TTL_S]
    for ip in stale:
        p4_telemetry.pop(ip, None)


def _telemetry_snapshot(now: float) -> list[dict]:
    """P4 telemetry dict'ini list[dict] olarak döner; stale flag ekler.

    Her entry: ip + P4'ün gönderdiği alanlar + stale (>30s post etmemiş).
    """
    p4_telemetry = _get_telemetry_state()
    out = []
    for ip, info in p4_telemetry.items():
        entry = dict(info)
        entry["ip"] = ip
        entry["stale"] = bool(now - info.get("last_post_at", 0) > 30)
        out.append(entry)
    return out


# ─── HTTP Handler ────────────────────────────────────────────────────────────

class _HealthHandler(BaseHTTPRequestHandler):
    """REST endpoint'leri BaseHTTPRequestHandler üzerinden."""

    def _send_json(self, payload: dict, status: int = 200) -> None:
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
        wss_clients = _get_websocket_state()
        clients = []
        for ip, info in wss_clients.items():
            clients.append({
                "ip": ip,
                "uptime_s": int(now - info["connected_at"]),
                "last_msg_age_s": int(now - info.get("last_msg_at", info["connected_at"])),
                "last_msg": info.get("last_msg", ""),
            })
        spotify_enabled = bool(SPOTIFY_CLIENT_ID and SPOTIFY_CLIENT_ID != "your-spotify-client-id")
        spotify_authed = bool(spotify_svc._token)
        return {
            "ts":           int(now),
            "agent":        "arkadash-voice-server",
            "version":      "v2.3-health",
            "uptime_s":     int(now - _START_TIME),
            "ws_server":    "0.0.0.0:8765",
            "clients":      clients,
            "p4_telemetry": _telemetry_snapshot(now),
            "spotify": {
                "enabled": spotify_enabled,
                "authed":  spotify_authed,
            },
            "whisper_stt":  WHISPER_SERVER,
            "voice": {
                "active_sessions": len(wss_clients),
                "history_msgs":    sum(len(h) for h in llm_svc.conversation_histories.values()),
            },
        }

    def do_POST(self) -> None:
        """P4 firmware periyodik telemetry POST eder (her 10s).

        Body JSON: {heap_free, rssi, ssid, mac, ip_local, uptime_s, matter_count}
        Sender IP = primary key (ws_clients ile aynı IP olur).
        """
        if self.path not in ("/api/telemetry", "/telemetry"):
            self.send_response(404)
            self.end_headers()
            return
        try:
            length = int(self.headers.get("Content-Length", 0))
            body = self.rfile.read(length) if length else b"{}"
            data = json.loads(body) if body else {}
        except (ValueError, json.JSONDecodeError) as e:
            self._send_json({"ok": False, "err": f"invalid json: {e}"}, status=400)
            return

        sender_ip = self.client_address[0]
        if not data.get("ip_local"):
            data["ip_local"] = sender_ip
        data["last_post_at"] = time.time()
        data["last_post_age_s"] = 0

        p4_telemetry = _get_telemetry_state()
        p4_telemetry[sender_ip] = data
        _gc_stale_telemetry(now=time.time())

        self._send_json({"ok": True, "stored_for": sender_ip})

    def do_GET(self) -> None:
        if self.path in ("/api/health", "/health"):
            self._send_json(self._collect())
        elif self.path in ("/api/clients", "/clients"):
            self._send_json({
                "ts": int(time.time()),
                "clients": [
                    {"ip": ip, "uptime_s": int(time.time() - v["connected_at"])}
                    for ip, v in _get_websocket_state().items()
                ],
            })
        else:
            self.send_response(404)
            self.end_headers()

    def log_message(self, format, *args) -> None:
        # access log bastır — sade
        pass


# ─── Server Başlatma ─────────────────────────────────────────────────────────

def start(port: int = HEALTH_HTTP_PORT) -> HTTPServer | None:
    """Health HTTP server'ı daemon thread'de başlat.

    asyncio loop'tan ÖNCE çağrılmalı. OSError (port meşgul) olursa None döner
    ve uyarı loglanır; uygulama yine de çalışabilir (sadece health endpoint yok).
    """
    try:
        server = HTTPServer(("0.0.0.0", port), _HealthHandler)
        threading.Thread(
            target=server.serve_forever,
            daemon=True,
            name="health-http",
        ).start()
        print(f"[HEALTH] HTTP server :{port} başlatıldı (FletApp / health app için)")
        print("[HEALTH] Endpoints: GET /api/health, /api/clients · POST /api/telemetry")
        return server
    except OSError as e:
        print(f"[HEALTH] Port {port} kullanımda: {e} — health server başlatılamadı")
        return None
