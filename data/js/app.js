const BASE_URL = `http://${window.location.hostname}`; // Use device IP automatically
const REFRESH_INTERVAL = 10000; // Refresh data every 10 seconds
const CHART_MAX_POINTS = 150;   // Max data points on the chart
const HISTORY_MAX_ENTRIES = 100; // Max entries displayed in the log
const TOAST_TIMEOUT = 5000;     // How long toasts stay visible (ms)
const FETCH_TIMEOUT = 8000;     // API fetch timeout (ms)

let clientStart = Date.now();
let lastDisplay = clientStart;
//const measurementIntervalMs = measurementInterval * 3600_000; // p. ej. 3 h → 10 800 000 ms
// Global para rastrear cuántas mediciones hemos procesado
let prevMeasurementCount = 0;
// fixedTimestamps almacena { rawEpochMs: fixedDisplayMs, ... } y persiste en localStorage
let fixedTimestamps = JSON.parse(localStorage.getItem('fixedTimestamps') || '{}');
// Cola de marcas de hora para mediciones manuales
let manualTimestampQueue = [];
// --- Element Selectors (Cached) ---
const El = {
    // Small Boxes / Widgets
    temp: document.getElementById('temperature'),
    humidity: document.getElementById('humidity'),
    currentStageLabel: document.getElementById('currentStageLabel'),
    currentStageParams: document.getElementById('currentStageParams'),
    pumpActivationCount: document.getElementById('pumpActivationCount'),
    vpd: document.getElementById('vpd'),
    totalElapsedTime: document.getElementById('totalElapsedTime'),
    // Status Indicators
    pumpStatusIndicator: document.getElementById('pumpStatusIndicator'),
    stageModeIndicator: document.getElementById('stageModeIndicator'),
    wifiStatus: document.getElementById('wifiStatus'),
    lastMeasurementTime: document.getElementById('lastMeasurementTime'),
    // Chart & History
    measurementHistory: document.getElementById('measurementHistory'),
    measurementChartCtx: document.getElementById('measurementChart')?.getContext('2d'),
    // Forms & Controls
    manualStageForm: document.getElementById('manualStageForm'),
    manualStageSelect: document.getElementById('manualStageSelect'),
    resetManualStageBtn: document.getElementById('resetManualStageButton'),
    refreshStageBtn: document.getElementById('refreshStage'),
    manualControlIndicator: document.getElementById('manualControlIndicator'),
    intervalForm: document.getElementById('intervalForm'),
    intervalInput: document.getElementById('intervalInput'),
    currentInterval: document.getElementById('currentInterval'),
    // Action Buttons
    activatePumpBtn: document.getElementById('activatePump'),
    deactivatePumpBtn: document.getElementById('deactivatePump'),
    takeMeasurementBtn: document.getElementById('takeMeasurement'),
    clearMeasurementsBtn: document.getElementById('clearMeasurements'),
    restartSystemBtn: document.getElementById('restartSystem'),
    // Global & Footer
    toastContainer: document.getElementById('toastContainer'),
    globalLoader: document.getElementById('globalLoader'),
    pageTitle: document.querySelector('title'),
    deviceIP: document.getElementById('deviceIP'),
};

// --- State Variables ---
let measurementChart = null; // Chart.js instance
let refreshTimeoutId = null; // Interval ID for auto-refresh
let currentStageData = {}; // Store latest stage info
let currentIntervalValue = null; // Store current interval

// --- API Functions ---
async function fetchData(endpoint, timeout = FETCH_TIMEOUT) {
    showGlobalLoader(true);
    const controller = new AbortController();
    const timeoutId = setTimeout(() => {
        controller.abort();
        console.warn(`Fetch timed out for ${endpoint}`);
    }, timeout);

    try {
        const res = await fetch(`${BASE_URL}${endpoint}`, { signal: controller.signal });
        clearTimeout(timeoutId); // Clear timeout if fetch completes

        if (!res.ok) {
            let errorMsg = `HTTP error! Status: ${res.status} ${res.statusText || ''}`;
            try { // Try to get error message from response body
                const errorBody = await res.text();
                if (errorBody) errorMsg += ` - ${errorBody}`;
            } catch (_) { /* Ignore if body can't be read */ }
            throw new Error(errorMsg);
        }
        // Check content type before parsing
        const contentType = res.headers.get("content-type");
        if (contentType && contentType.includes("application/json")) {
            return await res.json();
        } else {
            return await res.text(); // Return text for non-JSON (e.g., plain text success)
        }
    } catch (e) {
        clearTimeout(timeoutId);
        console.error(`Error fetching ${endpoint}:`, e);
        // Only show toast for non-abort errors (or maybe specific error types)
        if (e.name !== 'AbortError') {
            showToast(`Error fetching data (${endpoint}): ${e.message}`, 'error');
        } else {
            showToast(`Request timed out (${endpoint})`, 'warning');
        }
        throw e; // Re-throw for caller handling
    } finally {
        showGlobalLoader(false);
    }
}

