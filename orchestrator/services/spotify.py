"""
Spotify Web API entegrasyonu.

OAuth2 token caching + Web API client. spotipy kütüphanesi ile auth flow
(cache file üzerinden), httpx ile doğrudan REST çağrıları (kontrol + hız için).

Endpoint'ler (Spotify Web API):
  - GET  /v1/me/playlists              → playlist listesi
  - GET  /v1/playlists/{id}/tracks     → playlist track'leri
  - GET  /v1/me/player/devices         → aktif/uygun cihazlar
  - PUT  /v1/me/player/play            → playback başlat
  - PUT  /v1/me/player/pause           → pause
  - POST /v1/me/player/next            → sonraki şarkı
  - GET  /v1/me/player/currently-playing → çalan şarkı bilgisi

Token cache_path: ~/.cache/spotify_token (spotipy default).
"""

import random
import time

import httpx

from orchestrator.config import (
    SPOTIFY_BASE_URL,
    SPOTIFY_CACHE_PATH,
    SPOTIFY_CLIENT_ID,
    SPOTIFY_CLIENT_SECRET,
)


# ─── Token State ─────────────────────────────────────────────────────────────
# Global — refresh'ler burada cache'lenir. OAuth2 token ~1 saat geçerli.
# _token_expiry epoch'a göre (time.time() ile karşılaştırılabilir).
# asyncio loop'a bağımlılık yok — sync context'ten de çağrılabilir.
_token: str | None = None
_token_expiry: float = 0.0


def _is_token_valid() -> bool:
    return _token is not None and time.time() < _token_expiry


def _auth_headers() -> dict | None:
    """Geçerli token varsa Authorization header'ı, yoksa None."""
    if not _is_token_valid():
        return None
    return {"Authorization": f"Bearer {_token}"}


# ─── Token Yönetimi ─────────────────────────────────────────────────────────

async def refresh_token() -> bool:
    """Spotify OAuth2 token'ı cache'ten yükle (refresh veya initial).

    spotipy kütüphanesi cache_path'te (default ~/.cache/spotify_token) refresh
    token'ı saklar. Yenileme burada implicit — yeni access_token alıp global
    state'e yazar. İlk authentication cache'te yoksa False döner (kullanıcı
    Spotify login flow'u tamamlamalı).

    Returns:
        True: token yüklendi, geçerli
        False: credentials yok veya cache'te token yok (auth gerekli)
    """
    global _token, _token_expiry

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
            cache_path=SPOTIFY_CACHE_PATH,
        )

        token_info = oauth.get_cached_token()
        if not token_info:
            print("[Spotify] No cached token - need to authenticate")
            return False

        _token = token_info["access_token"]
        _token_expiry = time.time() + token_info["expires_in"]
        print(f"[Spotify] Token refreshed, expires in {token_info['expires_in']}s")
        return True

    except Exception as e:
        print(f"[Spotify] Token refresh error: {e}")
        return False


# ─── Playlist & Playback ────────────────────────────────────────────────────

async def get_playlists() -> list[dict]:
    """Kullanıcının playlistlerini döndür: [{id, name}, ...]."""
    headers = _auth_headers()
    if not headers:
        return []
    try:
        async with httpx.AsyncClient() as client:
            response = await client.get(
                f"{SPOTIFY_BASE_URL}/me/playlists",
                headers=headers,
                params={"limit": 50},
            )
        if response.status_code == 200:
            data = response.json()
            return [{"id": p["id"], "name": p["name"]} for p in data.get("items", [])]
        print(f"[Spotify] Playlists error: {response.status_code}")
        return []
    except Exception as e:
        print(f"[Spotify] Get playlists error: {e}")
        return []


async def get_current_track() -> dict:
    """Çalan şarkı bilgisi: {title, artist, is_playing}."""
    headers = _auth_headers()
    if not headers:
        return {"title": "", "artist": "", "is_playing": False}
    try:
        async with httpx.AsyncClient() as client:
            response = await client.get(
                f"{SPOTIFY_BASE_URL}/me/player/currently-playing",
                headers=headers,
            )
        if response.status_code == 200:
            data = response.json()
            if data and data.get("item"):
                track = data["item"]
                return {
                    "title": track.get("name", ""),
                    "artist": ", ".join(a["name"] for a in track.get("artists", [])),
                    "is_playing": data.get("is_playing", False),
                }
        return {"title": "", "artist": "", "is_playing": False}
    except Exception as e:
        print(f"[Spotify] Get current track error: {e}")
        return {"title": "", "artist": "", "is_playing": False}


