"""
Metin normalizasyonu yardımcıları.

İki endişe:
  1. STT halüsinasyonlarını kanonik komutlara eşle (fuzzy matching).
  2. TTS'e gönderilecek metinden emoji ve gereksiz karakterleri temizle.

Bu modül yan etki içermez (loglama dışında), saf fonksiyonlar.
"""

import re
from thefuzz import fuzz, process


# ─── Fuzzy Command Matching ──────────────────────────────────────────────────

# Türkçe STT'nin en sık karıştırdığı sesli harfleri önceden dönüştür.
# thefuzz karakter edit distance kullanır; "u" ile "ı" farkı mesafeyi büyütür.
# Önce bu dönüşüm, sonra fuzzy — "uşukları" -> "ışıklıri" -> "ışıkları".
TURKISH_PHONETIC_MAP = str.maketrans({
    "u": "ı",  # en yaygın halüsinasyon: "uşıkları" -> "ışıkları"
    "ü": "i",
    "ö": "o",
})


def _phonetic_normalize(text: str) -> str:
    return text.translate(TURKISH_PHONETIC_MAP)


CANONICAL_COMMANDS: list[str] = [
    "ışıkları aç",
    "ışıkları kapat",
    "salon ışıkları aç",
    "salon ışıkları kapat",
    "salon ışıklarını aç",
    "salon ışıklarını kapat",
    "yatak odası ışıkları aç",
    "yatak odası ışıkları kapat",
    "mutfak ışıkları aç",
    "mutfak ışıkları kapat",
    "banyo ışıkları aç",
    "çocuk odası ışıkları aç",
    "sıcaklık kaç",
    "sıcaklık nedir",
    "saat kaç",
    "saat kaçtı",
    "nem kaç",
    "müzik aç",
    "müzik durdur",
    "müzik kapat",
    "sonraki şarkı",
    "önceki şarkı",
    "ses aç",
    "ses kapat",
    "perdeleri aç",
    "perdeleri kapat",
]

# Yüzde eşleşme — yüksek tut, sohbet/selam yanlış eşleşmesin
FUZZY_THRESHOLD: int = 90  # 85 → 90: yanlış eşleşmeleri azalt ("saat kaç" ↔ "sıcaklık kaç" gibi)


def normalize_command(text: str) -> str:
    """STT çıktısını kanonik komuta fuzzy-match ile eşleştir.

    Yüksek eşleşme varsa kanonik komutu, yoksa olduğu gibi döner.
    Sohbet/selam ("nasılsın", "teşekkürler") eşleşmemeli — bunlar LLM'e gider.
    Threshold 85 = ortalama %85 token benzerliği gerekli.

    Akış:
      1. Fonetik ön-normalleştirme (u→ı, ü→i, ö→o) — STT halüsinasyonları için
      2. Thefuzz WRatio + partial_ratio — sıra/uzunluk esnek
      3. Kelime sayısı kontrolü (<1 veya >6 kelime = sohbet, fuzzy atlanır)
    """
    if not text or not text.strip():
        return text

    candidate = text.strip().lower()
    word_count = len(candidate.split())
    if word_count < 1 or word_count > 6:
        return text

    phon = _phonetic_normalize(candidate)
    try:
        # WRatio + partial_ratio: birden fazla scorer'ın en iyisini al.
        match, score = process.extractOne(phon, CANONICAL_COMMANDS, scorer=fuzz.WRatio)
        match2, score2 = process.extractOne(
            phon, CANONICAL_COMMANDS, scorer=fuzz.partial_ratio
        )
        if score2 > score:
            match, score = match2, score2
        if score >= FUZZY_THRESHOLD:
            print(f"[FUZZ] '{text}' -> '{match}' (score={score})")
            return match
    except Exception as e:
        print(f"[FUZZ] Error: {e}")
    return text


# ─── TTS Metin Temizleme ─────────────────────────────────────────────────────

_EMOJI_PATTERN = re.compile(
    "["
    "\U0001F600-\U0001F64F"  # emoticons
    "\U0001F300-\U0001F5FF"  # symbols & pictographs
    "\U0001F680-\U0001F6FF"  # transport & map symbols
    "\U0001F1E0-\U0001F1FF"  # flags
    "\U00002702-\U000027B0"
    "\U000024C2-\U0001F251"
    "\U0001F900-\U0001F9FF"  # supplemental symbols
    "\U0001FA00-\U0001FA6F"  # chess symbols
    "\U0001FA70-\U0001FAFF"  # symbols extended
    "\U00002600-\U000026FF"  # misc symbols
    "\U00002700-\U000027BF"  # dingbats
    "]+",
    flags=re.UNICODE,
)
_TEXT_EMOJI_PATTERN = re.compile(r":[a-zA-Z_]+:")


def clean_text_for_tts(text: str) -> str:
    """TTS'e gönderilecek metinden emoji ve gereksiz karakterleri temizle.

    Çıktıdaki emoji'ler MiniMax / Edge TTS'in ses patlamasına yol açıyor;
    metin-tabanlı :emoji: gösterimlerini de siler. Birim sembolleri
    (°C, °F) doğal dil karşılığına çevrilir (MiniMax "25°C" yerine
    "yirmi beş C derece" gibi okuyor). Çoklu boşlukları tek boşluğa
    indirir ve trim uygular.
    """
    text = _EMOJI_PATTERN.sub("", text)
    text = _TEXT_EMOJI_PATTERN.sub("", text)
    # Birim sembolleri → doğal dil
    text = text.replace("°C", " derece").replace("°F", " fahrenheit")
    # Markdown kalın/italic işaretleri → düz metin
    text = re.sub(r"\*+", "", text)
    text = re.sub(r"\s+", " ", text)
    return text.strip()
