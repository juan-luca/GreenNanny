// --- START OF FILE app.js ---

const BASE_URL = `http://${window.location.hostname}`; // Use device IP automatically
const REFRESH_INTERVAL = 10000; // Refresh data every 10 seconds
const CHART_MAX_POINTS = 150;   // Max data points on the chart
const HISTORY_MAX_ENTRIES = 100; // Max entries displayed in the log
const TOAST_TIMEOUT = 5000;     // How long toasts stay visible (ms)
const FETCH_TIMEOUT = 8000;     // API fetch timeout (ms)

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
        // Prevent adding multiple loaders
        if (!loader) {
             loader = document.createElement('span');
             loader.className = loaderClass;
             loader.style.marginLeft = '8px'; // Add space before loader
             loader.setAttribute('aria-hidden', 'true'); // Hide decorative loader from screen readers
             buttonElement.appendChild(loader);
        }
    } else {
        buttonElement.disabled = false;
        buttonElement.removeAttribute('aria-busy');
        if (loader) {
            loader.remove();
        }
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

    let parts = [];
    if (d > 0) parts.push(`${d}d`);
    if (h > 0) parts.push(`${h}h`);
    if (m > 0 || (d === 0 && h === 0)) parts.push(`${m}m`);

    return parts.length > 0 ? parts.join(' ') : '0m';
}


/**
 * Formats an epoch timestamp (milliseconds UTC) for display, explicitly using UTC.
 * Shows HH:MM for today (UTC), or Mon DD, HH:MM (UTC) for older dates.
 * Includes year if not the current year (UTC).
 * @param {number} epochMs - Timestamp in milliseconds since the epoch (UTC).
 * @returns {string} Formatted date/time string in UTC.
 */
function formatTimestampForHistory(epochMs) {
     if (!epochMs || isNaN(epochMs) || epochMs < 1e12) return '?:??'; // Basic check for valid epoch ms
     const date = new Date(epochMs);

     // --- Get components in UTC ---
     const year = date.getUTCFullYear();
     const month = date.getUTCMonth(); // 0-11
     const day = date.getUTCDate();
     const hours = date.getUTCHours().toString().padStart(2, '0');
     const minutes = date.getUTCMinutes().toString().padStart(2, '0');

     // --- Compare with the current date in UTC ---
     const now = new Date();
     const isTodayUTC = date.getUTCDate() === now.getUTCDate() &&
                        date.getUTCMonth() === now.getUTCMonth() &&
                        date.getUTCFullYear() === now.getUTCFullYear();

     const timeString = `${hours}:${minutes}`;

     if (isTodayUTC) {
         return `${timeString} UTC`; // Only HH:MM for today (indicate UTC)
     } else {
         // Format more explicitly for past dates
         const monthNames = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"];
         const monthShort = monthNames[month]; // Use UTC month
         let datePart = `${monthShort} ${day}`;
         if (year !== now.getUTCFullYear()) { // Compare UTC year
             datePart += `, ${year}`;
         }
         return `${datePart}, ${timeString} UTC`; // Indicate UTC
     }
}

