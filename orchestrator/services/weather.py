"""
Open-Meteo hava durumu tool'u.

Ücretsiz, API key gerektirmez, rate limit makul (10k istek/gün). WMO weather
code standardını döner (0=clear, 1-3=cloudy, 45-48=fog, 51-67=rain,
71-77=snow, 80-82=rain showers, 95-99=thunderstorm).

Endpoint:
  GET https://api.open-meteo.com/v1/forecast
    ?latitude={lat}&longitude={lon}&current=temperature_2m,relative_humidity_2m,weather_code
"""

import httpx


async def get_current(latitude: float, longitude: float) -> dict:
    """Verilen koordinat için güncel hava durumu.

    Args:
        latitude:  -90 ile 90 arası
        longitude: -180 ile 180 arası

    Returns:
        dict: temperature, humidity, weather_code, weather_label_tr

    Raises:
        httpx.HTTPError: network/API hatası → LLM "hava durumunu alamadım" der
    """
    url = "https://api.open-meteo.com/v1/forecast"
    params = {
        "latitude":  latitude,
        "longitude": longitude,
        "current":   "temperature_2m,relative_humidity_2m,weather_code",
        "timezone":  "auto",
    }
    async with httpx.AsyncClient(timeout=10) as client:
        response = await client.get(url, params=params)
    response.raise_for_status()
    data = response.json()
    current = data.get("current", {})
    weather_code = current.get("weather_code", 0)
    return {
        "temperature":     current.get("temperature_2m"),
        "humidity":        current.get("relative_humidity_2m"),
        "weather_code":    weather_code,
        "weather_label":   _WEATHER_CODE_TO_LABEL.get(weather_code, "Bilinmiyor"),
        "weather_label_tr": _WEATHER_CODE_TO_TR.get(weather_code, "bilinmiyor"),
        "latitude":        latitude,
        "longitude":       longitude,
    }


# ─── WMO Weather Code Mapping ───────────────────────────────────────────────

_WEATHER_CODE_TO_LABEL = {
    0:  "Clear sky",
    1:  "Mainly clear",
    2:  "Partly cloudy",
    3:  "Overcast",
    45: "Fog",
    48: "Depositing rime fog",
    51: "Drizzle (light)",
    53: "Drizzle (moderate)",
    55: "Drizzle (dense)",
    61: "Rain (slight)",
    63: "Rain (moderate)",
    65: "Rain (heavy)",
    71: "Snow (slight)",
    73: "Snow (moderate)",
    75: "Snow (heavy)",
    80: "Rain showers (slight)",
    81: "Rain showers (moderate)",
    82: "Rain showers (violent)",
    95: "Thunderstorm",
    96: "Thunderstorm with hail",
    99: "Thunderstorm with heavy hail",
}

_WEATHER_CODE_TO_TR = {
    0:  "açık",
    1:  "çoğunlukla açık",
    2:  "parçalı bulutlu",
    3:  "kapalı",
    45: "sisli",
    48: "kırağılı sis",
    51: "çisenti (hafif)",
    53: "çisenti (orta)",
    55: "çisenti (yoğun)",
    61: "yağmur (hafif)",
    63: "yağmur (orta)",
    65: "yağmur (şiddetli)",
    71: "kar (hafif)",
    73: "kar (orta)",
    75: "kar (yoğun)",
    80: "sağanak (hafif)",
    81: "sağanak (orta)",
    82: "sağanak (şiddetli)",
    95: "gök gürültülü fırtına",
    96: "dolu ile fırtına",
    99: "ağır dolu ile fırtına",
}


# ─── Varsayılan Konum ──────────────────────────────────────────────────────
# Kullanıcı koordinat belirtmezse İstanbul'u (Kadıköy civarı) varsay.
# Fuzzy match'te LLM "İstanbul hava durumu" derse buradan default çekilir.
DEFAULT_LATITUDE  = 41.0082   # İstanbul
DEFAULT_LONGITUDE = 28.9784


# ─── Tool Schemas (OpenAI function_calling) ─────────────────────────────────

TOOL_SCHEMA_GET_CURRENT = {
    "type": "function",
    "function": {
        "name": "weather.get_current",
        "description": "Verilen koordinat (latitude, longitude) için güncel "
                       "hava durumu: sıcaklık (°C), nem (%), hava durumu kodu "
                       "(WMO standardı, Türkçe açıklaması döner).",
        "parameters": {
            "type": "object",
            "properties": {
                "latitude": {
                    "type": "number",
                    "description": "Enlem (-90 ile 90 arası, örn İstanbul 41.0)",
                },
                "longitude": {
                    "type": "number",
                    "description": "Boylam (-180 ile 180 arası, örn İstanbul 29.0)",
                },
            },
            "required": ["latitude", "longitude"],
        },
    },
}
