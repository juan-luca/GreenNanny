// --- START OF FILE app.js ---

// Strict mode is implicitly enabled by type="module"

const BASE_URL = `http://${window.location.hostname}`;
const REFRESH_INTERVAL = 15000;
const CHART_MAX_POINTS = 180;
const HISTORY_MAX_ENTRIES = 150;
const TOAST_TIMEOUT = 6000;
const FETCH_TIMEOUT = 10000;
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
    wifiStatus: document.getElementById('wifiStatus'),
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
};

// --- State Variables ---
let measurementChart = null;
let refreshTimeoutId = null;
let currentStageData = {};
let currentIntervalValue = null;
let allStagesInfo = [];
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
        setTimeout(() => {
            El.srStatusMessages.textContent = message;
        }, 150);
    }
}

function showToast(message, type = 'info', duration = TOAST_TIMEOUT) {
    if (!El.toastContainer) return;
    if (El.toastContainer.children.length >= 5) {
        El.toastContainer.removeChild(El.toastContainer.firstChild);
    }

    const toastId = `toast-${Date.now()}`;
    const toast = document.createElement('div');
    const bgClass = { info: 'bg-info', success: 'bg-success', warning: 'bg-warning text-dark', error: 'bg-danger' }[type] || 'bg-secondary';
    const iconClass = { info: 'fas fa-info-circle', success: 'fas fa-check-circle', warning: 'fas fa-exclamation-triangle', error: 'fas fa-times-circle' }[type] || 'fas fa-bell';
    const textClass = (type === 'warning') ? '' : 'text-white';
    toast.id = toastId;
    toast.className = `toast align-items-center ${textClass} ${bgClass} border-0 mb-2 shadow-lg`;
    toast.setAttribute('role', 'alert');
    toast.setAttribute('aria-live', type === 'error' ? 'assertive' : 'polite');
    toast.setAttribute('aria-atomic', 'true');
    const closeBtnClass = (type === 'warning') ? 'btn-close-dark' : 'btn-close-white';

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
        if (type !== 'error') announceSRStatus(`${type}: ${message}`);
        toast.addEventListener('hidden.bs.toast', () => toast.remove(), { once: true });
    } catch (e) {
        console.error("Failed to initialize Bootstrap toast:", e);
        toast.remove();
    }
}