// Updated function signature to accept a custom formatter
function updateElementText(element, value, unit = '', precision = 1, naValue = '-', formatter = null) {
    if (!element) return;
    let text = naValue;
    let valueChanged = false;

    try {
        const currentValue = element.textContent; // Get current text before update

        if (formatter && value != null) { // Use formatter if value is not null/undefined
            text = formatter(value);
        } else if (value != null && !isNaN(value)) { // Default number formatting
            text = `${parseFloat(value).toFixed(precision)}${unit ? ` ${unit}` : ''}`;
        } else if (value != null) { // Handle non-numeric values directly (like stage name)
            text = `${value}${unit ? ` ${unit}` : ''}`;
        }

        // Use === for stricter comparison, handle potential type differences if needed
        if (currentValue !== text) {
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

    // Add a subtle flash effect on update only if value changed and element is not the target itself
    if (valueChanged && !element.classList.contains('updated')) { // Avoid re-triggering if animation is running
        element.classList.add('updated');
        requestAnimationFrame(() => {
            setTimeout(() => {
                element.classList.remove('updated');
            }, 600); // 600ms matches CSS
        });
    }
}

// --- Chart Functions ---
function createChart() {
    if (!El.measurementChartCtx) {
        console.error("Chart context not found");
        return null;
    }
    if (measurementChart) {
        console.log("Destroying existing chart instance.");
        measurementChart.destroy();
        measurementChart = null;
    }

    console.log("Creating new chart instance.");
    return new Chart(El.measurementChartCtx, {
        type: 'line',
        data: {
            labels: [], // Formatted Timestamps for display (UTC)
            datasets: [
                {
                    label: 'Temperature (°C)',
                    data: [], // Array of {x: epoch_ms, y: value}
                    borderColor: 'var(--gn-chart-temp)',
                    backgroundColor: 'rgba(255, 99, 132, 0.1)',
                    yAxisID: 'yTemp',
                    tension: 0.2,
                    pointRadius: 1,
                    pointHoverRadius: 5,
                    borderWidth: 1.5,
                    parsing: { xAxisKey: 'x', yAxisKey: 'y' }
                },
                {
                    label: 'Humidity (%)',
                    data: [], // Array of {x: epoch_ms, y: value}
                    borderColor: 'var(--gn-chart-humid)',
                    backgroundColor: 'rgba(54, 162, 235, 0.1)',
                    yAxisID: 'yHumid',
                    tension: 0.2,
                    pointRadius: 1,
                    pointHoverRadius: 5,
                    borderWidth: 1.5,
                     parsing: { xAxisKey: 'x', yAxisKey: 'y' }
                },
                {
                    label: 'Pump Activation',
                    data: [], // Array of {x: epoch_ms, y: temp_value_or_null}
                    borderColor: 'rgba(0,0,0,0)',
                    backgroundColor: 'var(--gn-chart-pump)',
                    pointBackgroundColor: 'var(--gn-chart-pump)',
                    yAxisID: 'yTemp',
                    pointStyle: 'triangle',
                    radius: 6,
                    hoverRadius: 8,
                    showLine: false,
                     parsing: { xAxisKey: 'x', yAxisKey: 'y' }
                }
            ]
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            scales: {
                x: {
                     type: 'category', // Using category axis with formatted labels
                     ticks: {
                         color: 'var(--gn-chart-text)',
                         maxRotation: 0,
                         autoSkip: true,
                         maxTicksLimit: 10,
                         callback: function(value, index, ticks) {
                             // 'value' is the index here. Get label from data.labels array
                             // Show limited labels to avoid clutter
                             if (index === 0 || index === ticks.length - 1 || index % Math.ceil(ticks.length / 10) === 0) {
                                  // Use getLabelForValue which reads from chart.data.labels[index]
                                  return this.getLabelForValue(index);
                             }
                             return ''; // Hide intermediate labels
                         }
                    },
                     grid: { color: 'var(--gn-chart-grid)' }
                },
                yTemp: {
                    type: 'linear', position: 'left',
                    title: { display: true, text: '°C', color: 'var(--gn-chart-temp)' },
                    ticks: { color: 'var(--gn-chart-temp)', precision: 0 },
                    grid: { color: 'var(--gn-chart-grid)' }
                },
                yHumid: {
                    type: 'linear', position: 'right',
                    title: { display: true, text: '%', color: 'var(--gn-chart-humid)' },
                    min: 0, max: 100,
                    ticks: { color: 'var(--gn-chart-humid)', precision: 0 },
                    grid: { drawOnChartArea: false }
                }
            },
            plugins: {
                tooltip: {
                    mode: 'index', intersect: false,
                    backgroundColor: 'rgba(0, 0, 0, 0.8)',
                    titleColor: '#fff', bodyColor: '#fff',
                    callbacks: {
                        // Display the formatted UTC timestamp in the tooltip title
                        title: function(tooltipItems) {
                            if (tooltipItems.length > 0) {
                                const item = tooltipItems[0];
                                // Get the raw epoch_ms value from the parsed data
                                const epochMs = item.parsed.x;
                                if (epochMs) {
                                     // Reuse the UTC formatting function
                                     return formatTimestampForHistory(epochMs);
                                }
                            }
                            return '';
                        },
                        label: function(context) {
                            let label = context.dataset.label || '';
                            if (!measurementChart) return label;

                            if (context.dataset.label === 'Pump Activation') {
                                return context.parsed.y !== null ? 'Pump Activated' : null;
                            }

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
                    labels: { color: 'var(--gn-chart-text)', usePointStyle: true, padding: 20 }
                }
            },
            interaction: { mode: 'nearest', axis: 'x', intersect: false },
             animation: { duration: 500 }
        }
    });
}

function updateChart(measurements) {
    if (!measurementChart && El.measurementChartCtx) {
        measurementChart = createChart();
    }
    if (!measurementChart) {
        console.error("Cannot update chart: instance not available.");
        return;
    }

    const limitedMeasurements = measurements.slice(-CHART_MAX_POINTS);

    // Use the UTC formatter for the axis labels
    const chartLabels = limitedMeasurements.map(m => formatTimestampForHistory(m.epoch_ms));

    // Prepare data in {x: epoch_ms, y: value} format
    const tempData = limitedMeasurements.map(m => ({
        x: m.epoch_ms,
        y: m.temperature ?? null
    }));
    const humidData = limitedMeasurements.map(m => ({
        x: m.epoch_ms,
        y: m.humidity ?? null
    }));
    const pumpPointsData = limitedMeasurements.map(m => ({
        x: m.epoch_ms,
        y: m.pumpActivated ? (m.temperature ?? null) : null
    }));

    measurementChart.data.labels = chartLabels; // Axis display labels (formatted UTC)
    measurementChart.data.datasets[0].data = tempData;
    measurementChart.data.datasets[1].data = humidData;
    measurementChart.data.datasets[2].data = pumpPointsData;

    measurementChart.update();
}


async function updateUI() {
    console.log("Updating UI (UTC Display Mode)...");
    try {
        if (currentIntervalValue === null) {
            await fetchAndUpdateIntervalDisplay();
        }

        const [dataRes, histRes] = await Promise.allSettled([
            fetchData('/data'),
            fetchData('/loadMeasurement')
        ]);

        // --- Process coreData ---
        if (dataRes.status === 'fulfilled' && dataRes.value) {
            const coreData = dataRes.value;
            // Update basic widgets
            if (El.pageTitle) El.pageTitle.textContent = `Green Nanny ${coreData.pumpStatus ? '💧' : '🌿'}`;
            updateElementText(El.temp, coreData.temperature, '°C');
            updateElementText(El.humidity, coreData.humidity, '%');
            updateElementText(El.vpd, coreData.vpd, ' kPa', 2);
            updateElementText(El.pumpActivationCount, coreData.pumpActivationCount, '', 0);
            updateElementText(El.totalElapsedTime, coreData.elapsedTime, '', 0, 'N/A', formatElapsedTime);

            // Update Last Measurement Time using reliable timestamp, FORMATTING IN UTC
            updateElementText(
                El.lastMeasurementTime,
                coreData.lastMeasurementTimestamp, // epoch_ms from backend
                '', 0, '-',
                ts => {
                    if (!ts || ts <= 0) return '-';
                    const d = new Date(ts);
                    // Format using Intl.DateTimeFormat for robust UTC display
                    const options = {
                        year: 'numeric', month: 'short', day: 'numeric',
                        hour: '2-digit', minute: '2-digit', second: '2-digit',
                        hour12: false, // Use 24h format
                        timeZone: 'UTC' // Specify UTC timezone
                    };
                    try {
                        // 'en-GB' or similar locale often gives DD/MM/YYYY HH:MM:SS
                        // 'undefined' uses browser default locale but forces UTC
                        return new Intl.DateTimeFormat(undefined, options).format(d) + ' UTC';
                    } catch (e) {
                         // Fallback simple UTC string if Intl fails
                         console.warn("Intl.DateTimeFormat failed, using toUTCString fallback.", e);
                         return d.toUTCString();
                    }
                }
            );

            // Update Stage info
            const cs = {
              name: coreData.currentStageName,
              threshold: coreData.currentStageThreshold,
              watering: coreData.currentStageWateringSec,
              manual: coreData.manualStageControl
            };
            currentStageData = {
                index: coreData.currentStageIndex,
                name: cs.name,
                manual: cs.manual
            };
            updateElementText(El.currentStageLabel, cs.name, '', null, '-');
            if (El.currentStageParams) El.currentStageParams.textContent = `Threshold: ${cs.threshold}% | Water: ${cs.watering}s`;
            if (El.manualControlIndicator) El.manualControlIndicator.style.display = cs.manual ? 'inline-block' : 'none';
            if (El.stageModeIndicator) {
              El.stageModeIndicator.textContent = cs.manual ? 'Manual' : 'Auto';
              El.stageModeIndicator.className = `badge ${cs.manual ? 'bg-warning text-dark' : 'bg-info'}`;
            }

            // Update Pump status
            if (El.pumpStatusIndicator) {
              const on = coreData.pumpStatus ?? false;
              El.pumpStatusIndicator.textContent = on ? 'ON' : 'OFF';
              El.pumpStatusIndicator.className = `badge ${on ? 'bg-success' : 'bg-secondary'}`;
              El.pumpStatusIndicator.classList.toggle('pulsing', on);
              if (El.activatePumpBtn) El.activatePumpBtn.disabled = on;
              if (El.deactivatePumpBtn) El.deactivatePumpBtn.disabled = !on;
            }

            // Update WiFi status
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
             // Update Device IP in footer
            if (El.deviceIP) {
                 updateElementText(El.deviceIP, coreData.deviceIP || window.location.hostname, '', null, '-');
            }

        } else {
            console.error('Failed to fetch core data:', dataRes.reason || 'Unknown error');
            showToast('Error fetching core device data!', 'error', 10000);
        }

        // --- Measurement History ---
        let measurements = [];
        if (histRes.status === 'fulfilled' && Array.isArray(histRes.value)) {
            measurements = histRes.value;
        } else {
            console.warn('Failed to fetch or parse measurement history:', histRes.reason || 'Empty response');
             if (El.measurementHistory) {
                 El.measurementHistory.innerHTML = `<div class="measurement-history-entry text-muted">Could not load history.</div>`;
             }
        }

        // --- Render History Log (using UTC timestamps) ---
        if (El.measurementHistory) {
            const start = Math.max(0, measurements.length - HISTORY_MAX_ENTRIES);
            const recentMeasurements = measurements.slice(start).reverse(); // Most recent first

            const html = recentMeasurements.map(m => {
                const ts = m.epoch_ms;
                // Use the UTC formatter for history display
                const displayTime = formatTimestampForHistory(ts);

                const tempText = m.temperature != null ? `${m.temperature.toFixed(1)}°C` : 'N/A';
                const humidText = m.humidity != null ? `${m.humidity.toFixed(1)}%` : 'N/A';

                return `
                    <div class="measurement-history-entry">
                        <span class="timestamp">${displayTime}</span>
                        <span class="data-point"><i class="fas fa-thermometer-half"></i>${tempText}</span>
                        <span class="data-point"><i class="fas fa-tint"></i>${humidText}</span>
                        ${m.pumpActivated ? '<span class="data-point pump-info"><i class="fas fa-play"></i>Pump</span>' : ''}
                        ${m.stage ? `<span class="data-point stage-info"><i class="fas fa-seedling"></i>${m.stage}</span>` : ''}
                    </div>
                `;
            }).join('') || `<div class="measurement-history-entry text-muted">No history recorded.</div>`;

            El.measurementHistory.innerHTML = html;
             El.measurementHistory.scrollTop = El.measurementHistory.scrollHeight; // Keep scrolling to bottom (newest = bottom)
        }

        // --- Update Chart ---
        updateChart(measurements);

    } catch (err) {
        console.error('Error during UI update cycle:', err);
        showToast(`UI Update failed: ${err.message}`, 'error');
    } finally {
        clearTimeout(refreshTimeoutId);
        refreshTimeoutId = setTimeout(updateUI, REFRESH_INTERVAL);
    }
}


// Function to fetch and update the displayed interval (no change needed)
async function fetchAndUpdateIntervalDisplay() {
     if (!El.currentInterval || !El.intervalInput) return;
     try {
        const intervalData = await fetchData('/getMeasurementInterval');
        if (intervalData && intervalData.interval != null) {
             currentIntervalValue = intervalData.interval;
             El.currentInterval.textContent = currentIntervalValue;
             El.intervalInput.value = currentIntervalValue;
             El.intervalInput.placeholder = `Current: ${currentIntervalValue}h`;
        }
     } catch (error) {
         console.error("Failed to fetch measurement interval:", error);
         El.currentInterval.textContent = 'Error';
     }
}

// Function to populate stage selector (no change needed)
async function populateStageSelector() {
    if (!El.manualStageSelect) return;
    El.manualStageSelect.innerHTML = `<option value="" disabled selected>Loading stages...</option>`;
    try {
        const stages = await fetchData('/listStages');
        if (stages && Array.isArray(stages) && stages.length > 0) {
            El.manualStageSelect.innerHTML = stages.map(stage =>
                `<option value="${stage.index}">${stage.name} (T:${stage.humidityThreshold}% W:${stage.wateringTimeSec}s)</option>`
            ).join('');
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

    // --- Forms --- (No changes needed in form logic)
    El.intervalForm?.addEventListener('submit', async (e) => {
        e.preventDefault();
        const value = El.intervalInput?.value;
        const numValue = parseInt(value);
        if (value === '' || isNaN(numValue) || numValue < 1 || numValue > 167) {
             showToast('Please enter a valid interval (1-167 hours).', 'warning');
             El.intervalInput?.classList.add('is-invalid'); return;
        }
         El.intervalInput?.classList.remove('is-invalid');
        const button = El.intervalForm.querySelector('button[type="submit"]');
        setLoadingState(button, true);
        try {
            await postData('/setMeasurementInterval', { interval: numValue });
            showToast(`Measurement interval set to ${numValue} hours`, 'success');
            currentIntervalValue = numValue;
             if(El.currentInterval) El.currentInterval.textContent = currentIntervalValue;
             El.intervalInput.placeholder = `Current: ${currentIntervalValue}h`;
        } catch (error) { /* Handled by postData */ }
        finally { setLoadingState(button, false); }
    });

     El.intervalInput?.addEventListener('input', () => {
          const value = El.intervalInput.value; const numValue = parseInt(value);
         El.intervalInput.classList.toggle('is-invalid', value === '' || isNaN(numValue) || numValue < 1 || numValue > 167);
     });

    El.manualStageForm?.addEventListener('submit', async (e) => {
        e.preventDefault();
        const value = El.manualStageSelect?.value; const numValue = parseInt(value);
        if (value === '' || isNaN(numValue)) {
            showToast('Please select a valid stage.', 'warning');
             El.manualStageSelect?.classList.add('is-invalid'); return;
        }
         El.manualStageSelect?.classList.remove('is-invalid');
        const button = El.manualStageForm.querySelector('button[type="submit"]');
        setLoadingState(button, true);
        try {
            await postData('/setManualStage', { stage: numValue });
            showToast(`Manual stage set to "${El.manualStageSelect.options[El.manualStageSelect.selectedIndex].text.split('(')[0].trim()}"`, 'success');
            await updateUI(); // Refresh UI to show change
        } catch (error) { /* Handled by postData */ }
        finally { setLoadingState(button, false); }
    });

    // --- Buttons --- (No changes needed in button action logic)
    El.activatePumpBtn?.addEventListener('click', async () => {
        const durationStr = prompt('Enter pump activation duration in seconds (e.g., 30):', '30');
        if (durationStr === null) return;
        const duration = parseInt(durationStr);
        if (isNaN(duration) || duration <= 0) {
            showToast('Invalid duration. Please enter a positive number of seconds.', 'warning'); return;
        }
        setLoadingState(El.activatePumpBtn, true);
        try {
            await postData('/controlPump', { action: 'on', duration: duration });
            showToast(`Pump activated manually for ${duration} seconds.`, 'success');
             await updateUI();
        } catch (error) { await updateUI(); } // Refresh even on error
    });

    El.deactivatePumpBtn?.addEventListener('click', async () => {
        if (!confirm('Are you sure you want to manually deactivate the pump?')) return;
        setLoadingState(El.deactivatePumpBtn, true);
        try {
            await postData('/controlPump', { action: 'off' });
            showToast('Pump deactivated manually.', 'success');
             await updateUI();
        } catch (error) { await updateUI(); } // Refresh even on error
    });

    El.takeMeasurementBtn?.addEventListener('click', async () => {
        setLoadingState(El.takeMeasurementBtn, true);
        showToast('Triggering manual measurement cycle...', 'info', 2000);
        try {
            await postData('/takeMeasurement', {}, 'POST');
            await new Promise(resolve => setTimeout(resolve, 1500)); // Wait for device
            await updateUI(); // Refresh UI with new measurement
            showToast('Measurement cycle complete.', 'success', 2000);
        } catch (error) { /* Handled by postData */ }
        finally { setLoadingState(El.takeMeasurementBtn, false); }
    });

    El.clearMeasurementsBtn?.addEventListener('click', async () => {
        if (!confirm('⚠️ DELETE ALL MEASUREMENT HISTORY? ⚠️\n\nThis action cannot be undone.')) return;
        setLoadingState(El.clearMeasurementsBtn, true);
        try {
            await postData('/clearHistory', {}, 'POST');
            showToast('Measurement history cleared successfully.', 'success');
            if(measurementChart) {
                 measurementChart.data.labels = [];
                 measurementChart.data.datasets.forEach(ds => ds.data = []);
                 measurementChart.update('none');
            }
            if(El.measurementHistory) El.measurementHistory.innerHTML = `<div class='measurement-history-entry text-muted'>History cleared.</div>`;
            await updateUI(); // Fetch empty history
        } catch (error) { /* Handled by postData */ }
        finally { setLoadingState(El.clearMeasurementsBtn, false); }
    });

    El.restartSystemBtn?.addEventListener('click', async () => {
        if (!confirm('Are you sure you want to restart the Green Nanny device?\n\nThe dashboard will become unresponsive.')) return;
        setLoadingState(El.restartSystemBtn, true);
        showToast('Sending restart command...', 'warning');
        document.body.classList.add('system-restarting');
        try {
             postData('/restartSystem', {}, 'POST'); // Fire and forget
             setTimeout(() => {
                 alert('System restart command sent.\nPlease wait ~30 seconds and manually refresh this page.');
                 document.querySelectorAll('button, input, select').forEach(el => el.disabled = true);
             }, 2000);
        } catch (error) {
            showToast('Failed to send restart command. Restart manually.', 'error');
            setLoadingState(El.restartSystemBtn, false);
             document.body.classList.remove('system-restarting');
        }
    });

    El.refreshStageBtn?.addEventListener('click', async () => {
         setLoadingState(El.refreshStageBtn, true);
         try {
             await updateUI();
             showToast('Stage information refreshed.', 'info', 2000);
         } catch (error) { /* Handled by updateUI */ }
         finally { setLoadingState(El.refreshStageBtn, false); }
    });

    El.resetManualStageBtn?.addEventListener('click', async () => {
        if (!confirm('Return to automatic stage progression?')) return;
        setLoadingState(El.resetManualStageBtn, true);
        try {
            await postData('/resetManualStage', {}, 'POST');
            showToast('Stage control reset to automatic.', 'success');
            await updateUI(); // Refresh UI
        } catch (error) { /* Handled by postData */ }
        finally { setLoadingState(El.resetManualStageBtn, false); }
    });

    // --- Initialize Tooltips --- (no change needed)
    if (typeof bootstrap !== 'undefined' && bootstrap.Tooltip) {
        const tooltipTriggerList = [].slice.call(document.querySelectorAll('[data-bs-toggle="tooltip"]'));
        tooltipTriggerList.map(function (tooltipTriggerEl) {
            return new bootstrap.Tooltip(tooltipTriggerEl, { trigger: 'hover focus' });
        });
    } else {
        console.warn("Bootstrap Tooltip component not found.");
    }
}


// --- Initialization ---
document.addEventListener('DOMContentLoaded', async () => {
    console.log("DOM loaded. Initializing Green Nanny Dashboard (UTC Display Mode)...");
    showGlobalLoader(true);

    if (!El.measurementChartCtx) {
         console.warn("Chart canvas context not found. Chart will be disabled.");
    }

    setupEventListeners(); // Setup listeners first

    try {
        // Initial UI population: fetches data, history, updates chart/log
        await updateUI();

        // Populate stage selector based on current state fetched by updateUI
        await populateStageSelector();

        // Fetch and display initial interval
        await fetchAndUpdateIntervalDisplay();

        console.log("Initialization complete.");

    } catch (error) {
        console.error("Dashboard initialization failed:", error);
        showToast("Failed to load initial dashboard data. Check device connection and refresh.", "error", 10000);
        const mainContent = document.querySelector('.content-wrapper');
        if(mainContent) {
             mainContent.insertAdjacentHTML('afterbegin', `<div class="alert alert-danger m-3"><strong>Initialization Error:</strong> Could not load initial data. Check device connection and refresh.</div>`);
        }
    } finally {
         showGlobalLoader(false);
         // Auto-refresh is started by the first updateUI call's finally block
    }
});
// --- END OF FILE app.js ---