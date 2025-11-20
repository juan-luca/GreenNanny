
const BASE_URL = `${location.protocol}//${window.location.hostname}`;
const REFRESH_INTERVAL = 30000; // Aumentado de 15s a 30s para reducir carga del ESP8266
const CHART_MAX_POINTS = 180;
const HISTORY_MAX_ENTRIES = 150;
const TOAST_TIMEOUT = 6000;
const FETCH_TIMEOUT = 20000;
const MS_PER_HOUR = 3600 * 1000;
const MS_PER_DAY = 24 * MS_PER_HOUR;
const UPDATE_FLASH_DURATION = 700;
const LOW_HEAP_THRESHOLD = 13000; // Si heap <13KB, reducir velocidad de polling

// --- Simple device buttons (Nanny 1/2/3) ---
// Discovery can spam console if .local/NBNS names aren't resolvable (Windows without mDNS).
// Enable by adding ?discover=1 to the URL. Default: disabled to avoid ERR_NAME_NOT_RESOLVED noise.
const ENABLE_DEVICE_DISCOVERY = new URLSearchParams(location.search).has('discover');
function hostForIndex(idx) {
    const base = 'greennanny';
    return idx === 1 ? `${base}.local` : `${base}${idx}.local`;
}

async function probeHostAvailable(host, timeout = 1200) {
    const urls = [`${location.protocol}//${host}/data`, `${location.protocol}//${host.replace(/\.local$/,'')}/data`];
    for (const url of urls) {
        try {
            const controller = new AbortController();
            const to = setTimeout(() => controller.abort(), timeout);
            const res = await fetch(url, { signal: controller.signal, cache: 'no-store' });
            clearTimeout(to);
            if (res.ok) {
                const j = await res.json();
                return { ok: true, ip: j.deviceIP || null };
            }
        } catch (_) { /* try next */ }
    }
    return { ok: false, ip: null };
}

function setBtnState(btn, available, isCurrent, ipText) {
    if (!btn) return;
    btn.disabled = !available || isCurrent;
    btn.classList.toggle('btn-success', available && isCurrent);
    btn.classList.toggle('btn-outline-light', !isCurrent);
    btn.classList.toggle('btn-outline-success', available && !isCurrent);
    if (ipText) {
        const baseLabel = btn.dataset.baseLabel || btn.textContent;
        btn.dataset.baseLabel = baseLabel;
        btn.textContent = `${baseLabel} ${ipText ? '(' + ipText + ')' : ''}`.trim();
    }
}

function wireBtnNav(btn, targetHost) {
    if (!btn) return;
    btn.addEventListener('click', () => {
        location.href = `${location.protocol}//${targetHost}/index.html`;
    });
}

async function updateDeviceButtons() {
    if (document.hidden) return; // avoid work when tab hidden
    if (!ENABLE_DEVICE_DISCOVERY) return; // discovery disabled by default
    const currentHost = window.location.hostname;
    for (let i = 1; i <= 3; i++) {
        const host = hostForIndex(i);
        const el = document.getElementById(`btnDev${i}`);
        const isCurrent = (currentHost === host || currentHost === host.replace(/\.local$/,''));
        if (isCurrent) {
            const ip = (window.lastCoreData && window.lastCoreData.deviceIP) ? window.lastCoreData.deviceIP : null;
            setBtnState(el, true, true, ip);
        } else {
            try {
                const res = await probeHostAvailable(host, 900);
                setBtnState(el, res.ok, false, res.ip);
            } catch (_) {
                // swallow any unexpected errors
            }
        }
    }
}

function initDeviceButtons() {
    const b1 = document.getElementById('btnDev1');
    const b2 = document.getElementById('btnDev2');
    const b3 = document.getElementById('btnDev3');
    wireBtnNav(b1, hostForIndex(1));
    wireBtnNav(b2, hostForIndex(2));
    wireBtnNav(b3, hostForIndex(3));
    // Only probe if discovery is explicitly enabled via query param
    if (ENABLE_DEVICE_DISCOVERY) {
        updateDeviceButtons();
        setInterval(updateDeviceButtons, 30000); // refrescar cada 30s
    }
}