async function postData(endpoint, data = {}, method = 'POST', timeout = FETCH_TIMEOUT) {
    showGlobalLoader(true);
    const controller = new AbortController();
    const timeoutId = setTimeout(() => {
        controller.abort();
        console.warn(`POST timed out for ${endpoint}`);
        }, timeout);

    try {
        const options = {
            method: method,
            headers: { 'Content-Type': 'application/json' },
            signal: controller.signal
        };
        // Only add body for methods that allow it
        if (method !== 'GET' && method !== 'HEAD' && Object.keys(data).length > 0) {
             options.body = JSON.stringify(data);
        }

        const res = await fetch(`${BASE_URL}${endpoint}`, options);
         clearTimeout(timeoutId);

        if (!res.ok) {
             let errorMsg = `HTTP error! Status: ${res.status} ${res.statusText || ''}`;
            try {
                const errorBody = await res.text();
                 if (errorBody) errorMsg += ` - ${errorBody}`;
            } catch (_) { /* Ignore */ }
            throw new Error(errorMsg);
        }

        const contentType = res.headers.get("content-type");
        if (contentType && contentType.includes("application/json")) {
            return await res.json();
        } else {
            return await res.text(); // Handle plain text responses
        }
    } catch (e) {
        clearTimeout(timeoutId);
        console.error(`Error posting to ${endpoint}:`, e);
        if (e.name !== 'AbortError') {
            showToast(`Error sending command (${endpoint}): ${e.message}`, 'error');
        } else {
             showToast(`Command timed out (${endpoint})`, 'warning');
        }
        throw e;
    } finally {
        showGlobalLoader(false);
    }
}

// --- UI Helper Functions ---

function showToast(message, type = 'info', duration = TOAST_TIMEOUT) {
    if (!El.toastContainer) return;

    const toastId = `toast-${Date.now()}`; // Unique ID for each toast
    const toast = document.createElement('div');
    toast.id = toastId;
    // Use Bootstrap background colors directly
    const bgClass = {
        info: 'bg-info',
        success: 'bg-success',
        warning: 'bg-warning',
        error: 'bg-danger'
    }[type] || 'bg-secondary'; // Default to secondary if type is unknown

    toast.className = `toast align-items-center text-white ${bgClass} border-0 mb-2`; // Add mb-2 for spacing
    toast.setAttribute('role', 'alert');
    toast.setAttribute('aria-live', type === 'error' ? 'assertive' : 'polite'); // Assertive for errors
    toast.setAttribute('aria-atomic', 'true');

    const iconClass = {
        info: 'fas fa-info-circle',
        success: 'fas fa-check-circle',
        warning: 'fas fa-exclamation-triangle',
        error: 'fas fa-times-circle'
    }[type] || 'fas fa-bell';

    toast.innerHTML = `
        <div class="d-flex">
            <div class="toast-body">
                <i class="${iconClass}"></i> ${message}
            </div>
            <button type="button" class="btn-close btn-close-white me-2 m-auto" data-bs-dismiss="toast" aria-label="Close"></button>
        </div>
    `;

    El.toastContainer.appendChild(toast);

    // Use Bootstrap's Toast component for proper handling
    const bsToast = new bootstrap.Toast(toast, { delay: duration, autohide: true });
    bsToast.show();

    // Remove the element from DOM after hiding transition completes
    toast.addEventListener('hidden.bs.toast', () => {
        const element = document.getElementById(toastId);
        if (element) {
            element.remove();
        }
    });
}


function setLoadingState(buttonElement, isLoading) {
    if (!buttonElement) return;
    const loaderClass = 'loader';
    let loader = buttonElement.querySelector(`.${loaderClass}`);

    if (isLoading) {
        buttonElement.disabled = true;
        buttonElement.setAttribute('aria-busy', 'true');
        // Keep original content, append loader
        const originalContent = buttonElement.innerHTML;
         // Prevent adding multiple loaders
        if (!loader) {
             loader = document.createElement('span');
             loader.className = loaderClass;
             loader.style.marginLeft = '8px'; // Add space before loader
             loader.setAttribute('aria-hidden', 'true'); // Hide decorative loader from screen readers
             buttonElement.appendChild(loader);
        }
        // Store original content if needed for restoration, but often not necessary
        // buttonElement.dataset.originalHTML = originalContent;

    } else {
        buttonElement.disabled = false;
        buttonElement.removeAttribute('aria-busy');
        if (loader) {
            loader.remove();
        }
         // Restore original HTML if it was stored and modified
         // if (buttonElement.dataset.originalHTML) {
         //     buttonElement.innerHTML = buttonElement.dataset.originalHTML;
         //     delete buttonElement.dataset.originalHTML;
         // }
    }
}

