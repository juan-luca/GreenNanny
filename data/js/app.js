
const BASE_URL = `http://${window.location.hostname}`;
const REFRESH_INTERVAL = 15000;
const CHART_MAX_POINTS = 180;
const HISTORY_MAX_ENTRIES = 150;
const TOAST_TIMEOUT = 6000;
const FETCH_TIMEOUT = 20000;
const MS_PER_HOUR = 3600 * 1000;
const MS_PER_DAY = 24 * MS_PER_HOUR;
const UPDATE_FLASH_DURATION = 700;

// --- Element Selectors (Cached) ---
const El = {
    // Widgets
    tempWidget: document.getElementById('tempWidget'),
    humidWidget: document.getElementById('humidWidget'),
    stageWidget: document.getElementById('stageWidget'),
    pumpCountWidget: document.getElementById('pumpCountWidget'),
    vpdWidget: document.getElementById('vpdWidget'),
    uptimeWidget: document.getElementById('uptimeWidget'),
    systemStatusWidget: document.getElementById('systemStatusWidget'),
    // Widget Inner Values
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
    wifiStatusWrapper: document.getElementById('wifiStatusWrapper'), // Outer span for tooltip
    wifiStatusContent: document.getElementById('wifiStatusContent'), // Inner span for content
    lastMeasurementTime: document.getElementById('lastMeasurementTime'),
    updatingIndicator: document.getElementById('updatingIndicator'),
    // Chart & History
    measurementChartContainer: document.getElementById('measurementChartContainer'),
    measurementChartCtx: document.getElementById('measurementChart')?.getContext('2d'),
    measurementHistoryContainer: document.getElementById('measurementHistoryContainer'),
    measurementHistory: document.getElementById('measurementHistory'),
    // Statistics
    statisticsSection: document.getElementById('statisticsSection'),
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
    navbarDeviceName: document.getElementById('navbarDeviceName'),
    srStatusMessages: document.getElementById('sr-status-messages'),
    // Stage Configuration
    stageConfigLoading: document.getElementById('stageConfigLoading'),
    stageConfigError: document.getElementById('stageConfigError'),
    stageConfigTableContainer: document.getElementById('stageConfigTableContainer'),
    stageConfigTableBody: document.getElementById('stageConfigTableBody'),
};

// --- State Variables ---
let measurementChart = null;
let refreshTimeoutId = null;
let currentStageData = {};
let currentIntervalValue = null;
let allStagesInfo = []; // Will hold data from /listStages, potentially modified
let lastCoreData = null;
let bsTooltips = [];

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
        clearTimeout(timeoutId);

        if (!res.ok) {
            let errorMsg = `HTTP error ${res.status} fetching ${endpoint}`;
            try {
                const errorBody = await res.text();
                if (errorBody) errorMsg += ` - ${errorBody.substring(0, 100)}`;
            } catch (_) { /* Ignore */ }
            throw new Error(errorMsg);
        }
        const contentType = res.headers.get("content-type");
        if (contentType && contentType.includes("application/json")) {
            return await res.json();
        } else {
            return await res.text();
        }
    } catch (e) {
        clearTimeout(timeoutId);
        console.error(`Error fetching ${endpoint}:`, e);
        if (e.name !== 'AbortError') {
            showToast(`Network error (${endpoint.split('/')[1]}): ${e.message}`, 'error');
        } else {
            showToast(`Request timed out (${endpoint.split('/')[1]})`, 'warning');
        }
        throw e;
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
        if (method !== 'GET' && method !== 'HEAD' && Object.keys(data).length > 0) {
             options.body = JSON.stringify(data);
        }

        const res = await fetch(`${BASE_URL}${endpoint}`, options);
         clearTimeout(timeoutId);

        if (!res.ok) {
            let errorMsg = `Command error ${res.status} on ${endpoint}`;
            try {
                const errorBody = await res.text();
                 if (errorBody) errorMsg += ` - ${errorBody.substring(0, 100)}`;
            } catch (_) { /* Ignore */ }
            throw new Error(errorMsg);
        }

        const contentType = res.headers.get("content-type");
        if (contentType && contentType.includes("application/json")) {
            return await res.json();
        } else {
            return await res.text();
        }
    } catch (e) {
        clearTimeout(timeoutId);
        console.error(`Error posting to ${endpoint}:`, e);
        if (e.name !== 'AbortError') {
            showToast(`Command error (${endpoint.split('/')[1]}): ${e.message}`, 'error');
        } else {
             showToast(`Command timed out (${endpoint.split('/')[1]})`, 'warning');
        }
        throw e;
    } finally {
        showGlobalLoader(false);
    }
}

// --- UI Helper Functions ---

function announceSRStatus(message) {
    if (El.srStatusMessages) {
        // Use a slight delay to ensure screen readers pick up changes reliably
        setTimeout(() => {
            El.srStatusMessages.textContent = message;
        }, 150);
    }
}

function showToast(message, type = 'info', duration = TOAST_TIMEOUT) {
    if (!El.toastContainer) return;
    // Limit number of toasts shown
    if (El.toastContainer.children.length >= 5) {
        El.toastContainer.removeChild(El.toastContainer.firstChild);
    }

    const toastId = `toast-${Date.now()}`;
    const toast = document.createElement('div');
    const bgClass = { info: 'bg-info', success: 'bg-success', warning: 'bg-warning text-dark', error: 'bg-danger' }[type] || 'bg-secondary';
    const iconClass = { info: 'fas fa-info-circle', success: 'fas fa-check-circle', warning: 'fas fa-exclamation-triangle', error: 'fas fa-times-circle' }[type] || 'fas fa-bell';
    const textClass = (type === 'warning') ? '' : 'text-white'; // Use default text color for warning bg
    toast.id = toastId;
    toast.className = `toast align-items-center ${textClass} ${bgClass} border-0 mb-2 shadow-lg`;
    toast.setAttribute('role', 'alert');
    toast.setAttribute('aria-live', type === 'error' ? 'assertive' : 'polite'); // Assertive for errors
    toast.setAttribute('aria-atomic', 'true');
    const closeBtnClass = (type === 'warning') ? 'btn-close-dark' : 'btn-close-white'; // Dark close button on light warning bg

    toast.innerHTML = `
        <div class="d-flex">
            <div class="toast-body">
                <i class="${iconClass}" aria-hidden="true"></i> ${message}
            </div>
            <button type="button" class="btn-close ${closeBtnClass} me-2 m-auto" data-bs-dismiss="toast" aria-label="Close"></button>
        </div>
    `;
    El.toastContainer.appendChild(toast);

    try {
        const bsToast = new bootstrap.Toast(toast, { delay: duration, autohide: true });
        bsToast.show();
        // Announce non-error toasts for screen readers
        if (type !== 'error') announceSRStatus(`${type}: ${message}`);
        // Remove toast from DOM after it's hidden
        toast.addEventListener('hidden.bs.toast', () => toast.remove(), { once: true });
    } catch (e) {
        console.error("Failed to initialize Bootstrap toast:", e);
        toast.remove(); // Clean up if init fails
    }
}

