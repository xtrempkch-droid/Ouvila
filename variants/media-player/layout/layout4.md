<!DOCTYPE html>
<html lang="pt-BR">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Console Handheld OS - Redesign</title>
    <!-- Tailwind CSS CDN -->
    <script src="https://cdn.tailwindcss.com"></script>
    <script>
        tailwind.config = {
            theme: {
                extend: {
                    colors: {
                        brand: {
                            dark: '#000000', // Preto absoluto OLED
                            card: '#080808', // Cards cinza quase invisível
                            border: '#171717',
                            accent: '#FFFFFF', // Acento totalmente neutro / branco
                            accentHover: '#E5E5E5',
                            muted: '#737373'
                        }
                    }
                }
            }
        }
    </script>
    <!-- FontAwesome Icons -->
    <link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.4.0/css/all.min.css">
    <style>
        body {
            font-family: 'Inter', sans-serif;
            background-color: #000000;
            color: #E5E5E5;
            user-select: none;
            overflow: hidden;
        }
        /* Glassmorphism monocromático e profundo */
        .glass-panel {
            background: rgba(10, 10, 10, 0.9);
            backdrop-filter: blur(20px);
            border: 1px solid rgba(255, 255, 255, 0.04);
        }
        .game-card {
            transition: all 0.2s cubic-bezier(0.4, 0, 0.2, 1);
        }
        .game-card:hover, .game-card.selected {
            transform: translateY(-4px);
            border-color: rgba(255, 255, 255, 0.3);
            background: rgba(18, 18, 18, 0.95);
            box-shadow: 0 10px 30px -10px rgba(255, 255, 255, 0.05);
        }
        /* Animação do equalizador monocromático */
        @keyframes pulseBar {
            0%, 100% { height: 12px; }
            50% { height: 40px; }
        }
        .eq-bar {
            animation: pulseBar 1.2s ease-in-out infinite;
        }
        .eq-bar:nth-child(2) { animation-delay: 0.2s; }
        .eq-bar:nth-child(3) { animation-delay: 0.4s; }
        .eq-bar:nth-child(4) { animation-delay: 0.1s; }
        .eq-bar:nth-child(5) { animation-delay: 0.5s; }

        /* Estilo para foco exclusivo via Gamepad */
        .gamepad-focus {
            border-color: rgba(255, 255, 255, 0.9) !important;
            background: rgba(24, 24, 24, 0.95) !important;
            box-shadow: 0 0 25px rgba(255, 255, 255, 0.15);
            transform: scale(1.02);
        }
    </style>
