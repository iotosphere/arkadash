"""Smart Home view — placeholder.

Phase Y (2026-06-29): Health tab'a odaklandık. Smart Home kontrol'leri
(Living Room / Kitchen / Temperature) sonraki phase'de tekrar
genişleyecek — agent_server üzerinden LED/Matter endpoint'leri
eklendiğinde gerçek cihaz kontrolleri gelecek.
"""

from __future__ import annotations

import flet as ft

from ..services.api import ArkadashApi


def smart_home_view(page: ft.Page, api: ArkadashApi) -> ft.Control:
    return ft.Column(
        [
            ft.Text("Smart Home", size=24, weight=ft.FontWeight.BOLD),
            ft.Container(height=8),
            ft.Text(
                "Phase Y: device controls land in Phase 2.\n"
                "Once agent_server exposes /api/devices (LED toggle, "
                "brightness, color, Matter commands), this tab will "
                "wire up the Living Room / Kitchen / Temperature widgets.",
                size=14,
                color=ft.Colors.ON_SURFACE_VARIANT,
            ),
        ],
        expand=True,
    )
