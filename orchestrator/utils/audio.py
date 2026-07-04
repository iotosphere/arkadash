"""
Saf DSP yardımcıları — I/O yok, sadece numpy/scipy.

İki endişe:
  1. STT ön-işleme: stereo → mono, peak normalizasyonu, sessizlik gate,
     trailing padding (whisper'ın kısa sessizliği doğru yorumlaması için).
  2. TTS son-işleme: 24k→16k resample, soft limiter (DAC clipping önleme).

ffmpeg subprocess çağrıları saf I/O olduğu için burada değil; services/tts.py
üzerinden çağrılır.

Atomic tasarım: tüm fonksiyonlar tek bir concern — ham PCM sample'ları üzerinde
deterministik dönüşüm. Log yok, network yok, sınıf durumu yok.
"""

import numpy as np
from scipy.signal import resample_poly


# ─── STT Ön-İşleme ───────────────────────────────────────────────────────────

def stereo_to_mono(stereo_bytes: bytes) -> bytes:
    """16-bit little-endian stereo PCM'i mono PCM'e çevir (kanal ortalaması).

    P4 ESP32 stereo kayıt yapar (16-bit, 2 channel). Whisper mono ister;
    basit ortalama (L + R) / 2 hem gürültüyü düşürür hem boyutu yarıya indirir.
    """
    samples = np.frombuffer(stereo_bytes, dtype=np.int16)
    if len(samples) % 2 != 0:
        samples = samples[:-1]  # hizalama
    paired = samples.reshape(-1, 2)
    mono = paired.mean(axis=1, dtype=np.int16)
    return mono.tobytes()


def normalize_peak(pcm_bytes: bytes, target_peak: int = 16384) -> tuple[bytes, float]:
    """Sample'ların tepe değerini target_peak'e ölçekler (clipping öncesi güvenli).

    whisper çok yüksek genlikli PCM'i clipping olarak yorumluyor, çok düşük
    genlikli PCM'i ise sessizlik olarak. target_peak = 16384 (%50 full-scale)
    whisper için ideal aralıkta bırakır.

    Returns:
        (scaled_bytes, scale_factor) — scale_factor 0..1+, debug için loglanır.
    """
    samples = np.frombuffer(pcm_bytes, dtype=np.int16)
    if len(samples) == 0:
        return pcm_bytes, 1.0
    max_amp = int(np.abs(samples).max())
    if max_amp == 0:
        return pcm_bytes, 1.0
    scale = target_peak / max_amp
    scaled = (samples.astype(np.float32) * scale).clip(-32768, 32767).astype(np.int16)
    return scaled.tobytes(), scale


def has_speech(pcm_bytes: bytes, threshold: int = 800) -> bool:
    """Peak amplitude eşik üzerindeyse True (sessizlikte False).

    Whisper'ın uzun sessizlikleri transcript üretmesini engellemek için STT
    öncesi hızlı enerji kontrolü. threshold=800 empirik: 16-bit / 32768 ≈ 2.4%
    full-scale. Oda sessizliği genelde 100-300 civarı, normal konuşma 2000+.
    """
    if not pcm_bytes:
        return False
    samples = np.frombuffer(pcm_bytes, dtype=np.int16)
    return int(np.abs(samples).max()) >= threshold


def pad_silence(pcm_bytes: bytes, duration_s: float, sample_rate: int = 16000) -> bytes:
    """Belirtilen süre kadar sıfır (sessizlik) sample'ı sona ekler.

    Whisper'ın PTT_RELEASE anındaki son heceyi kaçırmaması için trailing silence
    eklenir. 600ms civarı empirik: daha az → son kelimeler kesilir, daha fazla →
    STT latency artar.
    """
    n_silence = int(duration_s * sample_rate)
    silence = np.zeros(n_silence, dtype=np.int16)
    body = np.frombuffer(pcm_bytes, dtype=np.int16)
    return np.concatenate([body, silence]).tobytes()


# ─── TTS Son-İşleme ──────────────────────────────────────────────────────────

def resample_24k_to_16k(samples_24k: np.ndarray) -> np.ndarray:
    """24 kHz mono int16 → 16 kHz mono int16.

    P4 firmware I2S EXAMPLE_SAMPLE_RATE=16000. TTS MiniMax 24kHz verir (veya
    ffmpeg decode sonrası 24kHz). 24k→16k downsample için resample_poly(2, 3):
    24000 × 2/3 = 16000.
    """
    return resample_poly(samples_24k, up=2, down=3).astype(np.int16)


def soft_limit(samples: np.ndarray, drive: float = 1.3, gain: float = 0.65) -> np.ndarray:
    """ES8311 DAC clipping önlemek için yumuşak doyum (tanh soft saturation).

    Hard clip yerine matematiksel soft saturation: peak'ler yumuşak şekilde
    hedef seviyeye sıkıştırılır, distortion minimal. Parametreler:
      - drive: 1.0 = unity, >1 = peak'leri daha agresif doyuma uğratır
      - gain:  çıktı çarpanı (final peak seviyesi)

    Max sample ~target_peak × gain (örn. 16384 × 0.65 = 10650, headroom %67)
    """
    if len(samples) == 0:
        return samples
    # float32 normalizasyonu [-1, 1] bandına, tanh uygula, geri çevir
    norm = samples.astype(np.float32) / 32768.0
    saturated = np.tanh(norm * drive) * gain
    return (saturated * 32768).astype(np.int16)