function setLoadingState(buttonElement, isLoading, loadingText = "Loading...") {
    if (!buttonElement) return;
    const loaderClass = 'loader';
    const originalContentKey = 'originalContent';

    if (isLoading) {
        if (!buttonElement.dataset[originalContentKey]) {
            buttonElement.dataset[originalContentKey] = buttonElement.innerHTML;
        }
        buttonElement.disabled = true;
        buttonElement.setAttribute('aria-busy', 'true');
        buttonElement.innerHTML = `<span class="${loaderClass}" role="status" aria-hidden="true"></span><span class="ms-2">${loadingText}</span>`;
    } else {
        buttonElement.disabled = false;
        buttonElement.removeAttribute('aria-busy');
        if (buttonElement.dataset[originalContentKey]) {
            buttonElement.innerHTML = buttonElement.dataset[originalContentKey];
            delete buttonElement.dataset[originalContentKey];
        }
        else {
             const loaderSpan = buttonElement.querySelector(`.${loaderClass}`);
             if (loaderSpan) loaderSpan.parentElement.textContent = 'Action'; // Generic fallback
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
    if (m > 0 || parts.length === 0) parts.push(`${m}m`);
    return parts.length > 0 ? parts.join(' ') : '0m';
}

function formatTimestampForHistory(epochMs) {
     if (!epochMs || isNaN(epochMs) || epochMs < 1e12) return '?:??';
     const date = new Date(epochMs);
     const now = new Date();
     const options = { month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit', hour12: false, timeZone: 'UTC' };
     if(date.getUTCFullYear() !== now.getUTCFullYear()) {
         options.year = 'numeric';
     }
     try {
         let formatted = new Intl.DateTimeFormat('en-GB', options).format(date);
         const isTodayUTC = date.getUTCDate() === now.getUTCDate() && date.getUTCMonth() === now.getUTCMonth() && date.getUTCFullYear() === now.getUTCFullYear();
         if (isTodayUTC) {
              formatted = date.toLocaleTimeString('en-GB', { hour: '2-digit', minute: '2-digit', hour12: false, timeZone: 'UTC' });
              return `Today, ${formatted} UTC`;
         }
         return `${formatted} UTC`;
     } catch (e) { return date.toUTCString().substring(5, 22); }
}

function updateElementText(element, value, unit = '', precision = 1, naValue = '-', formatter = null) {
    if (!element) return;
    let text = naValue;
    let isNA = true;
    try {
        const currentValue = element.innerHTML;
        if (formatter && value != null) {
            text = formatter(value);
            isNA = (text === naValue || text === null || text === undefined);
        } else if (value != null && !isNaN(value)) {
            text = `${parseFloat(value).toFixed(precision)}${unit ? `<span class="text-muted small ms-1">${unit}</span>` : ''}`;
            isNA = false;
        } else if (typeof value === 'string' && value) {
            text = `${value}${unit ? `<span class="text-muted small ms-1">${unit}</span>` : ''}`;
            isNA = false;
        }
        element.classList.toggle('widget-value-na', isNA);
        if (element.innerHTML !== text) {
            element.innerHTML = text;
            if (!isNA && element.closest('.small-box, .widget-dark')) {
                 element.classList.remove('updated');
                 void element.offsetWidth;
                 element.classList.add('updated');
                 setTimeout(() => { element.classList.remove('updated'); }, UPDATE_FLASH_DURATION);
            }
        }
    } catch (e) {
         console.error("Error formatting element:", element?.id, value, e);
         if (element.textContent !== naValue) { element.textContent = naValue; element.classList.add('widget-value-na'); }
    }
}

// --- Chart Functions ---
function createChart() {
    if (!El.measurementChartCtx) { console.error("Chart context not found"); return null; }
    if (measurementChart) { console.log("Destroying existing chart."); measurementChart.destroy(); measurementChart = null; }
    console.log("Creating new chart.");
    try {
        return new Chart(El.measurementChartCtx, {
            type: 'line',
            data: {
                labels: [],
                datasets: [
                    { label: 'Temperature (°C)', data: [], borderColor: 'var(--gn-chart-temp)', backgroundColor: 'rgba(255, 99, 132, 0.05)', yAxisID: 'yTemp', tension: 0.3, pointRadius: 0, pointHoverRadius: 5, borderWidth: 1.5, parsing: { xAxisKey: 'x', yAxisKey: 'y' } },
                    { label: 'Humidity (%)', data: [], borderColor: 'var(--gn-chart-humid)', backgroundColor: 'rgba(54, 162, 235, 0.05)', yAxisID: 'yHumid', tension: 0.3, pointRadius: 0, pointHoverRadius: 5, borderWidth: 1.5, parsing: { xAxisKey: 'x', yAxisKey: 'y' } },
                    { label: 'Pump Activation', data: [], borderColor: 'transparent', backgroundColor: 'var(--gn-chart-pump)', pointBackgroundColor: 'var(--gn-chart-pump)', yAxisID: 'yHumid', pointStyle: 'triangle', radius: 7, hoverRadius: 9, showLine: false, parsing: { xAxisKey: 'x', yAxisKey: 'y' } }
                ]
            },
            options: {
                responsive: true, maintainAspectRatio: false,
                scales: {
                    x: { type: 'category', ticks: { color: 'var(--gn-chart-text)', maxRotation: 0, autoSkip: true, maxTicksLimit: 8, font: { size: 10 }, callback: function(v, i, t) { return (i === 0 || i === t.length - 1 || i % Math.ceil(t.length / 5) === 0) ? this.getLabelForValue(v) : ''; } }, grid: { color: 'var(--gn-chart-grid)', borderDash: [2, 3] } },
                    yTemp: { type: 'linear', position: 'left', title: { display: true, text: '°C', color: 'var(--gn-chart-temp)', font: { size: 11, weight: 'bold' } }, ticks: { color: 'var(--gn-chart-temp)', precision: 0, font: { size: 10 } }, grid: { color: 'var(--gn-chart-grid)' } },
                    yHumid: { type: 'linear', position: 'right', title: { display: true, text: '%', color: 'var(--gn-chart-humid)', font: { size: 11, weight: 'bold' } }, min: 0, suggestedMax: 100, ticks: { color: 'var(--gn-chart-humid)', precision: 0, font: { size: 10 } }, grid: { drawOnChartArea: false } }
                },
                plugins: {
                    tooltip: { enabled: true, mode: 'index', intersect: false, backgroundColor: 'var(--gn-chart-tooltip-bg)', titleColor: '#fff', bodyColor: '#fff', padding: 8, cornerRadius: 3, titleFont: { size: 11 }, bodyFont: { size: 11 }, titleAlign: 'center', bodyAlign: 'left', callbacks: { title: (i) => formatTimestampForHistory(i[0]?.parsed.x), label: (c) => { let l = c.dataset.label || ''; if (c.dataset.label === 'Pump Activation') return c.parsed.y !== null ? ' Pump Activated' : null; if (l) l += ': '; if (c.parsed.y !== null) { l += c.parsed.y.toFixed(1); if(c.dataset.label === 'Temperature (°C)') l += '°C'; if(c.dataset.label === 'Humidity (%)') l += '%'; } else { l += 'N/A'; } return l; } } },
                    legend: { position: 'bottom', align: 'center', labels: { color: 'var(--gn-chart-text)', usePointStyle: true, padding: 15, boxWidth: 10, font: { size: 11 } }, onHover: (e,i,l) => { try { const ci = l.chart; i.hidden = ci.isDatasetVisible(i.datasetIndex); ci.update(); } catch(e) {console.warn('Chart legend hover error', e)} }, onLeave: (e,i,l) => { try { const ci = l.chart; i.hidden = ci.isDatasetVisible(i.datasetIndex); ci.update(); } catch(e) {console.warn('Chart legend leave error', e)} } }
                },
                interaction: { mode: 'nearest', axis: 'x', intersect: false }, animation: { duration: 400 }, layout: { padding: { top: 5, right: 5, bottom: 0, left: 0 } }
            }
        });
    } catch (e) { console.error("Chart creation failed:", e); if (El.measurementChartContainer) El.measurementChartContainer.innerHTML = `<div class="error-indicator text-danger p-3"><i class="fas fa-exclamation-triangle me-2"></i>Chart Error</div>`; return null; }
}

function updateChart(measurements) {
     const loadingIndicator = El.measurementChartContainer?.querySelector('.loading-indicator');
     const emptyIndicator = El.measurementChartContainer?.querySelector('.empty-indicator');
     const canvas = El.measurementChartCtx?.canvas;
     if (!El.measurementChartCtx || !canvas) return;
     const hasData = measurements && measurements.length > 0;
     if (loadingIndicator) loadingIndicator.style.display = 'none';
     if (emptyIndicator) emptyIndicator.style.display = hasData ? 'none' : 'block';
     canvas.style.display = hasData ? 'block' : 'none';
     if (!hasData) { if (measurementChart) { measurementChart.data.labels = []; measurementChart.data.datasets.forEach(ds => ds.data = []); measurementChart.update('none'); } return; }
    if (!measurementChart) { measurementChart = createChart(); if (!measurementChart) return; }
    const limitedMeasurements = measurements.slice(-CHART_MAX_POINTS);
    const chartLabels = limitedMeasurements.map(m => formatTimestampForHistory(m.epoch_ms));
    const tempData = limitedMeasurements.map(m => ({ x: m.epoch_ms, y: m.temperature ?? null }));
    const humidData = limitedMeasurements.map(m => ({ x: m.epoch_ms, y: m.humidity ?? null }));
    const pumpData = limitedMeasurements.map(m => ({ x: m.epoch_ms, y: m.pumpActivated ? (m.humidity ?? 50) : null }));
    if (!measurementChart.data.datasets || measurementChart.data.datasets.length < 3) { console.warn("Chart datasets missing, recreating."); measurementChart.destroy(); measurementChart = createChart(); if (!measurementChart) return; }
    measurementChart.data.labels = chartLabels;
    measurementChart.data.datasets[0].data = tempData;
    measurementChart.data.datasets[1].data = humidData;
    measurementChart.data.datasets[2].data = pumpData;
    measurementChart.update('resize');
}

// --- Statistics Functions ---
function calculateVPD(temperature, humidity) { if (temperature == null || humidity == null || isNaN(temperature) || isNaN(humidity) || humidity < 0 || humidity > 105) return null; const clampedHumidity = Math.max(0, Math.min(100, humidity)); const svp = 0.6108 * Math.exp((17.27 * temperature) / (temperature + 237.3)); const avp = (clampedHumidity / 100.0) * svp; const vpd = svp - avp; return Math.max(0, vpd); }
function filterMeasurementsByTime(measurements, durationMs) { if (!measurements || measurements.length === 0) return []; const now = Date.now(); const cutoff = now - durationMs; return measurements.filter(m => m.epoch_ms >= cutoff); }
function calculateBasicStats(dataArray, key, recalculateVPD = false) { let sum = 0, min = null, max = null, validCount = 0; const totalCount = dataArray.length; dataArray.forEach(item => { let value = item[key]; if (key === 'vpd' && recalculateVPD) value = calculateVPD(item.temperature, item.humidity); if (value !== null && typeof value === 'number' && !isNaN(value)) { sum += value; validCount++; if (min === null || value < min) min = value; if (max === null || value > max) max = value; } }); const avg = validCount > 0 ? sum / validCount : null; const validity = totalCount > 0 ? (validCount / totalCount) * 100 : 0; return { avg: avg, min: min, max: max, count: totalCount, validCount: validCount, validity: validity }; }
function calculatePeriodStats(allMeasurements) { const stats = { overall: {}, last24h: {}, last7d: {} }; const dataSets = { overall: allMeasurements, last24h: filterMeasurementsByTime(allMeasurements, MS_PER_DAY), last7d: filterMeasurementsByTime(allMeasurements, 7 * MS_PER_DAY) }; for (const period in dataSets) { const data = dataSets[period]; stats[period] = data.length > 0 ? { temp: calculateBasicStats(data, 'temperature'), humid: calculateBasicStats(data, 'humidity'), vpd: calculateBasicStats(data, 'vpd', true), pumpActivations: data.filter(m => m.pumpActivated).length, measurementCount: data.length, firstTimestamp: data[0]?.epoch_ms || null, lastTimestamp: data[data.length - 1]?.epoch_ms || null, dataValidity: calculateBasicStats(data, 'temperature').validity } : { temp: { avg: null, min: null, max: null, validCount: 0, validity: 0 }, humid: { avg: null, min: null, max: null, validCount: 0, validity: 0 }, vpd: { avg: null, min: null, max: null, validCount: 0, validity: 0 }, pumpActivations: 0, measurementCount: 0, firstTimestamp: null, lastTimestamp: null, dataValidity: 0 }; } return stats; }
function calculateStageStats(allMeasurements, stagesInfo) { const stageStats = {}; const measurementsByStage = {}; if (!stagesInfo || stagesInfo.length === 0) return {}; stagesInfo.forEach(stage => { measurementsByStage[stage.name] = []; stageStats[stage.name] = { temp: {avg:null,min:null,max:null,validCount:0,validity:0}, humid: {avg:null,min:null,max:null,validCount:0,validity:0}, vpd: {avg:null,min:null,max:null,validCount:0,validity:0}, pumpActivations:0, measurementCount:0, firstTimestamp:null, lastTimestamp:null, dataValidity:0 }; }); allMeasurements.forEach(m => { if (m.stage && measurementsByStage[m.stage]) measurementsByStage[m.stage].push(m); }); for (const stageName in measurementsByStage) { const data = measurementsByStage[stageName]; if (data.length > 0) stageStats[stageName] = { temp: calculateBasicStats(data, 'temperature'), humid: calculateBasicStats(data, 'humidity'), vpd: calculateBasicStats(data, 'vpd', true), pumpActivations: data.filter(m => m.pumpActivated).length, measurementCount: data.length, firstTimestamp: data[0]?.epoch_ms || null, lastTimestamp: data[data.length - 1]?.epoch_ms || null, dataValidity: calculateBasicStats(data, 'temperature').validity }; } return stageStats; }
async function calculateAllStats(allMeasurements, stagesInfo) { return new Promise(resolve => { setTimeout(() => { const periodStats = calculatePeriodStats(allMeasurements); const stageStats = calculateStageStats(allMeasurements, stagesInfo); resolve({ periodStats, stageStats }); }, 10); }); }

// --- UI Update Functions ---
function updateStatisticsUI(statsData) {
    if (!El.statisticsSection) return;
    const { periodStats, stageStats } = statsData;
    const loadingIndicator = El.statisticsSection.querySelector('.loading-indicator');
    if (loadingIndicator) loadingIndicator.remove();
    if (!periodStats || !stageStats || !allStagesInfo || allStagesInfo.length === 0) { El.statisticsSection.innerHTML = `<div class="widget-dark p-3 error-indicator"><i class="fas fa-exclamation-circle me-2"></i>Stats Error</div>`; return; }
    const fmt = (v, p = 1, u = '') => v === null ? `<span class="stat-na">N/A</span>` : `${v.toFixed(p)}${u}`;
    const fmtRng = (s, e) => s ? `${formatTimestampForHistory(s)}<span class="text-muted mx-1">–</span> ${formatTimestampForHistory(e)}` : 'N/A';
    const fmtDet = (o, p = 1, u = '') => `Avg: ${fmt(o.avg,p,u)}\nMin: ${fmt(o.min,p,u)}\nMax: ${fmt(o.max,p,u)}\nData: ${o.validCount}/${o.count}\n(${fmt(o.validity,0)}% Valid)`;
    let html = '<div class="row gy-4">';
    html += `<div class="col-xl-6 col-lg-12"><div class="widget-dark h-100"><div class="widget-title">Period Stats</div><div class="widget-body stats-table-container"><table class="table table-sm table-borderless table-dark small mb-0"><thead class="sticky-top bg-dark"><tr><th>Period</th><th>Temp(°C)</th><th>Humid(%)</th><th>VPD(kPa)</th><th>Pump</th><th>Data</th></tr></thead><tbody>`;
    [{id:'overall',lbl:'Overall',d:periodStats.overall},{id:'last7d',lbl:'Last 7 Days',d:periodStats.last7d},{id:'last24h',lbl:'Last 24 Hrs',d:periodStats.last24h}].forEach(p=>{const d=p.d; const has=d&&d.measurementCount>0; html+=`<tr><td class="text-light"><strong>${p.lbl}</strong></td><td data-bs-toggle="tooltip" title="${fmtDet(d.temp)}">${has?`${fmt(d.temp.avg)}<small>(${fmt(d.temp.min)}–${fmt(d.temp.max)})</small>`:fmt(null)}</td><td data-bs-toggle="tooltip" title="${fmtDet(d.humid)}">${has?`${fmt(d.humid.avg)}<small>(${fmt(d.humid.min)}–${fmt(d.humid.max)})</small>`:fmt(null)}</td><td data-bs-toggle="tooltip" title="${fmtDet(d.vpd,2)}">${has?`${fmt(d.vpd.avg,2)}<small>(${fmt(d.vpd.min,2)}–${fmt(d.vpd.max,2)})</small>`:fmt(null)}</td><td data-bs-toggle="tooltip" title="${d.pumpActivations||0} act">${d.pumpActivations||0}</td><td data-bs-toggle="tooltip" title="${d.measurementCount||0} meas, ${fmt(d.dataValidity,0)}% ok">${d.measurementCount||0}(${fmt(d.dataValidity,0)}%)</td></tr>`; if(has&&d.firstTimestamp){html+=`<tr class="text-muted"><td colspan="6">Range: ${fmtRng(d.firstTimestamp,d.lastTimestamp)}</td></tr>`;}else if(!has){html+=`<tr class="text-muted"><td colspan="6">No data</td></tr>`;}}); html += `</tbody></table></div></div></div>`;
    html += `<div class="col-xl-6 col-lg-12"><div class="widget-dark h-100"><div class="widget-title">Stats Per Stage</div><div class="widget-body stats-table-container"><table class="table table-sm table-borderless table-dark small mb-0"><thead class="sticky-top bg-dark"><tr><th>Stage</th><th>Temp(°C)</th><th>Humid(%)</th><th>VPD(kPa)</th><th>Pump</th><th>Data</th></tr></thead><tbody>`;
    allStagesInfo.forEach(si=>{const sN=si.name;const d=stageStats[sN];const has=d&&d.measurementCount>0; html+=`<tr><td class="text-light"><strong>${sN}</strong></td><td data-bs-toggle="tooltip" title="${fmtDet(d.temp)}">${has?`${fmt(d.temp.avg)}<small>(${fmt(d.temp.min)}–${fmt(d.temp.max)})</small>`:fmt(null)}</td><td data-bs-toggle="tooltip" title="${fmtDet(d.humid)}">${has?`${fmt(d.humid.avg)}<small>(${fmt(d.humid.min)}–${fmt(d.humid.max)})</small>`:fmt(null)}</td><td data-bs-toggle="tooltip" title="${fmtDet(d.vpd,2)}">${has?`${fmt(d.vpd.avg,2)}<small>(${fmt(d.vpd.min,2)}–${fmt(d.vpd.max,2)})</small>`:fmt(null)}</td><td data-bs-toggle="tooltip" title="${d.pumpActivations||0} act">${d.pumpActivations||0}</td><td data-bs-toggle="tooltip" title="${d.measurementCount||0} meas, ${fmt(d.dataValidity,0)}% ok">${d.measurementCount||0}(${fmt(d.dataValidity,0)}%)</td></tr>`; if(has&&d.firstTimestamp){html+=`<tr class="text-muted"><td colspan="6">Range: ${fmtRng(d.firstTimestamp,d.lastTimestamp)}</td></tr>`;}else if(!has){html+=`<tr class="text-muted"><td colspan="6">No data</td></tr>`;}}); html += `</tbody></table></div></div></div>`;
    html += '</div>';
    El.statisticsSection.innerHTML = html;
    initTooltips(El.statisticsSection);
}

// --- Conditional Styling Helpers ---
function updateWidgetConditionalStyle(widgetEl, conditionClass, conditionMet) { if (widgetEl) widgetEl.classList.toggle(conditionClass, conditionMet); }

function updatePumpStatusIndicator(isOn) {
    if (!El.pumpStatusIndicator) return;
    const iconClass = isOn ? 'fa-play-circle text-success' : 'fa-stop-circle text-secondary';
    El.pumpStatusIndicator.innerHTML = `<i class="fas ${iconClass}" aria-hidden="true"></i> ${isOn ? 'ON' : 'OFF'}`;
    El.pumpStatusIndicator.classList.toggle('pulsing', isOn);
    if(El.activatePumpBtn) El.activatePumpBtn.disabled = isOn;
    if(El.deactivatePumpBtn) El.deactivatePumpBtn.disabled = !isOn;
}

function updateWiFiStatusIndicator(rssi) {
     if (!El.wifiStatus) return;
     let iconClass = 'fa-wifi text-secondary'; let text = 'Disconnected'; let title = 'WiFi Disconnected';
     if (rssi != null && rssi !== 0) {
         text = `Connected (${rssi} dBm)`; title = `Connected | Signal: ${rssi} dBm`;
         if (rssi >= -67) iconClass = 'fa-wifi text-success'; else if (rssi >= -75) iconClass = 'fa-wifi text-warning'; else iconClass = 'fa-wifi text-danger opactiy-75';
     } else { iconClass = 'fa-wifi-slash text-danger'; }
     El.wifiStatus.innerHTML = `<i class="fas ${iconClass}" aria-hidden="true"></i> ${text}`;
     // Set the attribute that Bootstrap uses to store the title
     El.wifiStatus.setAttribute('data-bs-original-title', title);

     // Initialize tooltip if it doesn't exist for this element
     let tooltipInstance = bootstrap.Tooltip.getInstance(El.wifiStatus);
     if (!tooltipInstance) {
         // Pass title option during initialization
         tooltipInstance = new bootstrap.Tooltip(El.wifiStatus, {
             trigger: 'hover',
             title: title // Ensure title is set on creation
         });
     } else {
         // If instance exists, Bootstrap should pick up the changed data-bs-original-title attribute
         // No explicit setContent needed, avoiding the previous error.
     }
}

// --- Main UI Update Function ---
async function updateUI() {
    if (document.hidden) return; // Don't run if tab is backgrounded
    let statsData = {};
    if(El.updatingIndicator) El.updatingIndicator.style.display = 'inline-block';

    try {
        // Fetch interval only if needed (reduces requests)
        if (currentIntervalValue === null) {
            fetchAndUpdateIntervalDisplay(); // Fire and forget is ok
        }

        const [dataRes, histRes] = await Promise.allSettled([
            fetchData('/data'),
            fetchData('/loadMeasurement')
        ]);

        // --- Process coreData ---
        if (dataRes.status === 'fulfilled' && dataRes.value) {
            const coreData = dataRes.value;
            lastCoreData = coreData; // Store current data

            // Update page title
            if (El.pageTitle) El.pageTitle.textContent = `Green Nanny ${coreData.pumpStatus ? '💧' : '🌿'}`;

            // Update Widgets
            updateElementText(El.temp, coreData.temperature, '°C');
            updateElementText(El.humidity, coreData.humidity, '%');
            updateElementText(El.vpd, coreData.vpd, 'kPa', 2);
            updateElementText(El.pumpActivationCount, coreData.pumpActivationCount, '', 0);
            updateElementText(El.totalElapsedTime, coreData.elapsedTime, '', 0, 'N/A', formatElapsedTime);
            updateElementText(El.lastMeasurementTime, coreData.lastMeasurementTimestamp, '', 0, '-', formatTimestampForHistory);

            // Apply conditional styles to widgets
            updateWidgetConditionalStyle(El.tempWidget, 'temp-cold', coreData.temperature < 18);
            updateWidgetConditionalStyle(El.tempWidget, 'temp-hot', coreData.temperature > 30);
            const stageThreshold = coreData.currentStageThreshold;
            updateWidgetConditionalStyle(El.humidWidget, 'humid-low', stageThreshold && coreData.humidity < stageThreshold - 5);
            updateWidgetConditionalStyle(El.humidWidget, 'humid-high', coreData.humidity > 85);

            // Update Stage information display
            const cs = { name: coreData.currentStageName, threshold: coreData.currentStageThreshold, watering: coreData.currentStageWateringSec, manual: coreData.manualStageControl };
            currentStageData = { index: coreData.currentStageIndex, name: cs.name, manual: cs.manual };
            updateElementText(El.currentStageLabel, cs.name || 'Unknown', '', null, '-');
            if (El.currentStageParams) El.currentStageParams.textContent = cs.threshold ? `Thr: ${cs.threshold}% | Water: ${cs.watering}s` : 'Threshold N/A';
            if (El.manualControlIndicator) El.manualControlIndicator.style.display = cs.manual ? 'inline-block' : 'none';
            if (El.stageModeIndicator) { El.stageModeIndicator.innerHTML = `<i class="fas ${cs.manual ? 'fa-user-cog' : 'fa-robot'}" aria-hidden="true"></i> ${cs.manual ? 'Manual' : 'Auto'}`; El.stageModeIndicator.className = `badge ${cs.manual ? 'bg-warning text-dark' : 'bg-info'}`; }

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
            console.error('Failed core data fetch:', dataRes.reason || 'Unknown');
            showToast('Error fetching core device data!', 'error', 10000);
            // Mark widgets as errored
            document.querySelectorAll('.widget-loading').forEach(el => {
                el.classList.remove('widget-loading');
                const h3 = el.querySelector('h3'); if(h3) h3.innerHTML='<span class="text-danger">Error</span>';
                el.classList.add('border', 'border-danger');
            });
        }

        // --- Process History & Statistics ---
        let measurements = [];
        if (histRes.status === 'fulfilled' && Array.isArray(histRes.value)) {
            measurements = histRes.value.sort((a, b) => a.epoch_ms - b.epoch_ms); // Sort just in case
        } else {
            console.warn('History fetch failed:', histRes.reason || 'Empty response');
            // Display appropriate message in history panel if needed
            if (El.measurementHistory) {
                const loadingIndicator = El.measurementHistory.querySelector('.loading-indicator');
                if (loadingIndicator) loadingIndicator.remove();
                if (!El.measurementHistory.querySelector('.empty-indicator')) {
                     El.measurementHistory.innerHTML = `<div class='measurement-history-entry empty-indicator'>Could not load history.</div>`;
                }
            }
        }

        // Update History Log UI
        if (El.measurementHistory) {
             const loadingIndicator = El.measurementHistory.querySelector('.loading-indicator');
             if (loadingIndicator) loadingIndicator.remove();
             const start = Math.max(0, measurements.length - HISTORY_MAX_ENTRIES);
             const recent = measurements.slice(start).reverse(); // Newest first
             const firstRenderedTs = El.measurementHistory.querySelector('.measurement-history-entry .timestamp')?.dataset.epochMs;
             const newestTs = recent[0]?.epoch_ms;
             const isNewData = newestTs && (!firstRenderedTs || newestTs > parseInt(firstRenderedTs));
             const html = recent.length > 0 ? recent.map((m, i) => { const ts = m.epoch_ms; const dispT = formatTimestampForHistory(ts); const tempT = m.temperature != null ? `${m.temperature.toFixed(1)}°C` : 'N/A'; const humT = m.humidity != null ? `${m.humidity.toFixed(1)}%` : 'N/A'; const fl = (i === 0 && isNewData) ? ' new-entry-flash' : ''; return `<div class="measurement-history-entry${fl}" data-timestamp="${ts}"><span class="timestamp" data-epoch-ms="${ts}">${dispT}</span><span class="data-point"><i class="fas fa-thermometer-half" aria-hidden="true"></i>${tempT}</span><span class="data-point"><i class="fas fa-tint" aria-hidden="true"></i>${humT}</span>${m.pumpActivated ? '<span class="data-point"><i class="fas fa-play-circle" aria-hidden="true"></i>Pump</span>' : ''}${m.stage ? `<span class="data-point"><i class="fas fa-seedling" aria-hidden="true"></i>${m.stage}</span>` : ''}</div>`; }).join('') : `<div class='measurement-history-entry empty-indicator'>No history recorded.</div>`;
             // Only update DOM if HTML content has actually changed
             if (El.measurementHistory.innerHTML !== html) {
                 El.measurementHistory.innerHTML = html;
                 El.measurementHistory.scrollTop = 0; // Scroll to top (newest entry)
             }
        }

        // Calculate and Update Statistics Section
        if (El.statisticsSection && !El.statisticsSection.querySelector('.error-indicator')) {
            El.statisticsSection.innerHTML = `<div class="widget-dark p-3 loading-indicator"><span class="spinner-border spinner-border-sm"></span> Calculating Stats...</div>`;
        }
        try {
            if (!allStagesInfo || allStagesInfo.length === 0) await fetchStagesInfo(); // Ensure stages are loaded
            statsData = await calculateAllStats(measurements, allStagesInfo);
            updateStatisticsUI(statsData); // Update the stats UI part
        } catch (e) {
            console.error("Stats calculation/update failed:", e);
            if (El.statisticsSection) El.statisticsSection.innerHTML = `<div class="widget-dark p-3 error-indicator"><i class="fas fa-exclamation-triangle me-2"></i>Error Calculating Stats</div>`;
            statsData = { periodStats: null, stageStats: null }; // Reset stats data on error
        }

        // Update Chart
        updateChart(measurements);

    } catch (err) { // Catch errors from overall update process (e.g., fetch failures)
        console.error('General UI update cycle error:', err);
        showToast(`UI Update failed: ${err.message}`, 'error');
        // Maybe add a persistent error banner?
        if (El.statisticsSection && !El.statisticsSection.querySelector('.error-indicator')) {
             El.statisticsSection.innerHTML = `<div class="widget-dark p-3 text-warning text-center">UI update cycle failed.</div>`;
        }
    } finally {
        // Always hide updating indicator
        if(El.updatingIndicator) El.updatingIndicator.style.display = 'none';
        // Clear previous timer and schedule next update
        clearTimeout(refreshTimeoutId);
        if (!document.body.classList.contains('system-restarting')) { // Don't reschedule if restarting
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
             El.intervalInput.value = currentIntervalValue;
             El.intervalInput.placeholder = `Current: ${currentIntervalValue}h`;
        }
     } catch (error) {
         console.error("Failed to fetch measurement interval:", error);
         El.currentInterval.textContent = 'Err'; // Indicate error
     }
}

async function fetchStagesInfo() {
     try {
         const stages = await fetchData('/listStages');
         if (stages && Array.isArray(stages)) {
             allStagesInfo = stages; // Store globally
         } else {
             console.warn("Could not fetch or parse stages info from /listStages");
             allStagesInfo = [];
         }
     } catch (error) {
         console.error("Failed to fetch stages info:", error);
         allStagesInfo = [];
     }
}

async function populateStageSelector() {
    if (!El.manualStageSelect) return;
    El.manualStageSelect.innerHTML = `<option value="" disabled selected>Loading...</option>`;
    try {
        if (!allStagesInfo || allStagesInfo.length === 0) {
            await fetchStagesInfo(); // Fetch if needed
        }
        if (allStagesInfo.length > 0) {
            El.manualStageSelect.innerHTML = allStagesInfo.map(stage =>
                `<option value="${stage.index}">${stage.name} (T:${stage.humidityThreshold}% W:${stage.wateringTimeSec}s)</option>`
            ).join('');
            // Set selected based on last known data, default to first if unknown
            if (lastCoreData && lastCoreData.currentStageIndex != null) {
                 El.manualStageSelect.value = lastCoreData.currentStageIndex;
            } else if (allStagesInfo.length > 0) {
                 El.manualStageSelect.value = allStagesInfo[0].index;
            }
        } else {
             El.manualStageSelect.innerHTML = `<option value="" disabled selected>No stages defined</option>`;
        }
    } catch (error) {
        console.error("Failed stage select populate:", error);
        El.manualStageSelect.innerHTML = `<option value="" disabled selected>Error</option>`;
    }
}

function initTooltips(parentElement = document.body) {
     // Dispose only tooltips within the specific parent element before re-initializing
     const currentTooltips = parentElement.querySelectorAll('[data-bs-toggle="tooltip"]');
     currentTooltips.forEach(el => {
         const instance = bootstrap.Tooltip.getInstance(el);
         if (instance) {
             instance.dispose();
         }
     });
     // Re-create tooltips for all elements within the parent
     bsTooltips = []; // Reset global array or manage differently if needed outside stats
     const tooltipTriggerList = [].slice.call(parentElement.querySelectorAll('[data-bs-toggle="tooltip"]'));
     bsTooltips = tooltipTriggerList.map(el => new bootstrap.Tooltip(el, {
         trigger: 'hover focus',
         boundary: 'window',
         customClass: 'gn-tooltip' // Optional custom class
     }));
}


// --- Event Listener Setup ---
function setupEventListeners() {
    // --- Forms ---
    El.intervalForm?.addEventListener('submit', async (e) => {
        e.preventDefault();
        const input = El.intervalInput;
        const value = input?.value;
        const numValue = parseInt(value);
        if (!value || isNaN(numValue) || numValue < 1 || numValue > 167) {
             showToast('Invalid interval (1-167h).', 'warning');
             input?.classList.add('is-invalid'); input?.focus(); return;
        }
        input?.classList.remove('is-invalid');
        const button = El.intervalForm.querySelector('button[type="submit"]');
        setLoadingState(button, true, "Saving...");
        try {
            await postData('/setMeasurementInterval', { interval: numValue });
            showToast(`Interval set to ${numValue}h`, 'success');
            announceSRStatus(`Interval updated to ${numValue} hours.`);
            currentIntervalValue = numValue;
            if(El.currentInterval) El.currentInterval.textContent = numValue;
            if(El.intervalInput) El.intervalInput.placeholder = `Current: ${numValue}h`;
        } catch (error) {/* Handled by postData */}
        finally { setLoadingState(button, false); }
    });

    El.intervalInput?.addEventListener('input', () => {
        const value = El.intervalInput.value;
        const numValue = parseInt(value);
        // Show invalid state only if not empty and invalid
        El.intervalInput.classList.toggle('is-invalid', value !== '' && (isNaN(numValue) || numValue < 1 || numValue > 167));
    });

    El.manualStageForm?.addEventListener('submit', async (e) => {
        e.preventDefault();
        const select = El.manualStageSelect;
        const value = select?.value;
        const numValue = parseInt(value);
        if (!value || isNaN(numValue)) {
             showToast('Select valid stage.', 'warning');
             select?.classList.add('is-invalid'); select?.focus(); return;
        }
        select?.classList.remove('is-invalid');
        const button = El.manualStageForm.querySelector('button[type="submit"]');
        setLoadingState(button, true, "Setting...");
        try {
            const stageName = select.options[select.selectedIndex].text.split(' (')[0];
            await postData('/setManualStage', { stage: numValue });
            showToast(`Manual stage: "${stageName}"`, 'success');
            announceSRStatus(`Stage control: manual - ${stageName}.`);
            await updateUI(); // Refresh UI immediately
        } catch (error) {/* Handled by postData */}
        finally { setLoadingState(button, false); }
    });

    El.manualStageSelect?.addEventListener('change', () => El.manualStageSelect?.classList.remove('is-invalid'));

    // --- Buttons ---
    El.activatePumpBtn?.addEventListener('click', async () => {
        const durationStr = prompt('Pump duration (SECONDS):', '30');
        if (durationStr === null) return;
        const duration = parseInt(durationStr);
        if (isNaN(duration) || duration <= 0 || duration > 600) { showToast('Invalid duration (1-600s).', 'warning'); return; }
        setLoadingState(El.activatePumpBtn, true, "Activating...");
        try { await postData('/controlPump', { action: 'on', duration: duration }); showToast(`Pump ON for ${duration}s.`, 'success'); announceSRStatus(`Pump ON ${duration}s.`); await updateUI(); } catch (error) { await updateUI(); } // Refresh anyway
        // No finally block needed here as updateUI handles loader state if button still exists
    });

    El.deactivatePumpBtn?.addEventListener('click', async () => {
        if (!confirm('DEACTIVATE PUMP NOW?')) return;
        setLoadingState(El.deactivatePumpBtn, true, "Stopping...");
        try { await postData('/controlPump', { action: 'off' }); showToast('Pump OFF.', 'success'); announceSRStatus('Pump OFF.'); await updateUI(); } catch (error) { await updateUI(); }
        // No finally block needed here
    });

    El.takeMeasurementBtn?.addEventListener('click', async () => {
        setLoadingState(El.takeMeasurementBtn, true, "Measuring...");
        showToast('Triggering measurement...', 'info', 2500);
        try { await postData('/takeMeasurement', {}, 'POST'); await new Promise(r => setTimeout(r, 2000)); await updateUI(); showToast('Measurement complete.', 'success', 3000); announceSRStatus('Measurement done.'); } catch (error) {/* Handled by postData */}
        finally { setLoadingState(El.takeMeasurementBtn, false); }
    });

    El.clearMeasurementsBtn?.addEventListener('click', async () => {
        if (!confirm('⚠️ DELETE ALL HISTORY? ⚠️\nCannot be undone.')) return;
        setLoadingState(El.clearMeasurementsBtn, true, "Clearing...");
        try {
            await postData('/clearHistory', {}, 'POST');
            showToast('History cleared.', 'success'); announceSRStatus('History deleted.');
            if(measurementChart) { measurementChart.data.labels = []; measurementChart.data.datasets.forEach(ds => ds.data = []); measurementChart.update('none'); }
            if(El.measurementHistory) El.measurementHistory.innerHTML = `<div class='measurement-history-entry empty-indicator'>History cleared.</div>`;
            if(El.statisticsSection) El.statisticsSection.innerHTML = `<div class="widget-dark p-3 text-muted text-center">Stats cleared.</div>`;
            await updateUI(); // Fetch empty data
        } catch (error) {/* Handled by postData */}
        finally { setLoadingState(El.clearMeasurementsBtn, false); }
    });

    El.restartSystemBtn?.addEventListener('click', async () => {
        if (!confirm('RESTART DEVICE?\nPage will stop updating. Refresh after ~30s.')) return;
        setLoadingState(El.restartSystemBtn, true, "Restarting...");
        showToast('Restarting... Refresh page in ~30s.', 'warning', 10000);
        announceSRStatus('System restart.');
        document.body.classList.add('system-restarting');
        document.querySelectorAll('button, input, select').forEach(el => el.disabled = true);
        clearTimeout(refreshTimeoutId); // Stop updates
        try { await postData('/restartSystem', {}, 'POST', 5000); } catch (error) { showToast('Restart sent, confirm failed. Refresh manually.', 'error'); }
        // Do not re-enable controls or remove overlay
    });

    El.refreshStageBtn?.addEventListener('click', async () => {
        setLoadingState(El.refreshStageBtn, true, "Refreshing...");
        try { await updateUI(); showToast('Stage refreshed.', 'info', 2000); announceSRStatus('Stage updated.'); } catch (error) {/* Handled by updateUI */}
        finally { setLoadingState(El.refreshStageBtn, false); }
    });

    El.resetManualStageBtn?.addEventListener('click', async () => {
        if (!confirm('RETURN TO AUTO STAGE CONTROL?')) return;
        setLoadingState(El.resetManualStageBtn, true, "Resetting...");
        try { await postData('/resetManualStage', {}, 'POST'); showToast('Stage control: automatic.', 'success'); announceSRStatus('Stage control: automatic.'); await updateUI(); } catch (error) {/* Handled by postData */}
        finally { setLoadingState(El.resetManualStageBtn, false); }
    });

    // --- Window Events ---
    document.addEventListener('visibilitychange', () => {
        if (!document.hidden) {
            console.log("Tab visible, forcing UI update.");
            clearTimeout(refreshTimeoutId); // Prevent duplicate updates
            updateUI(); // Update immediately when tab becomes visible
        } else {
            console.log("Tab hidden, pausing automatic updates.");
            clearTimeout(refreshTimeoutId); // Stop scheduled updates
            if(El.updatingIndicator) El.updatingIndicator.style.display = 'none'; // Hide indicator
        }
    });
}

// --- Initialization ---
document.addEventListener('DOMContentLoaded', async () => {
    console.log("DOM loaded. Initializing Green Nanny Dashboard...");
    document.querySelectorAll('.small-box, .widget-dark[id]').forEach(el => el.classList.add('widget-loading')); // Set initial loading state via CSS class
    showGlobalLoader(true); // Show global loader bar

    if (!El.measurementChartCtx) console.warn("Chart canvas context missing.");

    setupEventListeners(); // Set up button/form interactions

    try {
        await fetchStagesInfo(); // Get stage definitions first
        await updateUI(); // Perform the first full data fetch and UI update
        await populateStageSelector(); // Populate dropdown based on fetched data
        initTooltips(); // Initialize all Bootstrap tooltips after initial render

        console.log("Initialization complete.");
        announceSRStatus("Dashboard loaded."); // Announce successful load

    } catch (error) { // Catch errors during the initial setup phase
        console.error("Dashboard initialization failed:", error);
        showToast("Failed to load initial dashboard data.", "error", 15000); // Show persistent error toast
        announceSRStatus("Error: Dashboard failed to load."); // Announce error
        // Display error messages in relevant sections
        const main = document.querySelector('.content-wrapper');
        if(main) main.insertAdjacentHTML('afterbegin', `<div class="alert alert-danger m-3"><strong>Init Error:</strong> Check device connection & refresh.</div>`);
        if (El.statisticsSection) El.statisticsSection.innerHTML = `<div class="widget-dark p-3 error-indicator"><i class="fas fa-exclamation-triangle"></i> Init Failed</div>`;
        // Remove loading state even on error to show error messages
        document.querySelectorAll('.widget-loading').forEach(el => el.classList.remove('widget-loading'));
    } finally {
         showGlobalLoader(false); // Always hide global loader bar
         // Note: Auto-refresh timer is started within the `updateUI` function's finally block
    }
});
// --- END OF FILE app.js ---