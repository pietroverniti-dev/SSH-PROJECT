// --- SETUP INIZIALE ---
// Recuperiamo i dati passati dall'HTML tramite window.SERVER_DATA
const rooms = window.SERVER_DATA || [];
const roomIds = rooms.map(r => r.id);
const charts = {};
const MAX_DATA_POINTS = 20;

function flipCard(element) {
    element.classList.toggle("flipped");
}

// --- CONFIGURAZIONE GRAFICI ---
const commonLineOptions = {
    responsive: true,
    maintainAspectRatio: false,
    plugins: { legend: { display: false } },
    scales: {
        x: { display: false },
        y: { beginAtZero: false, grid: { color: '#f0f0f0' } }
    },
    elements: { point: { radius: 0, hitRadius: 10 } }
};

function initCharts() {
    roomIds.forEach(id => {
        // 1. Line Chart Temp (Back)
        const ctxTemp = document.getElementById(`chart-line-temp-${id}`).getContext('2d');
        charts[`temp_${id}`] = new Chart(ctxTemp, {
            type: 'line',
            data: { labels: [], datasets: [{ data: [], borderColor: '#ef476f', borderWidth: 2, tension: 0.4, fill: true, backgroundColor: 'rgba(239, 71, 111, 0.1)' }] },
            options: commonLineOptions
        });

        // 2. Line Chart Hum (Back)
        const ctxHum = document.getElementById(`chart-line-hum-${id}`).getContext('2d');
        charts[`hum_${id}`] = new Chart(ctxHum, {
            type: 'line',
            data: { labels: [], datasets: [{ data: [], borderColor: '#4361ee', borderWidth: 2, tension: 0.4, fill: true, backgroundColor: 'rgba(67, 97, 238, 0.1)' }] },
            options: commonLineOptions
        });

        // 3. Doughnut Chart (Front)
        const ctxDough = document.getElementById(`chart-doughnut-${id}`).getContext('2d');
        charts[`dough_${id}`] = new Chart(ctxDough, {
            type: 'doughnut',
            data: {
                labels: ["Umidità", "Vuoto"],
                datasets: [{
                    data: [0, 100],
                    backgroundColor: ['#4361ee', '#e0e0e0'],
                    borderWidth: 0,
                    circumference: 180,
                    rotation: 270,
                }]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                plugins: { tooltip: { enabled: false }, legend: { display: false } },
                cutout: '75%'
            }
        });
    });
}

// --- LOGICA AGGIORNAMENTO DATI ---
async function fetchData() {
    try {
        const response = await fetch('/api/data');
        const data = await response.json();

        roomIds.forEach(id => {
            const roomData = data[id];
            if(!roomData) return;

            // Aggiorna TESTO Front
            const tempEl = document.getElementById(`temp-${id}`);
            const humEl = document.getElementById(`hum-${id}`);
            
            if (tempEl) tempEl.textContent = roomData.temperature;
            if (humEl) humEl.textContent = roomData.humidity;

            // Logica visuale: se T > 27 diventa rosso
            if (tempEl) {
                if (roomData.temperature > 27) {
                    tempEl.classList.add('hot');
                    tempEl.classList.remove('normal');
                } else {
                    tempEl.classList.add('normal');
                    tempEl.classList.remove('hot');
                }
            }

            // Aggiorna CHART Doughnut (Front)
            if (charts[`dough_${id}`]) {
                charts[`dough_${id}`].data.datasets[0].data = [roomData.humidity, 100 - roomData.humidity];
                const humColor = roomData.humidity > 70 ? '#06d6a0' : '#4361ee'; 
                charts[`dough_${id}`].data.datasets[0].backgroundColor[0] = humColor;
                charts[`dough_${id}`].update();
            }

            // Aggiorna CHART Lineari (Back)
            const pushData = (chart, label, value) => {
                if (!chart) return;
                chart.data.labels.push(label);
                chart.data.datasets[0].data.push(value);
                if (chart.data.labels.length > MAX_DATA_POINTS) {
                    chart.data.labels.shift();
                    chart.data.datasets[0].data.shift();
                }
                chart.data.datasets[0].backgroundColor = value > 27 ? 'rgba(239, 71, 111, 0.2)' : 'rgba(67, 97, 238, 0.1)';
                chart.update('none');
            };

            pushData(charts[`temp_${id}`], roomData.timestamp, roomData.temperature);
            pushData(charts[`hum_${id}`], roomData.timestamp, roomData.humidity);
        });

    } catch (error) {
        console.error("Errore fetch dati:", error);
    }
}

// Avvio
document.addEventListener("DOMContentLoaded", () => {
    initCharts();
    setInterval(fetchData, 2000);
    fetchData();
});