function showGlobalLoader(show) {
    if (El.globalLoader) {
        El.globalLoader.style.display = show ? 'block' : 'none';
    }
}

function formatElapsedTime(totalSeconds) {
    if (totalSeconds == null || isNaN(totalSeconds) || totalSeconds < 0) return 'N/A';
    totalSeconds = Math.floor(totalSeconds);

    const d = Math.floor(totalSeconds / (3600 * 24));
    const h = Math.floor((totalSeconds % (3600 * 24)) / 3600);
    const m = Math.floor((totalSeconds % 3600) / 60);
    // const s = Math.floor(totalSeconds % 60); // Usually not needed for uptime

    let parts = [];
    if (d > 0) parts.push(`${d}d`);
    if (h > 0) parts.push(`${h}h`);
    if (m > 0 || (d === 0 && h === 0)) parts.push(`${m}m`); // Show minutes if < 1h or if there are hours/days
    // if (parts.length === 0) parts.push(`${s}s`); // Show seconds only if < 1 minute

    return parts.length > 0 ? parts.join(' ') : '0m'; // Default to 0m if very short
}


function formatTimestampForHistory(epochMs) {
     if (!epochMs || isNaN(epochMs)) return '?:??';
     const date = new Date(epochMs);
     // Format like "15:30" or "Mar 10, 15:30" if older than today
     const now = new Date();
     const isToday = date.getDate() === now.getDate() &&
                     date.getMonth() === now.getMonth() &&
                     date.getFullYear() === now.getFullYear();

     const timeFormat = { hour: '2-digit', minute: '2-digit', hour12: false };
     if (isToday) {
         return date.toLocaleTimeString([], timeFormat);
     } else {
         return date.toLocaleDateString([], { month: 'short', day: 'numeric' }) + ', ' + date.toLocaleTimeString([], timeFormat);
     }
}

// Updated function signature to accept a custom formatter
function updateElementText(element, value, unit = '', precision = 1, naValue = '-', formatter = null) {
    if (!element) return;
    let text = naValue;
    let valueChanged = false;

    try {
        if (formatter && value != null) { // Use formatter if value is not null/undefined
            text = formatter(value);
        } else if (value != null && !isNaN(value)) { // Default number formatting
            text = `${parseFloat(value).toFixed(precision)}${unit ? ` ${unit}` : ''}`;
        } else if (value != null) { // Handle non-numeric values directly (like stage name)
            text = `${value}${unit ? ` ${unit}` : ''}`;
        }
        // Use === for stricter comparison, handle potential type differences if needed
        if (element.textContent !== text) {
            element.textContent = text;
            valueChanged = true;
        }
    } catch (e) {
         console.error("Error formatting value for element:", element.id, value, e);
         if (element.textContent !== naValue) {
             element.textContent = naValue; // Fallback on error
             valueChanged = true;
         }
    }

    // Add a subtle flash effect on update only if value changed
    if (valueChanged) {
        element.classList.remove('updated'); // Remove first to re-trigger animation
        // Use requestAnimationFrame to ensure class removal is processed before adding it back
        requestAnimationFrame(() => {
            element.classList.add('updated');
            // Remove the class after the animation duration (must match CSS)
            setTimeout(() => element.classList.remove('updated'), 600); // 600ms matches CSS
        });
    }
}

