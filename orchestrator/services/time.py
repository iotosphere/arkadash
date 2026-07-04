"""
Sistem saati ve tarih tool'u.

LLM'in kendisi genellikle training data'dan güncel tarihi bilmez (özellikle
yeni model'lerde). "Bugün ayın kaçı?" veya "şu an saat kaç?" soruları için
explicit tool. P4 firmware NTP sync sonrası kendi RTC'sini kullanır; bu
modül server-side time.time() kullanır (NTP sync'li Mac'te).

Tool schema (OpenAI function_calling format):
  TOOL_SCHEMA_NOW → server time, ISO 8601 + Unix timestamp
  TOOL_SCHEMA_TODAY → date (YYYY-MM-DD), weekday name
"""

import time
from datetime import datetime, timezone


async def now() -> dict:
    """Server time, ISO 8601 + Unix timestamp.

    LLM bunu kullanarak "şu an saat kaç" veya "bugünün tarihi ne" sorularını
    doğru cevaplayabilir. NTP-sync'li host'larda zaman doğrudur.
    """
    ts = time.time()
    dt = datetime.fromtimestamp(ts, tz=timezone.utc).astimezone()
    return {
        "iso":        dt.isoformat(),
        "unix":       ts,
        "date":       dt.strftime("%Y-%m-%d"),
        "time":       dt.strftime("%H:%M:%S"),
        "timezone":   str(dt.tzinfo),
        "weekday":    dt.strftime("%A"),
        "weekday_tr": _WEEKDAY_TR[dt.strftime("%A")],
    }


async def today() -> dict:
    """Bugünün tarihi (YYYY-MM-DD) ve gün adı."""
    dt = datetime.now().astimezone()
    return {
        "date":       dt.strftime("%Y-%m-%d"),
        "weekday_tr": _WEEKDAY_TR[dt.strftime("%A")],
    }


_WEEKDAY_TR = {
    "Monday":    "Pazartesi",
    "Tuesday":   "Salı",
    "Wednesday": "Çarşamba",
    "Thursday":  "Perşembe",
    "Friday":    "Cuma",
    "Saturday":  "Cumartesi",
    "Sunday":    "Pazar",
}


# ─── Tool Schemas (OpenAI function_calling) ─────────────────────────────────

TOOL_SCHEMA_NOW = {
    "type": "function",
    "function": {
        "name": "time.now",
        "description": "Şu anki server saatini ISO 8601 ve Unix timestamp olarak "
                       "döner. Saat/tarih soruları için kullan.",
        "parameters": {
            "type": "object",
            "properties": {},
            "required": [],
        },
    },
}

TOOL_SCHEMA_TODAY = {
    "type": "function",
    "function": {
        "name": "time.today",
        "description": "Bugünün tarihini YYYY-MM-DD formatında ve Türkçe gün "
                       "adıyla (Pazartesi, Salı, ...) döner.",
        "parameters": {
            "type": "object",
            "properties": {},
            "required": [],
        },
    },
}
