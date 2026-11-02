```rust
/**
 * Handheld OS Interface - Rust Version
 * Implementado utilizando o framework iced para interfaces gráficas modernas em Rust.
 * Para rodar, adicione as dependências 'iced' e 'chrono' no seu Cargo.toml.
 */

use iced::widget::{button, column, container, row, scrollable, text, Space};
use iced::{Alignment, Element, Length, Sandbox, Settings, Theme};

pub fn main() -> iced::Result {
    HandheldOSApp::run(Settings::default())
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Tab {
    Music,
    Library,
    Home,
    Store,
    Ports,
}

#[derive(Debug, Clone)]
struct Game {
    title: &'static str,
    category: &'static str,
    color: &'static str,
}

struct HandheldOSApp {
    current_tab: Tab,
    selected_game_index: usize,
    is_playing: bool,
    wifi_enabled: bool,
    games: Vec<Game>,
}

#[derive(Debug, Clone)]
enum Message {
    SwitchTab(Tab),
    SelectGame(usize),
    TogglePlay,
    ToggleWifi,
    NavigateGrid(isize),
    ActionSelect,
}

impl Sandbox for HandheldOSApp {
    type Message = Message;

    fn new() -> Self {
        Self {
            current_tab: Tab::Library,
            selected_game_index: 0,
            is_playing: true,
            wifi_enabled: true,
            games: vec![
                Game { title: "Celeste", category: "Plataforma / Indie", color: "blue" },
                Game { title: "Hollow Knight", category: "Metroidvania", color: "slate" },
                Game { title: "Stardew Valley", category: "RPG / Simulação", color: "emerald" },
                Game { title: "Cuphead", category: "Run and Gun", color: "amber" },
                Game { title: "Sonic Mania", category: "Plataforma", color: "cyan" },
                Game { title: "Dead Cells", category: "Roguelike", color: "rose" },
                Game { title: "Ori and the Blind Forest", category: "Aventura", color: "teal" },
                Game { title: "Hades", category: "Action Roguelike", color: "red" },
                Game { title: "Blasphemous", category: "Metroidvania", color: "stone" },
                Game { title: "Undertale", category: "RPG", color: "purple" },
                Game { title: "Katana Zero", category: "Ação Rápida", color: "yellow" },
                Game { title: "Cyber Protocol X", category: "Port Nativo USB", color: "brand" },
            ],
        }
    }

    fn title(&self) -> String {
        String::from("Console Handheld OS - Redesign (Rust)")
    }

    fn update(&mut self, message: Message) {
        match message {
            Message::SwitchTab(tab) => {
                self.current_tab = tab;
            }
            Message::SelectGame(index) => {
                self.selected_game_index = index;
            }
            Message::TogglePlay => {
                self.is_playing = !self.is_playing;
            }
            Message::ToggleWifi => {
                self.wifi_enabled = !self.wifi_enabled;
            }
            Message::NavigateGrid(delta) => {
                if self.current_tab == Tab::Library {
                    let len = self.games.len() as isize;
                    let mut new_idx = (self.selected_game_index as isize) + delta;
                    if new_idx < 0 {
                        new_idx = 0;
                    } else if new_idx >= len {
                        new_idx = len - 1;
                    }
                    self.selected_game_index = new_idx as usize;
                }
            }
            Message::ActionSelect => {
                // Ação ao pressionar A / Enter
            }
        }
    }

    fn view(&self) -> Element<'_, Message> {
        // Cabeçalho de Navegação
        let header = row![
            row![
                button("Música").on_press(Message::SwitchTab(Tab::Music)),
                button("Biblioteca").on_press(Message::SwitchTab(Tab::Library)),
                button("Home").on_press(Message::SwitchTab(Tab::Home)),
                button("Loja").on_press(Message::SwitchTab(Tab::Store)),
                button("Ports").on_press(Message::SwitchTab(Tab::Ports)),
            ].spacing(10),
            Space::new(Length::Fill, Length::Shrink),
            row![
                button(if self.wifi_enabled { "📶 Wi-Fi On" } else { "❌ Offline" })
                    .on_press(Message::ToggleWifi),
                text("⚡ 82%"),
                text("12:34"),
            ].spacing(15)
        ]
        .align_items(Alignment::Center)
        .padding(15);

        // Conteúdo dinâmico de acordo com a aba selecionada
        let content: Element<'_, Message> = match self.current_tab {
            Tab::Library => {
                let mut grid = column![].spacing(10);
                for (i, game) in self.games.iter().enumerate() {
                    let is_selected = i == self.selected_game_index;
                    let card_text = format!("{}\n[{}]", game.title, game.category);
                    let card_btn = button(text(card_text))
                        .on_press(Message::SelectGame(i))
                        .padding(15);
                    grid = grid.push(card_btn);
                }
                scrollable(grid).into()
            }
            Tab::Music => {
                column![
                    text("Reproduzindo: Neon Horizon").size(22),
                    text("CyberPulse & Synthetics").size(14),
                    button(if self.is_playing { "⏸ Pausar" } else { "▶ Reproduzir" })
                        .on_press(Message::TogglePlay)
                ]
                .spacing(20)
                .align_items(Alignment::Center)
                .into()
            }
            Tab::Home => text("Bem-vindo ao HandheldOS").size(20).into(),
            Tab::Store => text("Loja de Jogos & Indie Ports").size(20).into(),
            Tab::Ports => text("Gerenciador de Ports (.giochi via USB)").size(20).into(),
        };

        // Rodapé com dicas de comandos
        let footer = row![
            text("[A] Iniciar / Pausar   [B] Voltar   [Setas] Navegar Grid   [I] Música   [L] Biblioteca   [H] Home")
        ]
        .padding(10);

        column![header, content, footer]
            .spacing(10)
            .padding(10)
            .into()
    }

    fn theme(&self) -> Theme {
        Theme::Dark
    }
}
```