// --- Chart Functions ---
function createChart() {
    if (!El.measurementChartCtx) {
        console.error("Chart context not found");
        return null;
    }
    // Destroy existing chart instance if it exists
    if (measurementChart) {
        console.log("Destroying existing chart instance.");
        measurementChart.destroy();
        measurementChart = null;
    }

    console.log("Creating new chart instance.");
    return new Chart(El.measurementChartCtx, {
        type: 'line',
        data: {
            labels: [], // Timestamps (formatted)
            datasets: [
                {
                    label: 'Temperature (°C)',
                    data: [], // Array of numbers
                    borderColor: 'var(--gn-chart-temp)',
                    backgroundColor: 'rgba(255, 99, 132, 0.1)',
                    yAxisID: 'yTemp',
                    tension: 0.2, // Smoother lines
                    pointRadius: 1,
                    pointHoverRadius: 5,
                    borderWidth: 1.5,
                },
                {
                    label: 'Humidity (%)',
                    data: [], // Array of numbers
                    borderColor: 'var(--gn-chart-humid)',
                    backgroundColor: 'rgba(54, 162, 235, 0.1)',
                    yAxisID: 'yHumid',
                    tension: 0.2,
                    pointRadius: 1,
                    pointHoverRadius: 5,
                    borderWidth: 1.5,
                },
                {
                    label: 'Pump Activation',
                    data: [], // Store {x: timestamp_index, y: temperature_at_activation} or just y=null/value
                    borderColor: 'rgba(0,0,0,0)', // Invisible line
                    backgroundColor: 'var(--gn-chart-pump)',
                    pointBackgroundColor: 'var(--gn-chart-pump)', // Ensure point color matches
                    yAxisID: 'yTemp', // Plot against temp axis for context
                    pointStyle: 'triangle',
                    radius: 6,
                    hoverRadius: 8,
                    showLine: false, // Just points
                }
            ]
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            scales: {
                x: {
                    // type: 'category', // Use category for simple labels like "1h", "2h"
                    // If using real timestamps, 'time' type is better with adapter
                    ticks: {
                         color: 'var(--gn-chart-text)',
                         maxRotation: 0, // Prevent label rotation
                         autoSkip: true, // Automatically skip labels to prevent overlap
                         maxTicksLimit: 10 // Limit number of visible ticks
                    },
                     grid: { color: 'var(--gn-chart-grid)' }
                },
                yTemp: {
                    type: 'linear',
                    position: 'left',
                    title: { display: true, text: '°C', color: 'var(--gn-chart-temp)' },
                    ticks: { color: 'var(--gn-chart-temp)', precision: 0 }, // Integer ticks usually suffice
                    grid: { color: 'var(--gn-chart-grid)' }
                },
                yHumid: {
                    type: 'linear',
                    position: 'right',
                    title: { display: true, text: '%', color: 'var(--gn-chart-humid)' },
                    min: 0,
                    max: 100, // Humidity is 0-100
                    ticks: { color: 'var(--gn-chart-humid)', precision: 0 },
                    grid: { drawOnChartArea: false } // Avoid overlapping Y grids
                }
            },
            plugins: {
                tooltip: {
                    mode: 'index',
                    intersect: false,
                    backgroundColor: 'rgba(0, 0, 0, 0.8)', // Darker tooltip
                    titleColor: '#fff',
                    bodyColor: '#fff',
                    callbacks: {
                        label: function(context) {
                            let label = context.dataset.label || '';
                            if (!measurementChart) return label; // Safety check

                            if (context.dataset.label === 'Pump Activation') {
                                // Only show label if point is not null (pump was active)
                                return context.parsed.y !== null ? 'Pump Activated' : null;
                            }

                            // Regular Temp/Humidity labels
                            if (label) label += ': ';
                            if (context.parsed.y !== null) {
                                label += context.parsed.y.toFixed(1);
                                if(context.dataset.label === 'Temperature (°C)') label += ' °C';
                                if(context.dataset.label === 'Humidity (%)') label += ' %';
                            }
                            return label;
                        }
                    }
                },
                legend: {
                    labels: {
                        color: 'var(--gn-chart-text)',
                        usePointStyle: true, // Use point style in legend
                        padding: 20
                    },
                    // Default click behavior is fine (toggle visibility)
                }
            },
            interaction: {
                 mode: 'nearest',
                 axis: 'x',
                 intersect: false
            },
             animation: {
                 duration: 500 // Faster animation on updates
             }
        }
    });
}

function updateChart(measurements) {
    // Ensure chart exists
    if (!measurementChart && El.measurementChartCtx) {
        measurementChart = createChart();
    }
    if (!measurementChart) {
        console.error("Cannot update chart: instance not available.");
        return;
    }

    // Limit data points
    const limitedMeasurements = measurements.slice(-CHART_MAX_POINTS);

    // Extract data, using null for potential missing values
    const timestamps = limitedMeasurements.map(m => formatTimestampForHistory(m.epoch_ms)); // Use formatted date/time for labels
    const humidity = limitedMeasurements.map(m => m.humidity ?? null);
    const temperature = limitedMeasurements.map(m => m.temperature ?? null);

    // Create pump activation data: use temperature value if activated, null otherwise
    const pumpPointsData = limitedMeasurements.map((m, index) =>
        m.pumpActivated ? (temperature[index] ?? null) : null // Plot at corresponding temp value
    );

    measurementChart.data.labels = timestamps;
    measurementChart.data.datasets[0].data = temperature;
    measurementChart.data.datasets[1].data = humidity;
    measurementChart.data.datasets[2].data = pumpPointsData;

    measurementChart.update();
}


