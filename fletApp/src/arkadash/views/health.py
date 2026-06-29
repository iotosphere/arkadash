"""Health view — Arkadash sistem sağlık monitoring.

agent_server.py'nin `/api/health` endpoint'ini her 3 saniyede poll eder.
Bağlı P4 client'larını, agent_server uptime'ını, Spotify auth, voice pipeline
ve P4 telemetry (WiFi, Memory, Matter) istatistiklerini gösterir.

Tasarım: dark theme, status ✓/✗ indicator, responsive kartlar.
Background polling: daemon thread + page.update.
"""

from __future__ import annotations

import flet as ft
import threading
import time

from ..services.api import ArkadashApi, HealthInfo, TelemetryInfo


# ── Renk sabitleri ───────────────────────────────────────────────────
_COLOR_OK    = ft.Colors.GREEN_400
_COLOR_FAIL  = ft.Colors.RED_400
_COLOR_DIM   = ft.Colors.ON_SURFACE_VARIANT
_COLOR_AMBER = ft.Colors.AMBER_400
_BORDER      = ft.Colors.with_opacity(0.15, ft.Colors.WHITE)


def _kv_card(title_value: str, ok: bool = False):
    """Status card: dot+label üst satır, value alt satırda full width.

    Returns (container, set_value_fn). Card'ın value'su uzun string olabilir
    (WiFi SSID gibi) — Column layout ile value full container genişliğinde
    wrap eder.
    """
    label_ref  = ft.Text(title_value, size=10, color=_COLOR_DIM)
    value_ref  = ft.Text(
        "—", size=12, weight=ft.FontWeight.BOLD,
        no_wrap=False, max_lines=2,
        overflow=ft.TextOverflow.VISIBLE,
    )
    dot_ref    = ft.Container(
        width=8, height=8, border_radius=4,
        bgcolor=_COLOR_OK if ok else _COLOR_AMBER,
    )

    def set_value(label: str, value: str, ok: bool) -> None:
        label_ref.value = label
        value_ref.value = value
        dot_ref.color  = _COLOR_OK if ok else _COLOR_FAIL

    container = ft.Container(
        padding=ft.Padding(left=10, top=8, right=10, bottom=8),
        border_radius=6,
        bgcolor=ft.Colors.with_opacity(0.06, ft.Colors.WHITE),
        expand=True,
        content=ft.Column(
            [
                ft.Row(
                    [dot_ref, label_ref],
                    spacing=4,
                    alignment=ft.MainAxisAlignment.START,
                ),
                value_ref,
            ],
            spacing=2, tight=True,
        ),
    )
    return container, set_value


def _fmt_duration(s: int) -> str:
    if s < 60:    return f"{s}s"
    if s < 3600:  return f"{s // 60}m {s % 60}s"
    if s < 86400: return f"{s // 3600}h {(s % 3600) // 60}m"
    return f"{s // 86400}d {(s % 86400) // 3600}h"


