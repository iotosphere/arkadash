"""
Whisper.cpp HTTP tabanlı STT servisi.

Akış:
  1. P4 stereo PCM gelir → utils.audio.stereo_to_mono
  2. utils.audio.normalize_peak ile %50 full-scale'e çek (whisper clipping algılar)
  3. utils.audio.pad_silence ile 600ms trailing sessizlik ekle
  4. utils.audio.has_speech ile enerji kontrolü (sessizlikte Whisper'a hiç gitme)
  5. WAV header yaz, multipart POST ile whisper.cpp /inference endpoint'ine
  6. Vocabulary bias prompt ile Türkçe halüsinasyon azalt

vocabulary bias prompt Türkçe komut setini içerir — Whisper'in dil modeli bu
kelimeleri yüksek olasılıkla tahmin eder, halüsinasyon yerine doğru Türkçe
kelimeler çıkar.
"""

import os
import tempfile
import wave

import httpx
import numpy as np

from orchestrator.config import WHISPER_SERVER
from orchestrator.utils.audio import (
    has_speech,
    normalize_peak,
    pad_silence,
    stereo_to_mono,
)


# Türkçe komut vocabulary bias — Whisper'in halüsinasyonunu büyük oranda azaltır.
# Sık kullanılan komutları buraya ekleyebilirsin; sıralama önemli değil, tekrar
# artırdıkça bias güçlenir.
_INITIAL_PROMPT = (
    "Işıkları aç, Işıkları kapat, Işıkları. "
    "Merhaba Arkadash. Selamlar, merhaba, günaydın, iyi geceler, "
    "teşekkürler, hoşça kal, nasılsın. "
    "Işıkları aç, kapat, rengini değiştir, sıcaklık kaç, nem kaç, "
    "salon, yatak odası, mutfak, banyo, çocuk odası. "
    "Müzik aç, durdur, sonraki şarkı, önceki şarkı, ses aç, ses kapat. "
    "Perdeleri aç, perdeleri kapat. "
    "Kısa Türkçe sesli komutlar."
)

# Parametre sözlüğü — Whisper greedy + Türkçe + vocabulary bias
_INFERENCE_PARAMS = {
    "response_format":            "json",
    "language":                   "tr",
    "prompt":                     _INITIAL_PROMPT,
    "temperature":                "0.0",   # greedy — halüsinasyon azaltır
    "temperature_inc":            "0.0",   # fallback yapmasın
    "beam_size":                  "1",     # beam 5'ten 3-4 kat hızlı
    "best_of":                    "1",
    "no_speech_threshold":        "0.7",   # kısa/belirsiz sesi agresif reddet
    "compression_ratio_threshold": "2.4",  # tekrarlayan metin filtresi
    "logprob_threshold":          "-1.0",  # düşük güvenli tahmin reddi
}

# Whisper'ın kabul ettiği minimum örnek sayısı — daha kısa kayıtlar geçersiz
_MIN_SAMPLES = 4800  # 0.3 saniye @ 16 kHz


async def transcribe_audio(pcm_bytes: bytes) -> str:
    """P4 stereo PCM → mono → normalize → padding → whisper.cpp.

    Returns:
        Türkçe transkripsiyon metni. Hata veya sessizlik durumunda boş string.

    Hata durumları (boş string döner, exception raise etmez):
        - PCM çok kısa (< 0.3s)
        - Sessiz kayıt (max amplitude < eşik)
        - whisper.cpp HTTP hatası
        - WAV dosya I/O hatası
    """
    try:
        # Stereo → mono
        mono_bytes = stereo_to_mono(pcm_bytes)
        samples = np.frombuffer(mono_bytes, dtype=np.int16)

        if len(samples) > 0:
            max_amp = int(np.abs(samples).max())
            duration = len(samples) / 16000.0
            print(f"[STT] Audio: {len(samples)} samples "
                  f"({duration:.2f}s), max={max_amp}")

        # Çok kısa kayıt → Whisper boş döner, boşuna HTTP çağrısı yapma
        if len(samples) < _MIN_SAMPLES:
            print(f"[STT] Audio too short: {len(samples)/16000:.2f}s")
            return ""

        # Sessizlik kontrolü (enerji gate) — klima/uğultu gibi noise'ları filtrele
        if not has_speech(mono_bytes, threshold=800):
            print(f"[STT] Silent audio, skipping")
            return ""

        # Peak normalize (firmware gain fazlaysa clipping'i Whisper'a göndermeden önle)
        mono_bytes, scale = normalize_peak(mono_bytes, target_peak=16384)
        if scale < 1.0:
            print(f"[STT] Normalized peak: scale={scale:.3f} "
                  f"(firmware gain fix needed)")

        # Trailing silence — PTT_STOP anında son hece yarıda kesilmesin
        mono_bytes = pad_silence(mono_bytes, duration_s=0.6, sample_rate=16000)
        print("[STT] Padded +600ms trailing silence")

        # WAV dosyası oluştur (geçici)
        with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
            temp_path = f.name
        try:
            with wave.open(temp_path, "wb") as wf:
                wf.setnchannels(1)
                wf.setsampwidth(2)
                wf.setframerate(16000)
                wf.writeframes(mono_bytes)

            print("[STT] Transcribing with whisper.cpp server...")
            with open(temp_path, "rb") as f:
                files = {"file": ("audio.wav", f, "audio/wav")}
                async with httpx.AsyncClient(timeout=30) as client:
                    response = await client.post(
                        f"{WHISPER_SERVER}/inference",
                        files=files,
                        params=_INFERENCE_PARAMS,
                    )

            if response.status_code == 200:
                result = response.json()
                text = (result.get("text", "") or result.get("output", "")).strip()
                print(f"[STT] Whisper: '{text}'")
                return text
            else:
                print(f"[STT] Error {response.status_code}: {response.text[:300]}")
                return ""
        finally:
            os.unlink(temp_path)

    except Exception as e:
        print(f"[STT] Error: {e}")
        import traceback
        traceback.print_exc()
        return ""