// Registrar el instante de la medición manual
El.takeMeasurementBtn?.addEventListener('click', () => {
    manualTimestampQueue.push(Date.now());
  });
  
  async function updateUI() {
    console.log("Updating UI...");
    try {
      // 0) Asegurarnos de tener currentIntervalValue
      if (currentIntervalValue === null) {
        await fetchAndUpdateIntervalDisplay();
      }
  
      // 1) Traer datos core y mediciones
      const [dataRes, histRes] = await Promise.allSettled([
        fetchData('/data'),
        fetchData('/loadMeasurement')
      ]);
  
      // --- Procesar coreData ---
      if (dataRes.status === 'fulfilled' && dataRes.value) {
        const coreData = dataRes.value;
        // Título
        if (El.pageTitle) El.pageTitle.textContent = `Green Nanny ${coreData.pumpStatus ? '💧' : '🌿'}`;
        // Widgets básicos
        updateElementText(El.temp, coreData.temperature, '°C');
        updateElementText(El.humidity, coreData.humidity, '%');
        updateElementText(El.vpd, coreData.vpd, ' kPa', 2);
        updateElementText(El.pumpActivationCount, coreData.pumpActivationCount, '', 0);
        updateElementText(El.totalElapsedTime, coreData.elapsedTime, '', 0, 'N/A', formatElapsedTime);
        // Etapa
        const cs = {
          name: coreData.currentStageName,
          threshold: coreData.currentStageThreshold,
          watering: coreData.currentStageWateringSec,
          manual: coreData.manualStageControl
        };
        updateElementText(El.currentStageLabel, cs.name, '', null, '-');
        if (El.currentStageParams) El.currentStageParams.textContent = `Threshold: ${cs.threshold}% | Water: ${cs.watering}s`;
        if (El.manualControlIndicator) El.manualControlIndicator.style.display = cs.manual ? 'inline-block' : 'none';
        if (El.stageModeIndicator) {
          El.stageModeIndicator.textContent = cs.manual ? 'Manual' : 'Auto';
          El.stageModeIndicator.className = `badge ${cs.manual ? 'bg-warning text-dark' : 'bg-info'}`;
        }
        // Bomba
        if (El.pumpStatusIndicator) {
          const on = coreData.pumpStatus ?? false;
          El.pumpStatusIndicator.textContent = on ? 'ON' : 'OFF';
          El.pumpStatusIndicator.className = `badge ${on ? 'bg-success' : 'bg-secondary'}`;
          El.pumpStatusIndicator.classList.toggle('pulsing', on);
          if (El.activatePumpBtn) El.activatePumpBtn.disabled = on;
          if (El.deactivatePumpBtn) El.deactivatePumpBtn.disabled = !on;
        }
        // WiFi
        if (El.wifiStatus) {
          const rssi = coreData.wifiRSSI;
          if (rssi != null && rssi !== 0) {
            let icon='fa-wifi text-success', txt=`Connected (${rssi} dBm)`;
            if (rssi < -80) { icon='fa-wifi text-danger'; txt=`Weak (${rssi} dBm)`; }
            else if (rssi < -70) { icon='fa-wifi text-warning'; txt=`Okay (${rssi} dBm)`; }
            El.wifiStatus.innerHTML = `<i class="fas ${icon}"></i> ${txt}`;
          } else {
            El.wifiStatus.innerHTML = `<i class="fas fa-wifi text-secondary"></i> Disconnected`;
          }
        }
      } else {
        console.error('Failed coreData:', dataRes.reason);
      }
  
      // --- Historial de mediciones ---
      let measurements = [];
      if (histRes.status === 'fulfilled' && Array.isArray(histRes.value)) {
        measurements = histRes.value;
      } else {
        console.warn('No history:', histRes);
      }
  
      // Si limpiaron el backend, reinicio los arrays
      if (measurements.length < prevMeasurementCount) {
        fixedTimestamps = {};
        manualTimestampQueue = [];
        prevMeasurementCount = 0;
      }
  
      const now = Date.now();
      const hours = Number.isFinite(currentIntervalValue) ? currentIntervalValue : 3;
      const intervalMs = hours * 3600000;
      const epochThreshold = 1e12;
  
      // Fijar timestamps **una sola vez** por cada rawEpochMs
      measurements.forEach((m, idx) => {
        const raw = m.epoch_ms;
        const key = String(raw);
        if (fixedTimestamps[key] === undefined) {
          // Si fue manual y coincide con el próximo índice, uso hora real
          if (idx === prevMeasurementCount && manualTimestampQueue.length) {
            fixedTimestamps[key] = manualTimestampQueue.shift();
          } else if (raw > epochThreshold) {
            // epoch real válido del backend
            fixedTimestamps[key] = raw;
          } else {
            // fallback: repartir según intervalo
            const posFromEnd = measurements.length - idx - 1;
            fixedTimestamps[key] = now - posFromEnd * intervalMs;
          }
        }
      });
  
      // Persisto en localStorage para que sobreviva al reload
      localStorage.setItem('fixedTimestamps', JSON.stringify(fixedTimestamps));
  
      // Construyo array de timestamps de display en orden
      const displayTimestamps = measurements.map(m => fixedTimestamps[String(m.epoch_ms)]);
  
      // Renderizo historial
      if (El.measurementHistory) {
        const start = Math.max(0, measurements.length - HISTORY_MAX_ENTRIES);
        const sliceMeas = measurements.slice(start).reverse();
        const sliceTimes = displayTimestamps.slice(start).reverse();
        const html = sliceMeas.map((m, i) => {
          const ts = sliceTimes[i];
          return `
            <div class="measurement-history-entry">
              <span class="timestamp">${new Date(ts).toLocaleString()}</span>
              <span class="data-point"><i class="fas fa-thermometer-half"></i>${m.temperature?.toFixed(1)}°C</span>
              <span class="data-point"><i class="fas fa-tint"></i>${m.humidity?.toFixed(1)}%</span>
              ${m.pumpActivated ? '<span class="data-point pump-info"><i class="fas fa-play"></i>Pump</span>' : ''}
            </div>
          `;
        }).join('') || `<div class="measurement-history-entry text-muted">No history recorded.</div>`;
  
        El.measurementHistory.innerHTML = html;
        El.measurementHistory.scrollTop = El.measurementHistory.scrollHeight;
      }
  
      // Actualizar “última medición” solo si hay nuevas
      if (measurements.length > prevMeasurementCount) {
        const lastTs = displayTimestamps[displayTimestamps.length - 1];
        if (El.lastMeasurementTime) {
          El.lastMeasurementTime.textContent = new Date(lastTs).toLocaleString();
        }
        prevMeasurementCount = measurements.length;
      }
  
      // --- Gráfico ---
      updateChart(measurements);
  
    } catch (err) {
      console.error('Error en updateUI:', err);
    } finally {
      clearTimeout(refreshTimeoutId);
      refreshTimeoutId = setTimeout(updateUI, REFRESH_INTERVAL);
    }
  }

