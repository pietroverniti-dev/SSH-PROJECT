/*
 * ================================================================
 *  STAZIONE METEO + CONTROLLO VENTOLA  —  Web Dashboard v2
 *  Hardware : ESP32 | BMP280 | AHT20 | SSD1306 | L298N (x2)
 *  Librerie : Adafruit_SSD1306, Adafruit_BMP280, Adafruit_AHTX0,
 *             WiFi, WebServer (built-in ESP32 Arduino core)
 * ================================================================
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_AHTX0.h>
#include <WiFi.h>
#include <WebServer.h>

// ===================== CREDENZIALI WIFI =====================
#define WIFI_SSID "sr1777"
#define WIFI_PASS "milanmilan"

// ===================== PIN I2C =====================
#define SDA_PIN 17
#define SCL_PIN 16

// ===================== OLED =====================
#define OLED_RST -1
#define ROW 64
#define COL 128
Adafruit_SSD1306 display(COL, ROW, &Wire, OLED_RST);

// ===================== SENSORI =====================
Adafruit_BMP280 bmp;
Adafruit_AHTX0  aht;

// ===================== TIMER =====================
hw_timer_t *timer0 = NULL;
unsigned long lastMs = 0;
const unsigned int period = 1000;

bool  useBmp = true;
float tBmp, pBmp, tAht, hAht;
unsigned long totSec;
unsigned int  minutes, seconds;

// ===================== MOTORI (L298N) =====================
#define ENA 13
#define IN1 12
#define IN2 14
#define ENB 25
#define IN3 27
#define IN4 26

#define BUTTON_PIN  0
#define LED_PIN    33
#define DEBOUNCE_MS 50
#define CW  1
#define CCW 0

const int PWM_CHANNEL_A   = 0;
const int PWM_CHANNEL_B   = 1;
const int PWM_FREQ        = 20000;
const int PWM_RES         = 8;
const int desiredSpeedPct = 100;

bool motorOn   = false;
bool autoMode  = true;
int  direction = CW;

unsigned long lastDebounceTime = 0;
int  lastButtonState = HIGH;
int  buttonState     = HIGH;
unsigned long manualTimer = 0;

// ===================== SOGLIE AUTO =====================
#define HUM_THRESHOLD  70
#define TEMP_THRESHOLD 28

// ===================== STORICO (buffer circolare) =====================
#define HISTORY_SIZE 120  // 2 minuti a 1 campione/s

struct History {
  float tBmp [HISTORY_SIZE] = {};
  float pBmp [HISTORY_SIZE] = {};
  float tAht [HISTORY_SIZE] = {};
  float hAht [HISTORY_SIZE] = {};
  uint8_t fan[HISTORY_SIZE] = {};
  unsigned long ts[HISTORY_SIZE] = {};
  int head  = 0;
  int count = 0;
} hist;

void histPush(float tb, float pb, float ta, float ha, bool fan) {
  hist.tBmp[hist.head] = tb;
  hist.pBmp[hist.head] = pb;
  hist.tAht[hist.head] = ta;
  hist.hAht[hist.head] = ha;
  hist.fan [hist.head] = fan ? 1 : 0;
  hist.ts  [hist.head] = totSec;
  hist.head = (hist.head + 1) % HISTORY_SIZE;
  if (hist.count < HISTORY_SIZE) hist.count++;
}

// ===================== WEB SERVER =====================
WebServer server(80);

// ----------------------------------------------------------------
//  HTML DASHBOARD  (PROGMEM)
// ----------------------------------------------------------------
static const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="it">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 · Stazione Meteo Pro</title>
    
    <!-- Librerie Esterne -->
    <script src="https://cdn.tailwindcss.com"></script>
    <script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.0/dist/chart.umd.min.js"></script>
    <script src="https://unpkg.com/lucide@latest"></script>

    <!-- Configurazione Tailwind personalizzata -->
    <script>
        tailwind.config = {
            theme: {
                extend: {
                    fontFamily: {
                        sans: ['Inter', '-apple-system', 'BlinkMacSystemFont', 'Segoe UI', 'Roboto', 'sans-serif'],
                    },
                    colors: {
                        slate: {
                            850: '#151e2e',
                        }
                    }
                }
            }
        }
    </script>

    <style>
        @import url('https://fonts.googleapis.com/css2?family=Inter:wght@300;400;500;600;700&display=swap');
        
        body {
            background-color: #f8fafc; /* slate-50 */
            -webkit-font-smoothing: antialiased;
            -moz-osx-font-smoothing: grayscale;
        }

        /* Animazione per il dot di stato */
        @keyframes pulse-ring {
            0% { transform: scale(0.8); box-shadow: 0 0 0 0 rgba(16, 185, 129, 0.7); }
            70% { transform: scale(1); box-shadow: 0 0 0 6px rgba(16, 185, 129, 0); }
            100% { transform: scale(0.8); box-shadow: 0 0 0 0 rgba(16, 185, 129, 0); }
        }
        
        @keyframes pulse-ring-dead {
            0% { transform: scale(0.8); box-shadow: 0 0 0 0 rgba(239, 68, 68, 0.7); }
            70% { transform: scale(1); box-shadow: 0 0 0 6px rgba(239, 68, 68, 0); }
            100% { transform: scale(0.8); box-shadow: 0 0 0 0 rgba(239, 68, 68, 0); }
        }

        .status-dot.live { animation: pulse-ring 2s infinite; background-color: #10b981; }
        .status-dot.dead { animation: pulse-ring-dead 2s infinite; background-color: #ef4444; }

        /* Stile personalizzato per le scrollbar */
        ::-webkit-scrollbar { width: 8px; height: 8px; }
        ::-webkit-scrollbar-track { background: transparent; }
        ::-webkit-scrollbar-thumb { background: #cbd5e1; border-radius: 4px; }
        ::-webkit-scrollbar-thumb:hover { background: #94a3b8; }
        
        .glass-card {
            background: rgba(255, 255, 255, 0.95);
            backdrop-filter: blur(10px);
            border: 1px solid rgba(226, 232, 240, 0.8);
            box-shadow: 0 4px 6px -1px rgba(0, 0, 0, 0.05), 0 2px 4px -1px rgba(0, 0, 0, 0.03);
        }
    </style>
</head>
<body class="text-slate-800 flex flex-col min-h-screen">

    <!-- HEADER -->
    <header class="bg-white border-b border-slate-200 sticky top-0 z-50 shadow-sm">
        <div class="max-w-7xl mx-auto px-4 sm:px-6 lg:px-8 h-16 flex items-center justify-between">
            <div class="flex items-center gap-4">
                <div class="bg-blue-500 p-2 rounded-lg text-white shadow-sm">
                    <i data-lucide="cloud-sun" class="w-6 h-6"></i>
                </div>
                <div>
                    <h1 class="text-lg font-semibold tracking-tight text-slate-900 leading-tight">Meteo ESP32</h1>
                    <p class="text-xs text-slate-500 font-medium">BMP280 · AHT20 · L298N</p>
                </div>
            </div>
            
            <div class="flex items-center gap-6">
                <div class="hidden sm:flex flex-col items-end">
                    <span class="text-xs text-slate-400 font-medium uppercase tracking-wider">Uptime</span>
                    <span class="text-sm font-semibold text-slate-700 font-mono" id="uptime-val">--:--</span>
                </div>
                <div class="h-8 w-px bg-slate-200 hidden sm:block"></div>
                <div class="flex items-center gap-2 bg-slate-50 px-3 py-1.5 rounded-full border border-slate-200">
                    <div id="livedot" class="status-dot live w-2.5 h-2.5 rounded-full"></div>
                    <span id="conn-status" class="text-xs font-medium text-slate-600">Connesso</span>
                </div>
            </div>
        </div>
    </header>

    <!-- MAIN CONTENT -->
    <main class="flex-grow max-w-7xl mx-auto px-4 sm:px-6 lg:px-8 py-8 w-full space-y-6">
        
        <!-- KPI CARDS GRID -->
        <div class="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-4 gap-4">
            
            <!-- Temp BMP280 -->
            <div class="glass-card rounded-2xl p-5 flex flex-col transition duration-300 hover:shadow-md">
                <div class="flex justify-between items-start mb-2">
                    <div class="text-sm font-medium text-slate-500">Temp. BMP280</div>
                    <div class="bg-blue-50 p-1.5 rounded-md text-blue-500">
                        <i data-lucide="thermometer" class="w-5 h-5"></i>
                    </div>
                </div>
                <div class="flex items-baseline gap-1 mt-1">
                    <span class="text-3xl font-bold text-slate-800 tracking-tight" id="k-tbmp">--.-</span>
                    <span class="text-lg font-semibold text-slate-500">°C</span>
                </div>
            </div>

            <!-- Temp AHT20 -->
            <div class="glass-card rounded-2xl p-5 flex flex-col transition duration-300 hover:shadow-md">
                <div class="flex justify-between items-start mb-2">
                    <div class="text-sm font-medium text-slate-500">Temp. AHT20</div>
                    <div class="bg-orange-50 p-1.5 rounded-md text-orange-500">
                        <i data-lucide="thermometer-sun" class="w-5 h-5"></i>
                    </div>
                </div>
                <div class="flex items-baseline gap-1 mt-1">
                    <span class="text-3xl font-bold text-slate-800 tracking-tight" id="k-taht">--.-</span>
                    <span class="text-lg font-semibold text-slate-500">°C</span>
                </div>
            </div>

            <!-- Umidità -->
            <div class="glass-card rounded-2xl p-5 flex flex-col transition duration-300 hover:shadow-md">
                <div class="flex justify-between items-start mb-2">
                    <div class="text-sm font-medium text-slate-500">Umidità Relativa</div>
                    <div class="bg-teal-50 p-1.5 rounded-md text-teal-500">
                        <i data-lucide="droplets" class="w-5 h-5"></i>
                    </div>
                </div>
                <div class="flex items-baseline gap-1 mt-1">
                    <span class="text-3xl font-bold text-slate-800 tracking-tight" id="k-haht">--.-</span>
                    <span class="text-lg font-semibold text-slate-500">%</span>
                </div>
            </div>

            <!-- Pressione -->
            <div class="glass-card rounded-2xl p-5 flex flex-col transition duration-300 hover:shadow-md relative overflow-hidden">
                <div class="flex justify-between items-start mb-2 relative z-10">
                    <div class="text-sm font-medium text-slate-500">Pressione Atm.</div>
                    <div class="bg-indigo-50 p-1.5 rounded-md text-indigo-500">
                        <i data-lucide="gauge" class="w-5 h-5"></i>
                    </div>
                </div>
                <div class="flex items-baseline gap-1 mt-1 relative z-10">
                    <span class="text-3xl font-bold text-slate-800 tracking-tight" id="k-pbmp">----</span>
                    <span class="text-lg font-semibold text-slate-500">hPa</span>
                </div>
                <div class="mt-3 flex items-center gap-1 text-xs font-medium relative z-10" id="press-trend-container">
                    <i data-lucide="minus" class="w-4 h-4 text-slate-400" id="press-trend-icon"></i>
                    <span class="text-slate-500" id="press-trend">Rilevamento in corso...</span>
                </div>
            </div>

        </div>

        <!-- CONTROL PANEL & FAN STATUS -->
        <div class="grid grid-cols-1 lg:grid-cols-3 gap-6">
            
            <!-- Fan Control Panel -->
            <div class="lg:col-span-2 glass-card rounded-2xl p-6">
                <h2 class="text-base font-semibold text-slate-800 mb-4 flex items-center gap-2">
                    <i data-lucide="fan" class="w-5 h-5 text-slate-400"></i>
                    Stato Sistema di Ventilazione
                </h2>
                
                <div class="flex flex-col sm:flex-row items-start sm:items-center justify-between gap-6 bg-slate-50 rounded-xl p-5 border border-slate-100">
                    
                    <div class="flex items-center gap-5">
                        <!-- Icona ventola animata -->
                        <div id="fan-icon-bg" class="w-14 h-14 rounded-full flex items-center justify-center bg-rose-100 text-rose-500 transition-colors duration-300">
                            <i data-lucide="fan" id="fan-icon" class="w-8 h-8 transition-transform duration-75"></i>
                        </div>
                        <div>
                            <div class="text-sm text-slate-500 font-medium mb-1">Stato Attuale</div>
                            <div class="flex items-center gap-2">
                                <span id="fan-badge" class="px-3 py-1 rounded-full text-sm font-bold bg-rose-100 text-rose-700 border border-rose-200">
                                    SPENTA
                                </span>
                            </div>
                        </div>
                    </div>

                    <div class="w-full sm:w-px sm:h-12 bg-slate-200"></div>

                    <div class="flex flex-wrap gap-4 flex-grow">
                        <div class="flex-1 min-w-[100px]">
                            <div class="text-xs text-slate-500 mb-1">Modalità</div>
                            <span id="fan-mode-badge" class="inline-flex items-center gap-1 px-2.5 py-1 rounded-md text-xs font-semibold bg-emerald-50 text-emerald-700 border border-emerald-200 uppercase">
                                <i data-lucide="cpu" class="w-3 h-3"></i> Automatico
                            </span>
                        </div>
                        <div class="flex-1 min-w-[100px]">
                            <div class="text-xs text-slate-500 mb-1">Direzione</div>
                            <div id="fan-dir" class="text-sm font-medium text-slate-700">—</div>
                        </div>
                        <div class="flex-1 min-w-[100px]">
                            <div class="text-xs text-slate-500 mb-1">Potenza</div>
                            <div id="fan-speed" class="text-sm font-medium text-slate-700">—</div>
                        </div>
                    </div>
                </div>
            </div>

            <!-- Thresholds Info -->
            <div class="glass-card rounded-2xl p-6 bg-gradient-to-br from-slate-800 to-slate-900 text-white relative overflow-hidden">
                <!-- Decorazione di sfondo -->
                <i data-lucide="sliders-horizontal" class="absolute -right-4 -bottom-4 w-32 h-32 text-white opacity-5"></i>
                
                <h2 class="text-base font-semibold mb-4 text-slate-100 flex items-center gap-2">
                    <i data-lucide="settings-2" class="w-5 h-5 text-slate-400"></i>
                    Soglie di Attivazione
                </h2>
                
                <div class="space-y-4 relative z-10">
                    <div class="bg-slate-800/50 rounded-xl p-4 border border-slate-700">
                        <div class="flex justify-between items-center mb-1">
                            <span class="text-sm text-slate-400">Temperatura Massima</span>
                            <span class="text-lg font-bold text-orange-400">28.0 °C</span>
                        </div>
                        <div class="w-full bg-slate-700 rounded-full h-1.5 mt-2">
                            <div class="bg-orange-400 h-1.5 rounded-full" style="width: 75%"></div>
                        </div>
                    </div>
                    
                    <div class="bg-slate-800/50 rounded-xl p-4 border border-slate-700">
                        <div class="flex justify-between items-center mb-1">
                            <span class="text-sm text-slate-400">Umidità Massima</span>
                            <span class="text-lg font-bold text-teal-400">70.0 %</span>
                        </div>
                        <div class="w-full bg-slate-700 rounded-full h-1.5 mt-2">
                            <div class="bg-teal-400 h-1.5 rounded-full" style="width: 65%"></div>
                        </div>
                    </div>
                </div>
            </div>

        </div>

        <!-- CHARTS SECTION -->
        <div class="grid grid-cols-1 xl:grid-cols-2 gap-6">
            
            <!-- Grafico Temperatura -->
            <div class="glass-card rounded-2xl p-5">
                <div class="flex flex-col sm:flex-row justify-between items-start sm:items-center mb-4 gap-4">
                    <div>
                        <h3 class="text-base font-semibold text-slate-800">Andamento Temperature</h3>
                        <p class="text-xs text-slate-500 mt-1">Ultimi <span id="temp-n" class="font-medium text-slate-700">0</span> campioni</p>
                    </div>
                    <div class="flex gap-3 text-xs font-medium">
                        <div class="flex items-center gap-1.5 bg-slate-50 px-2 py-1 rounded border border-slate-100">
                            <div class="w-2.5 h-2.5 rounded-full bg-blue-500"></div> BMP280
                        </div>
                        <div class="flex items-center gap-1.5 bg-slate-50 px-2 py-1 rounded border border-slate-100">
                            <div class="w-2.5 h-2.5 rounded-full bg-orange-500"></div> AHT20
                        </div>
                    </div>
                </div>
                <div class="relative h-[280px] w-full">
                    <canvas id="chartTemp"></canvas>
                </div>
            </div>

            <!-- Grafico Umidità -->
            <div class="glass-card rounded-2xl p-5">
                <div class="flex flex-col sm:flex-row justify-between items-start sm:items-center mb-4 gap-4">
                    <div>
                        <h3 class="text-base font-semibold text-slate-800">Umidità Relativa</h3>
                        <p class="text-xs text-slate-500 mt-1">Ultimi <span id="hum-n" class="font-medium text-slate-700">0</span> campioni</p>
                    </div>
                    <div class="flex gap-3 text-xs font-medium">
                        <div class="flex items-center gap-1.5 bg-slate-50 px-2 py-1 rounded border border-slate-100">
                            <div class="w-2.5 h-2.5 rounded-full bg-teal-500"></div> AHT20
                        </div>
                        <div class="flex items-center gap-1.5 bg-slate-50 px-2 py-1 rounded border border-slate-100">
                            <div class="w-4 h-0.5 border-t-2 border-dashed border-rose-500"></div> Soglia (70%)
                        </div>
                    </div>
                </div>
                <div class="relative h-[280px] w-full">
                    <canvas id="chartHum"></canvas>
                </div>
            </div>

        </div>
    </main>

    <footer class="bg-white border-t border-slate-200 py-6 text-center">
        <p class="text-sm text-slate-500">Sistema di monitoraggio basato su ESP32 e Sensori I2C</p>
    </footer>

    <script>
        // Inizializza le icone Lucide
        lucide.createIcons();

        /* --- CONFIGURAZIONE GRAFICI CHART.JS --- */
        const FONT_FAMILY = "'Inter', sans-serif";
        Chart.defaults.font.family = FONT_FAMILY;
        Chart.defaults.color = '#64748b'; // slate-500
        Chart.defaults.scale.grid.color = '#f1f5f9'; // slate-100

        const createGradient = (ctx, colorStart, colorEnd) => {
            const gradient = ctx.createLinearGradient(0, 0, 0, 300);
            gradient.addColorStop(0, colorStart);
            gradient.addColorStop(1, colorEnd);
            return gradient;
        };

        const baseOpts = (yLabel, min, max) => ({
            responsive: true,
            maintainAspectRatio: false,
            animation: { duration: 400, easing: 'easeOutQuart' },
            interaction: { mode: 'index', intersect: false },
            plugins: {
                legend: { display: false },
                tooltip: {
                    backgroundColor: 'rgba(255, 255, 255, 0.95)',
                    titleColor: '#0f172a',
                    bodyColor: '#334155',
                    borderColor: '#e2e8f0',
                    borderWidth: 1,
                    padding: 12,
                    boxPadding: 6,
                    usePointStyle: true,
                    titleFont: { family: FONT_FAMILY, size: 13, weight: '600' },
                    bodyFont: { family: FONT_FAMILY, size: 12 },
                    callbacks: { title: (items) => `Uptime: ${items[0].label}` }
                }
            },
            scales: {
                x: {
                    grid: { display: false, drawBorder: false },
                    ticks: { maxTicksLimit: 7, maxRotation: 0, font: { size: 11 } }
                },
                y: {
                    border: { display: false },
                    grid: { color: '#f1f5f9', drawTicks: false },
                    ticks: { padding: 10, font: { size: 11 } },
                    title: { display: !!yLabel, text: yLabel, font: { size: 12, weight: '500' } },
                    min: min,
                    max: max
                }
            }
        });

        // GRAFICO TEMPERATURA
        const ctxT = document.getElementById('chartTemp').getContext('2d');
        const chartTemp = new Chart(ctxT, {
            type: 'line',
            data: {
                labels: [],
                datasets: [
                    {
                        label: 'BMP280 (°C)',
                        data: [],
                        borderColor: '#3b82f6', // blue-500
                        backgroundColor: createGradient(ctxT, 'rgba(59, 130, 246, 0.2)', 'rgba(59, 130, 246, 0)'),
                        borderWidth: 2.5,
                        pointRadius: 0,
                        pointHoverRadius: 5,
                        pointHoverBackgroundColor: '#3b82f6',
                        fill: true,
                        tension: 0.4
                    },
                    {
                        label: 'AHT20 (°C)',
                        data: [],
                        borderColor: '#f97316', // orange-500
                        backgroundColor: 'transparent',
                        borderWidth: 2.5,
                        borderDash: [5, 5],
                        pointRadius: 0,
                        pointHoverRadius: 5,
                        pointHoverBackgroundColor: '#f97316',
                        fill: false,
                        tension: 0.4
                    }
                ]
            },
            options: baseOpts('Temperatura (°C)')
        });

        // GRAFICO UMIDITÀ
        const ctxH = document.getElementById('chartHum').getContext('2d');
        const chartHum = new Chart(ctxH, {
            type: 'line',
            data: {
                labels: [],
                datasets: [
                    {
                        label: 'AHT20 (%)',
                        data: [],
                        borderColor: '#14b8a6', // teal-500
                        backgroundColor: createGradient(ctxH, 'rgba(20, 184, 166, 0.2)', 'rgba(20, 184, 166, 0)'),
                        borderWidth: 2.5,
                        pointRadius: 0,
                        pointHoverRadius: 5,
                        pointHoverBackgroundColor: '#14b8a6',
                        fill: true,
                        tension: 0.4
                    },
                    {
                        label: 'Soglia Limite',
                        data: [],
                        borderColor: '#f43f5e', // rose-500
                        borderWidth: 2,
                        borderDash: [6, 4],
                        pointRadius: 0,
                        fill: false
                    }
                ]
            },
            options: baseOpts('Umidità (%)', 20, 100)
        });

        /* --- LOGICA DI AGGIORNAMENTO DATI --- */
        function pad(n) { return String(n).padStart(2, '0'); }
        function fmtT(s) {
            const h = Math.floor(s / 3600);
            const m = Math.floor((s % 3600) / 60);
            const sec = s % 60;
            return h > 0 ? `${pad(h)}:${pad(m)}:${pad(sec)}` : `${pad(m)}:${pad(sec)}`;
        }

        let prevPressure = null;
        let fanRotationDegree = 0;
        let fanInterval = null;

        // Anima l'icona della ventola quando è accesa
        function updateFanAnimation(isOn) {
            const icon = document.getElementById('fan-icon');
            if (isOn) {
                if (!fanInterval) {
                    fanInterval = setInterval(() => {
                        fanRotationDegree = (fanRotationDegree + 15) % 360;
                        icon.style.transform = `rotate(${fanRotationDegree}deg)`;
                    }, 50);
                }
            } else {
                clearInterval(fanInterval);
                fanInterval = null;
            }
        }

        function updateUI(d) {
            // Aggiorna KPI Testuali
            document.getElementById('k-tbmp').textContent = d.tBmp.toFixed(1);
            document.getElementById('k-taht').textContent = d.tAht.toFixed(1);
            document.getElementById('k-haht').textContent = d.hAht.toFixed(1);
            document.getElementById('k-pbmp').textContent = d.pBmp.toFixed(1);
            document.getElementById('uptime-val').textContent = fmtT(d.uptime);

            // Trend Pressione
            if (prevPressure !== null) {
                const diff = d.pBmp - prevPressure;
                const trendEl = document.getElementById('press-trend');
                const iconEl = document.getElementById('press-trend-icon');
                const containerEl = document.getElementById('press-trend-container');
                
                if (diff > 0.5) {
                    trendEl.textContent = 'In rapido aumento';
                    iconEl.setAttribute('data-lucide', 'trending-up');
                    containerEl.className = 'mt-3 flex items-center gap-1 text-xs font-semibold text-emerald-600 relative z-10';
                } else if (diff > 0.1) {
                    trendEl.textContent = 'Lieve aumento';
                    iconEl.setAttribute('data-lucide', 'arrow-up-right');
                    containerEl.className = 'mt-3 flex items-center gap-1 text-xs font-medium text-emerald-500 relative z-10';
                } else if (diff < -0.5) {
                    trendEl.textContent = 'In rapido calo';
                    iconEl.setAttribute('data-lucide', 'trending-down');
                    containerEl.className = 'mt-3 flex items-center gap-1 text-xs font-semibold text-rose-600 relative z-10';
                } else if (diff < -0.1) {
                    trendEl.textContent = 'Lieve calo';
                    iconEl.setAttribute('data-lucide', 'arrow-down-right');
                    containerEl.className = 'mt-3 flex items-center gap-1 text-xs font-medium text-rose-500 relative z-10';
                } else {
                    trendEl.textContent = 'Stabile';
                    iconEl.setAttribute('data-lucide', 'minus');
                    containerEl.className = 'mt-3 flex items-center gap-1 text-xs font-medium text-slate-500 relative z-10';
                }
                lucide.createIcons(); // Ricrea l'icona cambiata
            }
            prevPressure = d.pBmp;

            // Stato Ventola
            const on = d.fanOn;
            const badge = document.getElementById('fan-badge');
            const iconBg = document.getElementById('fan-icon-bg');
            
            if (on) {
                badge.textContent = 'IN FUNZIONE';
                badge.className = 'px-3 py-1 rounded-full text-sm font-bold bg-emerald-100 text-emerald-700 border border-emerald-200 shadow-sm';
                iconBg.className = 'w-14 h-14 rounded-full flex items-center justify-center bg-emerald-100 text-emerald-600 transition-colors duration-300 shadow-inner';
            } else {
                badge.textContent = 'SPENTA';
                badge.className = 'px-3 py-1 rounded-full text-sm font-bold bg-rose-100 text-rose-700 border border-rose-200';
                iconBg.className = 'w-14 h-14 rounded-full flex items-center justify-center bg-rose-50 text-rose-400 transition-colors duration-300';
            }
            
            updateFanAnimation(on);

            document.getElementById('fan-dir').textContent = on ? (d.direction === 1 ? 'Orario (Estrazione)' : 'Antiorario (Immissione)') : '—';
            document.getElementById('fan-speed').textContent = on ? '100% (MAX)' : '0%';

            // Aggiornamento Grafici
            const n = d.history.ts.length;
            const lbl = d.history.ts.map(fmtT);

            document.getElementById('temp-n').textContent = n;
            document.getElementById('hum-n').textContent = n;

            chartTemp.data.labels = lbl;
            chartTemp.data.datasets[0].data = d.history.tBmp;
            chartTemp.data.datasets[1].data = d.history.tAht;
            chartTemp.update('none');

            chartHum.data.labels = lbl;
            chartHum.data.datasets[0].data = d.history.hAht;
            chartHum.data.datasets[1].data = new Array(n).fill(70);
            chartHum.update('none');

            // Stato connessione UI
            document.getElementById('livedot').className = 'status-dot live w-2.5 h-2.5 rounded-full';
            document.getElementById('conn-status').textContent = 'Connesso';
            document.getElementById('conn-status').className = 'text-xs font-medium text-emerald-600';
        }

        function setOfflineState() {
            document.getElementById('livedot').className = 'status-dot dead w-2.5 h-2.5 rounded-full';
            document.getElementById('conn-status').textContent = 'Disconnesso / Mock';
            document.getElementById('conn-status').className = 'text-xs font-medium text-rose-600';
        }

        /* --- GENERATORE DI DATI SIMULATI (MOCK) --- 
           Utile per testare l'interfaccia senza avere l'ESP32 fisicamente collegato.
        */
        let mockData = {
            uptime: 3500,
            tBmp: 26.5, tAht: 26.3, hAht: 68.0, pBmp: 1013.2,
            fanOn: false, autoMode: true, direction: 1,
            history: { ts: [], tBmp: [], tAht: [], hAht: [] }
        };

        // Popola la storia iniziale del mock
        for(let i=60; i>=0; i--) {
            mockData.history.ts.push(mockData.uptime - i);
            mockData.history.tBmp.push(26.5 + Math.sin(i/5)*0.5);
            mockData.history.tAht.push(26.3 + Math.sin(i/5)*0.4);
            mockData.history.hAht.push(68.0 + Math.cos(i/5)*2);
        }

        function generateMockData() {
            mockData.uptime += 2; // +2 secondi
            
            // Simula fluttuazioni
            mockData.tBmp += (Math.random() - 0.5) * 0.2;
            mockData.tAht += (Math.random() - 0.5) * 0.2;
            mockData.hAht += (Math.random() - 0.5) * 1.5;
            mockData.pBmp += (Math.random() - 0.5) * 0.3;

            // Logica ventola
            mockData.fanOn = (mockData.tBmp > 28 || mockData.hAht > 70);

            // Aggiorna array storia (mantieni ultimi 60 campioni)
            mockData.history.ts.push(mockData.uptime);
            mockData.history.tBmp.push(mockData.tBmp);
            mockData.history.tAht.push(mockData.tAht);
            mockData.history.hAht.push(mockData.hAht);
            
            if(mockData.history.ts.length > 60) {
                mockData.history.ts.shift();
                mockData.history.tBmp.shift();
                mockData.history.tAht.shift();
                mockData.history.hAht.shift();
            }

            return JSON.parse(JSON.stringify(mockData)); // Ritorna una copia
        }

        /* --- FETCH PRINCIPALE --- */
        async function fetchData() {
            try {
                // Tenta la connessione reale all'ESP32
                const r = await fetch('/data');
                if (!r.ok) throw new Error('Network response was not ok');
                const d = await r.json();
                updateUI(d);
            } catch(e) {
                // FALLBACK: Se fallisce (es. se aperto in locale o qui nell'anteprima), usa i mock data
                setOfflineState();
                const fakeData = generateMockData();
                updateUI(fakeData);
            }
        }

        // Avvio
        fetchData();
        setInterval(fetchData, 2000); // Aggiornamento ogni 2 secondi

    </script>
</body>
</html>
)rawliteral";

// ----------------------------------------------------------------
//  ENDPOINT  /data  →  JSON
// ----------------------------------------------------------------
void handleData() {
  String json = "{";

  json += "\"tBmp\":"    + String(tBmp, 2)       + ",";
  json += "\"pBmp\":"    + String(pBmp, 2)       + ",";
  json += "\"tAht\":"    + String(tAht, 2)       + ",";
  json += "\"hAht\":"    + String(hAht, 2)       + ",";
  json += "\"fanOn\":"   + String(motorOn  ? "true" : "false") + ",";
  json += "\"autoMode\":" + String(autoMode ? "true" : "false") + ",";
  json += "\"direction\":" + String(direction)   + ",";
  json += "\"uptime\":"  + String(totSec)        + ",";

  int n     = hist.count;
  int start = (hist.count < HISTORY_SIZE) ? 0 : hist.head;

  // Macro-like helper: calcola l'indice reale nel buffer circolare
  #define HIDX(i) ((start + (i)) % HISTORY_SIZE)

  json += "\"history\":{";

  // ts
  json += "\"ts\":[";
  for (int i = 0; i < n; i++) { if (i) json += ","; json += String(hist.ts[HIDX(i)]); }
  json += "],";

  // tBmp
  json += "\"tBmp\":[";
  for (int i = 0; i < n; i++) { if (i) json += ","; json += String(hist.tBmp[HIDX(i)], 1); }
  json += "],";

  // pBmp
  json += "\"pBmp\":[";
  for (int i = 0; i < n; i++) { if (i) json += ","; json += String(hist.pBmp[HIDX(i)], 1); }
  json += "],";

  // tAht
  json += "\"tAht\":[";
  for (int i = 0; i < n; i++) { if (i) json += ","; json += String(hist.tAht[HIDX(i)], 1); }
  json += "],";

  // hAht
  json += "\"hAht\":[";
  for (int i = 0; i < n; i++) { if (i) json += ","; json += String(hist.hAht[HIDX(i)], 1); }
  json += "],";

  // fan
  json += "\"fan\":[";
  for (int i = 0; i < n; i++) { if (i) json += ","; json += String((int)hist.fan[HIDX(i)]); }
  json += "]";

  #undef HIDX

  json += "}}";

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Cache-Control", "no-cache");
  server.send(200, "application/json", json);
}

void handleRoot() {
  server.send_P(200, "text/html", HTML_PAGE);
}

// ===================== CLASSE MOTORE =====================
class Robojax_L298N_DC_motor {
public:
  Robojax_L298N_DC_motor(int in1, int in2, int ena, int ch)
    : _in1(in1), _in2(in2), _ena(ena), _ch(ch) {}

  void begin() {
    pinMode(_in1, OUTPUT);
    pinMode(_in2, OUTPUT);
    pinMode(_ena, OUTPUT);
    ledcSetup(_ch, PWM_FREQ, PWM_RES);
    ledcAttachPin(_ena, _ch);
    coast();
  }

  void rotate(int /*id*/, int pct, int dir) {
    uint8_t duty = map(pct, 0, 100, 0, 255);
    digitalWrite(_in1, dir == CW ? HIGH : LOW);
    digitalWrite(_in2, dir == CW ? LOW  : HIGH);
    ledcWrite(_ch, duty);
  }

  void coast() {
    ledcWrite(_ch, 0);
    digitalWrite(_in1, LOW);
    digitalWrite(_in2, LOW);
  }

private:
  int _in1, _in2, _ena, _ch;
};

Robojax_L298N_DC_motor motorA(IN1, IN2, ENA, PWM_CHANNEL_A);
Robojax_L298N_DC_motor motorB(IN3, IN4, ENB, PWM_CHANNEL_B);

// ===================== CONTROLLO AUTOMATICO =====================
void checkAutoControl() {
  if (!autoMode) return;

  sensors_event_t hum, temp;
  aht.getEvent(&hum, &temp);
  float h = hum.relative_humidity;
  float t = temp.temperature;
  if (isnan(h) || isnan(t)) return;

  if (h > HUM_THRESHOLD) {
    motorOn = true; direction = CCW;
    motorA.rotate(1, desiredSpeedPct, direction);
    motorB.rotate(1, desiredSpeedPct, direction);
    digitalWrite(LED_PIN, HIGH);
    return;
  }
  if (t > TEMP_THRESHOLD) {
    motorOn = true; direction = CW;
    motorA.rotate(1, desiredSpeedPct, direction);
    motorB.rotate(1, desiredSpeedPct, direction);
    digitalWrite(LED_PIN, HIGH);
    return;
  }
  motorOn = false;
  motorA.coast();
  motorB.coast();
  digitalWrite(LED_PIN, LOW);
}

// ===================== SETUP =====================
void setup() {
  Serial.begin(9600);

  timer0 = timerBegin(0, 80, true);
  timerStart(timer0);

  Wire.begin(SDA_PIN, SCL_PIN);

  while (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
    Serial.println("Display non trovato");
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  while (!bmp.begin(0x77))
    Serial.println("BMP280 non trovato");

  while (!aht.begin())
    Serial.println("AHT20 non trovato");

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);

  motorA.begin();  motorA.coast();
  motorB.begin();  motorB.coast();

  // WiFi
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Connessione WiFi...");
  display.display();

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 30) {
    delay(500); Serial.print("."); tries++;
  }

  display.clearDisplay();
  display.setCursor(0, 0);
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi OK — IP: " + WiFi.localIP().toString());
    display.println("WiFi OK");
    display.println(WiFi.localIP().toString());
  } else {
    Serial.println("\nWiFi non disponibile.");
    display.println("WiFi fallito");
    display.println("(no web server)");
  }
  display.display();
  delay(2000);

  server.on("/",     handleRoot);
  server.on("/data", handleData);
  server.begin();
  Serial.println("Web server avviato.");
}

// ===================== LOOP =====================
void loop() {
  server.handleClient();

  totSec  = timerRead(timer0) / 1000000ULL;
  minutes = totSec / 60;
  seconds = totSec % 60;

  // Debounce pulsante
  int reading = digitalRead(BUTTON_PIN);
  if (reading != lastButtonState) lastDebounceTime = millis();
  if ((millis() - lastDebounceTime) > DEBOUNCE_MS) {
    if (reading != buttonState) {
      buttonState = reading;
      if (buttonState == LOW) {
        autoMode    = false;
        manualTimer = millis();
        motorOn = !motorOn;
        if (motorOn) {
          direction = (direction == CW ? CCW : CW);
          motorA.rotate(1, desiredSpeedPct, direction);
          motorB.rotate(1, desiredSpeedPct, direction);
          digitalWrite(LED_PIN, HIGH);
        } else {
          motorA.coast();
          motorB.coast();
          digitalWrite(LED_PIN, LOW);
        }
      }
    }
  }
  lastButtonState = reading;

  if (!autoMode && millis() - manualTimer > 30000)
    autoMode = true;

  checkAutoControl();

  // Campionamento ogni secondo
  if ((millis() - lastMs) > period) {
    lastMs = millis();

    tBmp = bmp.readTemperature();
    pBmp = bmp.readPressure() / 100.0f;

    sensors_event_t humEv, tempEv;
    aht.getEvent(&humEv, &tempEv);
    tAht = tempEv.temperature;
    hAht = humEv.relative_humidity;

    histPush(tBmp, pBmp, tAht, hAht, motorOn);

    // OLED
    display.clearDisplay();
    display.setCursor(0, 0);
    display.printf("%02d:%02d", minutes, seconds);
    if (WiFi.status() == WL_CONNECTED) {
      display.print(" ");
      display.print(WiFi.localIP().toString());
    }
    display.println();

    if (useBmp) {
      display.println("BMP280:");
      display.println("T: " + String(tBmp, 1) + " C");
      display.println("P: " + String(pBmp, 1) + " hPa");
    } else {
      display.println("AHT20:");
      display.println("T: " + String(tAht, 1) + " C");
      display.println("H: " + String(hAht, 1) + " %");
    }
    display.print("Fan:");
    display.print(motorOn ? "ON " : "OFF");
    display.println(autoMode ? "[A]" : "[M]");
    display.display();

    useBmp = !useBmp;
  }

  delay(10);
}