def health_view(page: ft.Page, api: ArkadashApi) -> ft.Control:
    # ── UI eleman referansları (polling thread günceller) ───
    status_text      = ft.Text("Connecting...", size=14)
    last_poll_text   = ft.Text("—", size=11, color=_COLOR_DIM)
    uptime_text      = ft.Text("0s", size=22, weight=ft.FontWeight.BOLD)
    history_text     = ft.Text("0", size=20, weight=ft.FontWeight.BOLD)
    client_count_txt = ft.Text("0", size=11, color=_COLOR_DIM)
    clients_col      = ft.Column(spacing=4)
    spotify_card,    set_spotify    = _kv_card("Spotify")
    voice_card,      set_voice      = _kv_card("Voice")
    wifi_card,       set_wifi       = _kv_card("WiFi")
    memory_card,     set_memory     = _kv_card("Memory")
    matter_card,     set_matter     = _kv_card("Matter")
    p4_tel_col       = ft.Column(spacing=4)

    def render_clients(clients):
        clients_col.controls.clear()
        if not clients:
            clients_col.controls.append(
                ft.Container(
                    content=ft.Text(
                        "(no P4 client connected)",
                        size=12, color=_COLOR_DIM,
                    ),
                    padding=ft.Padding(left=8, top=4, right=8, bottom=4),
                )
            )
            return
        for c in clients:
            clients_col.controls.append(
                ft.Container(
                    content=ft.Row(
                        [
                            ft.Container(
                                width=8, height=8, border_radius=4,
                                bgcolor=_COLOR_OK,
                            ),
                            ft.Column([
                                ft.Text(c.ip, size=14, weight=ft.FontWeight.BOLD),
                                ft.Text(
                                    f"up {_fmt_duration(c.uptime_s)} · "
                                    f"last msg {c.last_msg_age_s}s ago · "
                                    f"\"{c.last_msg}\"",
                                    size=10, color=_COLOR_DIM,
                                ),
                            ], spacing=2, expand=True),
                        ], spacing=10,
                    ),
                    padding=ft.Padding(left=8, top=6, right=8, bottom=6),
                    border=ft.Border(
                        ft.BorderSide(1, _BORDER),
                        ft.BorderSide(1, _BORDER),
                        ft.BorderSide(1, _BORDER),
                        ft.BorderSide(1, _BORDER),
                    ),
                    border_radius=6,
                )
            )

    def render_p4_telemetry(tel_list):
        p4_tel_col.controls.clear()
        if not tel_list:
            p4_tel_col.controls.append(
                ft.Text(
                    "(no P4 telemetry yet — P4 firmware POST atmamış)",
                    size=11, color=_COLOR_DIM,
                )
            )
            return
        for t in tel_list:
            local = t.local_ip or t.sender_ip
            stale_color = _COLOR_FAIL if t.stale else _COLOR_OK
            stale_text  = "✗ stale" if t.stale else "✓ live"
            p4_tel_col.controls.append(
                ft.Container(
                    content=ft.Row([
                        ft.Container(
                            width=8, height=8, border_radius=4,
                            bgcolor=stale_color,
                        ),
                        ft.Column([
                            ft.Text(
                                f"{local} ({t.sender_ip})",
                                size=12,
                                weight=ft.FontWeight.BOLD,
                            ),
                            ft.Text(
                                f"{t.ssid or '—'} · RSSI {t.rssi} dBm · "
                                f"heap {t.heap_free//1024} KB · "
                                f"uptime {_fmt_duration(t.uptime_s)} · "
                                f"{stale_text}",
                                size=10, color=_COLOR_DIM,
                            ),
                        ], spacing=2, expand=True),
                    ], spacing=10),
                    padding=ft.Padding(left=8, top=6, right=8, bottom=6),
                    border=ft.Border(
                        ft.BorderSide(1, _BORDER),
                        ft.BorderSide(1, _BORDER),
                        ft.BorderSide(1, _BORDER),
                        ft.BorderSide(1, _BORDER),
                    ),
                    border_radius=6,
                )
            )

    def update_from_health(h):
        """Tek bir iterasyondaki tüm UI update'leri merkezi yerden yap.
        Exception'lar burada yakalanır, status_text'e yazılır — UI asla yarı
        dolu kalmaz, ya tamam ya baştan temizlenir."""
        if h is None:
            status_text.value      = "✗ agent_server unreachable"
            status_text.color      = _COLOR_FAIL
            uptime_text.value      = "—"
            history_text.value     = "0"
            client_count_txt.value = "0"
            render_clients([])
            render_p4_telemetry([])
            set_spotify("Spotify", "—", False)
            set_voice("Voice", "—", False)
            set_wifi("WiFi", "—", False)
            set_memory("Memory", "—", False)
            set_matter("Matter", "—", False)
            last_poll_text.value = f"poll failed · {time.strftime('%H:%M:%S')}"
            return

        status_text.value = "✓ agent_server reachable"
        status_text.color = _COLOR_OK
        uptime_text.value = _fmt_duration(h.uptime_s)
        history_text.value = str(h.history_msgs)
        n_clients = len(h.active_clients)
        client_count_txt.value = str(n_clients)
        render_clients(h.active_clients)
        render_p4_telemetry(h.p4_telemetry)

        sp_ok   = h.spotify_authed
        sp_text = ("✓ authed" if sp_ok
                   else "✗ not authed" if h.spotify_enabled
                   else "— disabled")
        set_spotify("Spotify", sp_text, sp_ok)

        set_voice("Voice", f"{n_clients} client(s)", n_clients > 0)

        # P4 telemetry → aggregate first P4 into the 3 KV cards
        tel_list = h.p4_telemetry
        if not tel_list:
            set_wifi("WiFi",    "— (no telemetry)", False)
            set_memory("Memory", "—", False)
            set_matter("Matter", "—", False)
        else:
            t = tel_list[0]
            ssid_ok = bool(t.ssid) and not t.stale
            set_wifi(
                "WiFi",
                f"{t.ssid}\n{t.rssi} dBm",
                ssid_ok,
            )
            heap_kb = t.heap_free // 1024 if t.heap_free else 0
            mem_ok  = heap_kb > 50 and not t.stale
            set_memory(
                "Memory",
                f"{heap_kb} KB free",
                mem_ok,
            )
            mat_ok = t.matter_count > 0 and not t.stale
            set_matter(
                "Matter",
                f"{t.matter_count} commissioned",
                mat_ok,
            )

        last_poll_text.value = f"last poll OK · {time.strftime('%H:%M:%S')}"

    def poll_loop():
        """Background daemon thread."""
        while True:
            try:
                h = api.get_health()
                update_from_health(h)
            except Exception as e:
                status_text.value = f"✗ poll exception: {e}"
                status_text.color = _COLOR_FAIL
            try:
                page.update()
            except Exception:
                return
            time.sleep(3)

    threading.Thread(
        target=poll_loop, daemon=True, name="health-poll"
    ).start()

    return ft.Container(
        padding=16,
        content=ft.Column(
            [
                ft.Row(
                    [
                        ft.Text("Arkadash Health", size=22,
                                weight=ft.FontWeight.BOLD, expand=True),
                        ft.Container(
                            width=10, height=10, border_radius=5,
                            bgcolor=_COLOR_AMBER,
                        ),
                        ft.Text("live polling", size=11, color=_COLOR_DIM),
                    ], spacing=8,
                ),
                ft.Container(height=8),

                ft.Container(
                    content=status_text,
                    padding=10,
                    expand=True,
                    border_radius=6,
                    bgcolor=ft.Colors.with_opacity(0.05, ft.Colors.WHITE),
                ),
                ft.Container(height=12),

                ft.Container(
                    padding=ft.Padding(left=14, top=10, right=14, bottom=10),
                    border_radius=8,
                    bgcolor=ft.Colors.with_opacity(0.08, ft.Colors.WHITE),
                    content=ft.Column([
                        ft.Text("Agent Uptime", size=11, color=_COLOR_DIM),
                        uptime_text,
                    ], spacing=2),
                ),
                ft.Container(height=8),

                # WiFi + Memory (yan yana) — Phase 2A
                ft.Row([wifi_card, memory_card], spacing=8),
                ft.Container(height=8),

                # Spotify + Voice (yan yana)
                ft.Row([spotify_card, voice_card], spacing=8),
                ft.Container(height=8),

                # Matter (tek başına)
                matter_card,
                ft.Container(height=14),

                # history msgs counter
                ft.Row([
                    ft.Column([
                        history_text,
                        ft.Text("history msgs", size=10, color=_COLOR_DIM),
                    ], spacing=0),
                    ft.Container(expand=True),
                ]),
                ft.Container(height=14),

                # P4 Telemetry listesi
                ft.Container(
                    padding=ft.Padding(left=8, top=8, right=8, bottom=8),
                    border_radius=8,
                    bgcolor=ft.Colors.with_opacity(0.06, ft.Colors.WHITE),
                    content=ft.Column([
                        ft.Row([
                            ft.Text("P4 Telemetry (last POST)",
                                    size=12, weight=ft.FontWeight.BOLD,
                                    expand=True),
                            ft.Text(f"{0}", size=11, color=_COLOR_DIM,
                                    key="tel_count"),
                        ], spacing=8),
                        p4_tel_col,
                    ], spacing=4),
                ),
                ft.Container(height=8),

                # Connected WS Clients
                ft.Container(
                    padding=ft.Padding(left=8, top=8, right=8, bottom=8),
                    border_radius=8,
                    bgcolor=ft.Colors.with_opacity(0.06, ft.Colors.WHITE),
                    content=ft.Column([
                        ft.Row([
                            ft.Text("Connected WS Clients",
                                    size=12, weight=ft.FontWeight.BOLD,
                                    expand=True),
                            client_count_txt,
                        ], spacing=8),
                        clients_col,
                    ], spacing=4),
                ),
                ft.Container(height=8),

                ft.Container(
                    content=last_poll_text,
                    alignment=ft.Alignment.CENTER,
                ),
            ], spacing=4, expand=True,
        ),
        expand=True,
    )
