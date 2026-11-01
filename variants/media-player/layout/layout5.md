<!DOCTYPE html>
<html lang="pt-BR">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Handheld iOS OS</title>
    <!-- Tailwind CSS CDN -->
    <script src="https://cdn.tailwindcss.com"></script>
    <script>
        tailwind.config = {
            theme: {
                extend: {
                    colors: {
                        ios: {
                            bg: '#000000',
                            card: 'rgba(28, 28, 30, 0.75)',
                            cardHover: 'rgba(44, 44, 46, 0.85)',
                            border: 'rgba(255, 255, 255, 0.12)',
                            textPrimary: '#F2F2F7',
                            textSecondary: '#8E8E93',
                            accent: '#FFFFFF'
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
            font-family: -apple-system, BlinkMacSystemFont, "SF Pro Display", "SF Pro Text", "Inter", sans-serif;
            background-color: #000000;
            color: #F2F2F7;
            user-select: none;
            overflow: hidden;
            letter-spacing: -0.01em;
        }
        /* Efeito Blur e Glassmorphism Inspirado no iOS */
        .ios-glass {
            background: rgba(28, 28, 30, 0.65);
            backdrop-filter: blur(30px);
            -webkit-backdrop-filter: blur(30px);
            border: 0.5px solid rgba(255, 255, 255, 0.12);
        }
        .ios-card {
            background: rgba(44, 44, 46, 0.5);
            backdrop-filter: blur(20px);
            border: 0.5px solid rgba(255, 255, 255, 0.08);
            transition: all 0.25s cubic-bezier(0.25, 1, 0.5, 1);
        }
        /* Foco Ativo via Gamepad (Estilo Foco tvOS / iOS Widget) */
        .gamepad-focus {
            border-color: rgba(255, 255, 255, 0.8) !important;
            background: rgba(60, 60, 67, 0.85) !important;
            box-shadow: 0 12px 40px rgba(0, 0, 0, 0.8), 0 0 0 2px rgba(255, 255, 255, 0.4);
            transform: scale(1.03);
        }
        /* Animação suave para Equalizador */
        @keyframes iosWave {
            0%, 100% { height: 8px; }
            50% { height: 32px; }
        }
        .ios-wave-bar {
            animation: iosWave 1.1s ease-in-out infinite;
        }
        .ios-wave-bar:nth-child(2) { animation-delay: 0.15s; }
        .ios-wave-bar:nth-child(3) { animation-delay: 0.3s; }
        .ios-wave-bar:nth-child(4) { animation-delay: 0.05s; }
        .ios-wave-bar:nth-child(5) { animation-delay: 0.4s; }
    </style>
</head>
<body class="h-screen w-screen flex flex-col justify-between p-5 md:p-8 bg-black">

    <!-- TOPO: Barra de Status e Segmented Control (Estilo iOS) -->
    <header class="flex flex-col space-y-4 pb-4 border-b border-white/10">
        <!-- Status Bar do Sistema -->
        <div class="flex items-center justify-between text-xs font-semibold text-neutral-400 px-1">
            <div class="flex items-center space-x-3">
                <span id="clock" class="text-sm font-bold text-white tracking-tight">12:34</span>
                <div class="flex items-center space-x-1 bg-white/10 px-2.5 py-0.5 rounded-full text-[11px] text-white">
                    <i class="fa-solid fa-wifi text-[10px]"></i>
                    <span>Wi-Fi</span>
                </div>
            </div>
            <div class="flex items-center space-x-2 bg-white/10 px-3 py-1 rounded-full text-white text-[11px]">
                <span class="w-1.5 h-1.5 rounded-full bg-white"></span>
                <span>82%</span>
                <i class="fa-solid fa-battery-three-quarters text-neutral-300"></i>
            </div>
        </div>

        <!-- iOS Segmented Control (Abas) -->
        <nav class="flex items-center justify-center">
            <div class="ios-glass p-1 rounded-xl flex space-x-1 shadow-lg max-w-xl w-full">
                <div id="tab-music" class="flex-1 py-2 text-center rounded-lg text-xs font-medium transition-all text-neutral-400">Música</div>
                <div id="tab-library" class="flex-1 py-2 text-center rounded-lg text-xs font-semibold transition-all text-black bg-white shadow-md">Biblioteca</div>
                <div id="tab-home" class="flex-1 py-2 text-center rounded-lg text-xs font-medium transition-all text-neutral-400">Home</div>
                <div id="tab-store" class="flex-1 py-2 text-center rounded-lg text-xs font-medium transition-all text-neutral-400">Loja</div>
                <div id="tab-ports" class="flex-1 py-2 text-center rounded-lg text-xs font-medium transition-all text-neutral-400">Ports</div>
            </div>
        </nav>
    </header>

    <!-- CONTEÚDO CENTRAL -->
    <main class="flex-1 overflow-hidden relative my-3">

        <!-- VIEW: MÚSICA (Player Estilo Apple Music) -->
        <div id="view-music" class="absolute inset-0 flex flex-col justify-between hidden opacity-0 transition-all duration-300">
            <div class="grid grid-cols-1 lg:grid-cols-12 gap-6 items-center h-full max-h-[400px]">
                <!-- Capa Estilo Widget iOS -->
                <div class="lg:col-span-5 ios-card p-6 rounded-3xl flex items-center space-x-6 relative overflow-hidden shadow-2xl">
                    <div class="w-32 h-32 md:w-36 md:h-36 rounded-2xl bg-neutral-900 shadow-xl flex items-center justify-center relative overflow-hidden shrink-0 border border-white/10">
                        <i class="fa-solid fa-compact-disc text-neutral-600 text-5xl animate-spin" style="animation-duration: 10s;"></i>
                        <div class="absolute inset-0 bg-black/20"></div>
                        <div class="absolute w-8 h-8 rounded-full bg-black border-2 border-neutral-700 flex items-center justify-center">
                            <div class="w-2 h-2 rounded-full bg-white"></div>
                        </div>
                    </div>
                    <div class="space-y-1.5 z-10">
                        <span class="text-[10px] uppercase tracking-wider font-semibold text-neutral-400">Reproduzindo Agora</span>
                        <h2 id="song-title" class="text-xl font-bold text-white tracking-tight leading-snug">Neon Horizon</h2>
                        <p id="song-artist" class="text-neutral-300 text-xs font-medium">CyberPulse & Synthetics</p>
                        <p id="song-album" class="text-neutral-500 text-[11px]">Retrowave Sessions Vol. 1</p>
                    </div>
                </div>

                <!-- Equalizador e Playlist Minimalista -->
                <div class="lg:col-span-7 flex flex-col justify-between h-full space-y-3">
                    <div class="ios-card p-5 rounded-3xl flex flex-col justify-between flex-1">
                        <div class="flex items-center justify-between">
                            <span class="text-xs font-semibold text-neutral-300"><i class="fa-solid fa-waveform-lines text-white mr-1.5"></i> Espectro Dinâmico</span>
                            <div class="flex items-center space-x-1 h-5">
                                <div class="w-1 bg-white rounded-full ios-wave-bar"></div>
                                <div class="w-1 bg-neutral-400 rounded-full ios-wave-bar"></div>
                                <div class="w-1 bg-white rounded-full ios-wave-bar"></div>
                                <div class="w-1 bg-neutral-500 rounded-full ios-wave-bar"></div>
                                <div class="w-1 bg-white rounded-full ios-wave-bar"></div>
                            </div>
                        </div>
                        <div class="mt-2 pt-3 border-t border-white/10">
                            <span class="text-[11px] font-semibold text-neutral-400">Próximas na Fila</span>
                            <div class="mt-2 space-y-1">
                                <div class="flex items-center justify-between text-xs p-2 rounded-xl bg-white/5 border border-white/5">
                                    <span class="font-medium text-neutral-200">Midnight Runner - Synthwave Boy</span>
                                    <span class="text-neutral-500 font-mono text-[11px]">3:15</span>
                                </div>
                            </div>
                        </div>
                    </div>
                </div>
            </div>

            <!-- Barra de Progresso e Guia Gamepad do Player -->
            <div class="ios-card p-4 rounded-3xl space-y-3 shadow-xl">
                <div class="flex items-center space-x-4">
                    <span class="text-[11px] font-mono text-neutral-400 w-10 text-right">1:12</span>
                    <div class="flex-1 relative">
                        <div class="h-1.5 w-full bg-white/10 rounded-full overflow-hidden">
                            <div class="h-full bg-white w-1/3 rounded-full"></div>
                        </div>
                    </div>
                    <span class="text-[11px] font-mono text-neutral-400 w-10">3:45</span>
                </div>
                
                <!-- Botões de Controle na Tela (Pad iOS) -->
                <div class="flex items-center justify-center space-x-6 pt-1">
                    <div class="flex items-center space-x-2 px-3.5 py-1.5 rounded-xl bg-white/10 border border-white/10 text-xs text-neutral-300">
                        <span class="w-5 h-5 rounded-full bg-black flex items-center justify-center font-bold text-white text-[10px] shadow">Y</span>
                        <span>Anterior</span>
                    </div>
                    <div class="flex items-center space-x-2 px-5 py-1.5 rounded-xl bg-white text-xs font-bold text-black shadow-lg">
                        <span class="w-5 h-5 rounded-full bg-black/10 flex items-center justify-center font-bold text-black text-[10px]">A</span>
                        <span id="btn-play-text">Pausar</span>
                    </div>
                    <div class="flex items-center space-x-2 px-3.5 py-1.5 rounded-xl bg-white/10 border border-white/10 text-xs text-neutral-300">
                        <span class="w-5 h-5 rounded-full bg-black flex items-center justify-center font-bold text-white text-[10px] shadow">X</span>
                        <span>Próxima</span>
                    </div>
                </div>
            </div>
        </div>

        <!-- VIEW: BIBLIOTECA (Grid de Apps Estilo iOS SpringBoard) -->
        <div id="view-library" class="absolute inset-0 flex flex-col justify-between opacity-100 transition-all duration-300">
            <div id="game-grid" class="grid grid-cols-2 sm:grid-cols-3 lg:grid-cols-4 gap-4 overflow-y-auto pr-2 max-h-[calc(100vh-180px)] pb-4">
                <!-- Injetado via JS -->
            </div>
        </div>

        <!-- VIEW: HOME -->
        <div id="view-home" class="absolute inset-0 flex flex-col justify-center items-center text-center hidden opacity-0 transition-all duration-300">
            <div class="ios-card p-8 rounded-3xl max-w-md space-y-3">
                <div class="w-14 h-14 bg-white/10 rounded-2xl flex items-center justify-center mx-auto text-white text-2xl border border-white/10">
                    <i class="fa-solid fa-house"></i>
                </div>
                <h2 class="text-xl font-bold text-white">Início iOS Handheld</h2>
                <p class="text-neutral-400 text-xs leading-relaxed">Sistema otimizado para navegação fluida via controle de console.</p>
            </div>
        </div>

        <!-- VIEW: LOJA -->
        <div id="view-store" class="absolute inset-0 flex flex-col justify-center items-center text-center hidden opacity-0 transition-all duration-300">
            <div class="ios-card p-8 rounded-3xl max-w-md space-y-3">
                <div class="w-14 h-14 bg-white/10 rounded-2xl flex items-center justify-center mx-auto text-white text-2xl border border-white/10">
                    <i class="fa-solid fa-bag-shopping"></i>
                </div>
                <h2 class="text-xl font-bold text-white">App Store Handheld</h2>
                <p class="text-neutral-400 text-xs leading-relaxed">Explore utilitários e jogos verificados com design minimalista.</p>
            </div>
        </div>

        <!-- VIEW: PORTS -->
        <div id="view-ports" class="absolute inset-0 flex flex-col justify-center items-center text-center hidden opacity-0 transition-all duration-300">
            <div class="ios-card p-8 rounded-3xl max-w-md space-y-3">
                <div class="w-14 h-14 bg-white/10 rounded-2xl flex items-center justify-center mx-auto text-white text-2xl border border-white/10">
                    <i class="fa-solid fa-usb"></i>
                </div>
                <h2 class="text-xl font-bold text-white">Ports (.giochi)</h2>
                <p class="text-neutral-400 text-xs leading-relaxed">Carregador de pacotes externos via USB.</p>
            </div>
        </div>

    </main>

    <!-- RODAPÉ: GUIA DE BOTÕES DO CONTROLE -->
    <footer class="flex items-center justify-between pt-3 border-t border-white/10 text-xs text-neutral-400 font-medium">
        <div class="flex items-center space-x-6">
            <div class="flex items-center space-x-2">
                <span class="w-5 h-5 rounded-full bg-white/10 border border-white/20 flex items-center justify-center text-white font-bold text-[10px] shadow">A</span>
                <span id="hint-a" class="text-neutral-300">A jogar</span>
            </div>
            <div class="flex items-center space-x-2">
                <span class="w-5 h-5 rounded-full bg-white/10 border border-white/20 flex items-center justify-center text-white font-bold text-[10px] shadow">B</span>
                <span id="hint-b" class="text-neutral-300">B voltar</span>
            </div>
            <div class="flex items-center space-x-2">
                <span class="w-5 h-5 rounded-full bg-white/10 border border-white/20 flex items-center justify-center text-white font-bold text-[10px] shadow">L/R</span>
                <span class="text-neutral-300">Abas</span>
            </div>
        </div>
        <div class="flex items-center space-x-2 text-[11px] text-neutral-400 font-mono bg-white/5 px-3 py-1 rounded-xl border border-white/5">
            <i class="fa-solid fa-gamepad text-white"></i>
            <span>Joystick Ativo</span>
        </div>
    </footer>

    <!-- NOTIFICAÇÃO ESTILO TOAST iOS -->
    <div id="toast" class="fixed bottom-16 left-1/2 -translate-x-1/2 ios-glass text-white text-xs px-4 py-2.5 rounded-2xl shadow-2xl flex items-center space-x-2.5 opacity-0 pointer-events-none transition-all duration-300 z-50 border border-white/20">
        <i class="fa-solid fa-circle-check text-white"></i>
        <span id="toast-msg">Sucesso</span>
    </div>

    <!-- SCRIPT DE NAVEGAÇÃO -->
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
        let currentTabIndex = 1;
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
                card.className = `ios-card p-4 rounded-3xl flex flex-col justify-between h-36 relative overflow-hidden transition-all ${isSelected ? 'gamepad-focus' : 'opacity-60'}`;
                card.innerHTML = `
                    <div class="flex items-center justify-between z-10">
                        <span class="text-[10px] uppercase font-semibold tracking-wider px-2 py-0.5 rounded-md bg-white/10 text-neutral-300 border border-white/5">${game.category}</span>
                        ${isSelected ? '<span class="w-2 h-2 rounded-full bg-white shadow-lg animate-pulse"></span>' : ''}
                    </div>
                    <div class="z-10 mt-auto">
                        <h3 class="text-base font-semibold text-white leading-snug">${game.title}</h3>
                        <p class="text-[11px] text-neutral-400 mt-0.5 font-medium">Toque em [A] para iniciar</p>
                    </div>
                `;
                grid.appendChild(card);
            });
        }

        function switchTab(tabName) {
            currentTab = tabName;
            currentTabIndex = tabs.indexOf(tabName);
            
            tabs.forEach((t) => {
                const view = document.getElementById(`view-${t}`);
                const tabItem = document.getElementById(`tab-${t}`);
                if (t === tabName) {
                    view.classList.remove('hidden');
                    setTimeout(() => view.classList.remove('opacity-0'), 10);
                    tabItem.className = "flex-1 py-2 text-center rounded-lg text-xs font-semibold transition-all text-black bg-white shadow-md";
                } else {
                    view.classList.add('opacity-0');
                    setTimeout(() => view.classList.add('hidden'), 300);
                    tabItem.className = "flex-1 py-2 text-center rounded-lg text-xs font-medium transition-all text-neutral-400";
                }
            });

            const hintA = document.getElementById('hint-a');
            const hintB = document.getElementById('hint-b');
            if (tabName === 'music') {
                hintA.innerText = "Reproduzir / Pausar";
                hintB.innerText = "Biblioteca";
            } else if (tabName === 'library') {
                hintA.innerText = "A jogar";
                hintB.innerText = "Recuar";
            } else {
                hintA.innerText = "Selecionar";
                hintB.innerText = "Biblioteca";
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

        window.addEventListener('keydown', (e) => {
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
        });

        window.onload = function() {
            renderGames();
            switchTab('library');
        };
    </script>
</body>
</html>
