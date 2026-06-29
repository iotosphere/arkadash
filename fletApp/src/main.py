import flet as ft
import sys
import os

# src klasörünü modül yoluna ekle (Import hatalarını kesin çözer)
sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))

# Modülleri mutlak (absolute) yolla import edin
from arkadash.services.api import ArkadashApi
from arkadash.views.smart_home import smart_home_view
from arkadash.views.music import music_view
from arkadash.views.agent import agent_view
from arkadash.views.health import health_view


def main(page: ft.Page) -> None:
    page.title = "Arkadash"
    page.theme_mode = ft.ThemeMode.DARK
    page.padding = 16
    page.window.width = 380
    page.window.height = 720

    api = ArkadashApi()

    ping_text = ft.Text(
        "Agent: ?", size=12, color=ft.Colors.ON_SURFACE_VARIANT
    )

    def refresh_ping() -> None:
        if api.ping():
            ping_text.value = f"Agent: ✓ {api.base_url}"
            ping_text.color = ft.Colors.GREEN
        else:
            ping_text.value = f"Agent: ✗ {api.base_url}"
            ping_text.color = ft.Colors.RED
        page.update()

    # Phase Y (2026-06-29): 4 sekme — "Health" ilk sırada,
    # sağlık monitoring aktif. Diğer sekmeler (Smart Home/Music/Agent)
    # sonraki phase'lerde genişleyecek.
    tab_bar = ft.TabBar(
        scrollable=True,
        tabs=[
            ft.Tab(label="Health",     icon=ft.Icons.MONITOR_HEART),
            ft.Tab(label="Smart Home", icon=ft.Icons.HOME),
            ft.Tab(label="Music",      icon=ft.Icons.MUSIC_NOTE),
            ft.Tab(label="Agent",      icon=ft.Icons.SMART_TOY),
        ],
    )

    tab_view = ft.TabBarView(
        expand=True,
        controls=[
            ft.Container(padding=8, content=health_view(page, api)),
            ft.Container(padding=16, content=smart_home_view(page, api)),
            ft.Container(padding=16, content=music_view(page, api)),
            ft.Container(padding=16, content=agent_view(page, api)),
        ],
    )

    def on_tab_change(e: ft.ControlEvent) -> None:
        tab_view.selected_index = e.control.selected_index
        page.update()

    tabs = ft.Tabs(
        length=4,
        selected_index=0,
        animation_duration=200,
        content=ft.Column([tab_bar, tab_view], expand=True),
        on_change=on_tab_change,
    )

    page.add(
        ft.Column(
            [
                ping_text,
                tabs,
            ],
            expand=True
        )
    )

    # Başlangıçta ping kontrolü
    refresh_ping()


if __name__ == "__main__":
    ft.run(main)