function setLoadingState(buttonElement, isLoading, loadingText = "Loading...") {
    if (!buttonElement) return;
    const loaderClass = 'loader'; // Or 'spinner-border spinner-border-sm' if using Bootstrap directly
    const originalContentKey = 'originalContent';

    if (isLoading) {
        // Store original content only if not already loading
        if (!buttonElement.dataset[originalContentKey]) {
            buttonElement.dataset[originalContentKey] = buttonElement.innerHTML;
        }
        buttonElement.disabled = true;
        buttonElement.setAttribute('aria-busy', 'true');
        // Replace content with loader and text
        buttonElement.innerHTML = `<span class="${loaderClass}" role="status" aria-hidden="true"></span><span class="ms-2">${loadingText}</span>`;
    } else {
        buttonElement.disabled = false;
        buttonElement.removeAttribute('aria-busy');
        // Restore original content if it was stored
        if (buttonElement.dataset[originalContentKey]) {
            buttonElement.innerHTML = buttonElement.dataset[originalContentKey];
            delete buttonElement.dataset[originalContentKey]; // Clean up data attribute
        } else {
            // Fallback if original content wasn't stored (e.g., state changed unexpectedly)
            // Try to determine a sensible default based on button ID or class
             let defaultText = 'Action'; // Generic fallback
             if (buttonElement.classList.contains('save-stage-btn')) defaultText = '<i class="fas fa-save me-1" aria-hidden="true"></i>Save';
             else if (buttonElement.id === 'activatePump') defaultText = '<i class="fas fa-play-circle" aria-hidden="true"></i> Activate Pump...';
             else if (buttonElement.id === 'deactivatePump') defaultText = '<i class="fas fa-stop-circle" aria-hidden="true"></i> Deactivate Pump';
             else if (buttonElement.id === 'takeMeasurement') defaultText = '<i class="fas fa-ruler-combined" aria-hidden="true"></i> Trigger Measurement';
             else if (buttonElement.id === 'clearMeasurements') defaultText = '<i class="fas fa-trash-alt" aria-hidden="true"></i> Clear History';
             else if (buttonElement.id === 'restartSystem') defaultText = '<i class="fas fa-power-off" aria-hidden="true"></i> Restart System';
             else if (buttonElement.id === 'resetManualStageButton') defaultText = '<i class="fas fa-undo" aria-hidden="true"></i> Reset to Auto';
             else if (buttonElement.id === 'refreshStage') defaultText = '<i class="fas fa-sync" aria-hidden="true"></i> Refresh';
             // Attempt to find the button text if no icon was present initially
             const textNode = Array.from(buttonElement.childNodes).find(node => node.nodeType === Node.TEXT_NODE);
             if(textNode) defaultText = textNode.textContent.trim();

             buttonElement.innerHTML = defaultText; // Set the determined fallback
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
    if (m > 0 || parts.length === 0) parts.push(`${m}m`); // Show minutes even if 0 if no days/hours
    return parts.length > 0 ? parts.join(' ') : '0m';
}

function formatTimestampForHistory(epochMs) {
     // Basic validation for epoch timestamp (milliseconds since 1970)
     if (!epochMs || isNaN(epochMs) || epochMs < 1e12) return '?:??';
     const date = new Date(epochMs);
     const now = new Date();
     // Define formatting options, force UTC display
     const options = { month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit', hour12: false, timeZone: 'UTC' };
     // Add year only if it's not the current year
     if(date.getUTCFullYear() !== now.getUTCFullYear()) {
         options.year = 'numeric';
     }
     try {
         // Format using Intl for better locale support (using 'en-GB' for DD/MM HH:MM format)
         let formatted = new Intl.DateTimeFormat('en-GB', options).format(date);
         // Check if the date is today (in UTC)
         const isTodayUTC = date.getUTCDate() === now.getUTCDate() &&
                            date.getUTCMonth() === now.getUTCMonth() &&
                            date.getUTCFullYear() === now.getUTCFullYear();
         // If today, show "Today, HH:MM UTC"
         if (isTodayUTC) {
              const timeFormatted = date.toLocaleTimeString('en-GB', { hour: '2-digit', minute: '2-digit', hour12: false, timeZone: 'UTC' });
              return `Today, ${timeFormatted} UTC`;
         }
         // Otherwise, return the formatted date/time + UTC
         return `${formatted} UTC`;
     } catch (e) {
         // Fallback for older browsers or errors
         console.warn("Intl.DateTimeFormat failed:", e);
         // Provide a basic UTC string representation
         return date.toUTCString().substring(5, 22); // Extracts "DD Mon YYYY HH:MM:SS" part
     }
}

function updateElementText(element, value, unit = '', precision = 1, naValue = '-', formatter = null) {
    if (!element) return;
    let text = naValue;
    let isNA = true;
    try {
        const currentValue = element.innerHTML; // Use innerHTML to compare with potentially complex content
        if (formatter && value != null) {
            text = formatter(value);
            // Check if formatter returned the N/A value or an empty string
            isNA = (text === naValue || text === null || text === undefined || text === '');
        } else if (value != null && !isNaN(value)) {
            // Format numeric values
            text = `${parseFloat(value).toFixed(precision)}${unit ? `<span class="text-muted small ms-1">${unit}</span>` : ''}`;
            isNA = false;
        } else if (typeof value === 'string' && value) {
            // Allow plain non-empty strings
            text = `${value}${unit ? `<span class="text-muted small ms-1">${unit}</span>` : ''}`;
            isNA = false;
        }

        // Only update DOM if content actually changed
        if (element.innerHTML !== text) {
            element.innerHTML = text; // Use innerHTML to set potentially complex content
            element.classList.toggle('widget-value-na', isNA); // Update N/A styling

            // Apply update flash effect only if not N/A and part of a widget
            if (!isNA && element.closest('.small-box, .widget-dark')) {
                 element.classList.remove('updated');
                 void element.offsetWidth; // Force reflow to restart animation
                 element.classList.add('updated');
                 setTimeout(() => { element.classList.remove('updated'); }, UPDATE_FLASH_DURATION);
            }
        } else {
            // If text is the same, still ensure NA class is correct
             element.classList.toggle('widget-value-na', isNA);
        }
    } catch (e) {
         console.error("Error formatting element:", element?.id, value, e);
         // Fallback to N/A value on error
         if (element.innerHTML !== naValue) { // Use innerHTML for comparison
            element.innerHTML = naValue; // Use innerHTML to set fallback
            element.classList.add('widget-value-na');
         }
    }
}


// --- Chart Functions ---
function createChart() {
    if (!El.measurementChartCtx) { console.error("Chart context not found"); return null; }
    // Destroy existing chart instance if it exists
    if (measurementChart) {
         console.log("Destroying existing chart before creating new one.");
         measurementChart.destroy();
         measurementChart = null;
    }
    console.log("Creating new chart instance.");
    try {
        return new Chart(El.measurementChartCtx, {
            type: 'line',
            data: {
                labels: [], // Populated by updateChart
                datasets: [
                    {
                        label: 'Temperature (°C)',
                        data: [], // Populated by updateChart
                        borderColor: 'var(--gn-chart-temp)',
                        backgroundColor: 'rgba(255, 99, 132, 0.05)', // Subtle fill
                        yAxisID: 'yTemp',
                        tension: 0.3, // Smoother lines
                        pointRadius: 0, // Hide points by default
                        pointHoverRadius: 5,
                        borderWidth: 1.5,
                        parsing: { xAxisKey: 'x', yAxisKey: 'y' } // Use object data structure
                    },
                    {
                        label: 'Humidity (%)',
                        data: [], // Populated by updateChart
                        borderColor: 'var(--gn-chart-humid)',
                        backgroundColor: 'rgba(54, 162, 235, 0.05)', // Subtle fill
                        yAxisID: 'yHumid',
                        tension: 0.3,
                        pointRadius: 0,
                        pointHoverRadius: 5,
                        borderWidth: 1.5,
                        parsing: { xAxisKey: 'x', yAxisKey: 'y' }
                    },
                    {
                        label: 'Pump Activation',
                        data: [], // Populated by updateChart
                        borderColor: 'transparent', // No line for pump events
                        backgroundColor: 'var(--gn-chart-pump)', // Point color
                        pointBackgroundColor: 'var(--gn-chart-pump)',
                        yAxisID: 'yHumid', // Plot against humidity axis
                        pointStyle: 'triangle', // Use triangle for pump events
                        radius: 7, // Size of the triangle
                        hoverRadius: 9,
                        showLine: false, // Ensure no line is drawn
                        parsing: { xAxisKey: 'x', yAxisKey: 'y' }
                    }
                ]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                scales: {
                    x: {
                        type: 'category', // Using formatted timestamps as categories
                        ticks: {
                             color: 'var(--gn-chart-text)',
                             maxRotation: 0, // Prevent label rotation
                             autoSkip: true, // Automatically skip labels to avoid overlap
                             maxTicksLimit: 8, // Limit number of visible ticks
                             font: { size: 10 },
                             // Callback to show only first/last and some intermediate labels
                             callback: function(value, index, ticks) {
                                 const showEvery = Math.ceil(ticks.length / 6); // Show ~6 labels + first/last
                                 if (index === 0 || index === ticks.length - 1 || index % showEvery === 0) {
                                     // Attempt to get label from the ticks array if available
                                     return ticks[index] ? ticks[index].label : '';
                                 }
                                 return ''; // Return empty string for labels to skip
                             }
                        },
                        grid: {
                             color: 'var(--gn-chart-grid)',
                             borderDash: [2, 3] // Dashed grid lines
                        }
                    },
                    yTemp: { // Temperature Axis (Left)
                        type: 'linear',
                        position: 'left',
                        title: { display: true, text: '°C', color: 'var(--gn-chart-temp)', font: { size: 11, weight: 'bold' } },
                        ticks: { color: 'var(--gn-chart-temp)', precision: 0, font: { size: 10 } },
                        grid: { color: 'var(--gn-chart-grid)' }
                    },
                    yHumid: { // Humidity Axis (Right)
                        type: 'linear',
                        position: 'right',
                        title: { display: true, text: '%', color: 'var(--gn-chart-humid)', font: { size: 11, weight: 'bold' } },
                        min: 0, // Humidity starts at 0
                        suggestedMax: 100, // Suggest 100% max
                        ticks: { color: 'var(--gn-chart-humid)', precision: 0, font: { size: 10 } },
                        grid: { drawOnChartArea: false } // Don't draw grid lines for the right axis
                    }
                },
                plugins: {
                    tooltip: {
                        enabled: true,
                        mode: 'index', // Show tooltips for all datasets at the same index
                        intersect: false, // Tooltip activates even if not directly hovering over a point
                        backgroundColor: 'var(--gn-chart-tooltip-bg)',
                        titleColor: '#fff', bodyColor: '#fff',
                        padding: 8, cornerRadius: 3,
                        titleFont: { size: 11 }, bodyFont: { size: 11 },
                        titleAlign: 'center', bodyAlign: 'left',
                        // Custom tooltip content
                        callbacks: {
                             // Format timestamp for the title
                             title: (tooltipItems) => formatTimestampForHistory(tooltipItems[0]?.parsed.x),
                             // Format each line in the tooltip body
                             label: (context) => {
                                 let label = context.dataset.label || '';
                                 // Special handling for pump activation dataset
                                 if (context.dataset.label === 'Pump Activation') {
                                     // Only show label if pump was actually activated (y value is not null)
                                     return context.parsed.y !== null ? ' Pump Activated' : null; // null hides the line
                                 }
                                 // Standard formatting for Temp/Humid
                                 if (label) label += ': ';
                                 if (context.parsed.y !== null) {
                                     label += context.parsed.y.toFixed(1);
                                     if(context.dataset.label === 'Temperature (°C)') label += '°C';
                                     if(context.dataset.label === 'Humidity (%)') label += '%';
                                 } else {
                                     label += 'N/A';
                                 }
                                 return label;
                             }
                         }
                    },
                    legend: {
                        position: 'bottom', align: 'center',
                        labels: {
                             color: 'var(--gn-chart-text)',
                             usePointStyle: true, // Use point style (circle/triangle) in legend
                             padding: 15,
                             boxWidth: 10, // Width of the color box/point
                             font: { size: 11 }
                        },
                        // Optional: Hide dataset on legend click (default behavior)
                        // Optional: Add hover effect to highlight dataset
                        onHover: (e, legendItem, legend) => { try { const ci = legend.chart; const index = legendItem.datasetIndex; const meta = ci.getDatasetMeta(index); meta.borderWidth = 3; ci.update(); } catch(e) {console.warn('Chart legend hover error', e)} },
                        onLeave: (e, legendItem, legend) => { try { const ci = legend.chart; const index = legendItem.datasetIndex; const meta = ci.getDatasetMeta(index); meta.borderWidth = 1.5; ci.update(); } catch(e) {console.warn('Chart legend leave error', e)} }
                    }
                },
                interaction: {
                    mode: 'nearest', // Find nearest item in all datasets
                    axis: 'x',      // Interaction happens across the x-axis
                    intersect: false
                },
                animation: { duration: 400 }, // Short animation duration
                layout: { padding: { top: 5, right: 5, bottom: 0, left: 0 } } // Minimal padding
            }
        });
    } catch (e) {
        console.error("Chart creation failed:", e);
        if (El.measurementChartContainer) El.measurementChartContainer.innerHTML = `<div class="alert alert-danger p-3 m-2"><i class="fas fa-exclamation-triangle me-2"></i>Chart Error: Could not initialize.</div>`;
        return null;
    }
}

function updateChart(measurements) {
     const loadingIndicator = El.measurementChartContainer?.querySelector('.loading-indicator');
     const emptyIndicator = El.measurementChartContainer?.querySelector('.empty-indicator');
     const canvas = El.measurementChartCtx?.canvas;

     if (!El.measurementChartCtx || !canvas) {
         console.warn("Cannot update chart: Context or canvas not available.");
         return;
     }

     const hasData = measurements && measurements.length > 0;

     // Manage visibility of indicators and canvas
     if (loadingIndicator) loadingIndicator.style.display = 'none'; // Always hide loading indicator after attempt
     if (emptyIndicator) emptyIndicator.style.display = hasData ? 'none' : 'block';
     canvas.style.display = hasData ? 'block' : 'none';

     // If no data, clear the chart and exit
     if (!hasData) {
         if (measurementChart) {
             measurementChart.data.labels = [];
             measurementChart.data.datasets.forEach(ds => ds.data = []);
             measurementChart.update('none'); // Update without animation
         }
         return;
     }

     // Ensure chart instance exists (create if needed)
     if (!measurementChart) {
         measurementChart = createChart();
         if (!measurementChart) return; // Exit if creation failed
     }

     // Limit data points to avoid performance issues
     const limitedMeasurements = measurements.slice(-CHART_MAX_POINTS);

     // Prepare data for Chart.js format (using objects for parsing keys)
     const chartLabels = limitedMeasurements.map(m => formatTimestampForHistory(m.epoch_ms));
     const tempData = limitedMeasurements.map(m => ({ x: m.epoch_ms, y: m.temperature ?? null })); // Use null for missing data
     const humidData = limitedMeasurements.map(m => ({ x: m.epoch_ms, y: m.humidity ?? null }));
     // Plot pump activation at the current humidity level, or null if not activated
     const pumpData = limitedMeasurements.map(m => ({ x: m.epoch_ms, y: m.pumpActivated ? (m.humidity ?? 50) : null })); // Use 50% as fallback y-pos if humidity is missing

     // Verify datasets exist before updating
     if (!measurementChart.data.datasets || measurementChart.data.datasets.length < 3) {
         console.warn("Chart datasets are missing or incomplete. Recreating chart.");
         measurementChart = createChart(); // Attempt to recreate
         if (!measurementChart) return; // Exit if recreation fails
     }

     // Update chart data
     measurementChart.data.labels = chartLabels;
     measurementChart.data.datasets[0].data = tempData;
     measurementChart.data.datasets[1].data = humidData;
     measurementChart.data.datasets[2].data = pumpData;

     // Update the chart, using 'resize' preserves animations better than 'none' sometimes
     measurementChart.update('resize');
}


// --- Statistics Functions ---
function calculateVPD(temperature, humidity) { if (temperature == null || humidity == null || isNaN(temperature) || isNaN(humidity) || humidity < 0 || humidity > 105) return null; const clampedHumidity = Math.max(0, Math.min(100, humidity)); const svp = 0.6108 * Math.exp((17.27 * temperature) / (temperature + 237.3)); const avp = (clampedHumidity / 100.0) * svp; const vpd = svp - avp; return Math.max(0, vpd); }
function filterMeasurementsByTime(measurements, durationMs) { if (!measurements || measurements.length === 0) return []; const now = Date.now(); const cutoff = now - durationMs; return measurements.filter(m => m.epoch_ms >= cutoff); }
function calculateBasicStats(dataArray, key, recalculateVPD = false) { let sum = 0, min = null, max = null, validCount = 0; const totalCount = dataArray.length; dataArray.forEach(item => { let value = item[key]; if (key === 'vpd' && recalculateVPD) value = calculateVPD(item.temperature, item.humidity); if (value !== null && typeof value === 'number' && !isNaN(value)) { sum += value; validCount++; if (min === null || value < min) min = value; if (max === null || value > max) max = value; } }); const avg = validCount > 0 ? sum / validCount : null; const validity = totalCount > 0 ? (validCount / totalCount) * 100 : 0; return { avg: avg, min: min, max: max, count: totalCount, validCount: validCount, validity: validity }; }
function calculatePeriodStats(allMeasurements) { const stats = { overall: {}, last24h: {}, last7d: {} }; const dataSets = { overall: allMeasurements, last24h: filterMeasurementsByTime(allMeasurements, MS_PER_DAY), last7d: filterMeasurementsByTime(allMeasurements, 7 * MS_PER_DAY) }; for (const period in dataSets) { const data = dataSets[period]; stats[period] = data.length > 0 ? { temp: calculateBasicStats(data, 'temperature'), humid: calculateBasicStats(data, 'humidity'), vpd: calculateBasicStats(data, 'vpd', true), pumpActivations: data.filter(m => m.pumpActivated).length, measurementCount: data.length, firstTimestamp: data[0]?.epoch_ms || null, lastTimestamp: data[data.length - 1]?.epoch_ms || null, dataValidity: calculateBasicStats(data, 'temperature').validity } : { temp: { avg: null, min: null, max: null, validCount: 0, validity: 0 }, humid: { avg: null, min: null, max: null, validCount: 0, validity: 0 }, vpd: { avg: null, min: null, max: null, validCount: 0, validity: 0 }, pumpActivations: 0, measurementCount: 0, firstTimestamp: null, lastTimestamp: null, dataValidity: 0 }; } return stats; }
function calculateStageStats(allMeasurements, stagesInfo) { const stageStats = {}; const measurementsByStage = {}; if (!stagesInfo || stagesInfo.length === 0) { console.warn("Cannot calculate stage stats: stagesInfo is empty."); return {}; } stagesInfo.forEach(stage => { measurementsByStage[stage.name] = []; stageStats[stage.name] = { temp: {avg:null,min:null,max:null,validCount:0,validity:0}, humid: {avg:null,min:null,max:null,validCount:0,validity:0}, vpd: {avg:null,min:null,max:null,validCount:0,validity:0}, pumpActivations:0, measurementCount:0, firstTimestamp:null, lastTimestamp:null, dataValidity:0 }; }); allMeasurements.forEach(m => { if (m.stage && measurementsByStage[m.stage]) measurementsByStage[m.stage].push(m); }); for (const stageName in measurementsByStage) { const data = measurementsByStage[stageName]; if (data.length > 0) stageStats[stageName] = { temp: calculateBasicStats(data, 'temperature'), humid: calculateBasicStats(data, 'humidity'), vpd: calculateBasicStats(data, 'vpd', true), pumpActivations: data.filter(m => m.pumpActivated).length, measurementCount: data.length, firstTimestamp: data[0]?.epoch_ms || null, lastTimestamp: data[data.length - 1]?.epoch_ms || null, dataValidity: calculateBasicStats(data, 'temperature').validity }; } return stageStats; }
async function calculateAllStats(allMeasurements, stagesInfo) { return new Promise(resolve => { setTimeout(() => { const periodStats = calculatePeriodStats(allMeasurements); const stageStats = calculateStageStats(allMeasurements, stagesInfo); resolve({ periodStats, stageStats }); }, 10); }); } // Use small timeout for pseudo-async behavior

// --- UI Update Functions ---
function updateStatisticsUI(statsData) {
    if (!El.statisticsSection) return;
    const { periodStats, stageStats } = statsData;
    const loadingIndicator = El.statisticsSection.querySelector('.loading-indicator');
    if (loadingIndicator) loadingIndicator.remove();

    // Check for necessary data, including stage information
    if (!periodStats || !stageStats || !allStagesInfo || allStagesInfo.length === 0) {
        El.statisticsSection.innerHTML = `<div class="widget-dark p-3 error-indicator"><i class="fas fa-exclamation-circle me-2"></i>Stats Error or Stage Info Missing</div>`;
        return;
    }

    // Helper formatters
    const fmt = (v, p = 1, u = '') => v === null || isNaN(v) ? `<span class="stat-na">N/A</span>` : `${v.toFixed(p)}${u ? `<small class='text-muted ms-1'>${u}</small>`:''}`;
    const fmtRng = (s, e) => (s && e) ? `${formatTimestampForHistory(s)}<span class="text-muted mx-1">–</span>${formatTimestampForHistory(e)}` : 'N/A';
    // Tooltip formatter (adds line breaks)
    const fmtDet = (o, p = 1, u = '') => o ? `Avg: ${fmt(o.avg,p,u)}\nMin: ${fmt(o.min,p,u)}\nMax: ${fmt(o.max,p,u)}\nData: ${o.validCount||0}/${o.count||0}\n(${fmt(o.validity,0)}% Valid)` : 'No Data';

    let html = '<div class="row gy-4">';

    // Period Stats Table
    html += `<div class="col-xl-6 col-lg-12"><div class="widget-dark h-100"><div class="widget-title">Period Stats</div><div class="widget-body stats-table-container"><table class="table table-sm table-borderless table-dark small mb-0"><thead class="sticky-top"><tr><th>Period</th><th>Temp(°C)</th><th>Humid(%)</th><th>VPD(kPa)</th><th>Pump</th><th>Data</th></tr></thead><tbody>`;
    [{id:'overall',lbl:'Overall',d:periodStats.overall},{id:'last7d',lbl:'Last 7 Days',d:periodStats.last7d},{id:'last24h',lbl:'Last 24 Hrs',d:periodStats.last24h}].forEach(p=>{
        const d=p.d;
        const has = d && d.measurementCount > 0;
        html+=`<tr>
                   <td class="text-light"><strong>${p.lbl}</strong></td>
                   <td data-bs-toggle="tooltip" title="${fmtDet(d.temp)}">${has?`${fmt(d.temp.avg)}<small class='text-muted'>(${fmt(d.temp.min)}–${fmt(d.temp.max)})</small>`:fmt(null)}</td>
                   <td data-bs-toggle="tooltip" title="${fmtDet(d.humid)}">${has?`${fmt(d.humid.avg)}<small class='text-muted'>(${fmt(d.humid.min)}–${fmt(d.humid.max)})</small>`:fmt(null)}</td>
                   <td data-bs-toggle="tooltip" title="${fmtDet(d.vpd,2)}">${has?`${fmt(d.vpd.avg,2)}<small class='text-muted'>(${fmt(d.vpd.min,2)}–${fmt(d.vpd.max,2)})</small>`:fmt(null)}</td>
                   <td data-bs-toggle="tooltip" title="${d.pumpActivations||0} activations">${d.pumpActivations||0}</td>
                   <td data-bs-toggle="tooltip" title="${d.measurementCount||0} measurements, ${fmt(d.dataValidity,0)}% valid">${d.measurementCount||0} (${fmt(d.dataValidity,0)}%)</td>
               </tr>`;
        if(has && d.firstTimestamp){ html+=`<tr class="text-muted"><td colspan="6">Range: ${fmtRng(d.firstTimestamp,d.lastTimestamp)}</td></tr>`;}
        else if(!has){ html+=`<tr class="text-muted"><td colspan="6">No data for this period</td></tr>`;}
    });
    html += `</tbody></table></div></div></div>`;

    // Stage Stats Table
    html += `<div class="col-xl-6 col-lg-12"><div class="widget-dark h-100"><div class="widget-title">Stats Per Stage</div><div class="widget-body stats-table-container"><table class="table table-sm table-borderless table-dark small mb-0"><thead class="sticky-top"><tr><th>Stage</th><th>Temp(°C)</th><th>Humid(%)</th><th>VPD(kPa)</th><th>Pump</th><th>Data</th></tr></thead><tbody>`;
    allStagesInfo.forEach(si=>{
        const sN=si.name;
        const d=stageStats[sN]; // Get stats for this stage name
        const has = d && d.measurementCount > 0;
        html+=`<tr>
                   <td class="text-light"><strong>${sN}</strong></td>
                   <td data-bs-toggle="tooltip" title="${fmtDet(d.temp)}">${has?`${fmt(d.temp.avg)}<small class='text-muted'>(${fmt(d.temp.min)}–${fmt(d.temp.max)})</small>`:fmt(null)}</td>
                   <td data-bs-toggle="tooltip" title="${fmtDet(d.humid)}">${has?`${fmt(d.humid.avg)}<small class='text-muted'>(${fmt(d.humid.min)}–${fmt(d.humid.max)})</small>`:fmt(null)}</td>
                   <td data-bs-toggle="tooltip" title="${fmtDet(d.vpd,2)}">${has?`${fmt(d.vpd.avg,2)}<small class='text-muted'>(${fmt(d.vpd.min,2)}–${fmt(d.vpd.max,2)})</small>`:fmt(null)}</td>
                   <td data-bs-toggle="tooltip" title="${d.pumpActivations||0} activations">${d.pumpActivations||0}</td>
                   <td data-bs-toggle="tooltip" title="${d.measurementCount||0} measurements, ${fmt(d.dataValidity,0)}% valid">${d.measurementCount||0} (${fmt(d.dataValidity,0)}%)</td>
               </tr>`;
        if(has && d.firstTimestamp){ html+=`<tr class="text-muted"><td colspan="6">Range: ${fmtRng(d.firstTimestamp,d.lastTimestamp)}</td></tr>`;}
        else if(!has){ html+=`<tr class="text-muted"><td colspan="6">No data recorded for this stage</td></tr>`;}
    });
    html += `</tbody></table></div></div></div>`;

    html += '</div>';
    El.statisticsSection.innerHTML = html;
    initTooltips(El.statisticsSection); // Re-initialize tooltips for the new content
}


// --- Conditional Styling Helpers ---
function updateWidgetConditionalStyle(widgetEl, conditionClass, conditionMet) { if (widgetEl) widgetEl.classList.toggle(conditionClass, conditionMet); }

function updatePumpStatusIndicator(isOn) {
    if (!El.pumpStatusIndicator) return;
    const iconClass = isOn ? 'fa-play-circle text-success' : 'fa-stop-circle text-secondary';
    El.pumpStatusIndicator.innerHTML = `<i class="fas ${iconClass}" aria-hidden="true"></i> ${isOn ? 'ON' : 'OFF'}`;
    El.pumpStatusIndicator.classList.toggle('pulsing', isOn);
    // Update button states based on pump status
    if(El.activatePumpBtn) El.activatePumpBtn.disabled = isOn;
    if(El.deactivatePumpBtn) El.deactivatePumpBtn.disabled = !isOn;
}

function updateWiFiStatusIndicator(rssi) {
     // Ensure both the outer wrapper and inner content elements exist
     if (!El.wifiStatusWrapper || !El.wifiStatusContent) {
         // console.warn("WiFi status wrapper or content elements not found."); // Optional: Log if elements are missing
         return; // Cannot update if elements are not present
     }

     let iconClass = 'fa-wifi text-secondary';
     let text = 'Disconnected';
     let title = 'WiFi Disconnected'; // Default tooltip text

     // Determine icon, text, and tooltip title based on RSSI
     if (rssi != null && rssi !== 0) { // Check for a valid signal value
         text = `Connected (${rssi} dBm)`;
         title = `Connected | Signal: ${rssi} dBm`;
         if (rssi >= -67) iconClass = 'fa-wifi text-success';       // Good signal
         else if (rssi >= -75) iconClass = 'fa-wifi text-warning';  // Okay signal
         else iconClass = 'fa-wifi text-danger opacity-75';         // Weak signal
     } else { // Handle disconnected or AP mode (RSSI is 0 or null)
         iconClass = 'fa-wifi-slash text-danger';
     }

     // --- Update Separated Elements ---

     // 1. Update the inner span's content (the visible icon and text)
     //    Check if content actually needs changing to minimize DOM manipulation
     const newContentHTML = `<i class="fas ${iconClass}" aria-hidden="true"></i> ${text}`;
     if (El.wifiStatusContent.innerHTML !== newContentHTML) {
         El.wifiStatusContent.innerHTML = newContentHTML;
     }

     // 2. Update the outer span's tooltip title attribute
     //    Bootstrap's tooltip will read this attribute when triggered.
     //    Check if attribute actually needs changing.
     if (El.wifiStatusWrapper.getAttribute('data-bs-original-title') !== title) {
         El.wifiStatusWrapper.setAttribute('data-bs-original-title', title);
     }

     // 3. Ensure Tooltip is Initialized on the Wrapper (Safety Check)
     //    Although initTooltips() should handle this, this makes the function more robust
     //    in case it's called before initTooltips or if the element was dynamic.
     //    We do NOT call setContent here.
     let tooltipInstance = bootstrap.Tooltip.getInstance(El.wifiStatusWrapper);
     if (!tooltipInstance) {
         // console.log("Initializing tooltip for wifiStatusWrapper on update."); // Optional debug log
          tooltipInstance = new bootstrap.Tooltip(El.wifiStatusWrapper, {
              trigger: 'hover focus',
              placement: 'top',
              customClass: 'gn-tooltip',
              // Title is automatically read from 'data-bs-original-title' by Bootstrap 5
          });
     }
     // --- End Update Separated Elements ---
}



// --- Main UI Update Function ---
async function updateUI() {
    if (document.hidden) return; // Don't run if tab is backgrounded
    let statsData = {};
    if(El.updatingIndicator) El.updatingIndicator.style.display = 'inline-block';

    try {
        // Fetch interval only if needed (reduces requests)
        if (currentIntervalValue === null) {
            fetchAndUpdateIntervalDisplay(); // Fire and forget is ok here
        }

        // Fetch core data and history/measurements concurrently
        const [dataRes, histRes] = await Promise.allSettled([
            fetchData('/data'),
            fetchData('/loadMeasurement')
        ]);

        // --- Process coreData ---
        if (dataRes.status === 'fulfilled' && dataRes.value) {
            const coreData = dataRes.value;
            lastCoreData = coreData; // Store current data for other functions

            // Update page title dynamically
            if (El.pageTitle) El.pageTitle.textContent = `Green Nanny ${coreData.pumpStatus ? '💧' : '🌿'}`;

            // Update Widgets with latest data
            updateElementText(El.temp, coreData.temperature, '°C');
            updateElementText(El.humidity, coreData.humidity, '%');
            updateElementText(El.vpd, coreData.vpd, 'kPa', 2);
            updateElementText(El.pumpActivationCount, coreData.pumpActivationCount, '', 0);
            updateElementText(El.totalElapsedTime, coreData.elapsedTime, '', 0, 'N/A', formatElapsedTime);
            updateElementText(El.lastMeasurementTime, coreData.lastMeasurementTimestamp, '', 0, '-', formatTimestampForHistory);

            // Apply conditional styles based on thresholds
            updateWidgetConditionalStyle(El.tempWidget, 'temp-cold', coreData.temperature != null && coreData.temperature < 18);
            updateWidgetConditionalStyle(El.tempWidget, 'temp-hot', coreData.temperature != null && coreData.temperature > 30);
            const stageThreshold = coreData.currentStageThreshold; // Use threshold from latest data
            updateWidgetConditionalStyle(El.humidWidget, 'humid-low', stageThreshold != null && coreData.humidity != null && coreData.humidity < stageThreshold - 5);
            updateWidgetConditionalStyle(El.humidWidget, 'humid-high', coreData.humidity != null && coreData.humidity > 85); // Example high threshold

            // Update Stage information display (using current values from /data)
            const cs = { name: coreData.currentStageName, threshold: coreData.currentStageThreshold, watering: coreData.currentStageWateringSec, manual: coreData.manualStageControl };
            currentStageData = { index: coreData.currentStageIndex, name: cs.name, manual: cs.manual }; // Update state
            updateElementText(El.currentStageLabel, cs.name || 'Unknown', '', null, '-');
            if (El.currentStageParams) El.currentStageParams.textContent = (cs.threshold != null && cs.watering != null) ? `Thr: ${cs.threshold}% | Water: ${cs.watering}s` : 'Params N/A';
            if (El.manualControlIndicator) El.manualControlIndicator.style.display = cs.manual ? 'inline-block' : 'none';
            if (El.stageModeIndicator) { El.stageModeIndicator.innerHTML = `<i class="fas ${cs.manual ? 'fa-user-cog' : 'fa-robot'}" aria-hidden="true"></i> ${cs.manual ? 'Manual' : 'Auto'}`; El.stageModeIndicator.className = `badge small ${cs.manual ? 'bg-warning text-dark' : 'bg-info'}`; }

            // Update Pump & WiFi Status indicators
            updatePumpStatusIndicator(coreData.pumpStatus ?? false);
            updateWiFiStatusIndicator(coreData.wifiRSSI);

            // Update Device Name/IP display
            const deviceName = coreData.deviceIP || window.location.hostname;
            if (El.deviceIP) updateElementText(El.deviceIP, deviceName, '', null, '-');
            if (El.navbarDeviceName) updateElementText(El.navbarDeviceName, `(${deviceName})`, '', null, '');

            // Remove general loading class after successful data processing
            document.querySelectorAll('.widget-loading').forEach(el => el.classList.remove('widget-loading'));

        } else { // Handle failure to get core data
            console.error('Failed core data fetch:', dataRes.reason || 'Unknown Error');
            showToast('Error fetching core device data! Check connection.', 'error', 10000);
            // Mark relevant widgets as errored if they were previously loading
            [El.tempWidget, El.humidWidget, El.stageWidget, El.pumpCountWidget, El.vpdWidget, El.uptimeWidget, El.systemStatusWidget].forEach(el => {
                if (el?.classList.contains('widget-loading')) { // Check if still loading
                    el.classList.remove('widget-loading');
                    const h3 = el.querySelector('h3'); if(h3) h3.innerHTML='<span class="text-danger small">Error</span>';
                    const p = el.querySelector('p'); if(p) p.textContent = 'Data unavailable';
                    el.classList.add('border', 'border-danger');
                }
            });
        }

        // --- Process History & Statistics ---
        let measurements = [];
        if (histRes.status === 'fulfilled' && Array.isArray(histRes.value)) {
            measurements = histRes.value.sort((a, b) => a.epoch_ms - b.epoch_ms); // Ensure sorted by time
        } else {
            console.warn('History fetch failed:', histRes.reason || 'Empty or Invalid Response');
            // Display error in history panel if fetch failed
            if (El.measurementHistory) {
                const loadingIndicator = El.measurementHistory.querySelector('.loading-indicator');
                if (loadingIndicator) loadingIndicator.remove();
                // Avoid adding multiple error messages
                if (!El.measurementHistory.querySelector('.error-indicator')) {
                     El.measurementHistory.innerHTML = `<div class='measurement-history-entry error-indicator text-warning'><i class="fas fa-exclamation-triangle me-1"></i> Could not load history data.</div>`;
                }
            }
        }

        // Update History Log UI
        if (El.measurementHistory) {
             const loadingIndicator = El.measurementHistory.querySelector('.loading-indicator');
             if (loadingIndicator) loadingIndicator.remove(); // Ensure loading is removed

             const start = Math.max(0, measurements.length - HISTORY_MAX_ENTRIES);
             const recent = measurements.slice(start).reverse(); // Show newest first
             const firstRenderedTsStr = El.measurementHistory.querySelector('.measurement-history-entry .timestamp')?.dataset.epochMs;
             const newestTs = recent[0]?.epoch_ms;
             // Determine if new data arrived since last render
             const isNewData = newestTs && (!firstRenderedTsStr || newestTs > parseInt(firstRenderedTsStr));

             // Generate HTML for history entries
             const html = recent.length > 0 ? recent.map((m, i) => {
                 const ts = m.epoch_ms;
                 const dispT = formatTimestampForHistory(ts);
                 const tempT = m.temperature != null ? `${m.temperature.toFixed(1)}°C` : 'N/A';
                 const humT = m.humidity != null ? `${m.humidity.toFixed(1)}%` : 'N/A';
                 // Add flash class only to the very first entry if it's new
                 const fl = (i === 0 && isNewData) ? ' new-entry-flash' : '';
                 return `<div class="measurement-history-entry${fl}" data-timestamp="${ts}">
                            <span class="timestamp" data-epoch-ms="${ts}">${dispT}</span>
                            <span class="data-point"><i class="fas fa-thermometer-half" aria-hidden="true"></i>${tempT}</span>
                            <span class="data-point"><i class="fas fa-tint" aria-hidden="true"></i>${humT}</span>
                            ${m.pumpActivated ? '<span class="data-point"><i class="fas fa-play-circle" aria-hidden="true"></i>Pump</span>' : ''}
                            ${m.stage ? `<span class="data-point"><i class="fas fa-seedling" aria-hidden="true"></i>${m.stage}</span>` : ''}
                        </div>`;
             }).join('') : `<div class='measurement-history-entry empty-indicator'>No history recorded.</div>`;

             // Optimize DOM update: Only change innerHTML if content differs
             if (El.measurementHistory.innerHTML !== html) {
                 El.measurementHistory.innerHTML = html;
                 // Scroll to top only if new data arrived to show the latest entry
                 if (isNewData) El.measurementHistory.scrollTop = 0;
             }
        }

        // Calculate and Update Statistics Section
        // Check if the stats section exists and doesn't already show an error
        if (El.statisticsSection && !El.statisticsSection.querySelector('.error-indicator')) {
            // Show loading state before calculation
            El.statisticsSection.innerHTML = `<div class="widget-dark p-3 loading-indicator"><span class="spinner-border spinner-border-sm"></span> Calculating Statistics...</div>`;
        }
        try {
            // Ensure stage info is available (fetch if needed, should be cached most times)
            if (!allStagesInfo || allStagesInfo.length === 0) {
                console.log("Fetching stages info required for stats calculation...");
                await fetchStagesInfo();
            }
            // Perform calculations (using pseudo-async timeout)
            statsData = await calculateAllStats(measurements, allStagesInfo);
            updateStatisticsUI(statsData); // Render the stats table

        } catch (e) {
            console.error("Statistics calculation or update failed:", e);
            if (El.statisticsSection) {
                El.statisticsSection.innerHTML = `<div class="widget-dark p-3 error-indicator"><i class="fas fa-exclamation-triangle me-2"></i>Error Calculating Stats: ${e.message}</div>`;
            }
            statsData = { periodStats: null, stageStats: null }; // Reset stats data on error
        }

        // Update Chart with potentially new measurements
        updateChart(measurements);

    } catch (err) { // Catch errors from the overall update process (e.g., top-level fetch failures)
        console.error('General UI update cycle error:', err);
        showToast(`UI Update failed: ${err.message}`, 'error');
        // Display a generic error message in the stats section if it wasn't already showing a specific error
        if (El.statisticsSection && !El.statisticsSection.querySelector('.error-indicator')) {
             El.statisticsSection.innerHTML = `<div class="widget-dark p-3 text-warning text-center">UI update cycle failed. Check connection and refresh.</div>`;
        }
    } finally {
        // Always hide the global updating indicator
        if(El.updatingIndicator) El.updatingIndicator.style.display = 'none';
        // Clear previous timer and schedule the next update cycle
        clearTimeout(refreshTimeoutId);
        // Only schedule next update if the system isn't in the restarting state
        if (!document.body.classList.contains('system-restarting')) {
             refreshTimeoutId = setTimeout(updateUI, REFRESH_INTERVAL);
        }
    }
}


// --- Fetch and Setup Functions ---
async function fetchAndUpdateIntervalDisplay() {
    if (!El.currentInterval || !El.intervalInput) return;
    try {
        const intervalData = await fetchData('/getMeasurementInterval');
        if (intervalData && intervalData.interval != null) {
             currentIntervalValue = intervalData.interval;
             El.currentInterval.textContent = currentIntervalValue;
             El.intervalInput.value = currentIntervalValue; // Pre-fill input
             El.intervalInput.placeholder = `Current: ${currentIntervalValue}h`; // Update placeholder
        }
     } catch (error) {
         console.error("Failed to fetch measurement interval:", error);
         El.currentInterval.textContent = 'Err'; // Indicate error in UI
         El.intervalInput.placeholder = 'Error loading interval';
     }
}

async function fetchStagesInfo() {
     try {
         // Fetch the current stage definitions from the device
         const stages = await fetchData('/listStages');
         if (stages && Array.isArray(stages)) {
             allStagesInfo = stages; // Store globally
             console.log("Fetched/Updated stages info:", allStagesInfo);
         } else {
             console.warn("Could not fetch or parse valid stages info from /listStages. Using empty array.");
             allStagesInfo = []; // Ensure it's an empty array on failure
         }
     } catch (error) {
         console.error("Failed to fetch stages info:", error);
         allStagesInfo = []; // Ensure empty on error
         throw error; // Re-throw so callers (like init) know it failed
     }
}

async function populateStageSelector() {
    if (!El.manualStageSelect) return;
    El.manualStageSelect.innerHTML = `<option value="" disabled selected>Loading...</option>`;
    try {
        // Ensure stages are fetched if not already available in the global cache
        if (!allStagesInfo || allStagesInfo.length === 0) {
            await fetchStagesInfo();
        }
        // Populate dropdown if stages were successfully fetched
        if (allStagesInfo.length > 0) {
            El.manualStageSelect.innerHTML = allStagesInfo.map(stage =>
                // Create option elements with stage index as value and descriptive text
                `<option value="${stage.index}">${stage.name} (T:${stage.humidityThreshold}% W:${stage.wateringTimeSec}s)</option>`
            ).join('');
            // Set the selected option based on the last known core data
            if (lastCoreData && lastCoreData.currentStageIndex != null) {
                 El.manualStageSelect.value = lastCoreData.currentStageIndex;
            } else if (allStagesInfo.length > 0) {
                 // Default to the first stage if current stage is unknown
                 El.manualStageSelect.value = allStagesInfo[0].index;
            }
        } else {
             // Handle case where no stages are defined or fetched
             El.manualStageSelect.innerHTML = `<option value="" disabled selected>No stages found</option>`;
        }
    } catch (error) {
        console.error("Failed stage select populate:", error);
        El.manualStageSelect.innerHTML = `<option value="" disabled selected>Error loading stages</option>`;
    }
}

function initTooltips(parentElement = document.body) {
     // Dispose existing tooltips within the specified parent element to avoid duplicates
     const currentTooltips = parentElement.querySelectorAll('[data-bs-toggle="tooltip"]');
     currentTooltips.forEach(el => {
         const instance = bootstrap.Tooltip.getInstance(el);
         if (instance) {
             instance.dispose();
         }
     });
     // Re-initialize tooltips for all elements with the attribute within the parent
     bsTooltips = []; // Reset global array (or manage differently if needed)
     const tooltipTriggerList = [].slice.call(parentElement.querySelectorAll('[data-bs-toggle="tooltip"]'));
     bsTooltips = tooltipTriggerList.map(tooltipTriggerEl => {
         return new bootstrap.Tooltip(tooltipTriggerEl, {
             trigger: 'hover focus', // Use hover and focus for better accessibility
             boundary: 'window',    // Keep tooltips within the viewport
             customClass: 'gn-tooltip' // Apply custom class for styling (defined in <style>)
         });
     });
}


// --- NEW: Stage Configuration Table Functions ---
function renderStageConfigTable(stagesData) {
    if (!El.stageConfigTableBody || !El.stageConfigTableContainer || !El.stageConfigLoading || !El.stageConfigError) {
        console.error("Stage config table elements not found.");
        return;
    }

    El.stageConfigLoading.classList.add('d-none'); // Hide loading indicator

    // Handle case where no data is received or is invalid
    if (!stagesData || !Array.isArray(stagesData) || stagesData.length === 0) {
        El.stageConfigError.textContent = "No stage configuration data received from the device.";
        El.stageConfigError.classList.remove('d-none'); // Show error message
        El.stageConfigTableContainer.classList.add('d-none'); // Hide table container
        return;
    }

    El.stageConfigError.classList.add('d-none'); // Hide error message if data is valid
    El.stageConfigTableBody.innerHTML = ''; // Clear previous table rows

    // Populate table body with stage data
    stagesData.forEach(stage => {
        const row = El.stageConfigTableBody.insertRow();
        row.dataset.stageIndex = stage.index; // Add index to row for easier access
        row.innerHTML = `
            <td><strong class="text-light">${stage.name}</strong></td>
            <td class="text-center">${stage.duration_days}</td>
            <td class="text-center">
                <input type="number" class="form-control form-control-sm bg-dark text-light text-center stage-input"
                       value="${stage.humidityThreshold}" min="0" max="100" step="1" required
                       aria-label="Humidity threshold for ${stage.name}" data-field="humidityThreshold" data-index="${stage.index}" style="width: 70px; display: inline-block;">
            </td>
            <td class="text-center">
                 <input type="number" class="form-control form-control-sm bg-dark text-light text-center stage-input"
                        value="${stage.wateringTimeSec}" min="1" max="600" step="1" required
                        aria-label="Watering time for ${stage.name}" data-field="wateringTimeSec" data-index="${stage.index}" style="width: 70px; display: inline-block;">
            </td>
            <td class="text-center">
                <button class="btn btn-sm btn-outline-primary save-stage-btn" data-index="${stage.index}" aria-label="Save changes for ${stage.name}">
                    <i class="fas fa-save me-1" aria-hidden="true"></i>Save
                </button>
            </td>
        `;
    });

    El.stageConfigTableContainer.classList.remove('d-none'); // Show table container now that it's populated
}

async function fetchAndRenderStageConfig() {
    if (!El.stageConfigLoading) return; // Exit if loading element doesn't exist

    // Reset UI state
    El.stageConfigError?.classList.add('d-none');
    El.stageConfigTableContainer?.classList.add('d-none');
    El.stageConfigLoading.classList.remove('d-none'); // Show loading indicator

    try {
        // Use the globally stored/fetched allStagesInfo if available, otherwise fetch again
        let stages = allStagesInfo;
        if (!stages || stages.length === 0) {
            console.log("Fetching stages info specifically for config table rendering...");
            stages = await fetchData('/listStages');
            allStagesInfo = stages; // Update global cache with fresh data
        }
        // Render the table with the obtained stage data
        renderStageConfigTable(stages);
    } catch (error) {
        // Handle errors during fetch or render
        console.error("Failed to fetch or render stage config:", error);
        if(El.stageConfigError) {
            El.stageConfigError.textContent = `Error loading stage config: ${error.message}. Please check device connection.`;
            El.stageConfigError.classList.remove('d-none'); // Show error message
        }
        // Ensure loading indicator is hidden and table remains hidden on error
        if(El.stageConfigLoading) El.stageConfigLoading.classList.add('d-none');
        if(El.stageConfigTableContainer) El.stageConfigTableContainer.classList.add('d-none');
    }
}


// --- Event Listener Setup ---
function setupEventListeners() {
    // --- Forms ---
    El.intervalForm?.addEventListener('submit', async (e) => {
        e.preventDefault();
        const input = El.intervalInput;
        const value = input?.value;
        const numValue = parseInt(value);
        // Validate input value
        if (!value || isNaN(numValue) || numValue < 1 || numValue > 167) {
             showToast('Invalid interval (must be 1-167 hours).', 'warning');
             input?.classList.add('is-invalid');
             input?.focus();
             input?.select();
             return;
        }
        input?.classList.remove('is-invalid');
        const button = El.intervalForm.querySelector('button[type="submit"]');
        setLoadingState(button, true, "Saving...");
        try {
            await postData('/setMeasurementInterval', { interval: numValue });
            showToast(`Measurement interval set to ${numValue} hours.`, 'success');
            announceSRStatus(`Interval updated to ${numValue} hours.`);
            // Update UI immediately
            currentIntervalValue = numValue;
            if(El.currentInterval) El.currentInterval.textContent = numValue;
            if(El.intervalInput) El.intervalInput.placeholder = `Current: ${numValue}h`;
            // Optionally trigger a full UI refresh if interval change affects planning display
            // await updateUI();
        } catch (error) {/* Error toast handled by postData */}
        finally { setLoadingState(button, false); }
    });

    // Live validation feedback for interval input
    El.intervalInput?.addEventListener('input', () => {
        const value = El.intervalInput.value;
        const numValue = parseInt(value);
        // Show invalid state only if field is not empty and value is invalid
        El.intervalInput.classList.toggle('is-invalid', value !== '' && (isNaN(numValue) || numValue < 1 || numValue > 167));
    });

    // Manual Stage Selection Form
    El.manualStageForm?.addEventListener('submit', async (e) => {
        e.preventDefault();
        const select = El.manualStageSelect;
        const value = select?.value;
        const numValue = parseInt(value);
        // Validate that a stage is selected
        if (value === "" || isNaN(numValue)) { // Check for empty string or NaN
             showToast('Please select a valid stage.', 'warning');
             select?.classList.add('is-invalid');
             select?.focus();
             return;
        }
        select?.classList.remove('is-invalid');
        const button = El.manualStageForm.querySelector('button[type="submit"]');
        setLoadingState(button, true, "Setting...");
        try {
            const stageName = select.options[select.selectedIndex].text.split(' (')[0]; // Get name part
            await postData('/setManualStage', { stage: numValue });
            showToast(`Manual stage control enabled: "${stageName}"`, 'success');
            announceSRStatus(`Stage control changed to manual, set to ${stageName}.`);
            await updateUI(); // Refresh UI immediately to reflect manual mode and stage change
        } catch (error) {/* Error handled by postData */}
        finally { setLoadingState(button, false); }
    });

    // Clear validation state on change
    El.manualStageSelect?.addEventListener('change', () => El.manualStageSelect?.classList.remove('is-invalid'));

    // --- Buttons ---
    // Activate Pump Button
    El.activatePumpBtn?.addEventListener('click', async () => {
        const durationStr = prompt('Enter pump activation duration in SECONDS (1-600):', '30');
        if (durationStr === null) return; // User cancelled prompt
        const duration = parseInt(durationStr);
        // Validate duration input
        if (isNaN(duration) || duration <= 0 || duration > 600) {
             showToast('Invalid duration entered (must be 1-600 seconds).', 'warning');
             return;
        }
        setLoadingState(El.activatePumpBtn, true, "Activating...");
        try {
             await postData('/controlPump', { action: 'on', duration: duration });
             showToast(`Pump activated for ${duration} seconds.`, 'success');
             announceSRStatus(`Pump activated for ${duration} seconds.`);
             await updateUI(); // Refresh UI to show pump ON state and disable button
         } catch (error) {
             // Error handled by postData, refresh UI anyway to reflect actual state
             await updateUI();
         }
        // No 'finally' block needed for loader state here, as updateUI handles button state based on actual pump status
    });

    // Deactivate Pump Button
    El.deactivatePumpBtn?.addEventListener('click', async () => {
        if (!confirm('Are you sure you want to DEACTIVATE the pump now?')) return;
        setLoadingState(El.deactivatePumpBtn, true, "Stopping...");
        try {
             await postData('/controlPump', { action: 'off' });
             showToast('Pump deactivated.', 'success');
             announceSRStatus('Pump manually deactivated.');
             await updateUI(); // Refresh UI to show pump OFF state and enable activate button
         } catch (error) {
             // Error handled by postData, refresh UI anyway
             await updateUI();
         }
        // No 'finally' block needed here
    });

    // Trigger Measurement Button
    El.takeMeasurementBtn?.addEventListener('click', async () => {
        setLoadingState(El.takeMeasurementBtn, true, "Measuring...");
        showToast('Triggering new measurement cycle...', 'info', 2500);
        try {
             await postData('/takeMeasurement', {}, 'POST');
             // Add a short delay to allow the measurement to potentially complete on the device
             await new Promise(resolve => setTimeout(resolve, 2000));
             await updateUI(); // Fetch the new data and update all UI elements
             showToast('Measurement complete and data updated.', 'success', 3000);
             announceSRStatus('Manual measurement completed.');
         } catch (error) {/* Error handled by postData */}
        finally { setLoadingState(El.takeMeasurementBtn, false); }
    });

    // Clear History Button
    El.clearMeasurementsBtn?.addEventListener('click', async () => {
        if (!confirm('⚠️ WARNING! ⚠️\nAre you sure you want to DELETE ALL measurement history?\nThis action cannot be undone.')) return;
        setLoadingState(El.clearMeasurementsBtn, true, "Clearing...");
        try {
            await postData('/clearHistory', {}, 'POST');
            showToast('Measurement history has been cleared.', 'success');
            announceSRStatus('Measurement history deleted.');
            // Clear relevant UI sections immediately for responsiveness
            if(measurementChart) { measurementChart.data.labels = []; measurementChart.data.datasets.forEach(ds => ds.data = []); measurementChart.update('none'); }
            if(El.measurementHistory) El.measurementHistory.innerHTML = `<div class='measurement-history-entry empty-indicator'>History cleared.</div>`;
            if(El.statisticsSection) El.statisticsSection.innerHTML = `<div class="widget-dark p-3 text-muted text-center">Statistics cleared. Data will repopulate.</div>`;
            // Fetch fresh (empty) data to update counts etc.
            await updateUI();
        } catch (error) {/* Error handled by postData */}
        finally { setLoadingState(El.clearMeasurementsBtn, false); }
    });

    // Restart System Button
    El.restartSystemBtn?.addEventListener('click', async () => {
        if (!confirm('Are you sure you want to RESTART the device?\nThe page will become unresponsive. You will need to manually refresh after ~30 seconds.')) return;
        setLoadingState(El.restartSystemBtn, true, "Restarting...");
        showToast('Sending restart command... Please refresh the page in about 30 seconds.', 'warning', 10000);
        announceSRStatus('System restart command sent.');
        document.body.classList.add('system-restarting'); // Apply overlay CSS
        // Disable all interactive elements
        document.querySelectorAll('button, input, select, a').forEach(el => { if(el && typeof el.disabled !== 'undefined') el.disabled = true; });
        clearTimeout(refreshTimeoutId); // Stop automatic UI updates
        try {
             // Send command with a short timeout, as we don't expect a normal response
             await postData('/restartSystem', {}, 'POST', 5000);
             // If the request *does* complete (e.g., network error before restart), show a message
             showToast('Restart command sent. Device should be restarting.', 'info', 8000);
        } catch (error) {
             // If the request fails (e.g., timeout because device restarted quickly),
             // it's actually the expected behavior. We might still inform the user.
             console.warn("Restart request likely timed out as expected:", error);
             showToast('Restart initiated. Refresh page shortly.', 'info', 8000);
        }
        // Do NOT re-enable controls or remove overlay here. User must refresh.
    });

    // Refresh Stage Info Button (Stage Control Card)
    El.refreshStageBtn?.addEventListener('click', async () => {
        setLoadingState(El.refreshStageBtn, true, "Refreshing...");
        try {
            // Specifically refetch and populate stage selector
            await fetchStagesInfo(); // Get latest definitions (in case edited elsewhere)
            await populateStageSelector(); // Update dropdown content
            // Also update the main UI to reflect current stage name/params from /data
            await updateUI();
            showToast('Stage info and selector refreshed.', 'info', 2000);
            announceSRStatus('Current stage information updated.');
        } catch (error) {
             showToast('Error refreshing stage info.', 'error');
             /* Error handling done by underlying fetch/update functions */
        }
        finally { setLoadingState(El.refreshStageBtn, false); }
    });

    // Reset to Auto Stage Control Button
    El.resetManualStageBtn?.addEventListener('click', async () => {
        if (!confirm('Return to AUTOMATIC stage control?\nThe system will determine the stage based on its internal timer.')) return;
        setLoadingState(El.resetManualStageBtn, true, "Resetting...");
        try {
             await postData('/resetManualStage', {}, 'POST');
             showToast('Stage control returned to automatic mode.', 'success');
             announceSRStatus('Stage control set back to automatic.');
             await updateUI(); // Refresh UI immediately to show auto mode and correct stage
         } catch (error) {/* Error handled by postData */}
        finally { setLoadingState(El.resetManualStageBtn, false); }
    });


    // --- NEW: Stage Configuration Event Listener (Delegated to table body) ---
    El.stageConfigTableBody?.addEventListener('click', async (event) => {
        // Check if the clicked element or its parent is the save button
        if (event.target.matches('.save-stage-btn, .save-stage-btn *')) {
            const button = event.target.closest('.save-stage-btn');
            const stageIndex = parseInt(button.dataset.index);
            const row = button.closest('tr');

            if (isNaN(stageIndex) || !row) {
                console.error("Could not find stage index or row for save button.");
                showToast("Internal error: Cannot save stage.", 'error');
                return;
            }

            // Find the input fields within this specific row
            const humidInput = row.querySelector('input[data-field="humidityThreshold"]');
            const waterInput = row.querySelector('input[data-field="wateringTimeSec"]');

            // Get and parse values
            const newHumid = parseInt(humidInput?.value);
            const newWaterTime = parseInt(waterInput?.value);

            // --- Frontend Validation ---
            let isValid = true;
            // Validate Humidity Threshold
            if (humidInput && (isNaN(newHumid) || newHumid < 0 || newHumid > 100)) {
                humidInput.classList.add('is-invalid');
                isValid = false;
            } else if (humidInput) {
                humidInput.classList.remove('is-invalid');
            }
            // Validate Watering Time
            if (waterInput && (isNaN(newWaterTime) || newWaterTime < 1 || newWaterTime > 600)) { // Match backend validation
                 waterInput.classList.add('is-invalid');
                 isValid = false;
            } else if (waterInput) {
                 waterInput.classList.remove('is-invalid');
            }

            // If any field is invalid, show toast and focus the first invalid one
            if (!isValid) {
                showToast("Invalid input values. Check ranges (Hum: 0-100%, Time: 1-600s).", 'warning');
                row.querySelector('.is-invalid')?.focus();
                return;
            }

            // Get stage name for feedback messages
            const stageName = row.cells[0].textContent.trim();

            // --- Send Update to Backend ---
            setLoadingState(button, true, "Saving...");
            try {
                const response = await postData('/updateStage', {
                    index: stageIndex,
                    humidityThreshold: newHumid,
                    wateringTimeSec: newWaterTime
                });
                showToast(`Stage "${stageName}" updated successfully.`, 'success');
                announceSRStatus(`Stage ${stageName} configuration saved.`);

                // Update the global stage info cache immediately
                const cachedStage = allStagesInfo.find(s => s.index === stageIndex);
                if(cachedStage) {
                    cachedStage.humidityThreshold = newHumid;
                    cachedStage.wateringTimeSec = newWaterTime;
                }

                // Re-populate the manual stage selector dropdown in case it's open
                await populateStageSelector();
                // Refresh main dashboard data as settings affect behaviour/stats
                await updateUI(); // Fetch data again to confirm changes reflected everywhere

            } catch (error) {
                 // Error toast is shown by postData function
                 console.error(`Failed to save stage ${stageIndex}:`, error);
                 // Optionally add specific error details if available from backend response?
                 // e.g., showToast(`Failed to save "${stageName}": ${error.message}`, 'error');
            } finally {
                 setLoadingState(button, false); // Restore button state regardless of success/failure
            }
        }
    });

    // Add live input validation feedback for stage config table using event delegation
     El.stageConfigTableBody?.addEventListener('input', (event) => {
         if (event.target.matches('input[type="number"].stage-input')) {
             const input = event.target;
             const value = parseInt(input.value);
             const min = parseInt(input.min);
             const max = parseInt(input.max);
             // Toggle 'is-invalid' class based on whether the value is a number and within range
             input.classList.toggle('is-invalid', isNaN(value) || value < min || value > max);
         }
     });


    // --- Window Events ---
    // Handle tab visibility change to pause/resume updates
    document.addEventListener('visibilitychange', () => {
        if (!document.hidden) {
            // Tab became visible
            console.log("Tab became visible, forcing UI update.");
            clearTimeout(refreshTimeoutId); // Prevent potential duplicate updates if timer was close
            updateUI(); // Update immediately
        } else {
            // Tab became hidden
            console.log("Tab became hidden, pausing automatic updates.");
            clearTimeout(refreshTimeoutId); // Stop scheduled updates
            if(El.updatingIndicator) El.updatingIndicator.style.display = 'none'; // Hide sync indicator immediately
        }
    });
}

// --- Initialization ---
document.addEventListener('DOMContentLoaded', async () => {
    console.log("DOM loaded. Initializing Green Nanny Dashboard...");
    // Set initial loading state for widgets and sections that fetch data
    document.querySelectorAll('.small-box, .widget-dark[id]').forEach(el => el.classList.add('widget-loading'));
    showGlobalLoader(true); // Show global loader bar during init

    if (!El.measurementChartCtx) console.warn("Chart canvas context ('measurementChart') not found.");

    // Set up all event listeners for buttons, forms, etc.
    setupEventListeners();

    try {
        // --- Initial Data Fetching and Setup ---
        // 1. Fetch stage definitions - needed for stats and controls
        await fetchStagesInfo();
        // 2. Render the stage configuration table (uses cached/fetched stage info)
        await fetchAndRenderStageConfig();
        // 3. Perform the first full data fetch and UI update (fetches /data, /loadMeasurement)
        await updateUI(); // This populates widgets, history, stats, chart
        // 4. Populate controls that depend on initial data (like the manual stage selector)
        await populateStageSelector();
        // 5. Initialize interactive elements like Bootstrap tooltips
        initTooltips();

        console.log("Initialization complete.");
        announceSRStatus("Dashboard loaded successfully.");

    } catch (error) { // Catch errors during the critical initialization phase
        console.error("Dashboard initialization failed:", error);
        showToast("Failed to load initial dashboard data. Check device connection and refresh.", "error", 15000); // Show persistent error toast
        announceSRStatus("Error: Dashboard failed to load."); // Announce error for screen readers

        // Display error messages in relevant sections
        const main = document.querySelector('.content-wrapper');
        if(main) main.insertAdjacentHTML('afterbegin', `<div class="alert alert-danger m-3"><strong>Initialization Error:</strong> Could not load essential data. Please check device connection and refresh the page.</div>`);
        if (El.statisticsSection) El.statisticsSection.innerHTML = `<div class="widget-dark p-3 error-indicator"><i class="fas fa-exclamation-triangle"></i> Initialization Failed - Stats Unavailable</div>`;
        if (El.stageConfigError) { // Show error in stage config section too
            El.stageConfigError.textContent = `Initialization Error: ${error.message}. Stage configuration unavailable.`;
            El.stageConfigError.classList.remove('d-none');
            El.stageConfigLoading?.classList.add('d-none');
            El.stageConfigTableContainer?.classList.add('d-none');
        }
        // Attempt to remove loading state from widgets even on error to show potential error messages within them
        document.querySelectorAll('.widget-loading').forEach(el => {
            el.classList.remove('widget-loading');
            const h3 = el.querySelector('h3'); if(h3) h3.innerHTML='<span class="text-danger small">Error</span>';
            const p = el.querySelector('p'); if(p) p.textContent = 'Load Failed';
        });
    } finally {
         showGlobalLoader(false); // Always hide global loader bar after init attempt
         // Auto-refresh timer is started within the `updateUI` function's finally block if init was successful
    }
});