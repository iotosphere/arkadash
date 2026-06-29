"""
Arkadash mobile API client.

Talks to the Mac-side `agent_server.py` over HTTP. agent_server (port 8088'de
/health endpoint'i) sesli pipeline istatistiklerini ve WebSocket bağlı
client'ları raporlar. FletApp bu endpoint'i polling yaparak canlı
bağlantı sağlığı gösterir.

Endpoints (Phase 2A):
  GET  /api/health     -> {ts, agent, ..., clients, p4_telemetry, spotify, voice}
  GET  /api/clients    -> {ts, clients: [{ip, uptime_s}]}
  POST /api/telemetry  -> P4 firmware periyodik POST eder;
                          body: {heap_free, rssi, ssid, mac, ip_local,
                                 uptime_s, matter_count}
                          Sender IP = storage key.
"""

from __future__ import annotations

import os
import urllib.request
import urllib.parse
import urllib.error
import json
from dataclasses import dataclass
from typing import Optional


# Default to the Mac's LAN IP. agent_server.py port 8088'de health
# HTTP endpoint'i yayınlar. Override with ARKADASH_AGENT_URL env var.
DEFAULT_AGENT_URL = os.environ.get("ARKADASH_AGENT_URL", "http://192.168.1.6:8088")


@dataclass
class ClientInfo:
    ip: str
    uptime_s: int
    last_msg_age_s: int
    last_msg: str


@dataclass
class TelemetryInfo:
    """Phase 2A: P4 firmware'in POST /api/telemetry ile gönderdiği veri.
    sender IP > sender_ip (boilerplate)."""
    sender_ip:    str          # post eden IP (agent_server sender IP)
    heap_free:    int          # bytes
    rssi:         int          # dBm (negatif, e.g. -58)
    ssid:         str          # bağlı olduğu WiFi ağı
    mac:          str          # P4 WiFi STA MAC
    local_ip:     str          # P4 kendi IP'si
    uptime_s:     int          # P4 boot sonrası
    matter_count: int          # commission'lı device sayısı (Phase 2B)
    stale:        bool         # last_post > 30s ise True
    raw:          dict


@dataclass
class HealthInfo:
    uptime_s: int               # agent_server'ın çalışma süresi
    active_clients: list        # bağlı P4 client'ları
    p4_telemetry: list          # P4'lerin son POST ettikleri telemetry
    spotify_enabled: bool
    spotify_authed: bool
    history_msgs: int           # toplam konuşma geçmişi
    raw: dict                   # debug için tam JSON


class ArkadashApi:
    """Thin synchronous HTTP client. Swap for `aiohttp` if we go async."""

    def __init__(self, base_url: str = DEFAULT_AGENT_URL, timeout_s: float = 4.0):
        self.base_url = base_url.rstrip("/")
        self.timeout_s = timeout_s

    # ── low-level ────────────────────────────────────────────────────────

    def _request(self, method: str, path: str, params: dict | None = None) -> dict | None:
        url = f"{self.base_url}{path}"
        if params:
            url += "?" + urllib.parse.urlencode(params)
        req = urllib.request.Request(url, method=method)
        try:
            with urllib.request.urlopen(req, timeout=self.timeout_s) as r:
                body = r.read().decode("utf-8")
                return json.loads(body) if body else None
        except (urllib.error.URLError, urllib.error.HTTPError, TimeoutError, OSError) as e:
            return {"_error": str(e), "_url": url}

    # ── health ──────────────────────────────────────────────────────────

    def ping(self) -> bool:
        """Hızlı canlılık testi: agent_server ayakta mı?"""
        r = self._request("GET", "/api/health")
        return bool(r) and "_error" not in r

    def get_health(self) -> Optional[HealthInfo]:
        """Detaylı sağlık: clients, uptime, spotify, voice."""
        raw = self._request("GET", "/api/health")
        if not raw or "_error" in raw:
            return None
        try:
            spotify_block = raw.get("spotify", {}) or {}
            voice_block = raw.get("voice", {}) or {}
            clients = [
                ClientInfo(
                    ip=c.get("ip", "?"),
                    uptime_s=int(c.get("uptime_s", 0)),
                    last_msg_age_s=int(c.get("last_msg_age_s", 0)) if c.get("last_msg_age_s") else -1,
                    last_msg=c.get("last_msg", ""),
                )
                for c in raw.get("clients", [])
            ]
            telemetry = [
                TelemetryInfo(
                    sender_ip    = t.get("ip", "?"),
                    heap_free    = int(t.get("heap_free", 0)),
                    rssi         = int(t.get("rssi", 0)),
                    ssid         = t.get("ssid", ""),
                    mac          = t.get("mac", ""),
                    local_ip     = t.get("ip_local", ""),
                    uptime_s     = int(t.get("uptime_s", 0)),
                    matter_count = int(t.get("matter_count", 0)),
                    stale        = bool(t.get("stale", False)),
                    raw          = t,
                )
                for t in raw.get("p4_telemetry", [])
            ]
            return HealthInfo(
                uptime_s=int(raw.get("uptime_s", 0)),
                active_clients=clients,
                p4_telemetry=telemetry,
                spotify_enabled=bool(spotify_block.get("enabled", False)),
                spotify_authed=bool(spotify_block.get("authed", False)),
                history_msgs=int(voice_block.get("history_msgs", 0)),
                raw=raw,
            )
        except (KeyError, TypeError, ValueError) as e:
            return None

    def get_clients(self) -> list:
        """Sadece bağlı client listesi (raw)."""
        raw = self._request("GET", "/api/clients")
        if not raw or "_error" in raw:
            return []
        return raw.get("clients", []) or []


# CLI debug için
if __name__ == "__main__":
    import json as _json
    api = ArkadashApi()
    print("=== ping ===")
    print(api.ping())
    print("=== health ===")
    h = api.get_health()
    if h:
        print(_json.dumps(h.raw, indent=2, ensure_ascii=False))
    else:
        print("no health")
