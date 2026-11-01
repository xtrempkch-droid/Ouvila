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
    </style>
</head>
<body class="h-screen w-screen flex flex-col justify-between p-4 md:p-6 bg-black">

    <!-- TOPO: Barra de Navegação e Sistema -->
    <header class="flex items-center justify-between pb-3 border-b border-white/5">
        <!-- Abas do Sistema -->
        <nav class="flex items-center space-x-2 md:space-x-6">
            <button onclick="switchTab('music')" id="tab-music" class="tab-btn px-4 py-1.5 rounded-lg text-sm font-medium transition-all text-neutral-500 hover:text-white">Música</button>
            <button onclick="switchTab('library')" id="tab-library" class="tab-btn px-4 py-1.5 rounded-lg text-sm font-medium transition-all text-black bg-white shadow-sm font-semibold">Biblioteca</button>
            <button onclick="switchTab('home')" id="tab-home" class="tab-btn px-4 py-1.5 rounded-lg text-sm font-medium transition-all text-neutral-500 hover:text-white">Home</button>
            <button onclick="switchTab('store')" id="tab-store" class="tab-btn px-4 py-1.5 rounded-lg text-sm font-medium transition-all text-neutral-500 hover:text-white">Loja</button>
            <button onclick="switchTab('ports')" id="tab-ports" class="tab-btn px-4 py-1.5 rounded-lg text-sm font-medium transition-all text-neutral-500 hover:text-white">Ports</button>
        </nav>

        <!-- Indicadores de Status -->
        <div class="flex items-center space-x-3 text-xs font-medium text-neutral-400">
            <button onclick="toggleSettingsModal()" class="flex items-center space-x-1.5 px-3 py-1 rounded-lg bg-neutral-900 hover:bg-neutral-800 border border-white/5 transition text-neutral-300">
                <i class="fa-solid fa-sliders text-neutral-400"></i>
                <span class="hidden sm:inline">UI</span>
            </button>
            <!-- Indicador de Wi-Fi Interativo -->
            <button onclick="toggleWifi()" id="wifi-btn" class="flex items-center space-x-1.5 bg-neutral-900 px-3 py-1 rounded-full border border-white/5 hover:bg-neutral-800 transition cursor-pointer" title="Clique para alternar Wi-Fi">
                <i id="wifi-icon" class="fa-solid fa-wifi text-white"></i>
                <span id="wifi-status" class="text-[11px] text-neutral-300">Wi-Fi On</span>
            </button>
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
                <div class="lg:col-span-5 flex items-center space-x-6 glass-panel p-6 rounded-2xl relative overflow-hidden group">
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
                    <!-- Visualizador de Espectro / Gráfico -->
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
                                <span class="text-[10px] text-neutral-400 cursor-pointer hover:underline" onclick="clearQueue()">Limpar</span>
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

            <!-- Barra de Progresso e Controles Botão -->
            <div class="glass-panel p-4 rounded-2xl space-y-3">
                <div class="flex items-center space-x-4">
                    <span id="current-time" class="text-xs font-mono text-neutral-500 w-10 text-right">1:12</span>
                    <div class="flex-1 relative group cursor-pointer" onclick="seekSong(event)">
                        <div class="h-2 w-full bg-neutral-900 rounded-full overflow-hidden border border-white/5">
                            <div id="progress-bar" class="h-full bg-white w-1/3 rounded-full relative transition-all"></div>
                        </div>
                        <div id="progress-handle" class="absolute top-1/2 -translate-y-1/2 w-3 h-3 bg-neutral-300 rounded-full shadow-lg left-1/3 opacity-0 group-hover:opacity-100 transition"></div>
                    </div>
                    <span id="total-time" class="text-xs font-mono text-neutral-500 w-10">3:45</span>
                </div>
                
                <!-- Botões de Ação do Gamepad -->
                <div class="flex items-center justify-center space-x-6 pt-1">
                    <button onclick="prevSong()" class="flex items-center space-x-2 px-4 py-2 rounded-xl bg-neutral-900 hover:bg-neutral-800 border border-white/10 text-xs font-semibold text-neutral-300 transition">
                        <span class="w-6 h-6 rounded-full bg-neutral-800 flex items-center justify-center font-bold text-white text-[11px] shadow border border-white/10">Y</span>
                        <span>Anterior</span>
                    </button>
                    <button onclick="togglePlay()" class="flex items-center space-x-2 px-6 py-2 rounded-xl bg-white hover:bg-neutral-200 text-xs font-bold text-black shadow-lg transition transform active:scale-95">
                        <span id="btn-play-icon" class="w-6 h-6 rounded-full bg-black/10 flex items-center justify-center font-bold text-black text-[11px]">A</span>
                        <span id="btn-play-text">Pausar</span>
                    </button>
                    <button onclick="nextSong()" class="flex items-center space-x-2 px-4 py-2 rounded-xl bg-neutral-900 hover:bg-neutral-800 border border-white/10 text-xs font-semibold text-neutral-300 transition">
                        <span class="w-6 h-6 rounded-full bg-neutral-800 flex items-center justify-center font-bold text-white text-[11px] shadow border border-white/10">X</span>
                        <span>Próxima</span>
                    </button>
                </div>
            </div>
        </div>

        <!-- VIEW: BIBLIOTECA DE JOGOS (Grid Minimalista) -->
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
                <p class="text-neutral-400 text-sm">Interface minimalista projetada para máxima sobriedade e foco nos conteúdos.</p>
                <button onclick="switchTab('library')" class="px-6 py-2.5 bg-white hover:bg-neutral-200 text-black text-xs font-bold rounded-xl transition">Explorar Biblioteca</button>
            </div>
        </div>

        <!-- VIEW: LOJA -->
        <div id="view-store" class="absolute inset-0 flex flex-col justify-center items-center text-center hidden opacity-0 transition-opacity duration-300">
            <div class="glass-panel p-8 rounded-3xl max-w-lg space-y-4 border border-white/5">
                <div class="w-16 h-16 bg-neutral-900 rounded-2xl flex items-center justify-center mx-auto text-white text-3xl border border-white/10">
                    <i class="fa-solid fa-store"></i>
                </div>
                <h2 class="text-2xl font-bold text-white">Loja & Catálogo</h2>
                <p class="text-neutral-400 text-sm">Novos ports e utilitários disponíveis para instalação local.</p>
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

    <!-- RODAPÉ -->
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
                <span class="w-5 h-5 rounded-full bg-neutral-800 flex items-center justify-center text-white font-bold text-[10px] shadow border border-white/20">Select</span>
                <span class="text-neutral-400">Configurações</span>
            </div>
        </div>
        <div class="hidden sm:flex items-center space-x-2 text-[11px] text-neutral-600">
            <i class="fa-solid fa-keyboard"></i>
            <span>Setas do teclado</span>
        </div>
    </footer>

    <!-- MODAL DE CONFIGURAÇÕES -->
    <div id="settings-modal" class="fixed inset-0 bg-black/85 backdrop-blur-md z-50 flex items-center justify-center hidden opacity-0 transition-opacity duration-300">
        <div class="glass-panel p-6 rounded-3xl w-full max-w-md space-y-6 shadow-2xl border border-white/10 bg-neutral-950">
            <div class="flex items-center justify-between border-b border-white/5 pb-4">
                <div class="flex items-center space-x-2">
                    <i class="fa-solid fa-sliders text-white text-lg"></i>
                    <h3 class="font-bold text-white text-lg">Ajustes da Interface</h3>
                </div>
                <button onclick="toggleSettingsModal()" class="w-8 h-8 rounded-full bg-white/5 hover:bg-white/10 flex items-center justify-center text-neutral-400 hover:text-white transition">
                    <i class="fa-solid fa-xmark"></i>
                </button>
            </div>
            <div class="space-y-4 text-sm">
                <div class="flex items-center justify-between p-3 rounded-xl bg-neutral-900 border border-white/5">
                    <div>
                        <div class="font-medium text-neutral-200">Espectro de Áudio</div>
                        <div class="text-xs text-neutral-500">Exibir barras de frequência</div>
                    </div>
                    <label class="relative inline-flex items-center cursor-pointer">
                        <input type="checkbox" id="toggle-eq" checked class="sr-only peer" onchange="toggleFeature('eq', this.checked)">
                        <div class="w-11 h-6 bg-neutral-800 peer-focus:outline-none rounded-full peer peer-checked:after:translate-x-full peer-checked:after:border-white after:content-[''] after:absolute after:top-[2px] after:left-[2px] after:bg-white after:border-neutral-300 after:border after:rounded-full after:h-5 after:w-5 after:transition-all peer-checked:bg-white"></div>
                    </label>
                </div>
            </div>
            <div class="pt-2">
                <button onclick="toggleSettingsModal()" class="w-full py-2.5 bg-white hover:bg-neutral-200 text-black font-bold rounded-xl transition text-sm">Salvar e Fechar</button>
            </div>
        </div>
    </div>

    <!-- NOTIFICAÇÃO -->
    <div id="toast" class="fixed bottom-16 left-1/2 -translate-x-1/2 bg-neutral-900 border border-white/10 text-white text-xs px-4 py-2.5 rounded-xl shadow-2xl flex items-center space-x-2 opacity-0 pointer-events-none transition-opacity duration-300 z-50">
        <i class="fa-solid fa-circle-check text-white"></i>
        <span id="toast-msg">Sucesso</span>
    </div>

    <!-- SCRIPT -->
    <script>
        const games = [
            { title: "Celeste", category: "Plataforma", active: true },
            { title: "Hollow Knight", category: "Metroidvania", active: false },
            { title: "Stardew Valley", category: "RPG", active: false },
            { title: "Cuphead", category: "Action", active: false },
            { title: "Sonic Mania", category: "Plataforma", active: false },
            { title: "Dead Cells", category: "Roguelike", active: false },
            { title: "Ori", category: "Aventura", active: false },
            { title: "Hades", category: "Roguelike", active: false },
            { title: "Blasphemous", category: "Metroidvania", active: false },
            { title: "Undertale", category: "RPG", active: false },
            { title: "Katana Zero", category: "Ação", active: false },
            { title: "Cyber Protocol X", category: "Port USB", active: false }
        ];

        let selectedGameIndex = 0;
        let currentTab = 'library';
        let isPlaying = true;
        let wifiEnabled = true;

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
                const isSelected = index === selectedGameIndex;
                const card = document.createElement('div');
                card.className = `game-card glass-panel p-4 rounded-2xl flex flex-col justify-between h-36 cursor-pointer relative overflow-hidden bg-neutral-950 ${isSelected ? 'border border-white shadow-lg scale-[1.02]' : 'border-white/5 opacity-70 hover:opacity-100'}`;
                card.onclick = () => {
                    selectedGameIndex = index;
                    renderGames();
                    showToast(`Selecionado: ${game.title}`);
                };
                card.innerHTML = `
                    <div class="flex items-center justify-between z-10">
                        <span class="text-[10px] uppercase font-medium tracking-wider px-2 py-0.5 rounded-full bg-neutral-900 text-neutral-400 border border-white/5">${game.category}</span>
                        ${isSelected ? '<span class="w-1.5 h-1.5 rounded-full bg-white animate-ping"></span>' : ''}
                    </div>
                    <div class="z-10 mt-auto">
                        <h3 class="text-base font-semibold text-white leading-tight">${game.title}</h3>
                        <p class="text-[11px] text-neutral-500 mt-0.5">Pronto</p>
                    </div>
                `;
                grid.appendChild(card);
            });
        }

        function switchTab(tabName) {
            currentTab = tabName;
            ['music', 'library', 'home', 'store', 'ports'].forEach(t => {
                const view = document.getElementById(`view-${t}`);
                const btn = document.getElementById(`tab-${t}`);
                if (t === tabName) {
                    view.classList.remove('hidden');
                    setTimeout(() => view.classList.remove('opacity-0'), 10);
                    btn.className = "tab-btn px-4 py-1.5 rounded-lg text-sm font-medium transition-all text-black bg-white shadow-sm font-semibold";
                } else {
                    view.classList.add('opacity-0');
                    setTimeout(() => view.classList.add('hidden'), 300);
                    btn.className = "tab-btn px-4 py-1.5 rounded-lg text-sm font-medium transition-all text-neutral-500 hover:text-white";
                }
            });

            const hintA = document.getElementById('hint-a');
            const hintB = document.getElementById('hint-b');
            if (tabName === 'music') {
                hintA.innerText = "Reproduzir / Pausar";
                hintB.innerText = "Voltar";
            } else if (tabName === 'library') {
                hintA.innerText = "A jogar";
                hintB.innerText = "Voltar";
            } else {
                hintA.innerText = "Selecionar";
                hintB.innerText = "Fechar";
            }
        }

        function togglePlay() {
            isPlaying = !isPlaying;
            document.getElementById('btn-play-text').innerText = isPlaying ? "Pausar" : "Reproduzir";
            showToast(isPlaying ? "Reproduzindo" : "Pausado");
        }

        function nextSong() {
            showToast("Próxima faixa");
        }

        function prevSong() {
            showToast("Faixa anterior");
        }

        function toggleWifi() {
            wifiEnabled = !wifiEnabled;
            const wifiIcon = document.getElementById('wifi-icon');
            const wifiStatus = document.getElementById('wifi-status');
            
            if (wifiEnabled) {
                wifiIcon.className = "fa-solid fa-wifi text-white";
                wifiStatus.innerText = "Wi-Fi On";
                showToast("Wi-Fi conectado");
            } else {
                wifiIcon.className = "fa-solid fa-wifi-slash text-neutral-500";
                wifiStatus.innerText = "Offline";
                showToast("Wi-Fi desconectado");
            }
        }

        function seekSong(event) {
            const bar = event.currentTarget;
            const clickX = event.offsetX;
            const width = bar.clientWidth;
            const percentage = (clickX / width) * 100;
            document.getElementById('progress-bar').style.width = percentage + '%';
            document.getElementById('progress-handle').style.left = percentage + '%';
            showToast(`Posição: ${Math.round(percentage)}%`);
        }

        function clearQueue() {
            document.getElementById('queue-list').innerHTML = '<div class="text-xs text-neutral-600 py-2 text-center">Fila vazia</div>';
            document.getElementById('queue-count').innerText = '0';
            showToast("Fila limpa");
        }

        function toggleSettingsModal() {
            const modal = document.getElementById('settings-modal');
            if (modal.classList.contains('hidden')) {
                modal.classList.remove('hidden');
                setTimeout(() => modal.classList.remove('opacity-0'), 10);
            } else {
                modal.classList.add('opacity-0');
                setTimeout(() => modal.classList.add('hidden'), 300);
            }
        }

        function toggleFeature(feature, enabled) {
            if (feature === 'eq') {
                document.querySelectorAll('.eq-bar').forEach(el => el.style.display = enabled ? 'block' : 'none');
                showToast(enabled ? "Espectro ativado" : "Espectro desativado");
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
                }
            }
            if (e.key.toLowerCase() === 'm') switchTab('music');
            if (e.key.toLowerCase() === 'l') switchTab('library');
            if (e.key.toLowerCase() === 'h') switchTab('home');
        });

        window.onload = function() {
            renderGames();
            switchTab('library');
        };
    </script>
</body>
</html>