// Function to fetch and update the displayed interval
async function fetchAndUpdateIntervalDisplay() {
     if (!El.currentInterval || !El.intervalInput) return;
     try {
        const intervalData = await fetchData('/getMeasurementInterval');
        if (intervalData && intervalData.interval != null) {
             currentIntervalValue = intervalData.interval;
             El.currentInterval.textContent = currentIntervalValue;
             El.intervalInput.value = currentIntervalValue; // Pre-fill input
             El.intervalInput.placeholder = `Current: ${currentIntervalValue}h`;
        }
     } catch (error) {
         console.error("Failed to fetch measurement interval:", error);
         El.currentInterval.textContent = 'Error';
     }
}


// Function to populate stage selector
async function populateStageSelector() {
    if (!El.manualStageSelect) return;
    El.manualStageSelect.innerHTML = `<option value="" disabled selected>Loading stages...</option>`;
    try {
        const stages = await fetchData('/listStages');
        if (stages && Array.isArray(stages) && stages.length > 0) {
            El.manualStageSelect.innerHTML = stages.map(stage =>
                `<option value="${stage.index}">${stage.name} (T:${stage.humidityThreshold}% W:${stage.wateringTimeSec}s)</option>`
            ).join('');
            // Select the currently active stage if available
            if (currentStageData && currentStageData.index != null) {
                 El.manualStageSelect.value = currentStageData.index;
            }
        } else {
             El.manualStageSelect.innerHTML = `<option value="" disabled selected>No stages found</option>`;
        }
    } catch (error) {
        console.error("Failed to load stages for selector:", error);
         El.manualStageSelect.innerHTML = `<option value="" disabled selected>Error loading stages</option>`;
    }
}