// (Device discovery & switching removed per request; dashboard targets current host only)

// --- Element Selectors (Cached) ---
const El = {
    // Widgets
    tempWidget: document.getElementById('tempWidget'),
    humidWidget: document.getElementById('humidWidget'),
    stageWidget: document.getElementById('stageWidget'),
    pumpCountWidget: document.getElementById('pumpCountWidget'),
    vpdWidget: document.getElementById('vpdWidget'),
    uptimeWidget: document.getElementById('uptimeWidget'),
    diskWidget: document.getElementById('diskWidget'),
    systemStatusWidget: document.getElementById('systemStatusWidget'),
    // Widget Inner Values
    temp: document.getElementById('temperature'),
    humidity: document.getElementById('humidity'),
    currentStageLabel: document.getElementById('currentStageLabel'),
    currentStageParams: document.getElementById('currentStageParams'),
    pumpActivationCount: document.getElementById('pumpActivationCount'),
    vpd: document.getElementById('vpd'),
    totalElapsedTime: document.getElementById('totalElapsedTime'),
    diskFreePercent: document.getElementById('diskFreePercent'),
    diskFreeBytes: document.getElementById('diskFreeBytes'),
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
    // Fan & Extractor Controls (NEW)
    fanStatusIndicator: document.getElementById('fanStatusIndicator'),
    extractorStatusIndicator: document.getElementById('extractorStatusIndicator'),
    activateFanBtn: document.getElementById('activateFan'),
    deactivateFanBtn: document.getElementById('deactivateFan'),
    activateExtractorBtn: document.getElementById('activateExtractor'),
    deactivateExtractorBtn: document.getElementById('deactivateExtractor'),
    thresholdsForm: document.getElementById('thresholdsForm'),
    fanTempThreshold: document.getElementById('fanTempThreshold'),
    fanHumThreshold: document.getElementById('fanHumThreshold'),
    extractorTempThreshold: document.getElementById('extractorTempThreshold'),
    extractorHumThreshold: document.getElementById('extractorHumThreshold'),
    // Test Mode Controls (NEW)
    toggleTestModeBtn: document.getElementById('toggleTestMode'),
    testModeButtonText: document.getElementById('testModeButtonText'),
    testModeIndicator: document.getElementById('testModeIndicator'),
    // Discord Configuration (NEW)
    discordForm: document.getElementById('discordForm'),
    discordWebhookUrl: document.getElementById('discordWebhookUrl'),
    discordEnabled: document.getElementById('discordEnabled'),
    tempHighAlert: document.getElementById('tempHighAlert'),
    tempHighThreshold: document.getElementById('tempHighThreshold'),
    tempLowAlert: document.getElementById('tempLowAlert'),
    tempLowThreshold: document.getElementById('tempLowThreshold'),
    humHighAlert: document.getElementById('humHighAlert'),
    humHighThreshold: document.getElementById('humHighThreshold'),
    humLowAlert: document.getElementById('humLowAlert'),
    humLowThreshold: document.getElementById('humLowThreshold'),
    sensorFailAlert: document.getElementById('sensorFailAlert'),
    deviceActivationAlert: document.getElementById('deviceActivationAlert'),
    testDiscordAlert: document.getElementById('testDiscordAlert'),
    // Global & Footer
    toastContainer: document.getElementById('toastContainer'),
    globalLoader: document.getElementById('globalLoader'),
    pageTitle: document.querySelector('title'),
    deviceIP: document.getElementById('deviceIP'),
    navbarDeviceName: document.getElementById('navbarDeviceName'),
    downloadLogsBtn: document.getElementById('downloadLogsBtn'),
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

function downloadLogs() {
    try {
        const a = document.createElement('a');
        a.href = `${BASE_URL}/downloadLogs`;
        a.download = `greennanny-logs.txt`;
        document.body.appendChild(a);
        a.click();
        a.remove();
    } catch (e) {
        console.error('Failed to trigger logs download', e);
        showToast('Failed to download logs', 'error');
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
             else if (buttonElement.id === 'downloadLogsBtn') defaultText = '<i class="fas fa-file-download" aria-hidden="true"></i> Logs';
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
     //const options = { month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit', hour12: false, timeZone: 'UTC' };
     const options = { month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit', hour12: false };
    
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
              const timeFormatted = date.toLocaleTimeString('en-GB', { hour: '2-digit', minute: '2-digit', hour12: false});
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

function updateFanStatusIndicator(isOn) {
    if (!El.fanStatusIndicator) return;
    const iconClass = isOn ? 'fa-fan text-success' : 'fa-fan text-secondary';
    El.fanStatusIndicator.innerHTML = `<i class="fas ${iconClass}" aria-hidden="true"></i> ${isOn ? 'ON' : 'OFF'}`;
    El.fanStatusIndicator.className = `badge ${isOn ? 'bg-success' : 'bg-secondary'} ms-2`;
    // Update button states
    if(El.activateFanBtn) El.activateFanBtn.disabled = isOn;
    if(El.deactivateFanBtn) El.deactivateFanBtn.disabled = !isOn;
}

function updateExtractorStatusIndicator(isOn) {
    if (!El.extractorStatusIndicator) return;
    const iconClass = isOn ? 'fa-wind text-success' : 'fa-wind text-secondary';
    El.extractorStatusIndicator.innerHTML = `<i class="fas ${iconClass}" aria-hidden="true"></i> ${isOn ? 'ON' : 'OFF'}`;
    El.extractorStatusIndicator.className = `badge ${isOn ? 'bg-success' : 'bg-secondary'} ms-2`;
    // Update button states
    if(El.activateExtractorBtn) El.activateExtractorBtn.disabled = isOn;
    if(El.deactivateExtractorBtn) El.deactivateExtractorBtn.disabled = !isOn;
}

function updateTestModeIndicator(isEnabled) {
    if (!El.testModeIndicator) return;
    if (isEnabled) {
        El.testModeIndicator.classList.remove('d-none');
    } else {
        El.testModeIndicator.classList.add('d-none');
    }
    
    // Update button text
    if (El.testModeButtonText) {
        El.testModeButtonText.textContent = isEnabled ? 'Disable Test Mode' : 'Enable Test Mode';
    }
    
    // Update button style
    if (El.toggleTestModeBtn) {
        if (isEnabled) {
            El.toggleTestModeBtn.classList.remove('btn-outline-warning');
            El.toggleTestModeBtn.classList.add('btn-warning', 'text-dark');
        } else {
            El.toggleTestModeBtn.classList.remove('btn-warning', 'text-dark');
            El.toggleTestModeBtn.classList.add('btn-outline-warning');
        }
    }
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

        // Fetch core data, disk info, and history/measurements concurrently
        const [dataRes, diskRes, histRes] = await Promise.allSettled([
            fetchData('/data'),
            fetchData('/diskInfo'),
            fetchData('/loadMeasurement')
        ]);

        // --- Process coreData ---
        if (dataRes.status === 'fulfilled' && dataRes.value) {
            const coreData = dataRes.value;
            lastCoreData = coreData; // Store current data for other functions
            try { window.lastCoreData = coreData; } catch (_) {}

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
            updateFanStatusIndicator(coreData.fanStatus ?? false);
            updateExtractorStatusIndicator(coreData.extractorStatus ?? false);
            updateTestModeIndicator(coreData.testModeEnabled ?? false);
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
            [El.tempWidget, El.humidWidget, El.stageWidget, El.pumpCountWidget, El.vpdWidget, El.uptimeWidget, El.diskWidget, El.systemStatusWidget].forEach(el => {
                if (el?.classList.contains('widget-loading')) { // Check if still loading
                    el.classList.remove('widget-loading');
                    const h3 = el.querySelector('h3'); if(h3) h3.innerHTML='<span class="text-danger small">Error</span>';
                    const p = el.querySelector('p'); if(p) p.textContent = 'Data unavailable';
                    el.classList.add('border', 'border-danger');
                }
            });
        }

        // --- Process Disk Info ---
        if (diskRes.status === 'fulfilled' && diskRes.value) {
            const diskData = diskRes.value;
            const freePercent = parseFloat(diskData.free_percent);
            const freeBytes = diskData.free_bytes;
            
            // Update disk widget
            if (El.diskFreePercent) {
                El.diskFreePercent.textContent = `${freePercent.toFixed(1)}%`;
            }
            if (El.diskFreeBytes) {
                // Format bytes to KB
                const freeKB = (freeBytes / 1024).toFixed(1);
                El.diskFreeBytes.textContent = `${freeKB} KB`;
            }
            
            // Apply conditional styling based on free space
            if (El.diskWidget) {
                El.diskWidget.classList.remove('widget-loading');
                // Remove any previous warning classes
                El.diskWidget.classList.remove('border', 'border-warning', 'border-danger');
                
                // Add warning if space is low
                if (freePercent < 10) {
                    El.diskWidget.classList.add('border', 'border-danger');
                    console.warn('[DISK] Critical: Less than 10% free space!');
                } else if (freePercent < 20) {
                    El.diskWidget.classList.add('border', 'border-warning');
                    console.warn('[DISK] Warning: Less than 20% free space');
                }
            }
        } else {
            console.warn('Disk info fetch failed:', diskRes.reason || 'Unknown Error');
            if (El.diskWidget) {
                El.diskWidget.classList.remove('widget-loading');
                if (El.diskFreePercent) El.diskFreePercent.textContent = 'N/A';
                if (El.diskFreeBytes) El.diskFreeBytes.textContent = 'unavailable';
            }
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
                 
                 // Build event string for special events
                 let eventStr = '';
                 if (m.event) {
                     // This is an event log entry (fan/extractor activation)
                     eventStr = `<span class="data-point event-badge"><i class="fas fa-bolt" aria-hidden="true"></i>${m.event}</span>`;
                 }
                 
                 return `<div class="measurement-history-entry${fl}" data-timestamp="${ts}">
                            <span class="timestamp" data-epoch-ms="${ts}">${dispT}</span>
                            ${m.temperature != null ? `<span class="data-point"><i class="fas fa-thermometer-half" aria-hidden="true"></i>${tempT}</span>` : ''}
                            ${m.humidity != null ? `<span class="data-point"><i class="fas fa-tint" aria-hidden="true"></i>${humT}</span>` : ''}
                            ${m.pumpActivated ? '<span class="data-point"><i class="fas fa-play-circle" aria-hidden="true"></i>Pump</span>' : ''}
                            ${m.fanActivated ? '<span class="data-point"><i class="fas fa-fan" aria-hidden="true"></i>Fan</span>' : ''}
                            ${m.extractorActivated ? '<span class="data-point"><i class="fas fa-wind" aria-hidden="true"></i>Extractor</span>' : ''}
                            ${eventStr}
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
             // OPTIMIZACIÓN: Usar backoff exponencial si heap está bajo
             // Use lastCoreData captured from the last successful /data fetch
             const currentHeap = (window.lastCoreData && typeof window.lastCoreData.freeHeap === 'number')
                 ? window.lastCoreData.freeHeap
                 : 20000; // Default alto si no hay dato
             let nextInterval = REFRESH_INTERVAL;
             if (currentHeap < LOW_HEAP_THRESHOLD) {
                 nextInterval = REFRESH_INTERVAL * 2; // Duplicar intervalo si heap bajo (30s → 60s)
                 console.warn(`[HEAP LOW] ${currentHeap} bytes. Slowing polling to ${nextInterval/1000}s`);
             }
             refreshTimeoutId = setTimeout(updateUI, nextInterval);
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

async function fetchAndUpdateThresholdsDisplay() {
    if (!El.fanTempThreshold || !El.fanHumThreshold || !El.extractorTempThreshold || !El.extractorHumThreshold) return;
    try {
        const thresholdsData = await fetchData('/getThresholds');
        if (thresholdsData) {
            El.fanTempThreshold.value = thresholdsData.fanTempOn ?? 28.0;
            El.fanHumThreshold.value = thresholdsData.fanHumOn ?? 70;
            El.extractorTempThreshold.value = thresholdsData.extractorTempOn ?? 32.0;
            El.extractorHumThreshold.value = thresholdsData.extractorHumOn ?? 85;
        }
    } catch (error) {
        console.error("Failed to fetch thresholds:", error);
        showToast('Error loading thresholds', 'warning');
    }
}

// Load Discord configuration from device
async function loadDiscordConfig() {
    if (!El.discordWebhookUrl || !El.discordEnabled) return;
    try {
        const config = await fetchData('/getDiscordConfig');
        if (config) {
            El.discordWebhookUrl.value = config.webhookUrl || '';
            El.discordEnabled.checked = config.enabled || false;
            
            if (config.alerts) {
                if (El.tempHighAlert) El.tempHighAlert.checked = config.alerts.tempHighAlert ?? true;
                if (El.tempHighThreshold) El.tempHighThreshold.value = config.alerts.tempHighThreshold ?? 35;
                if (El.tempLowAlert) El.tempLowAlert.checked = config.alerts.tempLowAlert ?? true;
                if (El.tempLowThreshold) El.tempLowThreshold.value = config.alerts.tempLowThreshold ?? 15;
                if (El.humHighAlert) El.humHighAlert.checked = config.alerts.humHighAlert ?? true;
                if (El.humHighThreshold) El.humHighThreshold.value = config.alerts.humHighThreshold ?? 85;
                if (El.humLowAlert) El.humLowAlert.checked = config.alerts.humLowAlert ?? true;
                if (El.humLowThreshold) El.humLowThreshold.value = config.alerts.humLowThreshold ?? 30;
                if (El.sensorFailAlert) El.sensorFailAlert.checked = config.alerts.sensorFailAlert ?? true;
                if (El.deviceActivationAlert) El.deviceActivationAlert.checked = config.alerts.deviceActivationAlert ?? false;
            }
            console.log("Discord configuration loaded successfully.");
        }
    } catch (error) {
        console.error("Error loading Discord config:", error);
        // Don't show toast error as this is not critical for page load
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
    if (!El.stageConfigTableBody) return;

    El.stageConfigLoading.classList.add('d-none');
    El.stageConfigError.classList.add('d-none');
    El.stageConfigTableBody.innerHTML = '';

    if (!Array.isArray(stagesData) || stagesData.length === 0) {
        El.stageConfigError.textContent = "No stage configuration data received.";
        El.stageConfigError.classList.remove('d-none');
        return;
    }

    stagesData.forEach(stage => {
        const row = El.stageConfigTableBody.insertRow();
        row.dataset.stageIndex = stage.index;
        row.innerHTML = `
            <td><strong class="text-light">${stage.name}</strong></td>

            <td class="text-center">
                <input type="number"
                       class="form-control form-control-sm bg-dark text-light text-center stage-input"
                       value="${stage.duration_days}" min="1" max="365" step="1" required
                       aria-label="Duration (days) for ${stage.name}"
                       data-field="duration_days" data-index="${stage.index}"
                       style="width:60px;display:inline-block;">
            </td>

            <td class="text-center">
                <input type="number"
                       class="form-control form-control-sm bg-dark text-light text-center stage-input"
                       value="${stage.humidityThreshold}" min="0" max="100" step="1" required
                       aria-label="Humidity threshold for ${stage.name}"
                       data-field="humidityThreshold" data-index="${stage.index}"
                       style="width:70px;display:inline-block;">
            </td>

            <td class="text-center">
                <input type="number"
                       class="form-control form-control-sm bg-dark text-light text-center stage-input"
                       value="${stage.wateringTimeSec}" min="1" max="600" step="1" required
                       aria-label="Watering time (s) for ${stage.name}"
                       data-field="wateringTimeSec" data-index="${stage.index}"
                       style="width:70px;display:inline-block;">
            </td>

            <td class="text-center">
                <button class="btn btn-sm btn-outline-primary save-stage-btn"
                        data-index="${stage.index}">
                    <i class="fas fa-save me-1"></i>Save
                </button>
            </td>`;
    });

    El.stageConfigTableContainer.classList.remove('d-none');
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
    // MODIFIED: Switched to URL params to avoid JSON parsing issues on device
El.activatePumpBtn?.addEventListener('click', async () => {
    const durationStr = prompt('Enter pump activation duration in SECONDS (1-600):', '30');
    if (durationStr === null) return; // User cancelled prompt
    const duration = parseInt(durationStr);

    if (isNaN(duration) || duration <= 0 || duration > 600) {
        showToast('Invalid duration entered (must be 1-600 seconds).', 'warning');
        return;
    }

    setLoadingState(El.activatePumpBtn, true, "Activating...");
    try {
        // Construct URL with parameters instead of sending a JSON body
        const url = `${BASE_URL}/controlPump?action=on&duration=${duration}`;

        const response = await fetch(url, { method: 'POST' });

        if (!response.ok) {
            // Try to get error message from response body
            const errorText = await response.text();
            throw new Error(`Command failed: ${response.status} ${errorText}`);
        }

        showToast(`Pump activated for ${duration} seconds.`, 'success');
        announceSRStatus(`Pump activated for ${duration} seconds.`);
        await updateUI(); // Refresh UI to show pump ON state

    } catch (error) {
        console.error("Error activating pump:", error);
        showToast(error.message, 'error');
        await updateUI(); // Refresh UI to reflect actual device state even on error
    }
    // The loader state is handled by updateUI, which disables the button based on pump status
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

    // --- Fan Control Buttons (NEW) ---
    El.activateFanBtn?.addEventListener('click', async () => {
        setLoadingState(El.activateFanBtn, true, "Starting...");
        try {
            const url = `${BASE_URL}/controlFan?action=on`;
            const response = await fetch(url, { method: 'POST' });
            if (!response.ok) throw new Error(`Command failed: ${response.status}`);
            showToast('Fan activated.', 'success');
            announceSRStatus('Fan turned on.');
            await updateUI();
        } catch (error) {
            console.error("Error activating fan:", error);
            showToast(error.message, 'error');
            await updateUI();
        }
    });

    El.deactivateFanBtn?.addEventListener('click', async () => {
        setLoadingState(El.deactivateFanBtn, true, "Stopping...");
        try {
            const url = `${BASE_URL}/controlFan?action=off`;
            const response = await fetch(url, { method: 'POST' });
            if (!response.ok) throw new Error(`Command failed: ${response.status}`);
            showToast('Fan deactivated.', 'success');
            announceSRStatus('Fan turned off.');
            await updateUI();
        } catch (error) {
            console.error("Error deactivating fan:", error);
            showToast(error.message, 'error');
            await updateUI();
        }
    });

    // --- Extractor Control Buttons (NEW) ---
    El.activateExtractorBtn?.addEventListener('click', async () => {
        setLoadingState(El.activateExtractorBtn, true, "Starting...");
        try {
            const url = `${BASE_URL}/controlExtractor?action=on`;
            const response = await fetch(url, { method: 'POST' });
            if (!response.ok) throw new Error(`Command failed: ${response.status}`);
            showToast('Extractor activated.', 'success');
            announceSRStatus('Extractor turned on.');
            await updateUI();
        } catch (error) {
            console.error("Error activating extractor:", error);
            showToast(error.message, 'error');
            await updateUI();
        }
    });

    El.deactivateExtractorBtn?.addEventListener('click', async () => {
        setLoadingState(El.deactivateExtractorBtn, true, "Stopping...");
        try {
            const url = `${BASE_URL}/controlExtractor?action=off`;
            const response = await fetch(url, { method: 'POST' });
            if (!response.ok) throw new Error(`Command failed: ${response.status}`);
            showToast('Extractor deactivated.', 'success');
            announceSRStatus('Extractor turned off.');
            await updateUI();
        } catch (error) {
            console.error("Error deactivating extractor:", error);
            showToast(error.message, 'error');
            await updateUI();
        }
    });

    // --- Thresholds Form (NEW) ---
    El.thresholdsForm?.addEventListener('submit', async (e) => {
        e.preventDefault();
        const data = {
            fanTempOn: parseFloat(El.fanTempThreshold.value),
            fanHumOn: parseFloat(El.fanHumThreshold.value),
            extractorTempOn: parseFloat(El.extractorTempThreshold.value),
            extractorHumOn: parseFloat(El.extractorHumThreshold.value)
        };

        // Validate
        let valid = true;
        if (isNaN(data.fanTempOn) || data.fanTempOn < 0 || data.fanTempOn > 50) valid = false;
        if (isNaN(data.fanHumOn) || data.fanHumOn < 0 || data.fanHumOn > 100) valid = false;
        if (isNaN(data.extractorTempOn) || data.extractorTempOn < 0 || data.extractorTempOn > 50) valid = false;
        if (isNaN(data.extractorHumOn) || data.extractorHumOn < 0 || data.extractorHumOn > 100) valid = false;

        if (!valid) {
            showToast('Invalid threshold values. Check ranges.', 'warning');
            return;
        }

        const button = El.thresholdsForm.querySelector('button[type="submit"]');
        setLoadingState(button, true, "Saving...");
        try {
            await postData('/setThresholds', data);
            showToast('Thresholds updated successfully.', 'success');
            announceSRStatus('Thresholds configuration saved.');
        } catch (error) {
            /* Error handled by postData */
        }
        finally { setLoadingState(button, false); }
    });

    // --- Test Mode Toggle Button (NEW) ---
    El.toggleTestModeBtn?.addEventListener('click', async () => {
        setLoadingState(El.toggleTestModeBtn, true, "Processing...");
        try {
            const response = await fetch(`${BASE_URL}/testMode`, { method: 'POST' });
            if (!response.ok) throw new Error(`Command failed: ${response.status}`);
            const result = await response.json();
            const isEnabled = result.testModeEnabled;
            showToast(result.message || (isEnabled ? 'Test mode activated' : 'Test mode deactivated'), isEnabled ? 'warning' : 'success');
            announceSRStatus(isEnabled ? 'Test mode enabled. Simulated values active.' : 'Test mode disabled. Real sensor readings resumed.');
            await updateUI();
        } catch (error) {
            console.error("Error toggling test mode:", error);
            showToast(error.message, 'error');
            await updateUI();
        }
    });

    // --- Discord Configuration (NEW) ---
    // Save Discord configuration
    El.discordForm?.addEventListener('submit', async (e) => {
        e.preventDefault();

        const data = {
            webhookUrl: El.discordWebhookUrl.value.trim(),
            enabled: El.discordEnabled.checked,
            alerts: {
                tempHighAlert: El.tempHighAlert.checked,
                tempHighThreshold: parseFloat(El.tempHighThreshold.value),
                tempLowAlert: El.tempLowAlert.checked,
                tempLowThreshold: parseFloat(El.tempLowThreshold.value),
                humHighAlert: El.humHighAlert.checked,
                humHighThreshold: parseFloat(El.humHighThreshold.value),
                humLowAlert: El.humLowAlert.checked,
                humLowThreshold: parseFloat(El.humLowThreshold.value),
                sensorFailAlert: El.sensorFailAlert.checked,
                deviceActivationAlert: El.deviceActivationAlert.checked
            }
        };

        const button = El.discordForm.querySelector('button[type="submit"]');
        setLoadingState(button, true, "Saving...");
        try {
            await postData('/setDiscordConfig', data);
            showToast('Discord configuration saved successfully.', 'success');
            announceSRStatus('Discord alerts configuration updated.');
        } catch (error) {
            /* Error handled by postData */
        }
        finally { setLoadingState(button, false); }
    });

    // Test Discord alert button
    El.testDiscordAlert?.addEventListener('click', async () => {
        if (!El.discordWebhookUrl.value.trim()) {
            showToast('Please enter a webhook URL first.', 'warning');
            return;
        }
        
        // Check if enabled
        if (!El.discordEnabled.checked) {
            showToast('Please enable Discord alerts first.', 'warning');
            return;
        }
        
        setLoadingState(El.testDiscordAlert, true, "Sending...");
        try {
            // Save config first to ensure webhook URL is updated
            const data = {
                webhookUrl: El.discordWebhookUrl.value.trim(),
                enabled: El.discordEnabled.checked,
                alerts: {
                    tempHighAlert: El.tempHighAlert.checked,
                    tempHighThreshold: parseFloat(El.tempHighThreshold.value),
                    tempLowAlert: El.tempLowAlert.checked,
                    tempLowThreshold: parseFloat(El.tempLowThreshold.value),
                    humHighAlert: El.humHighAlert.checked,
                    humHighThreshold: parseFloat(El.humHighThreshold.value),
                    humLowAlert: El.humLowAlert.checked,
                    humLowThreshold: parseFloat(El.humLowThreshold.value),
                    sensorFailAlert: El.sensorFailAlert.checked,
                    deviceActivationAlert: El.deviceActivationAlert.checked
                }
            };
            await postData('/setDiscordConfig', data);
            
            // Send test alert
            await fetch(`${BASE_URL}/testDiscordAlert`, { method: 'POST' });
            
            showToast('Test alert sent! Check your Discord channel.', 'success');
            announceSRStatus('Discord test alert sent.');
        } catch (error) {
            showToast('Error sending test alert: ' + error.message, 'error');
        }
        finally { setLoadingState(El.testDiscordAlert, false); }
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


    El.stageConfigTableBody?.addEventListener('click', async evt => {
        if (!evt.target.closest('.save-stage-btn')) return;
    
        const btn   = evt.target.closest('.save-stage-btn');
        const index = parseInt(btn.dataset.index, 10);
        const row   = btn.closest('tr');
    
        // Inputs de la fila
        const durInput   = row.querySelector('input[data-field="duration_days"]');
        const humInput   = row.querySelector('input[data-field="humidityThreshold"]');
        const waterInput = row.querySelector('input[data-field="wateringTimeSec"]');
    
        // Valores numéricos
        const newDur   = parseInt(durInput.value,   10);
        const newHum   = parseInt(humInput.value,   10);
        const newWater = parseInt(waterInput.value, 10);
    
        // Validaciones
        let ok = true;
        const mark = (inp, cond) => { inp.classList.toggle('is-invalid', !cond); ok = ok && cond; };
    
        mark(durInput,   !isNaN(newDur)   && newDur   >= 1 && newDur   <= 365);
        mark(humInput,   !isNaN(newHum)   && newHum   >= 0 && newHum   <= 100);
        mark(waterInput, !isNaN(newWater) && newWater >= 1 && newWater <= 600);
    
        if (!ok) { showToast('Valores fuera de rango.', 'warning'); return; }
    
        setLoadingState(btn, true, 'Saving...');
        try {
            await postData('/updateStage', {
                index:           index,
                duration_days:   newDur,
                humidityThreshold: newHum,
                wateringTimeSec: newWater
            });
    
            // Refrescar caché local
            const s = allStagesInfo.find(s => s.index === index);
            if (s) {
                s.duration_days     = newDur;
                s.humidityThreshold = newHum;
                s.wateringTimeSec   = newWater;
            }
    
            showToast('Stage updated.', 'success');
        } catch (e) { /* postData ya muestra toast de error */ }
        finally      { setLoadingState(btn, false); }
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

    // Initialize device jump buttons
    initDeviceButtons();

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
        // 5. Load thresholds into form inputs
        await fetchAndUpdateThresholdsDisplay();
        // 6. Load Discord configuration
        await loadDiscordConfig();
        // 7. Initialize interactive elements like Bootstrap tooltips
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

// Logs download button (NEW)
El.downloadLogsBtn?.addEventListener('click', (e) => {
    e.preventDefault();
    downloadLogs();
});
