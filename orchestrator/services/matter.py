"""
ESP32-S3 (TBR = Thread Border Router + Matter controller) LED kontrolü.

Akış:
  P4 panel → WebSocket → agent_server → TBR HTTP /led/* → Matter → ESP32-H2 light

TBR local network'te HTTP server çalıştırır (port 8766). Matter komisyonlu
ESP32-H2 light cihazını kontrol eder. agent_server doğrudan Matter SDK
çağırmaz, sadece TBR'ın HTTP endpoint'lerine GET atar.

Endpoint'ler (TBR HTTP):
  GET /led/toggle                  → toggle LED
  GET /led/color?x=<0-32768>&y=...  → CIE xyY color set
  GET /led/brightness?level=<0-254> → brightness set
  GET /led/status                  → JSON {state, color, brightness}

TBR IP config.ESP32_S3_HTTP_BASE'de — router DHCP ile atar, reboot sonrası
değişebilir.
"""

import httpx

from orchestrator.config import ESP32_S3_HTTP_BASE


_TIMEOUT_S = 5.0  # HTTP timeout — Matter yavaş olabilir ama 5s yeterli


async def _tbr_get(path: str, params: dict | None = None) -> tuple[int, str | dict | None]:
    """TBR HTTP GET helper. (status_code, body) döner."""
    try:
        async with httpx.AsyncClient(timeout=_TIMEOUT_S) as client:
            response = await client.get(f"{ESP32_S3_HTTP_BASE}{path}", params=params)
        if response.headers.get("content-type", "").startswith("application/json"):
            return response.status_code, response.json()
        return response.status_code, response.text
    except Exception as e:
        print(f"[Matter] {path} error: {e}")
        return 0, None


# ─── LED Kontrol Komutları ──────────────────────────────────────────────────

async def toggle() -> bool:
    """Living room LED'i aç↔kapa toggle et. Başarı → True."""
    status, _ = await _tbr_get("/led/toggle")
    return status == 200


async def set_color(color_x: int, color_y: int) -> bool:
    """CIE xyY color coordinates ile LED rengini ayarla (Matter standard).

    color_x, color_y: 0-32768 arası int (CIE 1931 xy chromaticity).
    Başarı → True.
    """
    status, _ = await _tbr_get("/led/color", params={"x": color_x, "y": color_y})
    return status == 200


async def set_brightness(level: int) -> bool:
    """LED brightness seviyesini ayarla. level: 0 (off) - 254 (max).

    Matter LevelControl cluster standardı.
    """
    status, _ = await _tbr_get("/led/brightness", params={"level": level})
    return status == 200


async def get_status() -> dict | None:
    """LED durumunu sorgula. None = hata veya TBR kapalı."""
    status, body = await _tbr_get("/led/status")
    if status == 200 and isinstance(body, dict):
        return body
    return None


# ─── Tool Schemas (OpenAI function_calling) ─────────────────────────────────
# LLM agentic runtime için her tool'un TOOL_SCHEMA_* dict'i dışa aktarılır.
# services/agent.py bu schema'ları toplar ve LLM'e gönderir.

TOOL_SCHEMA_TOGGLE = {
    "type": "function",
    "function": {
        "name": "led.toggle",
        "description": "Living room LED'i aç↔kapa toggle eder. Mevcut durumu "
                       "açıksa kapatır, kapalıysa açar.",
        "parameters": {"type": "object", "properties": {}, "required": []},
    },
}

TOOL_SCHEMA_SET_COLOR = {
    "type": "function",
    "function": {
        "name": "led.set_color",
        "description": "Living room LED rengini CIE xyY color coordinates "
                       "ile ayarlar (Matter standardı). x ve y 0-32768 arası.",
        "parameters": {
            "type": "object",
            "properties": {
                "x": {"type": "integer", "description": "CIE x coordinate (0-32768)"},
                "y": {"type": "integer", "description": "CIE y coordinate (0-32768)"},
            },
            "required": ["x", "y"],
        },
    },
}

TOOL_SCHEMA_SET_BRIGHTNESS = {
    "type": "function",
    "function": {
        "name": "led.set_brightness",
        "description": "Living room LED parlaklığını ayarlar. 0=kapalı, 254=max.",
        "parameters": {
            "type": "object",
            "properties": {
                "level": {"type": "integer", "minimum": 0, "maximum": 254},
            },
            "required": ["level"],
        },
    },
}

TOOL_SCHEMA_GET_STATUS = {
    "type": "function",
    "function": {
        "name": "led.get_status",
        "description": "Living room LED'in mevcut durumunu sorgular: "
                       "açık/kapalı, parlaklık, renk.",
        "parameters": {"type": "object", "properties": {}, "required": []},
    },
}