// --- Event Listener Setup ---
function setupEventListeners() {

    // --- Forms ---
    El.intervalForm?.addEventListener('submit', async (e) => {
        e.preventDefault();
        const value = El.intervalInput?.value;
        const numValue = parseInt(value);
        if (value === '' || isNaN(numValue) || numValue < 1 || numValue > 167) {
             showToast('Please enter a valid interval (1-167 hours).', 'warning');
             El.intervalInput?.classList.add('is-invalid');
             return;
        }
         El.intervalInput?.classList.remove('is-invalid');
        const button = El.intervalForm.querySelector('button[type="submit"]');
        setLoadingState(button, true);
        try {
            await postData('/setMeasurementInterval', { interval: numValue });
            showToast(`Measurement interval set to ${numValue} hours`, 'success');
            currentIntervalValue = numValue; // Update local state
             if(El.currentInterval) El.currentInterval.textContent = currentIntervalValue;
             El.intervalInput.placeholder = `Current: ${currentIntervalValue}h`;
            // Optionally trigger UI refresh to reflect potential scheduling changes
             // await updateUI();
        } catch (error) {
             // Error logged by postData
        } finally {
            setLoadingState(button, false);
        }
    });

     El.intervalInput?.addEventListener('input', () => {
          const value = El.intervalInput.value;
          const numValue = parseInt(value);
         if (value === '' || isNaN(numValue) || numValue < 1 || numValue > 167) {
             El.intervalInput.classList.add('is-invalid');
         } else {
             El.intervalInput.classList.remove('is-invalid');
         }
     });

    El.manualStageForm?.addEventListener('submit', async (e) => {
        e.preventDefault();
        const value = El.manualStageSelect?.value;
        const numValue = parseInt(value);
        if (value === '' || isNaN(numValue)) {
            showToast('Please select a valid stage.', 'warning');
             El.manualStageSelect?.classList.add('is-invalid');
            return;
        }
         El.manualStageSelect?.classList.remove('is-invalid');
        const button = El.manualStageForm.querySelector('button[type="submit"]');
        setLoadingState(button, true);
        try {
            await postData('/setManualStage', { stage: numValue });
            showToast(`Manual stage set to "${El.manualStageSelect.options[El.manualStageSelect.selectedIndex].text.split('(')[0].trim()}"`, 'success');
            // Refresh UI immediately to show the change
            await updateUI();
            // Ensure selector reflects the change if updateUI didn't catch it fast enough
             // Maybe update currentStageData directly?
             if (currentStageData) currentStageData.manual = true;
             if (El.manualControlIndicator) El.manualControlIndicator.style.display = 'inline-block';
             if (El.stageModeIndicator) {
                 El.stageModeIndicator.textContent = 'Manual';
                 El.stageModeIndicator.className = 'badge bg-warning text-dark';
             }

        } catch (error) {
            // Error logged by postData
        } finally {
            setLoadingState(button, false);
        }
    });

    // --- Buttons ---
    El.activatePumpBtn?.addEventListener('click', async () => {
        // Consider replacing prompt with a modal or inline input for duration
        const durationStr = prompt('Enter pump activation duration in seconds (e.g., 30):', '30');
        if (durationStr === null) return; // User cancelled
        const duration = parseInt(durationStr);

        if (isNaN(duration) || duration <= 0) {
            showToast('Invalid duration. Please enter a positive number of seconds.', 'warning');
            return;
        }

        setLoadingState(El.activatePumpBtn, true);
        try {
            const response = await postData('/controlPump', { action: 'on', duration: duration });
            // Check response? Assume success if no error thrown by postData
            showToast(`Pump activated manually for ${duration} seconds.`, 'success');
             await updateUI(); // Refresh UI to show pump status
        } catch (error) {
             // Error handled by postData
        } finally {
            // The updateUI call should handle button state, but ensure it's re-enabled if needed
             // setLoadingState(El.activatePumpBtn, false); // Re-enable after updateUI potentially
        }
    });

    El.deactivatePumpBtn?.addEventListener('click', async () => {
        // Confirmation is good practice for stopping something potentially needed
        if (!confirm('Are you sure you want to manually deactivate the pump?\nThis will stop the current watering cycle if active.')) return;

        setLoadingState(El.deactivatePumpBtn, true);
        try {
            await postData('/controlPump', { action: 'off' });
            showToast('Pump deactivated manually.', 'success');
             await updateUI(); // Refresh UI
        } catch (error) {
             // Error handled by postData
        } finally {
             // setLoadingState(El.deactivatePumpBtn, false); // updateUI should handle this
        }
    });

    El.takeMeasurementBtn?.addEventListener('click', async () => {
        setLoadingState(El.takeMeasurementBtn, true);
        showToast('Triggering manual measurement cycle...', 'info', 2000);
        try {
            await postData('/takeMeasurement', {}, 'POST');
            // Give device a moment to process and save before fetching new data
            await new Promise(resolve => setTimeout(resolve, 1500));
            await updateUI(); // Refresh UI immediately
            showToast('Measurement cycle complete.', 'success', 2000);
        } catch (error) {
             // Error toast shown by postData
             // Optionally show a specific failure toast here
             // showToast('Failed to trigger measurement cycle.', 'error');
        } finally {
            setLoadingState(El.takeMeasurementBtn, false);
        }
    });

    El.clearMeasurementsBtn?.addEventListener('click', async () => {
        if (!confirm('⚠️ DELETE ALL MEASUREMENT HISTORY? ⚠️\n\nThis action cannot be undone.')) return;

        setLoadingState(El.clearMeasurementsBtn, true);
        try {
            await postData('/clearHistory', {}, 'POST');
            showToast('Measurement history cleared successfully.', 'success');
            // Clear chart data immediately
            if(measurementChart) {
                 measurementChart.data.labels = [];
                 measurementChart.data.datasets.forEach(ds => ds.data = []);
                 measurementChart.update('none'); // Update without animation
            }
            // Clear history log display immediately
            if(El.measurementHistory) El.measurementHistory.innerHTML = `<div class='measurement-history-entry text-muted'>History cleared.</div>`;
            // Trigger full UI update to fetch potentially empty history from backend
            await updateUI();
        } catch (error) {
            // Error toast shown by postData
        } finally {
            setLoadingState(El.clearMeasurementsBtn, false);
        }
    });

    El.restartSystemBtn?.addEventListener('click', async () => {
        if (!confirm('Are you sure you want to restart the Green Nanny device?\n\nThe dashboard will become unresponsive for a short time.')) return;

        setLoadingState(El.restartSystemBtn, true);
        showToast('Sending restart command...', 'warning');
        document.body.classList.add('system-restarting'); // Show overlay

        try {
            // Don't necessarily wait for response, as device might disconnect immediately
             postData('/restartSystem', {}, 'POST');
             // Inform user, they need to refresh manually later
             setTimeout(() => {
                 alert('System restart command sent. The device is now restarting.\n\nPlease wait about 30 seconds and then manually refresh this page.');
                 // Maybe disable all controls permanently until refresh?
                 document.querySelectorAll('button, input, select').forEach(el => el.disabled = true);
             }, 2000); // Wait 2s before alert
        } catch (error) {
             // This might happen if the fetch fails before server goes down
            showToast('Failed to send restart command. You may need to restart manually.', 'error');
            setLoadingState(El.restartSystemBtn, false);
             document.body.classList.remove('system-restarting');
        }
        // No 'finally' block to reset button, as the page context will be lost/needs refresh
    });

    El.refreshStageBtn?.addEventListener('click', async () => {
         setLoadingState(El.refreshStageBtn, true);
         try {
             // Re-fetch core data which includes stage info
             await updateUI();
             // Refetch stage list to update selector options? Not strictly necessary unless stages change.
             // await populateStageSelector();
              showToast('Stage information refreshed.', 'info', 2000);
         } catch (error) {
              // Error handled by updateUI
         } finally {
             setLoadingState(El.refreshStageBtn, false);
         }
    });

    El.resetManualStageBtn?.addEventListener('click', async () => {
        if (!confirm('Return to automatic stage progression based on system uptime?')) return;
        setLoadingState(El.resetManualStageBtn, true);
        try {
            await postData('/resetManualStage', {}, 'POST');
            showToast('Stage control reset to automatic.', 'success');
            // Refresh UI immediately
            await updateUI();
             // Update local state and indicators
             if (currentStageData) currentStageData.manual = false;
             if (El.manualControlIndicator) El.manualControlIndicator.style.display = 'none';
             if (El.stageModeIndicator) {
                 El.stageModeIndicator.textContent = 'Auto';
                 El.stageModeIndicator.className = 'badge bg-info';
             }
        } catch (error) {
             // Error handled by postData
        } finally {
            setLoadingState(El.resetManualStageBtn, false);
        }
    });

    // --- Initialize Tooltips ---
    if (typeof bootstrap !== 'undefined' && bootstrap.Tooltip) {
        const tooltipTriggerList = [].slice.call(document.querySelectorAll('[data-bs-toggle="tooltip"]'));
        tooltipTriggerList.map(function (tooltipTriggerEl) {
            return new bootstrap.Tooltip(tooltipTriggerEl, {
                trigger: 'hover focus' // Show on hover and focus for accessibility
            });
        });
    } else {
        console.warn("Bootstrap Tooltip component not found. Tooltips will not work.");
    }
}


