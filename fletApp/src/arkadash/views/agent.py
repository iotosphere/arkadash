"""Agent view — placeholder for text input + STT/TTS (planned step d)."""

from __future__ import annotations

import flet as ft

from ..services.api import ArkadashApi


def agent_view(page: ft.Page, api: ArkadashApi) -> ft.Control:
    return ft.Column(
        [
            ft.Text("Agent", size=24, weight=ft.FontWeight.BOLD),
            ft.Container(height=8),
            ft.Text(
                "Voice / text agent will land in step (d).\n"
                "Planned: text input, mic button (STT via agent_server).",
                size=14,
                color=ft.Colors.ON_SURFACE_VARIANT,
            ),
        ],
        expand=True,
    )
