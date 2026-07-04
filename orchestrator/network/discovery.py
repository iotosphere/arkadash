"""
UDP broadcast tabanlı otomatik keşif.

P4 firmware `components/discovery/` UDP 53000'de dinler; bu modül her 5
saniyede "ARKADASH:<ip>:8765" mesajını 255.255.255.255:53000'a broadcast eder.
P4 ilk geçerli broadcast'te global IP'yi alır, WebSocket bağlantısını
oraya yönlendirir.

mDNS/Bonjour alternatifi: hiçbir sistem servisine bağımlı değil, broadcast
her ağa ulaşır, reboot sonrası yeni IP otomatik öğrenilir.

Threading:
  - get_local_ip(): senkron (routing interface sorgu)
  - run_broadcaster(): daemon thread, stop_event ile kapatılabilir
"""

import socket
import threading


_BROADCAST_PORT = 53000
_BROADCAST_INTERVAL_S = 5.0
_MESSAGE_PREFIX = "ARKADASH"
_WS_PORT = 8765


def get_local_ip() -> str:
    """Routing interface üzerinden kendi IP'mizi bul (192.168.1.X gibi).

    Broadcast için doğru IP'yi bulmak kritik — socket.gethostbyname()
    bazen 127.0.0.1 döner. UDP connect-non-blocking trick: routing tablosuna
    bakmak için 8.8.8.8:80'e connect et (bağlantı kurulmaz), getsockname()
    ile local interface IP'sini al.
    """
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.connect(("8.8.8.8", 80))  # routing olmayan hedef, bağlantı kurmaz
        return sock.getsockname()[0]
    finally:
        sock.close()


def run_broadcaster(stop_event: threading.Event) -> None:
    """Sonsuz broadcast loop'u (5s interval). Daemon thread olarak çalıştır.

    stop_event set() edilince döngüden çıkar ve socket kapatılır.
    Hata durumunda log yazıp devam eder (single broadcast failure kabul edilir).
    """
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.settimeout(1.0)

    while not stop_event.is_set():
        try:
            ip = get_local_ip()
            msg = f"{_MESSAGE_PREFIX}:{ip}:{_WS_PORT}".encode("utf-8")
            sock.sendto(msg, ("255.255.255.255", _BROADCAST_PORT))
            print(f"[DISC] broadcast gönderildi: {msg.decode()} -> "
                  f"255.255.255.255:{_BROADCAST_PORT}")
        except Exception as e:
            print(f"[DISC] broadcast hatası: {e}")
        # stop_event check ile bekle — interrupt-friendly
        stop_event.wait(_BROADCAST_INTERVAL_S)

    sock.close()
    print("[DISC] broadcaster durdu")
