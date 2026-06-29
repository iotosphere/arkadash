"""Music view — placeholder for Spotify controls (planned step d)."""

from __future__ import annotations

import flet as ft

from ..services.api import ArkadashApi


def music_view(page: ft.Page, api: ArkadashApi) -> ft.Control:
    return ft.Column(
        [
            ft.Text("Music", size=24, weight=ft.FontWeight.BOLD),
            ft.Container(height=8),
            ft.Text(
                "Spotify controls will land in step (d).\n"
                "Planned: playlist list, play/pause, next/prev.",
                size=14,
                color=ft.Colors.ON_SURFACE_VARIANT,
            ),
        ],
        expand=True,
    )