</head>
<body class="h-screen w-screen flex flex-col justify-between p-4 md:p-6 bg-black">

    <!-- TOPO: Barra de Navegação e Sistema -->
    <header class="flex items-center justify-between pb-3 border-b border-white/5">
        <!-- Abas do Sistema -->
        <nav class="flex items-center space-x-2 md:space-x-6">
            <div id="tab-music" class="tab-item px-4 py-1.5 rounded-lg text-sm font-medium transition-all text-neutral-500">Música</div>
            <div id="tab-library" class="tab-item px-4 py-1.5 rounded-lg text-sm font-medium transition-all text-black bg-white shadow-sm font-semibold">Biblioteca</div>
            <div id="tab-home" class="tab-item px-4 py-1.5 rounded-lg text-sm font-medium transition-all text-neutral-500">Home</div>
            <div id="tab-store" class="tab-item px-4 py-1.5 rounded-lg text-sm font-medium transition-all text-neutral-500">Loja</div>
            <div id="tab-ports" class="tab-item px-4 py-1.5 rounded-lg text-sm font-medium transition-all text-neutral-500">Ports</div>
        </nav>

        <!-- Indicadores de Status -->
        <div class="flex items-center space-x-3 text-xs font-medium text-neutral-400">
            <div id="wifi-indicator" class="flex items-center space-x-1.5 bg-neutral-900 px-3 py-1 rounded-full border border-white/5 text-white">
                <i id="wifi-icon" class="fa-solid fa-wifi text-white"></i>
                <span id="wifi-status" class="text-[11px] text-neutral-300">Wi-Fi On</span>
            </div>
            <div class="flex items-center space-x-2 bg-neutral-900 px-3 py-1 rounded-full border border-white/5 text-neutral-300">
                <span class="w-1.5 h-1.5 rounded-full bg-white"></span>
                <span>82%</span>
                <i class="fa-solid fa-battery-three-quarters text-neutral-400"></i>
            </div>
            <div id="clock" class="font-mono text-sm tracking-wider text-white">12:34</div>
        </div>
    </header>

    <!-- CONTEÚDO PRINCIPAL (Áreas Dinâmicas) -->
    <main class="flex-1 overflow-hidden relative my-4">

        <!-- VIEW: MÚSICA (Player Redesenhado) -->
        <div id="view-music" class="absolute inset-0 flex flex-col justify-between hidden opacity-0 transition-opacity duration-300">
            <div class="grid grid-cols-1 lg:grid-cols-12 gap-6 items-center h-full max-h-[440px]">
                <!-- Capa e Info do Álbum -->
                <div class="lg:col-span-5 flex items-center space-x-6 glass-panel p-6 rounded-2xl relative overflow-hidden">
                    <div id="album-cover-container" class="w-32 h-32 md:w-40 md:h-40 rounded-xl bg-neutral-950 shadow-2xl flex items-center justify-center relative overflow-hidden shrink-0 border border-white/10">
                        <i class="fa-solid fa-compact-disc text-neutral-700 text-6xl animate-spin" style="animation-duration: 12s;"></i>
                        <div class="absolute inset-0 bg-black/30"></div>
                        <div class="absolute w-10 h-10 rounded-full bg-black border-4 border-neutral-800 flex items-center justify-center shadow-inner">
                            <div class="w-2 h-2 rounded-full bg-white"></div>
                        </div>
                    </div>
                    <div class="space-y-2 z-10">
                        <div class="inline-block px-2.5 py-0.5 rounded-full bg-neutral-900 text-neutral-300 text-[11px] font-medium tracking-wide border border-white/10">Reproduzindo</div>
                        <h2 id="song-title" class="text-2xl font-bold text-white tracking-tight leading-snug">Neon Horizon</h2>
                        <p id="song-artist" class="text-neutral-300 text-sm font-medium">CyberPulse & Synthetics</p>
                        <p id="song-album" class="text-neutral-500 text-xs">Retrowave Sessions Vol. 1</p>
                        <div class="flex items-center space-x-2 pt-1 text-[11px] text-neutral-400 font-mono">
                            <span class="px-1.5 py-0.5 bg-neutral-900 rounded border border-white/5">MP3 320kbps</span>
                            <span class="px-1.5 py-0.5 bg-neutral-900 rounded border border-white/5">FLAC 24-bit</span>
                        </div>
                    </div>
                </div>

                <!-- Painel Direito / Visualizador de Áudio e Fila -->
                <div class="lg:col-span-7 flex flex-col justify-between h-full space-y-4">
                    <div class="glass-panel p-5 rounded-2xl flex flex-col justify-between flex-1">
                        <div class="flex items-center justify-between">
                            <span class="text-xs uppercase tracking-wider text-neutral-400 font-semibold"><i class="fa-solid fa-wave-square text-white mr-1.5"></i> Espectro Monocromático</span>
                            <div class="flex items-center space-x-1 h-6">
                                <div class="w-1.5 bg-white rounded-full eq-bar"></div>
                                <div class="w-1.5 bg-neutral-400 rounded-full eq-bar"></div>
                                <div class="w-1.5 bg-white rounded-full eq-bar"></div>
                                <div class="w-1.5 bg-neutral-500 rounded-full eq-bar"></div>
                                <div class="w-1.5 bg-white rounded-full eq-bar"></div>
                            </div>
                        </div>
                        <!-- Fila de Reprodução -->
                        <div class="mt-3 pt-3 border-t border-white/5">
                            <div class="flex items-center justify-between mb-2">
                                <span class="text-xs font-bold text-neutral-300">Fila (<span id="queue-count">2</span>)</span>
                            </div>
                            <div id="queue-list" class="space-y-1.5 max-h-24 overflow-y-auto pr-1">
                                <div class="flex items-center justify-between text-xs p-2 rounded-lg bg-neutral-900/60 border border-white/5">
                                    <div class="flex items-center space-x-2">
                                        <i class="fa-solid fa-music text-white"></i>
                                        <span class="font-medium text-neutral-200">Midnight Runner - Synthwave Boy</span>
                                    </div>
                                    <span class="text-neutral-500 font-mono">3:15</span>
                                </div>
                                <div class="flex items-center justify-between text-xs p-2 rounded-lg bg-neutral-900/30 border border-white/5">
                                    <div class="flex items-center space-x-2">
                                        <i class="fa-solid fa-music text-neutral-600"></i>
                                        <span class="font-medium text-neutral-400">Starlight Highway - Nebula</span>
                                    </div>
                                    <span class="text-neutral-600 font-mono">4:02</span>
                                </div>
                            </div>
                        </div>
                    </div>
                </div>
            </div>

            <!-- Barra de Progresso e Guia de Gamepad do Player -->
            <div class="glass-panel p-4 rounded-2xl space-y-3">
                <div class="flex items-center space-x-4">
                    <span id="current-time" class="text-xs font-mono text-neutral-500 w-10 text-right">1:12</span>
                    <div class="flex-1 relative">
                        <div class="h-2 w-full bg-neutral-900 rounded-full overflow-hidden border border-white/5">
                            <div id="progress-bar" class="h-full bg-white w-1/3 rounded-full relative transition-all"></div>
                        </div>
                    </div>
                    <span id="total-time" class="text-xs font-mono text-neutral-500 w-10">3:45</span>
                </div>
                
                <!-- Feedback visual dos Botões Físicos do Gamepad -->
                <div class="flex items-center justify-center space-x-8 pt-1">
                    <div class="flex items-center space-x-2 px-4 py-2 rounded-xl bg-neutral-900 border border-white/10 text-xs text-neutral-300">
                        <span class="w-6 h-6 rounded-full bg-neutral-800 flex items-center justify-center font-bold text-white text-[11px] shadow border border-white/10">Y</span>
                        <span>Anterior</span>
                    </div>
                    <div class="flex items-center space-x-2 px-6 py-2 rounded-xl bg-white text-xs font-bold text-black shadow-lg">
                        <span class="w-6 h-6 rounded-full bg-black/10 flex items-center justify-center font-bold text-black text-[11px]">A</span>
                        <span id="btn-play-text">Pausar</span>
                    </div>
                    <div class="flex items-center space-x-2 px-4 py-2 rounded-xl bg-neutral-900 border border-white/10 text-xs text-neutral-300">
                        <span class="w-6 h-6 rounded-full bg-neutral-800 flex items-center justify-center font-bold text-white text-[11px] shadow border border-white/10">X</span>
                        <span>Próxima</span>
                    </div>
                </div>
            </div>
        </div>

        <!-- VIEW: BIBLIOTECA DE JOGOS (Grid Focado por Joystick) -->
        <div id="view-library" class="absolute inset-0 flex flex-col justify-between opacity-100 transition-opacity duration-300">
            <div id="game-grid" class="grid grid-cols-2 sm:grid-cols-3 lg:grid-cols-4 gap-4 overflow-y-auto pr-2 max-h-[calc(100vh-180px)] pb-4">
                <!-- Injetado via JS -->
            </div>
        </div>

        <!-- VIEW: HOME -->
        <div id="view-home" class="absolute inset-0 flex flex-col justify-center items-center text-center hidden opacity-0 transition-opacity duration-300">
            <div class="glass-panel p-8 rounded-3xl max-w-lg space-y-4 border border-white/5">
                <div class="w-16 h-16 bg-neutral-900 rounded-2xl flex items-center justify-center mx-auto text-white text-3xl border border-white/10">
                    <i class="fa-solid fa-gamepad"></i>
                </div>
                <h2 class="text-2xl font-bold text-white">HandheldOS Monocromático</h2>
                <p class="text-neutral-400 text-sm">Navegue utilizando exclusivamente o D-Pad / Analógico e botões do Joystick.</p>
                <div class="pt-2 text-xs text-neutral-500 font-mono">Pressione [A] para explorar</div>
            </div>
        </div>

        <!-- VIEW: LOJA -->
        <div id="view-store" class="absolute inset-0 flex flex-col justify-center items-center text-center hidden opacity-0 transition-opacity duration-300">
            <div class="glass-panel p-8 rounded-3xl max-w-lg space-y-4 border border-white/5">
                <div class="w-16 h-16 bg-neutral-900 rounded-2xl flex items-center justify-center mx-auto text-white text-3xl border border-white/10">
                    <i class="fa-solid fa-store"></i>
                </div>
                <h2 class="text-2xl font-bold text-white">Loja & Catálogo</h2>
                <p class="text-neutral-400 text-sm">Novos ports e utilitários disponíveis para instalação local via gamepad.</p>
            </div>
        </div>

        <!-- VIEW: PORTS -->
        <div id="view-ports" class="absolute inset-0 flex flex-col justify-center items-center text-center hidden opacity-0 transition-opacity duration-300">
            <div class="glass-panel p-8 rounded-3xl max-w-lg space-y-4 border border-white/5">
                <div class="w-16 h-16 bg-neutral-900 rounded-2xl flex items-center justify-center mx-auto text-white text-3xl border border-white/10">
                    <i class="fa-solid fa-usb"></i>
                </div>
                <h2 class="text-2xl font-bold text-white">Gerenciador de Ports (.giochi)</h2>
                <p class="text-neutral-400 text-sm">Leitor de pacotes portáteis via USB integrado.</p>
            </div>
        </div>

    </main>

    <!-- RODAPÉ ESTILO CONTROLE FÍSICO -->
    <footer class="flex items-center justify-between pt-3 border-t border-white/5 text-xs text-neutral-500 font-medium">
        <div id="footer-hints" class="flex items-center space-x-6">
            <div class="flex items-center space-x-2">
                <span class="w-5 h-5 rounded-full bg-neutral-800 flex items-center justify-center text-white font-bold text-[10px] shadow border border-white/20">A</span>
                <span id="hint-a" class="text-neutral-400">A jogar</span>
            </div>
            <div class="flex items-center space-x-2">
                <span class="w-5 h-5 rounded-full bg-neutral-800 flex items-center justify-center text-white font-bold text-[10px] shadow border border-white/20">B</span>
                <span id="hint-b" class="text-neutral-400">B voltar</span>
            </div>
            <div class="flex items-center space-x-2">
                <span class="w-5 h-5 rounded-full bg-neutral-800 flex items-center justify-center text-white font-bold text-[10px] shadow border border-white/20">L/R</span>
                <span class="text-neutral-400">Abas de Sistema</span>
            </div>
        </div>
        <div class="flex items-center space-x-3 text-[11px] text-neutral-400 font-mono bg-neutral-900 px-3 py-1 rounded-lg border border-white/5">
            <i class="fa-solid fa-gamepad text-white"></i>
            <span>Controle Ativo (D-Pad / Setas)</span>
        </div>
    </footer>

    <!-- NOTIFICAÇÃO ESTILO CONSOLE -->
    <div id="toast" class="fixed bottom-16 left-1/2 -translate-x-1/2 bg-neutral-900 border border-white/10 text-white text-xs px-4 py-2.5 rounded-xl shadow-2xl flex items-center space-x-2 opacity-0 pointer-events-none transition-opacity duration-300 z-50">
        <i class="fa-solid fa-circle-check text-white"></i>
        <span id="toast-msg">Sucesso</span>
    </div>

    <!-- SCRIPT DE NAVEGAÇÃO EXCLUSIVA POR JOYSTICK -->
    <script>
        const games = [
            { title: "Celeste", category: "Plataforma" },
            { title: "Hollow Knight", category: "Metroidvania" },
            { title: "Stardew Valley", category: "RPG" },
            { title: "Cuphead", category: "Action" },
            { title: "Sonic Mania", category: "Plataforma" },
            { title: "Dead Cells", category: "Roguelike" },
            { title: "Ori", category: "Aventura" },
            { title: "Hades", category: "Roguelike" },
            { title: "Blasphemous", category: "Metroidvania" },
            { title: "Undertale", category: "RPG" },
            { title: "Katana Zero", category: "Ação" },
            { title: "Cyber Protocol X", category: "Port USB" }
        ];

        const tabs = ['music', 'library', 'home', 'store', 'ports'];
        let currentTabIndex = 1; // Começa em Biblioteca
        let selectedGameIndex = 0;
        let currentTab = 'library';
        let isPlaying = true;

        function updateClock() {
            const now = new Date();
            const hours = String(now.getHours()).padStart(2, '0');
            const minutes = String(now.getMinutes()).padStart(2, '0');
            document.getElementById('clock').innerText = `${hours}:${minutes}`;
        }
        setInterval(updateClock, 1000);
        updateClock();

        function renderGames() {
            const grid = document.getElementById('game-grid');
            grid.innerHTML = '';
            games.forEach((game, index) => {
                const isSelected = index === selectedGameIndex && currentTab === 'library';
                const card = document.createElement('div');
                card.className = `game-card glass-panel p-4 rounded-2xl flex flex-col justify-between h-36 relative overflow-hidden bg-neutral-950 transition-all ${isSelected ? 'gamepad-focus border border-white shadow-xl' : 'border-white/5 opacity-50'}`;
                card.innerHTML = `
                    <div class="flex items-center justify-between z-10">
                        <span class="text-[10px] uppercase font-medium tracking-wider px-2 py-0.5 rounded-full bg-neutral-900 text-neutral-400 border border-white/5">${game.category}</span>
                        ${isSelected ? '<span class="w-2 h-2 rounded-full bg-white animate-pulse"></span>' : ''}
                    </div>
                    <div class="z-10 mt-auto">
                        <h3 class="text-base font-semibold text-white leading-tight">${game.title}</h3>
                        <p class="text-[11px] text-neutral-500 mt-0.5">Pronto para jogar</p>
                    </div>
                `;
                grid.appendChild(card);
            });
        }

        function switchTab(tabName) {
            currentTab = tabName;
            currentTabIndex = tabs.indexOf(tabName);
            
            tabs.forEach((t, idx) => {
                const view = document.getElementById(`view-${t}`);
                const tabItem = document.getElementById(`tab-${t}`);
                if (t === tabName) {
                    view.classList.remove('hidden');
                    setTimeout(() => view.classList.remove('opacity-0'), 10);
                    tabItem.className = "tab-item px-4 py-1.5 rounded-lg text-sm font-medium transition-all text-black bg-white shadow-sm font-semibold";
                } else {
                    view.classList.add('opacity-0');
                    setTimeout(() => view.classList.add('hidden'), 300);
                    tabItem.className = "tab-item px-4 py-1.5 rounded-lg text-sm font-medium transition-all text-neutral-500";
                }
            });

            const hintA = document.getElementById('hint-a');
            const hintB = document.getElementById('hint-b');
            if (tabName === 'music') {
                hintA.innerText = "Reproduzir / Pausar";
                hintB.innerText = "Voltar à Biblioteca";
            } else if (tabName === 'library') {
                hintA.innerText = "A jogar";
                hintB.innerText = "Recuar";
            } else {
                hintA.innerText = "Selecionar";
                hintB.innerText = "Voltar à Biblioteca";
            }
        }

        function showToast(message) {
            const toast = document.getElementById('toast');
            document.getElementById('toast-msg').innerText = message;
            toast.classList.remove('opacity-0');
            setTimeout(() => {
                toast.classList.add('opacity-0');
            }, 2500);
        }

        // CONTROLE EXCLUSIVO POR TECLADO / JOYSTICK EMULADO
        window.addEventListener('keydown', (e) => {
            // Navegar entre abas com L/R (Teclas Q e E ou L/R simulados)
            if (e.key === 'q' || e.key === 'Q' || e.key === 'PageUp') {
                currentTabIndex = (currentTabIndex - 1 + tabs.length) % tabs.length;
                switchTab(tabs[currentTabIndex]);
                return;
            }
            if (e.key === 'e' || e.key === 'E' || e.key === 'PageDown') {
                currentTabIndex = (currentTabIndex + 1) % tabs.length;
                switchTab(tabs[currentTabIndex]);
                return;
            }

            if (currentTab === 'library') {
                if (e.key === 'ArrowRight') {
                    selectedGameIndex = (selectedGameIndex + 1) % games.length;
                    renderGames();
                } else if (e.key === 'ArrowLeft') {
                    selectedGameIndex = (selectedGameIndex - 1 + games.length) % games.length;
                    renderGames();
                } else if (e.key === 'ArrowDown') {
                    selectedGameIndex = Math.min(selectedGameIndex + 3, games.length - 1);
                    renderGames();
                } else if (e.key === 'ArrowUp') {
                    selectedGameIndex = Math.max(selectedGameIndex - 3, 0);
                    renderGames();
                } else if (e.key.toLowerCase() === 'a' || e.key === 'Enter') {
                    showToast(`Iniciando ${games[selectedGameIndex].title}...`);
                } else if (e.key.toLowerCase() === 'm') {
                    switchTab('music');
                }
            } else if (currentTab === 'music') {
                if (e.key.toLowerCase() === 'a' || e.key === 'Enter') {
                    isPlaying = !isPlaying;
                    document.getElementById('btn-play-text').innerText = isPlaying ? "Pausar" : "Reproduzir";
                    showToast(isPlaying ? "Reproduzindo" : "Pausado");
                } else if (e.key.toLowerCase() === 'x') {
                    showToast("Próxima faixa carregada");
                } else if (e.key.toLowerCase() === 'y') {
                    showToast("Faixa anterior carregada");
                } else if (e.key.toLowerCase() === 'b') {
                    switchTab('library');
                }
            } else {
                if (e.key.toLowerCase() === 'b' || e.key === 'Escape') {
                    switchTab('library');
                }
            }

            // Atalhos rápidos para abas adicionais
            if (e.key === '1') switchTab('music');
            if (e.key === '2') switchTab('library');
            if (e.key === '3') switchTab('home');
            if (e.key === '4') switchTab('store');
            if (e.key === '5') switchTab('ports');
        });

        window.onload = function() {
            renderGames();
            switchTab('library');
        };
    </script>
</body>
</html>
