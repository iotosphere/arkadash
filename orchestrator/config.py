"""
Merkezi konfigürasyon modülü.

Tüm ortam değişkenleri, API anahtarları ve sabitler tek bir yerden yönetilir.
.env dosyası veya process env'den okunur; runtime'da değiştirilmez.

Bölümler:
  - LLM provider credentials
  - TTS / STT backend seçimi
  - HTTP server portları
  - Spotify Web API
  - Donanım: TBR (ESP32-S3) Matter controller HTTP endpoint
  - Sistem promptu ve sohbet geçmişi sınırları
"""

import os


# ─── LLM Provider Credentials ────────────────────────────────────────────────

MINIMAX_API_KEY: str = os.environ.get("MINIMAX_API_KEY", "")
MINIMAX_GROUP_ID: str = os.environ.get("MINIMAX_GROUP_ID", "")

# Gemini sadece TTS (gemini-2.5-flash-tts-preview) için kullanılıyor; LLM değil.
GEMINI_API_KEY: str = os.environ.get("GEMINI_API_KEY", "")


# ─── TTS / STT Backends ──────────────────────────────────────────────────────

# TTS_BACKEND: "minimax" | "edge" | "google_cloud" | "gemini"
TTS_BACKEND: str = os.environ.get("TTS_BACKEND", "minimax")

# whisper.cpp server (local). /inference endpoint'ine WAV multipart POST atar.
WHISPER_SERVER: str = "http://localhost:8080"


# ─── HTTP Server Ports ───────────────────────────────────────────────────────

# FletApp / health monitor REST endpoint'leri.
HEALTH_HTTP_PORT: int = int(os.environ.get("HEALTH_PORT", "8088"))


# ─── Spotify Web API ─────────────────────────────────────────────────────────

SPOTIFY_CLIENT_ID: str = os.environ.get("SPOTIFY_CLIENT_ID", "")
SPOTIFY_CLIENT_SECRET: str = os.environ.get("SPOTIFY_CLIENT_SECRET", "")
SPOTIFY_CACHE_PATH: str = os.path.expanduser("~/.cache/spotify_token")
SPOTIFY_BASE_URL: str = "https://api.spotify.com/v1"


# ─── Donanım: TBR (ESP32-S3) Matter Controller ───────────────────────────────
#
# TBR = Thread Border Router + Matter controller + HTTP server. Living room
# LED bu endpoint üzerinden kontrol edilir: TBR HTTP → Matter → ESP32-H2 light.
# Router DHCP ile bu IP'yi atar; reboot sonrası değişebilir → agent_server
# başlarken log'dan doğrula veya kullanıcıya sor.
ESP32_S3_HTTP_BASE: str = "http://192.168.1.15:8766"


# ─── Sistem Promptu ve Sohbet Geçmişi ───────────────────────────────────────

SYSTEM_PROMPT: str = """Sen Turkce konusan yardimci bir sesli asistansin.
SADECE Turkce cevap ver, asla baska dil kullanma.
Kisa ve dogal cevaplar ver, 1-2 cumle yeterli."""

# Kullanıcı + asistan mesaj çiftlerinin (turn) korunma sınırı. Token tasarrufu
# için eski mesajlar otomatik düşer. Konuşma bağlamı IP-keyed dict'te tutulur.
MAX_HISTORY_TURNS: int = 20