// --- Initialization ---
document.addEventListener('DOMContentLoaded', async () => {
    console.log("DOM loaded. Initializing Green Nanny Dashboard...");
    showGlobalLoader(true);

    // Ensure chart context exists before proceeding
    if (!El.measurementChartCtx) {
         console.warn("Chart canvas context not found. Chart will be disabled.");
         // Optionally disable chart-related UI elements
    }

    setupEventListeners(); // Setup listeners first

    try {
        // Fetch initial data and populate UI
        await updateUI(); // Includes fetching data, populating elements, and creating/updating chart

        // Populate stage selector after initial data load (might have current stage info)
        await populateStageSelector();

        // Fetch and display initial interval
        await fetchAndUpdateIntervalDisplay();


        console.log("Initialization complete.");

    } catch (error) {
        console.error("Dashboard initialization failed:", error);
        showToast("Failed to load initial dashboard data. Please check device connection and refresh.", "error", 10000);
        // Display a more prominent error message?
        const mainContent = document.querySelector('.content-wrapper');
        if(mainContent) {
             mainContent.insertAdjacentHTML('afterbegin', `<div class="alert alert-danger m-3"><strong>Initialization Error:</strong> Could not load initial data. Check device connection and refresh.</div>`);
        }
    } finally {
         showGlobalLoader(false);
    }
});
