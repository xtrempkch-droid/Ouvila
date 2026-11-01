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
                            dark: '#020408', // Fundo OLED profundo
                            card: '#0D1117', // Cards ultra escuros
                            border: '#1F2937',
                            accent: '#EF4444', // Vermelho vibrante
                            accentHover: '#DC2626',
                            muted: '#6B7280'
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
            background-color: #020408;
            color: #F3F4F6;
            user-select: none;
            overflow: hidden;
        }
        /* Glassmorphism profundo / OLED style */
        .glass-panel {
            background: rgba(13, 17, 23, 0.85);
            backdrop-filter: blur(16px);
            border: 1px solid rgba(255, 255, 255, 0.05);
        }
        .game-card {
            transition: all 0.25s cubic-bezier(0.4, 0, 0.2, 1);
        }
        .game-card:hover, .game-card.selected {
            transform: translateY(-6px) scale(1.02);
            border-color: #EF4444;
            box-shadow: 0 15px 30px -5px rgba(239, 68, 68, 0.25);
            background: rgba(20, 27, 38, 0.95);
        }
        /* Animação do equalizador */
        @keyframes pulseBar {
            0%, 100% { height: 15px; }
            50% { height: 45px; }
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
<body class="h-screen w-screen flex flex-col justify-between p-4 md:p-6 bg-[#020408]">

    <!-- TOPO: Barra de Navegação e Sistema -->
    <header class="flex items-center justify-between pb-3 border-b border-white/5">
        <!-- Abas do Sistema -->
        <nav class="flex items-center space-x-2 md:space-x-6">
            <button onclick="switchTab('music')" id="tab-music" class="tab-btn px-4 py-1.5 rounded-lg text-sm font-medium transition-all text-slate-500 hover:text-white">Música</button>
            <button onclick="switchTab('library')" id="tab-library" class="tab-btn px-4 py-1.5 rounded-lg text-sm font-medium transition-all text-white bg-white/[0.07] border border-white/10 shadow-sm">Biblioteca</button>
            <button onclick="switchTab('home')" id="tab-home" class="tab-btn px-4 py-1.5 rounded-lg text-sm font-medium transition-all text-slate-500 hover:text-white">Home</button>
            <button onclick="switchTab('store')" id="tab-store" class="tab-btn px-4 py-1.5 rounded-lg text-sm font-medium transition-all text-slate-500 hover:text-white">Loja</button>
            <button onclick="switchTab('ports')" id="tab-ports" class="tab-btn px-4 py-1.5 rounded-lg text-sm font-medium transition-all text-slate-500 hover:text-white">Ports</button>
        </nav>

        <!-- Indicadores de Status -->
        <div class="flex items-center space-x-4 text-xs font-semibold text-slate-400">
            <button onclick="toggleSettingsModal()" class="flex items-center space-x-1.5 px-3 py-1 rounded-lg bg-white/[0.03] hover:bg-white/[0.08] border border-white/5 transition">
                <i class="fa-solid fa-wand-magic-sparkles text-brand-accent"></i>
                <span class="hidden sm:inline">Melhorias UI</span>
            </button>
            <!-- Indicador de Wi-Fi Interativo -->
            <button onclick="toggleWifi()" id="wifi-btn" class="flex items-center space-x-1.5 bg-black/60 px-3 py-1 rounded-full border border-white/5 hover:bg-white/[0.08] transition cursor-pointer" title="Clique para alternar Wi-Fi">
                <i id="wifi-icon" class="fa-solid fa-wifi text-emerald-400"></i>
                <span id="wifi-status" class="text-[11px] text-slate-300">Wi-Fi On</span>
            </button>
            <div class="flex items-center space-x-2 bg-black/60 px-3 py-1 rounded-full border border-white/5">
                <span class="w-2 h-2 rounded-full bg-emerald-500 animate-pulse"></span>
                <span>82%</span>
                <i class="fa-solid fa-battery-three-quarters text-emerald-400"></i>
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
                    <div class="absolute -right-10 -bottom-10 w-40 h-40 bg-brand-accent/15 rounded-full blur-3xl pointer-events-none"></div>
                    <div id="album-cover-container" class="w-32 h-32 md:w-40 md:h-40 rounded-xl bg-gradient-to-br from-neutral-900 via-zinc-900 to-black shadow-2xl flex items-center justify-center relative overflow-hidden shrink-0 transform group-hover:scale-105 transition duration-300 border border-white/5">
                        <i class="fa-solid fa-compact-disc text-white/20 text-6xl animate-spin" style="animation-duration: 10s;"></i>
                        <div class="absolute inset-0 bg-black/40"></div>
                        <div class="absolute w-10 h-10 rounded-full bg-black border-4 border-white/10 flex items-center justify-center shadow-inner">
                            <div class="w-2 h-2 rounded-full bg-brand-accent"></div>
                        </div>
                    </div>
                    <div class="space-y-2 z-10">
                        <div class="inline-block px-2.5 py-0.5 rounded-full bg-brand-accent/15 text-brand-accent text-xs font-semibold uppercase tracking-wider">Reproduzindo</div>
                        <h2 id="song-title" class="text-2xl font-bold text-white tracking-tight leading-snug">Neon Horizon</h2>
                        <p id="song-artist" class="text-slate-300 text-sm font-medium">CyberPulse & Synthetics</p>
                        <p id="song-album" class="text-slate-500 text-xs">Retrowave Sessions Vol. 1</p>
                        <div class="flex items-center space-x-2 pt-1 text-[11px] text-slate-400 font-mono">
                            <span class="px-1.5 py-0.5 bg-white/[0.04] rounded border border-white/5">MP3 320kbps</span>
                            <span class="px-1.5 py-0.5 bg-white/[0.04] rounded border border-white/5">FLAC 24-bit</span>
                        </div>
                    </div>
                </div>

                <!-- Painel Direito / Visualizador de Áudio e Fila -->
                <div class="lg:col-span-7 flex flex-col justify-between h-full space-y-4">
                    <!-- Visualizador de Espectro / Gráfico -->
                    <div class="glass-panel p-5 rounded-2xl flex flex-col justify-between flex-1">
                        <div class="flex items-center justify-between">
                            <span class="text-xs uppercase tracking-wider text-slate-400 font-semibold"><i class="fa-solid fa-wave-square text-brand-accent mr-1.5"></i> Espectro de Áudio Ativo</span>
                            <div class="flex items-center space-x-1 h-6">
                                <div class="w-1.5 bg-brand-accent rounded-full eq-bar"></div>
                                <div class="w-1.5 bg-brand-accent rounded-full eq-bar"></div>
                                <div class="w-1.5 bg-brand-accent rounded-full eq-bar"></div>
                                <div class="w-1.5 bg-brand-accent rounded-full eq-bar"></div>
                                <div class="w-1.5 bg-brand-accent rounded-full eq-bar"></div>
                            </div>
                        </div>
                        <!-- Fila de Reprodução -->
                        <div class="mt-3 pt-3 border-t border-white/5">
                            <div class="flex items-center justify-between mb-2">
                                <span class="text-xs font-bold text-slate-300">Fila de Reprodução (<span id="queue-count">2</span>)</span>
                                <span class="text-[10px] text-brand-accent cursor-pointer hover:underline" onclick="clearQueue()">Limpar fila</span>
                            </div>
                            <div id="queue-list" class="space-y-1.5 max-h-24 overflow-y-auto pr-1">
                                <div class="flex items-center justify-between text-xs p-2 rounded-lg bg-white/[0.02] hover:bg-white/[0.05] border border-white/5 transition">
                                    <div class="flex items-center space-x-2">
                                        <i class="fa-solid fa-music text-brand-accent"></i>
                                        <span class="font-medium text-slate-200">Midnight Runner - Synthwave Boy</span>
                                    </div>
                                    <span class="text-slate-500 font-mono">3:15</span>
                                </div>
                                <div class="flex items-center justify-between text-xs p-2 rounded-lg bg-white/[0.02] hover:bg-white/[0.05] border border-white/5 transition">
                                    <div class="flex items-center space-x-2">
                                        <i class="fa-solid fa-music text-slate-500"></i>
                                        <span class="font-medium text-slate-400">Starlight Highway - Nebula</span>
                                    </div>
                                    <span class="text-slate-600 font-mono">4:02</span>
                                </div>
                            </div>
                        </div>
                    </div>
                </div>
            </div>

            <!-- Barra de Progresso e Controles Botão -->
            <div class="glass-panel p-4 rounded-2xl space-y-3">
                <div class="flex items-center space-x-4">
                    <span id="current-time" class="text-xs font-mono text-slate-500 w-10 text-right">1:12</span>
                    <div class="flex-1 relative group cursor-pointer" onclick="seekSong(event)">
                        <div class="h-2 w-full bg-black/60 rounded-full overflow-hidden border border-white/5">
                            <div id="progress-bar" class="h-full bg-gradient-to-r from-brand-accent to-rose-500 w-1/3 rounded-full relative transition-all"></div>
                        </div>
                        <div id="progress-handle" class="absolute top-1/2 -translate-y-1/2 w-3.5 h-3.5 bg-white rounded-full shadow-lg left-1/3 opacity-0 group-hover:opacity-100 transition"></div>
                    </div>
                    <span id="total-time" class="text-xs font-mono text-slate-500 w-10">3:45</span>
                </div>
                
                <!-- Botões de Ação do Gamepad -->
                <div class="flex items-center justify-center space-x-6 pt-1">
                    <button onclick="prevSong()" class="flex items-center space-x-2 px-4 py-2 rounded-xl bg-white/[0.03] hover:bg-white/[0.07] border border-white/5 text-xs font-semibold text-slate-300 transition">
                        <span class="w-6 h-6 rounded-full bg-zinc-800 flex items-center justify-center font-bold text-white text-[11px] shadow border border-white/10">Y</span>
                        <span>Anterior</span>
                    </button>
                    <button onclick="togglePlay()" class="flex items-center space-x-2 px-6 py-2 rounded-xl bg-brand-accent hover:bg-brand-accentHover text-xs font-bold text-white shadow-lg shadow-brand-accent/20 transition transform active:scale-95">
                        <span id="btn-play-icon" class="w-6 h-6 rounded-full bg-black/20 flex items-center justify-center font-bold text-white text-[11px]">A</span>
                        <span id="btn-play-text">Pausar</span>
                    </button>
                    <button onclick="nextSong()" class="flex items-center space-x-2 px-4 py-2 rounded-xl bg-white/[0.03] hover:bg-white/[0.07] border border-white/5 text-xs font-semibold text-slate-300 transition">
                        <span class="w-6 h-6 rounded-full bg-zinc-800 flex items-center justify-center font-bold text-white text-[11px] shadow border border-white/10">X</span>
                        <span>Próxima</span>
                    </button>
                </div>
            </div>
        </div>

        <!-- VIEW: BIBLIOTECA DE JOGOS (Grid Aprimorado) -->
        <div id="view-library" class="absolute inset-0 flex flex-col justify-between opacity-100 transition-opacity duration-300">
            <!-- Grid de Jogos Estilo Portátil -->
            <div id="game-grid" class="grid grid-cols-2 sm:grid-cols-3 lg:grid-cols-4 gap-4 overflow-y-auto pr-2 max-h-[calc(100vh-180px)] pb-4">
                <!-- Os cards serão injetados via JS -->
            </div>
        </div>

        <!-- VIEW: HOME -->
        <div id="view-home" class="absolute inset-0 flex flex-col justify-center items-center text-center hidden opacity-0 transition-opacity duration-300">
            <div class="glass-panel p-8 rounded-3xl max-w-lg space-y-4 border border-white/5">
                <div class="w-16 h-16 bg-brand-accent/15 rounded-2xl flex items-center justify-center mx-auto text-brand-accent text-3xl">
                    <i class="fa-solid fa-gamepad"></i>
                </div>
                <h2 class="text-2xl font-bold text-white">Bem-vindo ao HandheldOS</h2>
                <p class="text-slate-400 text-sm">Seu sistema portátil otimizado. Navegue entre as abas superiores usando o D-Pad ou selecione diretamente na tela touch.</p>
                <button onclick="switchTab('library')" class="px-6 py-2.5 bg-brand-accent hover:bg-brand-accentHover text-white text-xs font-bold rounded-xl shadow-lg shadow-brand-accent/20 transition">Explorar Biblioteca</button>
            </div>
        </div>

        <!-- VIEW: LOJA -->
        <div id="view-store" class="absolute inset-0 flex flex-col justify-center items-center text-center hidden opacity-0 transition-opacity duration-300">
            <div class="glass-panel p-8 rounded-3xl max-w-lg space-y-4 border border-white/5">
                <div class="w-16 h-16 bg-amber-500/15 rounded-2xl flex items-center justify-center mx-auto text-amber-400 text-3xl">
                    <i class="fa-solid fa-store"></i>
                </div>
                <h2 class="text-2xl font-bold text-white">Loja de Jogos & Indie Ports</h2>
                <p class="text-slate-400 text-sm">Novos títulos disponíveis para download direto no seu console. Ofertas especiais da semana em breve.</p>
            </div>
        </div>

        <!-- VIEW: PORTS -->
        <div id="view-ports" class="absolute inset-0 flex flex-col justify-center items-center text-center hidden opacity-0 transition-opacity duration-300">
            <div class="glass-panel p-8 rounded-3xl max-w-lg space-y-4 border border-white/5">
                <div class="w-16 h-16 bg-sky-500/15 rounded-2xl flex items-center justify-center mx-auto text-sky-400 text-3xl">
                    <i class="fa-solid fa-usb"></i>
                </div>
                <h2 class="text-2xl font-bold text-white">Gerenciador de Ports (.giochi via USB)</h2>
                <p class="text-slate-400 text-sm">Conecte um pendrive USB para carregar automaticamente arquivos .giochi e ports nativos otimizados.</p>
            </div>
        </div>

    </main>

    <!-- RODAPÉ: Guia de Atalhos / Botões do Gamepad -->
    <footer class="flex items-center justify-between pt-3 border-t border-white/5 text-xs text-slate-500 font-medium">
        <div id="footer-hints" class="flex items-center space-x-6">
            <div class="flex items-center space-x-2">
                <span class="w-5 h-5 rounded-full bg-emerald-700 flex items-center justify-center text-white font-bold text-[10px] shadow border border-white/15">A</span>
                <span id="hint-a" class="text-slate-400">A jogar</span>
            </div>
            <div class="flex items-center space-x-2">
                <span class="w-5 h-5 rounded-full bg-amber-700 flex items-center justify-center text-white font-bold text-[10px] shadow border border-white/15">B</span>
                <span id="hint-b" class="text-slate-400">B voltar</span>
            </div>
            <div class="flex items-center space-x-2">
                <span class="w-5 h-5 rounded-full bg-zinc-800 flex items-center justify-center text-white font-bold text-[10px] shadow border border-white/15">Select</span>
                <span class="text-slate-400">Configurações</span>
            </div>
            <div class="flex items-center space-x-2">
                <span class="text-slate-600 font-mono text-[11px]">.giochi via USB</span>
            </div>
        </div>
        <div class="hidden sm:flex items-center space-x-2 text-[11px] text-slate-600">
            <i class="fa-solid fa-keyboard"></i>
            <span>Use setas do teclado para navegar</span>
        </div>
    </footer>

    <!-- MODAL DE MELHORIAS DE DESIGN -->
    <div id="settings-modal" class="fixed inset-0 bg-black/80 backdrop-blur-md z-50 flex items-center justify-center hidden opacity-0 transition-opacity duration-300">
        <div class="glass-panel p-6 rounded-3xl w-full max-w-md space-y-6 shadow-2xl border border-white/10 bg-[#070A0F]">
            <div class="flex items-center justify-between border-b border-white/5 pb-4">
                <div class="flex items-center space-x-2">
                    <i class="fa-solid fa-wand-magic-sparkles text-brand-accent text-lg"></i>
                    <h3 class="font-bold text-white text-lg">Painel de Melhorias UI</h3>
                </div>
                <button onclick="toggleSettingsModal()" class="w-8 h-8 rounded-full bg-white/5 hover:bg-white/10 flex items-center justify-center text-slate-400 hover:text-white transition">
                    <i class="fa-solid fa-xmark"></i>
                </button>
            </div>
            <div class="space-y-4 text-sm">
                <!-- Toggle 1 -->
                <div class="flex items-center justify-between p-3 rounded-xl bg-white/[0.02] border border-white/5">
                    <div>
                        <div class="font-medium text-slate-200">Visualizador de Espectro</div>
                        <div class="text-xs text-slate-500">Ativa animação gráfica de áudio no player</div>
                    </div>
                    <label class="relative inline-flex items-center cursor-pointer">
                        <input type="checkbox" id="toggle-eq" checked class="sr-only peer" onchange="toggleFeature('eq', this.checked)">
                        <div class="w-11 h-6 bg-zinc-800 peer-focus:outline-none rounded-full peer peer-checked:after:translate-x-full peer-checked:after:border-white after:content-[''] after:absolute after:top-[2px] after:left-[2px] after:bg-white after:border-slate-300 after:border after:rounded-full after:h-5 after:w-5 after:transition-all peer-checked:bg-brand-accent"></div>
                    </label>
                </div>
                <!-- Toggle 2 -->
                <div class="flex items-center justify-between p-3 rounded-xl bg-white/[0.02] border border-white/5">
                    <div>
                        <div class="font-medium text-slate-200">Capas de Álbuns Maiores</div>
                        <div class="text-xs text-slate-500">Expande o destaque visual da capa e vinil</div>
                    </div>
                    <label class="relative inline-flex items-center cursor-pointer">
                        <input type="checkbox" id="toggle-cover" checked class="sr-only peer" onchange="toggleFeature('cover', this.checked)">
                        <div class="w-11 h-6 bg-zinc-800 peer-focus:outline-none rounded-full peer peer-checked:after:translate-x-full peer-checked:after:border-white after:content-[''] after:absolute after:top-[2px] after:left-[2px] after:bg-white after:border-slate-300 after:border after:rounded-full after:h-5 after:w-5 after:transition-all peer-checked:bg-brand-accent"></div>
                    </label>
                </div>
                <!-- Toggle 3 -->
                <div class="flex items-center justify-between p-3 rounded-xl bg-white/[0.02] border border-white/5">
                    <div>
                        <div class="font-medium text-slate-200">Grid Dinâmico e Harmônico</div>
                        <div class="text-xs text-slate-500">Aplica efeitos de hover e escala fluida nos cards</div>
                    </div>
                    <label class="relative inline-flex items-center cursor-pointer">
                        <input type="checkbox" id="toggle-grid" checked class="sr-only peer" onchange="toggleFeature('grid', this.checked)">
                        <div class="w-11 h-6 bg-zinc-800 peer-focus:outline-none rounded-full peer peer-checked:after:translate-x-full peer-checked:after:border-white after:content-[''] after:absolute after:top-[2px] after:left-[2px] after:bg-white after:border-slate-300 after:border after:rounded-full after:h-5 after:w-5 after:transition-all peer-checked:bg-brand-accent"></div>
                    </label>
                </div>
            </div>
            <div class="pt-2">
                <button onclick="toggleSettingsModal()" class="w-full py-2.5 bg-brand-accent hover:bg-brand-accentHover text-white font-bold rounded-xl shadow-lg shadow-brand-accent/20 transition text-sm">Aplicar e Fechar</button>
            </div>
        </div>
    </div>

    <!-- MENSAGEM NOTIFICAÇÃO FLUTUANTE -->
    <div id="toast" class="fixed bottom-16 left-1/2 -translate-x-1/2 bg-zinc-950/95 border border-white/10 text-white text-xs px-4 py-2.5 rounded-xl shadow-2xl flex items-center space-x-2 opacity-0 pointer-events-none transition-opacity duration-300 z-50">
        <i class="fa-solid fa-circle-check text-emerald-400"></i>
        <span id="toast-msg">Ação realizada com sucesso</span>
    </div>

    <!-- SCRIPT DE INTERATIVIDADE -->
    <script>
        const games = [
            { title: "Celeste", category: "Plataforma / Indie", color: "from-[#080B12] via-[#0D1322] to-[#0A0E1A]", active: true },
            { title: "Hollow Knight", category: "Metroidvania", color: "from-[#0A0D14] via-[#0E1420] to-[#090C12]", active: false },
            { title: "Stardew Valley", category: "RPG / Simulação", color: "from-[#07100D] via-[#0D1F1A] to-[#060D0B]", active: false },
            { title: "Cuphead", category: "Run and Gun", color: "from-[#120B07] via-[#20130C] to-[#0E0905]", active: false },
            { title: "Sonic Mania", category: "Plataforma", color: "from-[#070D14] via-[#0D192B] to-[#060A10]", active: false },
            { title: "Dead Cells", category: "Roguelike", color: "from-[#120707] via-[#210D0D] to-[#0D0505]", active: false },
            { title: "Ori and the Blind Forest", category: "Aventura", color: "from-[#07100E] via-[#0D1F1C] to-[#060D0B]", active: false },
            { title: "Hades", category: "Action Roguelike", color: "from-[#140606] via-[#260C0C] to-[#0F0404]", active: false },
            { title: "Blasphemous", category: "Metroidvania", color: "from-[#0D0D0D] via-[#171717] to-[#0A0A0A]", active: false },
            { title: "Undertale", category: "RPG", color: "from-[#0E0714] via-[#1A0D2B] to-[#0B0510]", active: false },
            { title: "Katana Zero", category: "Ação Rápida", color: "from-[#140D07] via-[#261B0E] to-[#0F0A05]", active: false },
            { title: "Cyber Protocol X", category: "Port Nativo USB", color: "from-[#14080D] via-[#26101B] to-[#0F060A]", active: false }
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
                card.className = `game-card glass-panel p-4 rounded-2xl flex flex-col justify-between h-36 cursor-pointer relative overflow-hidden bg-gradient-to-br ${game.color} ${isSelected ? 'border-2 border-brand-accent shadow-xl shadow-brand-accent/20 scale-[1.02]' : 'border-white/5 opacity-80 hover:opacity-100'}`;
                card.onclick = () => {
                    selectedGameIndex = index;
                    renderGames();
                    showToast(`Selecionado: ${game.title}`);
                };
                card.innerHTML = `
                    <div class="flex items-center justify-between z-10">
                        <span class="text-[10px] uppercase font-bold tracking-wider px-2 py-0.5 rounded-full bg-black/60 text-slate-400 border border-white/5">${game.category}</span>
                        ${isSelected ? '<span class="w-2 h-2 rounded-full bg-brand-accent animate-ping"></span>' : ''}
                    </div>
                    <div class="z-10 mt-auto">
                        <h3 class="text-base font-bold text-white leading-tight">${game.title}</h3>
                        <p class="text-[11px] text-slate-500 mt-0.5">Pronto para iniciar</p>
                    </div>
                    <div class="absolute -right-4 -bottom-4 w-24 h-24 bg-white/[0.02] rounded-full blur-xl pointer-events-none"></div>
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
                    btn.className = "tab-btn px-4 py-1.5 rounded-lg text-sm font-medium transition-all text-white bg-white/[0.07] border border-white/10 shadow-sm";
                } else {
                    view.classList.add('opacity-0');
                    setTimeout(() => view.classList.add('hidden'), 300);
                    btn.className = "tab-btn px-4 py-1.5 rounded-lg text-sm font-medium transition-all text-slate-500 hover:text-white";
                }
            });

            const hintA = document.getElementById('hint-a');
            const hintB = document.getElementById('hint-b');
            if (tabName === 'music') {
                hintA.innerText = "A reproduzir / pausar";
                hintB.innerText = "B voltar";
            } else if (tabName === 'library') {
                hintA.innerText = "A jogar";
                hintB.innerText = "B voltar";
            } else {
                hintA.innerText = "A selecionar";
                hintB.innerText = "B fechar";
            }
        }

        function togglePlay() {
            isPlaying = !isPlaying;
            document.getElementById('btn-play-text').innerText = isPlaying ? "Pausar" : "Reproduzir";
            showToast(isPlaying ? "Música reproduzindo" : "Música pausada");
        }

        function nextSong() {
            showToast("Próxima faixa carregada");
        }

        function toggleWifi() {
            wifiEnabled = !wifiEnabled;
            const wifiIcon = document.getElementById('wifi-icon');
            const wifiStatus = document.getElementById('wifi-status');
            
            if (wifiEnabled) {
                wifiIcon.className = "fa-solid fa-wifi text-emerald-400";
                wifiStatus.innerText = "Wi-Fi On";
                showToast("Wi-Fi conectado com sucesso");
            } else {
                wifiIcon.className = "fa-solid fa-wifi-slash text-rose-500";
                wifiStatus.innerText = "Offline";
                showToast("Wi-Fi desconectado");
            }
        }

        function prevSong() {
            showToast("Faixa anterior");
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
            document.getElementById('queue-list').innerHTML = '<div class="text-xs text-slate-600 py-2 text-center">Nenhuma faixa na fila</div>';
            document.getElementById('queue-count').innerText = '0';
            showToast("Fila de reprodução limpa");
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
                showToast(enabled ? "Espectro de áudio ativado" : "Espectro desativado");
            } else if (feature === 'cover') {
                const cover = document.getElementById('album-cover-container');
                cover.style.transform = enabled ? 'scale(1)' : 'scale(0.9)';
                showToast(enabled ? "Capa de álbum expandida" : "Capa compacta");
            } else if (feature === 'grid') {
                showToast(enabled ? "Grid dinâmico ativado" : "Grid padrão");
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