async def play_random_from_playlist(playlist_id: str) -> bool:
    """Playlist'ten rastgele şarkı seç ve mevcut cihazda çal.

    Akış:
      1. /playlists/{id}/tracks → track listesi
      2. /me/player/devices → aktif/ilk cihaz
      3. /me/player/play (PUT) → context_uri + offset ile başlat

    Returns:
        True: şarkı çalmaya başladı (HTTP 200/204)
    """
    headers = _auth_headers()
    if not headers:
        return False
    try:
        async with httpx.AsyncClient() as client:
            resp = await client.get(
                f"{SPOTIFY_BASE_URL}/playlists/{playlist_id}/tracks",
                headers=headers,
                params={"fields": "items(track(id,name,artists(name)))"},
            )
        if resp.status_code != 200:
            print(f"[Spotify] Playlist tracks error: {resp.status_code}")
            return False

        tracks = resp.json().get("items", [])
        if not tracks:
            print("[Spotify] Empty playlist")
            return False

        # Active device bul
        async with httpx.AsyncClient() as client:
            dev_resp = await client.get(
                f"{SPOTIFY_BASE_URL}/me/player/devices",
                headers=headers,
            )
        device_id = None
        if dev_resp.status_code == 200:
            devices = dev_resp.json().get("devices", [])
            for d in devices:
                if d.get("is_active"):
                    device_id = d["id"]
                    break
            if not device_id and devices:
                device_id = devices[0]["id"]
        if not device_id:
            print("[Spotify] No active device")
            return False

        async with httpx.AsyncClient() as client:
            play_resp = await client.put(
                f"{SPOTIFY_BASE_URL}/me/player/play",
                headers=headers,
                json={
                    "context_uri": f"spotify:playlist:{playlist_id}",
                    "offset": {"position": random.randint(0, len(tracks) - 1)},
                    "device_id": device_id,
                },
            )
        if play_resp.status_code in (200, 204):
            print(f"[Spotify] Playing random track from playlist {playlist_id}")
            return True
        print(f"[Spotify] Play error: {play_resp.status_code}")
        return False
    except Exception as e:
        print(f"[Spotify] Play error: {e}")
        return False


async def pause() -> bool:
    """Playback'i duraklat."""
    headers = _auth_headers()
    if not headers:
        return False
    try:
        async with httpx.AsyncClient() as client:
            response = await client.put(
                f"{SPOTIFY_BASE_URL}/me/player/pause", headers=headers
            )
        return response.status_code in (200, 204)
    except Exception as e:
        print(f"[Spotify] Pause error: {e}")
        return False


async def resume() -> bool:
    """Playback'i devam ettir."""
    headers = _auth_headers()
    if not headers:
        return False
    try:
        async with httpx.AsyncClient() as client:
            response = await client.put(
                f"{SPOTIFY_BASE_URL}/me/player/play", headers=headers
            )
        return response.status_code in (200, 204)
    except Exception as e:
        print(f"[Spotify] Resume error: {e}")
        return False


async def skip() -> bool:
    """Sonraki şarkıya atla."""
    headers = _auth_headers()
    if not headers:
        return False
    try:
        async with httpx.AsyncClient() as client:
            response = await client.post(
                f"{SPOTIFY_BASE_URL}/me/player/next", headers=headers
            )
        return response.status_code in (200, 204)
    except Exception as e:
        print(f"[Spotify] Skip error: {e}")
        return False


# ─── Tool Schemas (OpenAI function_calling) ─────────────────────────────────

TOOL_SCHEMA_GET_PLAYLISTS = {
    "type": "function",
    "function": {
        "name": "spotify.get_playlists",
        "description": "Kullanıcının tüm Spotify playlistlerini listeler: "
                       "[{id, name}, ...]. 'müzik aç' gibi komutlarda ilk "
                       "playlist seçilebilir.",
        "parameters": {"type": "object", "properties": {}, "required": []},
    },
}

TOOL_SCHEMA_PLAY_RANDOM = {
    "type": "function",
    "function": {
        "name": "spotify.play_random_from_playlist",
        "description": "Belirtilen playlist ID'sinden rastgele bir şarkı çalar. "
                       "Önce get_playlists ile liste al, kullanıcı seçerse veya "
                       "ilk playlist'i kullan.",
        "parameters": {
            "type": "object",
            "properties": {
                "playlist_id": {
                    "type": "string",
                    "description": "Spotify playlist ID (URI sonundaki 22-char hash)",
                },
            },
            "required": ["playlist_id"],
        },
    },
}

TOOL_SCHEMA_PAUSE = {
    "type": "function",
    "function": {
        "name": "spotify.pause",
        "description": "Spotify playback'i duraklatır.",
        "parameters": {"type": "object", "properties": {}, "required": []},
    },
}

TOOL_SCHEMA_RESUME = {
    "type": "function",
    "function": {
        "name": "spotify.resume",
        "description": "Spotify playback'i devam ettirir.",
        "parameters": {"type": "object", "properties": {}, "required": []},
    },
}

TOOL_SCHEMA_SKIP = {
    "type": "function",
    "function": {
        "name": "spotify.skip",
        "description": "Spotify'da sonraki şarkıya atlar.",
        "parameters": {"type": "object", "properties": {}, "required": []},
    },
}

TOOL_SCHEMA_GET_CURRENT_TRACK = {
    "type": "function",
    "function": {
        "name": "spotify.get_current_track",
        "description": "Spotify'da şu an çalan şarkının bilgisi: title, artist.",
        "parameters": {"type": "object", "properties": {}, "required": []},
    },
}
