```python
"""
Handheld OS Interface - Python Version
Replicando o layout e a interatividade da interface do console portátil usando Tkinter.
"""

import tkinter as tk
from tkinter import ttk, messagebox
import datetime

class HandheldOSApp:
    def __init__(self, root):
        self.root = root
        self.root.title("Console Handheld OS - Redesign (Python)")
        self.root.geometry("1024x640")
        self.root.configure(bg="#07090E")
        self.root.minsize(800, 500)

        # Estado da Aplicação
        self.current_tab = "library"
        self.selected_game_index = 0
        self.is_playing = True
        self.wifi_enabled = True

        # Lista de jogos inspirada no projeto original
        self.games = [
            {"title": "Celeste", "category": "Plataforma / Indie", "color": "#1e1b4b"},
            {"title": "Hollow Knight", "category": "Metroidvania", "color": "#18181b"},
            {"title": "Stardew Valley", "category": "RPG / Simulação", "color": "#064e3b"},
            {"title": "Cuphead", "category": "Run and Gun", "color": "#78350f"},
            {"title": "Sonic Mania", "category": "Plataforma", "color": "#1e3a8a"},
            {"title": "Dead Cells", "category": "Roguelike", "color": "#881337"},
            {"title": "Ori and the Blind Forest", "category": "Aventura", "color": "#134e4a"},
            {"title": "Hades", "category": "Action Roguelike", "color": "#450a0a"},
            {"title": "Blasphemous", "category": "Metroidvania", "color": "#292524"},
            {"title": "Undertale", "category": "RPG", "color": "#3b0764"},
            {"title": "Katana Zero", "category": "Ação Rápida", "color": "#451a03"},
            {"title": "Cyber Protocol X", "category": "Port Nativo USB", "color": "#881337"}
        ]

        self.setup_styles()
        self.create_header()
        self.create_main_content()
        self.create_footer()

        # Vinculação de Teclado (Simulação de Gamepad)
        self.root.bind("<Left>", lambda e: self.navigate_grid(-1))
        self.root.bind("<Right>", lambda e: self.navigate_grid(1))
        self.root.bind("<Up>", lambda e: self.navigate_grid(-3))
        self.root.bind("<Down>", lambda e: self.navigate_grid(3))
        self.root.bind("<Return>", lambda e: self.action_select())
        self.root.bind("<i>", lambda e: self.switch_tab("music"))
        self.root.bind("<l>", lambda e: self.switch_tab("library"))
        self.root.bind("<h>", lambda e: self.switch_tab("home"))

        # Iniciar relógio em tempo real
        self.update_clock()

    def setup_styles(self):
        """Configura estilos e cores da interface."""
        self.bg_dark = "#07090E"
        self.panel_bg = "#161E2E"
        self.accent_color = "#F43F5E"
        self.text_light = "#F8FAFC"
        self.text_muted = "#94A3B8"

    def create_header(self):
        """Cria a barra de navegação superior e indicadores de sistema."""
        self.header_frame = tk.Frame(self.root, bg=self.bg_dark, height=60)
        self.header_frame.pack(fill=tk.X, padx=20, pady=15)
        self.header_frame.pack_propagate(False)

        # Abas de Navegação
        nav_frame = tk.Frame(self.header_frame, bg=self.bg_dark)
        nav_frame.pack(side=tk.LEFT)

        self.tabs = {}
        tab_names = [("music", "Música"), ("library", "Biblioteca"), ("home", "Home"), ("store", "Loja"), ("ports", "Ports")]
        
        for key, name in tab_names:
            btn = tk.Button(
                nav_frame, text=name, 
                command=lambda k=key: self.switch_tab(k),
                bg="#161E2E" if key == self.current_tab else self.bg_dark,
                fg=self.text_light if key == self.current_tab else self.text_muted,
                activebackground=self.accent_color, activeforeground="white",
                relief=tk.FLAT, padx=14, pady=6, font=("Inter", 10, "bold")
            )
            btn.pack(side=tk.LEFT, padx=4)
            self.tabs[key] = btn

        # Indicadores de Status (Direita)
        status_frame = tk.Frame(self.header_frame, bg=self.bg_dark)
        status_frame.pack(side=tk.RIGHT)

        # Wi-Fi Status Button
        self.wifi_btn = tk.Button(
            status_frame, text="📶 Wi-Fi On", command=self.toggle_wifi,
            bg="#111827", fg="#34d399", relief=tk.FLAT, padx=10, pady=4, font=("Inter", 9, "bold")
        )
        self.wifi_btn.pack(side=tk.LEFT, padx=6)

        # Bateria
        battery_lbl = tk.Label(status_frame, text="⚡ 82%", bg="#111827", fg="#34d399", padx=10, pady=4, font=("Inter", 9, "bold"))
        battery_lbl.pack(side=tk.LEFT, padx=6)

        # Relógio
        self.clock_lbl = tk.Label(status_frame, text="12:34", bg=self.bg_dark, fg=self.text_light, font=("Courier", 11, "bold"))
        self.clock_lbl.pack(side=tk.LEFT, padx=10)

    def create_main_content(self):
        """Cria a área dinâmica de conteúdo principal."""
        self.main_container = tk.Frame(self.root, bg=self.bg_dark)
        self.main_container.pack(fill=tk.BOTH, expand=True, padx=20, pady=5)

        # Frame da Biblioteca (Padrão inicial)
        self.library_frame = tk.Frame(self.main_container, bg=self.bg_dark)
        
        # Grid de Jogos com Scrollbar
        canvas = tk.Canvas(self.library_frame, bg=self.bg_dark, highlightthickness=0)
        scrollbar = ttk.Scrollbar(self.library_frame, orient="vertical", command=canvas.yview)
        self.grid_inner = tk.Frame(canvas, bg=self.bg_dark)

        self.grid_inner.bind(
            "<Configure>",
            lambda e: canvas.configure(scrollregion=canvas.bbox("all"))
        )
        canvas.create_window((0, 0), window=self.grid_inner, anchor="nw")
        canvas.configure(yscrollcommand=scrollbar.set)

        canvas.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        scrollbar.pack(side=tk.RIGHT, fill=tk.Y)

        self.render_game_grid()

        # Frames para outras abas (Música, Home, Loja, Ports)
        self.music_frame = tk.Frame(self.main_container, bg=self.bg_dark)
        self.build_music_view()

        self.home_frame = self.build_simple_view("Bem-vindo ao HandheldOS", "Seu sistema portátil otimizado. Navegue entre as abas superiores usando o D-Pad ou selecione diretamente na tela touch.", "🎮")
        self.store_frame = self.build_simple_view("Loja de Jogos & Indie Ports", "Novos títulos disponíveis para download direto no seu console. Ofertas especiais da semana em breve.", "🏪")
        self.ports_frame = self.build_simple_view("Gerenciador de Ports (.giochi via USB)", "Conecte um pendrive USB para carregar automaticamente arquivos .giochi e ports nativos otimizados.", "🔌")

        # Exibir inicialmente a biblioteca
        self.show_frame(self.library_frame)

    def render_game_grid(self):
        """Renderiza os cards dos jogos no grid da biblioteca."""
        for widget in self.grid_inner.winfo_children():
            widget.destroy()

        for idx, game in enumerate(self.games):
            row = idx // 4
            col = idx % 4

            is_selected = (idx == self.selected_game_index)
            border_color = self.accent_color if is_selected else "#2A374E"
            
            card = tk.Frame(self.grid_inner, bg=game["color"], bd=2, relief=tk.SOLID, width=220, height=130)
            card.grid(row=row, column=col, padx=10, pady=10)
            card.pack_propagate(False)

            # Categoria
            cat_lbl = tk.Label(card, text=game["category"].upper(), bg="#000000", fg=self.text_muted, font=("Inter", 8, "bold"))
            cat_lbl.pack(anchor="nw", padx=8, pady=6)

            # Título do Jogo
            title_lbl = tk.Label(card, text=game["title"], bg=game["color"], fg=self.text_light, font=("Inter", 12, "bold"), anchor="w")
            title_lbl.pack(anchor="sw", padx=8, fill=tk.X, pady=(20, 0))

            status_lbl = tk.Label(card, text="Pronto para iniciar", bg=game["color"], fg=self.text_muted, font=("Inter", 8), anchor="w")
            status_lbl.pack(anchor="sw", padx=8, fill=tk.X)

            # Evento de clique no card
            card.bind("<Button-1>", lambda e, i=idx: self.select_game(i))
            cat_lbl.bind("<Button-1>", lambda e, i=idx: self.select_game(i))
            title_lbl.bind("<Button-1>", lambda e, i=idx: self.select_game(i))

    def build_music_view(self):
        """Constrói a interface do Player de Música."""
        title = tk.Label(self.music_frame, text="Reproduzindo: Neon Horizon", bg=self.bg_dark, fg=self.text_light, font=("Inter", 16, "bold"))
        title.pack(pady=40)

        artist = tk.Label(self.music_frame, text="CyberPulse & Synthetics • Retrowave Sessions Vol. 1", bg=self.bg_dark, fg=self.text_muted, font=("Inter", 11))
        artist.pack(pady=5)

        controls_frame = tk.Frame(self.music_frame, bg=self.bg_dark)
        controls_frame.pack(pady=30)

        btn_prev = tk.Button(controls_frame, text="⏮ Anterior (Y)", bg="#161E2E", fg=self.text_light, font=("Inter", 10, "bold"), padx=15, pady=8, command=lambda: self.show_toast("Faixa anterior"))
        btn_prev.pack(side=tk.LEFT, padx=10)

        self.btn_play = tk.Button(controls_frame, text="⏸ Pausar (A)", bg=self.accent_color, fg="white", font=("Inter", 10, "bold"), padx=20, pady=8, command=self.toggle_play_music)
        self.btn_play.pack(side=tk.LEFT, padx=10)

        btn_next = tk.Button(controls_frame, text="⏭ Próxima (X)", bg="#161E2E", fg=self.text_light, font=("Inter", 10, "bold"), padx=15, pady=8, command=lambda: self.show_toast("Próxima faixa"))
        btn_next.pack(side=tk.LEFT, padx=10)

    def build_simple_view(self, title_text, desc_text, icon):
        """Constrói views padronizadas para Home, Loja e Ports."""
        frame = tk.Frame(self.main_container, bg=self.bg_dark)
        
        inner = tk.Frame(frame, bg=self.panel_bg, padx=40, pady=40)
        inner.place(relx=0.5, rely=0.5, anchor=tk.CENTER)

        tk.Label(inner, text=icon, bg=self.panel_bg, fg=self.accent_color, font=("Inter", 32)).pack(pady=10)
        tk.Label(inner, text=title_text, bg=self.panel_bg, fg=self.text_light, font=("Inter", 16, "bold")).pack(pady=5)
        tk.Label(inner, text=desc_text, bg=self.panel_bg, fg=self.text_muted, font=("Inter", 10), wraplength=400, justify=tk.CENTER).pack(pady=10)
        
        return frame

    def create_footer(self):
        """Cria o rodapé com dicas de comandos do gamepad e teclado."""
        footer_frame = tk.Frame(self.root, bg=self.bg_dark, height=40)
        footer_frame.pack(fill=tk.X, padx=20, pady=10)
        footer_frame.pack_propagate(False)

        hints_lbl = tk.Label(footer_frame, text="[A] Iniciar / Pausar   [B] Voltar   [Setas] Navegar Grid   [I] Música   [L] Biblioteca   [H] Home", bg=self.bg_dark, fg=self.text_muted, font=("Inter", 9))
        hints_lbl.pack(side=tk.LEFT)

    def switch_tab(self, tab_name):
        """Alterna entre as abas do sistema."""
        self.current_tab = tab_name
        
        # Atualizar cores dos botões de navegação
        for key, btn in self.tabs.items():
            if key == tab_name:
                btn.configure(bg="#161E2E", fg=self.text_light)
            else:
                btn.configure(bg=self.bg_dark, fg=self.text_muted)

        # Mostrar o frame correto
        if tab_name == "library":
            self.show_frame(self.library_frame)
        elif tab_name == "music":
            self.show_frame(self.music_frame)
        elif tab_name == "home":
            self.show_frame(self.home_frame)
        elif tab_name == "store":
            self.show_frame(self.store_frame)
        elif tab_name == "ports":
            self.show_frame(self.ports_frame)

    def show_frame(self, frame_to_show):
        """Esconde todas as views e exibe apenas a solicitada."""
        for frame in [self.library_frame, self.music_frame, self.home_frame, self.store_frame, self.ports_frame]:
            frame.pack_forget()
        frame_to_show.pack(fill=tk.BOTH, expand=True)

    def select_game(self, index):
        """Seleciona um jogo na biblioteca pelo índice."""
        self.selected_game_index = index
        self.render_game_grid()
        self.show_toast(f"Selecionado: {self.games[index]['title']}")

    def navigate_grid(self, delta):
        """Permite navegar pelo grid de jogos usando o teclado (setas)."""
        if self.current_tab == "library":
            self.selected_game_index = (self.selected_game_index + delta) % len(self.games)
            if self.selected_game_index < 0:
                self.selected_game_index = 0
            self.render_game_grid()

    def action_select(self):
        """Ação disparada ao pressionar Enter / Botão A."""
        if self.current_tab == "library":
            game = self.games[self.selected_game_index]
            self.show_toast(f"Iniciando {game['title']}...")
        elif self.current_tab == "music":
            self.toggle_play_music()

    def toggle_play_music(self):
        """Alterna estado de reprodução de música."""
        self.is_playing = not self.is_playing
        status = "Reproduzindo" if self.is_playing else "Pausado"
        self.btn_play.configure(text=f"⏸ {status} (A)" if self.is_playing else f"▶ Reproduzir (A)")
        self.show_toast(f"Música {status.lower()}")

    def toggle_wifi(self):
        """Alterna o status do Wi-Fi."""
        self.wifi_enabled = not self.wifi_enabled
        if self.wifi_enabled:
            self.wifi_btn.configure(text="📶 Wi-Fi On", fg="#34d399")
            self.show_toast("Wi-Fi conectado")
        else:
            self.wifi_btn.configure(text="❌ Offline", fg="#f43f5e")
            self.show_toast("Wi-Fi desconectado")

    def update_clock(self):
        """Atualiza o relógio em tempo real na barra de topo."""
        now = datetime.datetime.now().strftime("%H:%M")
        self.clock_lbl.config(text=now)
        self.root.after(1000, self.update_clock)

    def show_toast(self, message):
        """Exibe uma mensagem flutuante temporária."""
        toast = tk.Toplevel(self.root)
        toast.overrideredirect(True)
        toast.configure(bg="#1e293b")
        
        lbl = tk.Label(toast, text=message, bg="#1e293b", fg="white", font=("Inter", 9, "bold"), padx=15, pady=8)
        lbl.pack()

        # Centralizar na parte inferior da janela principal
        x = self.root.winfo_x() + (self.root.winfo_width() // 2) - 100
        y = self.root.winfo_y() + self.root.winfo_height() - 80
        toast.geometry(f"+{x}+{y}")

        # Fechar após 2 segundos
        self.root.after(2000, toast.destroy)

if __name__ == "__main__":
    root = tk.Tk()
    app = HandheldOSApp(root)
    root.mainloop()
```