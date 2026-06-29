# Arkadash Flet App

Dark-theme mobile/desktop dashboard for Arkadash (P4 panel health + voice agent).

## What's here

- `src/arkadash/app.py` — 4-tab layout: **Health** (active), Smart Home, Music, Agent
- `src/arkadash/services/api.py` — HTTP client for agent_server `/api/health`
- `src/arkadash/views/health.py` — live polling view (WiFi / Memory / Matter / Spotify / Voice / P4 Telemetry / Connected WS Clients)

## Run

```bash
# First time — install deps via Flet CLI into .fletApp/
uv run flet run
# or
.fletApp/bin/python -u src/main.py
```

Pencere açılır, **Health** sekmesi otomatik polling başlatır (3s interval).
agent_server'ın ayakta ve `http://192.168.1.6:8088` üzerinden erişilebilir olması gerekir.

Override agent URL:

```bash
export ARKADASH_AGENT_URL=http://192.168.1.45:8088
uv run flet run
```

## Build

```bash
flet build apk -v          # Android
flet build macos -v        # macOS
flet build ipa -v          # iOS
flet build linux -v        # Linux
flet build windows -v      # Windows
flet build web -v          # Web
```

## Notes

- Source layout: `src/arkadash/...` (NOT `src/main.py` standalone). Run
  from the project root with `src/` as the module path.
- `pyproject.toml` has `tool.flet.app.module_name = "arkadash.app"` so
  `flet run` finds the entry point.
- Health view polls every 3s. Pure-urllib HTTP (no aiohttp deps).
