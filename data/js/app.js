// js/app.js completo - CON MEJORAS INTEGRADAS Y CORRECCIÓN DE CHART
// =================================================================

// --- Constants ---
const BASE_URL = 'http://192.168.0.78'; // Casa: 192.168.0.78, Hotspot: 192.168.4.1
const REFRESH_INTERVAL = 15000; // Refresh data every 15 seconds
const CHART_MAX_POINTS = 100; // Limit chart history points
const TOAST_TIMEOUT = 4000; // How long toasts stay visible

// --- Element Selectors (using const for robustness) ---
const El = {
    temp: document.getElementById('temperature'),
    humidity: document.getElementById('humidity'),
    humidityThreshold: document.getElementById('humidityThreshold'),
    pumpActivationCount: document.getElementById('pumpActivationCount'),
    vpd: document.getElementById('vpd'),
    totalElapsedTime: document.getElementById('totalElapsedTime'),
    measurementHistory: document.getElementById('measurementHistory'),
    measurementChartCtx: document.getElementById('measurementChart')?.getContext('2d'),
    humidityForm: document.getElementById('humidityForm'),
    umbralInput: document.getElementById('umbral'),
    activatePumpBtn: document.getElementById('activatePump'),
    deactivatePumpBtn: document.getElementById('deactivatePump'),
    takeMeasurementBtn: document.getElementById('takeMeasurement'),
    clearMeasurementsBtn: document.getElementById('clearMeasurements'),
    restartSystemBtn: document.getElementById('restartSystem'),
    currentStageLabel: document.getElementById('currentStageLabel'),
    currentStageParams: document.getElementById('currentStageParams'),
    refreshStageBtn: document.getElementById('refreshStage'),
    manualStageForm: document.getElementById('manualStageForm'),
    manualStageSelect: document.getElementById('manualStageSelect'),
    resetManualStageBtn: document.getElementById('resetManualStageButton'),
    pumpStatusIndicator: document.getElementById('pumpStatusIndicator'),
    lastMeasurementTime: document.getElementById('lastMeasurementTime'),
    toastContainer: document.getElementById('toastContainer'),
    pageTitle: document.querySelector('title'),
    // Add more selectors as needed
};

let measurementChart = null; // Chart instance
let refreshTimeoutId = null; // To manage refresh interval

// --- API Functions ---
async function fetchData(endpoint) {
    showGlobalLoader(true);
    try {
        const res = await fetch(`${BASE_URL}${endpoint}`);
        if (!res.ok) {
            throw new Error(`HTTP error! status: ${res.status} ${res.statusText || ''}`);
        }
        return await res.json();
    } catch (e) {
        console.error('Error fetching data:', endpoint, e);
        showToast(`Error fetching ${endpoint}: ${e.message}`, 'error');
        throw e; // Re-throw for caller handling if needed
    } finally {
        showGlobalLoader(false);
    }
}

async function postData(endpoint, data = {}, method = 'POST') {
    showGlobalLoader(true);
    try {
        const options = {
            method: method,
            headers: { 'Content-Type': 'application/json' },
        };
        if (method !== 'GET' && method !== 'HEAD') {
             options.body = JSON.stringify(data);
        }
        const res = await fetch(`${BASE_URL}${endpoint}`, options);
        if (!res.ok) {
            let errorBody = '';
            try { // Try to get error message from response body
                errorBody = await res.text();
            } catch (_) { /* Ignore */ }
            throw new Error(`HTTP error! status: ${res.status} ${res.statusText || ''} ${errorBody ? `(${errorBody})` : ''}`);
        }
        // Check if response is JSON before parsing
        const contentType = res.headers.get("content-type");
        if (contentType && contentType.indexOf("application/json") !== -1) {
            return await res.json();
        } else {
            return await res.text(); // Return text for non-JSON responses
        }
    } catch (e) {
        console.error('Error posting data:', endpoint, data, e);
        showToast(`Error posting to ${endpoint}: ${e.message}`, 'error');
        throw e;
    } finally {
        showGlobalLoader(false);
    }
}

// --- UI Helper Functions ---

function showToast(message, type = 'info') { // types: info, success, warning, error
    if (!El.toastContainer) return;

    const toast = document.createElement('div');
    toast.className = `toast show align-items-center text-white bg-${type === 'error' ? 'danger' : type} border-0`;
    toast.setAttribute('role', 'alert');
    toast.setAttribute('aria-live', 'assertive');
    toast.setAttribute('aria-atomic', 'true');

    const iconClass = {
        info: 'fas fa-info-circle',
        success: 'fas fa-check-circle',
        warning: 'fas fa-exclamation-triangle',
        error: 'fas fa-times-circle'
    }[type];

    toast.innerHTML = `
        <div class="d-flex">
            <div class="toast-body">
                <i class="${iconClass} mr-2"></i> ${message}
            </div>
            <button type="button" class="btn-close btn-close-white me-2 m-auto" data-bs-dismiss="toast" aria-label="Close"></button>
        </div>
    `;

    // Use Bootstrap's Toast component if available, otherwise fallback
    if (typeof bootstrap !== 'undefined' && bootstrap.Toast) {
        El.toastContainer.appendChild(toast);
        const bsToast = new bootstrap.Toast(toast, { delay: TOAST_TIMEOUT, autohide: true });
        bsToast.show();
        toast.addEventListener('hidden.bs.toast', () => toast.remove());
    } else {
        // Manual fallback
        El.toastContainer.appendChild(toast);
        const closeButton = toast.querySelector('.btn-close');
        closeButton?.addEventListener('click', () => toast.remove());
        setTimeout(() => {
             toast.style.opacity = '0';
             setTimeout(() => toast.remove(), 500); // Wait for fade out
        }, TOAST_TIMEOUT);
         // Add basic fade-in
         requestAnimationFrame(() => { toast.style.opacity = '1'; });
    }
}


function setLoadingState(buttonElement, isLoading) {
    if (!buttonElement) return;
    const loaderClass = 'loader'; // Defined in HTML CSS
    let loader = buttonElement.querySelector(`.${loaderClass}`);

    if (isLoading) {
        buttonElement.disabled = true;
        buttonElement.setAttribute('aria-busy', 'true');
        if (!loader) {
            loader = document.createElement('span');
            loader.className = loaderClass;
            // Try to append loader after icon if present, otherwise at the end
            const icon = buttonElement.querySelector('i');
            if (icon && icon.nextSibling) {
                icon.parentNode.insertBefore(loader, icon.nextSibling);
                loader.style.marginLeft = '5px'; // Add some space after the icon
            } else if (icon) {
                icon.parentNode.appendChild(loader);
                 loader.style.marginLeft = '5px';
            }
             else {
                buttonElement.appendChild(loader);
            }
        }
        // Keep original text visible or adjust as needed
    } else {
        buttonElement.disabled = false;
        buttonElement.removeAttribute('aria-busy');
        if (loader) {
            loader.remove();
        }
         // Restore original text display if it was hidden
    }
}

function showGlobalLoader(show) {
    const loader = document.getElementById('globalLoader');
    if (loader) {
        loader.style.display = show ? 'block' : 'none';
    }
}

function formatElapsedTime(seconds) {
    if (seconds == null || isNaN(seconds)) return 'N/A';
    seconds = Math.floor(seconds); // Ensure integer seconds
    const d = Math.floor(seconds / (3600 * 24));
    const h = Math.floor(seconds % (3600 * 24) / 3600);
    const m = Math.floor(seconds % 3600 / 60);
    const s = Math.floor(seconds % 60);

    let parts = [];
    if (d > 0) parts.push(`${d}d`);
    if (h > 0) parts.push(`${h}h`);
    if (m > 0) parts.push(`${m}m`);
    // Show seconds only if total is less than 1 minute OR if it's the only unit
    if (d === 0 && h === 0 && m === 0) parts.push(`${s}s`);

    return parts.length > 0 ? parts.join(', ') : '0s';
}


function formatTimestamp(isoString) {
    // Assuming timestamp from measurement IS NOT an ISO string, but potentially just "Xh"
    // If it were a proper timestamp (e.g., epoch ms):
    // if (!isoString || isNaN(new Date(isoString).getTime())) return isoString || 'N/A'; // Return original if invalid
    // const date = new Date(isoString);
    // return date.toLocaleString(); // Adjust format as needed
    // For now, just return the string as is, maybe clean it
    return typeof isoString === 'string' ? isoString.trim() : 'N/A';
}

// Updated function signature to accept a custom formatter
function updateElementText(element, value, unit = '', precision = 2, naValue = '-', formatter = null) {
    if (!element) return;
    let text = naValue;

    if (formatter) {
        // Use custom formatter if provided and value is valid for it
        text = formatter(value);
        // Add unit if formatter doesn't include it (or if needed)
        // Example: if (text !== naValue) text += `${unit ? ` ${unit}` : ''}`;
    } else if (value != null && !isNaN(value)) {
        // Default number formatting
        text = `${parseFloat(value).toFixed(precision)}${unit ? ` ${unit}` : ''}`;
    }

    if (element.innerText !== text) {
        element.innerText = text;
        // Add a subtle flash effect on update
        element.classList.add('updated');
        setTimeout(() => element.classList.remove('updated'), 500);
    }
}


// --- Chart Functions ---
function createChart() {
    if (!El.measurementChartCtx) {
        console.error("Chart context not found");
        return null;
    }
    // --- IMPORTANT: Destroy existing chart if it exists ---
    if (measurementChart) {
        console.log("Destroying existing chart instance before creating a new one.");
        measurementChart.destroy();
        measurementChart = null; // Reset the variable
    }
    // --- End of addition ---

    console.log("Creating new chart instance.");
    return new Chart(El.measurementChartCtx, {
        type: 'line',
        data: {
            labels: [], // Timestamps
            datasets: [
                {
                    label: 'Temperature (°C)',
                    data: [],
                    borderColor: 'rgba(255, 99, 132, 1)', // Red
                    backgroundColor: 'rgba(255, 99, 132, 0.2)',
                    yAxisID: 'yTemp',
                    fill: false,
                    tension: 0.1,
                    pointRadius: 3,
                    pointHoverRadius: 5,
                },
                {
                    label: 'Humidity (%)',
                    data: [],
                    borderColor: 'rgba(54, 162, 235, 1)', // Blue
                    backgroundColor: 'rgba(54, 162, 235, 0.2)',
                    yAxisID: 'yHumid',
                    fill: false,
                    tension: 0.1,
                    pointRadius: 3,
                    pointHoverRadius: 5,
                },
                {
                    label: 'Pump Activation',
                    data: [], // Store {x: timestamp, y: temperature_at_activation}
                    borderColor: 'rgba(255, 206, 86, 0)', // Invisible line
                    backgroundColor: 'rgba(255, 206, 86, 1)', // Yellow for points
                    yAxisID: 'yTemp', // Plot against temp axis
                    fill: false,
                    pointStyle: 'triangle',
                    radius: 7, // Larger symbol for activation
                    hoverRadius: 9,
                    showLine: false, // Don't show line, just points
                }
                // Maybe add Threshold line later
            ]
        },
        options: {
            responsive: true,
            maintainAspectRatio: false, // Allow chart to fill container height
            scales: {
                x: {
                    type: 'category', // Assuming timestamps are strings like "1h", "2h"
                    title: { display: true, text: 'Tiempo Transcurrido (aprox)' },
                    ticks: {
                         maxTicksLimit: 10, // Limit number of x-axis labels
                         color: '#adb5bd' // Lighter text for dark mode
                    },
                     grid: { color: 'rgba(255, 255, 255, 0.1)' }
                },
                yTemp: {
                    type: 'linear',
                    position: 'left',
                    title: { display: true, text: 'Temperatura (°C)', color: 'rgba(255, 99, 132, 1)' },
                    ticks: { color: 'rgba(255, 99, 132, 1)' },
                    grid: { color: 'rgba(255, 255, 255, 0.1)' }
                },
                yHumid: {
                    type: 'linear',
                    position: 'right',
                    title: { display: true, text: 'Humedad (%)', color: 'rgba(54, 162, 235, 1)' },
                    min: 0,
                    max: 100,
                    ticks: { color: 'rgba(54, 162, 235, 1)' },
                    grid: { drawOnChartArea: false } // Avoid overlapping grids
                }
            },
            plugins: {
                tooltip: {
                    mode: 'index',
                    intersect: false,
                    callbacks: {
                        label: function(context) {
                            let label = context.dataset.label || '';
                            if (label) {
                                label += ': ';
                            }
                            // Check if measurementChart exists before accessing its data
                            if (!measurementChart) return label; // Prevent errors if chart is destroyed mid-callback

                            if (context.dataset.label === 'Pump Activation') {
                                // Find corresponding temp/humid values for this timestamp
                                const idx = context.dataIndex;
                                // Ensure data exists at this index
                                const tempValue = measurementChart.data.datasets[0]?.data[idx];
                                const humidValue = measurementChart.data.datasets[1]?.data[idx];
                                return `Pump Activated (Temp: ${tempValue != null ? tempValue.toFixed(1) : '-'}°C, Humid: ${humidValue != null ? humidValue.toFixed(1) : '-'}%)`;
                            }
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
                    labels: { color: '#f8f9fa' }, // Legend text color for dark mode
                    onClick: (e, legendItem, legend) => { // Toggle dataset visibility
                        const index = legendItem.datasetIndex;
                        const ci = legend.chart;
                        if (ci.isDatasetVisible(index)) {
                            ci.hide(index);
                            legendItem.hidden = true;
                        } else {
                            ci.show(index);
                            legendItem.hidden = false;
                        }
                    }
                }
            },
            interaction: { // Enable zooming/panning later if needed (requires plugin)
                 mode: 'nearest',
                 axis: 'x',
                 intersect: false
            }
        }
    });
}

function updateChart(measurements) {
    // Create the chart instance ONLY if it doesn't exist
    if (!measurementChart) {
        measurementChart = createChart();
        // If creation still fails (e.g., no context), exit
        if (!measurementChart) {
            console.error("Failed to create chart in updateChart.");
            return;
         }
    }

    // Limit data points for performance/clarity
    const limitedMeasurements = measurements.slice(-CHART_MAX_POINTS);

    const timestamps = limitedMeasurements.map(m => formatTimestamp(m.timestamp));
    const humidity = limitedMeasurements.map(m => m.humidity ?? null); // Use null for gaps
    const temperature = limitedMeasurements.map(m => m.temperature ?? null);

    // Use null for points where pump wasn't activated in the dedicated dataset
    const pumpPointsData = limitedMeasurements.map((m) =>
        m.pumpActivated ? m.temperature ?? null : null
    );

    measurementChart.data.labels = timestamps;
    measurementChart.data.datasets[0].data = temperature; // Temp
    measurementChart.data.datasets[1].data = humidity;  // Humidity
    measurementChart.data.datasets[2].data = pumpPointsData; // Pump Activation Points

    measurementChart.update();
}

// --- Main UI Update Function ---
async function updateUI() {
    console.log("Updating UI...");
    try {
        // Fetch data in parallel
        const [data, measurements] = await Promise.all([
            fetchData('/data'),
            fetchData('/loadMeasurement')
        ]);

        // Update Title
        if (El.pageTitle) El.pageTitle.textContent = `Green Nanny ${data.pumpStatus ? '- Watering' : '- OK'}`;

        // Update Sensor Readings & Derived Values
        updateElementText(El.temp, data.temperature, '°C');
        updateElementText(El.humidity, data.humidity, '%');
        updateElementText(El.vpd, data.vpd, 'Kpa');
        updateElementText(El.humidityThreshold, data.humidityThreshold, '%', 0); // Threshold likely integer
        updateElementText(El.pumpActivationCount, data.pumpActivationCount, '', 0);
        // Pass the custom formatter for elapsed time
        updateElementText(El.totalElapsedTime, data.elapsedTime, '', 0, 'N/A', formatElapsedTime);

        // Update Pump Status Indicator
        if (El.pumpStatusIndicator) {
            const pumpOn = data.pumpStatus ?? false;
            El.pumpStatusIndicator.textContent = pumpOn ? 'ON' : 'OFF';
            El.pumpStatusIndicator.className = `badge bg-${pumpOn ? 'success' : 'secondary'}`;
            El.pumpStatusIndicator.classList.toggle('pulsing', pumpOn);
            // Disable buttons based on status
            if(El.activatePumpBtn) El.activatePumpBtn.disabled = pumpOn;
            if(El.deactivatePumpBtn) El.deactivatePumpBtn.disabled = !pumpOn;
        }

         // Update Last Measurement Time (assuming data endpoint provides it)
        // Assuming data.lastMeasurementTimestamp is epoch milliseconds or a parseable date string
        let lastReadingText = '-';
        if (data.lastMeasurementTimestamp) {
            const date = new Date(data.lastMeasurementTimestamp);
            if (!isNaN(date.getTime())) { // Check if date is valid
                 lastReadingText = date.toLocaleString();
            } else {
                 lastReadingText = 'Invalid Date';
            }
        }
        if(El.lastMeasurementTime) El.lastMeasurementTime.textContent = lastReadingText; // Direct update as it's not a number


        // Update Measurement History Log
        if (El.measurementHistory) {
            if (measurements && Array.isArray(measurements) && measurements.length > 0) {
                 const historyHtml = measurements
                    // Slice to limit displayed history, maybe show most recent first
                    .slice(-50).reverse()
                    .map((m, i) => `
                        <div class="measurement-history-entry" role="listitem">
                            <span class="timestamp">${formatTimestamp(m.timestamp)}:</span>
                            <span class="humidity" title="Humidity"><i class="fas fa-tint text-primary"></i> ${m.humidity != null ? m.humidity.toFixed(1) : '-'}%</span>,
                            <span class="temperature" title="Temperature"><i class="fas fa-thermometer-half text-danger"></i> ${m.temperature != null ? m.temperature.toFixed(1) : '-'}°C</span>
                            ${m.pumpActivated ? '<span class="pump-info" title="Pump Activated"><i class="fas fa-tint-slash text-warning"></i> Pump On</span>' : ''}
                            ${m.stage ? `<span class="stage-info" title="Plant Stage"><i class="fas fa-leaf text-success"></i> ${m.stage}</span>` : ''}
                        </div>`).join('');
                 El.measurementHistory.innerHTML = historyHtml;
            } else {
                 El.measurementHistory.innerHTML = `<div class='measurement-history-entry text-muted'>No measurement data found.</div>`;
            }
        }

        // Update Chart
        if (El.measurementChartCtx && Array.isArray(measurements)) {
            updateChart(measurements);
        }

        // Update Current Stage Info (if not already handled by its own function)
         await updateCurrentStageLabel(); // Ensure stage info is also fresh

    } catch (error) {
        console.error('Error updating UI:', error);
        showToast('Failed to update dashboard data.', 'error');
        // Optionally update elements to show error state
        updateElementText(El.temp, null, '°C', 2, 'Error');
        updateElementText(El.humidity, null, '%', 2, 'Error');
        // ... other elements
    } finally {
        // Schedule next refresh
        clearTimeout(refreshTimeoutId); // Clear previous timeout if any
        refreshTimeoutId = setTimeout(updateUI, REFRESH_INTERVAL);
    }
}

// Function to update the current stage label and parameters
async function updateCurrentStageLabel() {
    const labelEl = El.currentStageLabel;
    const paramsEl = El.currentStageParams;
    const manualIndicator = document.getElementById('manualControlIndicator');
    if (!labelEl) return;

    try {
        // Assuming /getCurrentStage provides { currentStage, stageIndex, manualControl, params: { threshold, watering } }
        const stageData = await fetchData('/getCurrentStage');
        if (stageData && stageData.currentStage) {
            labelEl.textContent = `${stageData.currentStage} (Index ${stageData.stageIndex})`;
            labelEl.classList.remove('text-muted', 'text-danger');
            // Display parameters for the current stage
            if (paramsEl && stageData.params) {
                 paramsEl.textContent = `Threshold: ${stageData.params.threshold ?? '-'}%, Watering: ${stageData.params.watering ?? '-'}s`;
                 paramsEl.style.display = 'block';
            } else if (paramsEl) {
                 paramsEl.style.display = 'none';
            }
            // Indicate manual control
            if (manualIndicator) {
                 manualIndicator.style.display = stageData.manualControl ? 'inline-block' : 'none';
            }
        } else {
            labelEl.textContent = '-';
            labelEl.classList.add('text-muted');
            labelEl.classList.remove('text-danger');
            if (paramsEl) paramsEl.style.display = 'none';
            if (manualIndicator) manualIndicator.style.display = 'none';
        }
    } catch (error) {
        labelEl.textContent = 'Error loading stage';
        labelEl.classList.add('text-danger');
        labelEl.classList.remove('text-muted');
        if (paramsEl) paramsEl.style.display = 'none';
        if (manualIndicator) manualIndicator.style.display = 'none';
         // Error already logged by fetchData
    }
}

// Example: Function to populate stage selector (call during init)
// Make sure this is defined before it's called in DOMContentLoaded
async function populateStageSelector() {
    if (!El.manualStageSelect) return;
    // Show loading state in selector
    El.manualStageSelect.innerHTML = `<option value="" disabled selected>Loading stages...</option>`;
    try {
        // Assuming /listStages returns an array: [{ index, name, ... }, ...]
        const stages = await fetchData('/listStages');
        if (stages && Array.isArray(stages) && stages.length > 0) {
            El.manualStageSelect.innerHTML = stages.map(stage =>
                `<option value="${stage.index}">${stage.name} (Index ${stage.index})</option>`
            ).join('');
        } else {
             El.manualStageSelect.innerHTML = `<option value="" disabled selected>No stages found</option>`;
        }
    } catch (error) {
        console.error("Failed to load stages for selector:", error);
        // Optionally show an error in the UI near the selector
         El.manualStageSelect.innerHTML = `<option value="" disabled selected>Error loading stages</option>`;
    }
}


// --- Event Listener Setup ---
function setupEventListeners() {
    // --- Forms ---
    El.humidityForm?.addEventListener('submit', async (e) => {
        e.preventDefault();
        const value = El.umbralInput?.value;
        if (value === '' || isNaN(value) || value < 0 || value > 100) {
             showToast('Please enter a valid humidity threshold (0-100).', 'warning');
             El.umbralInput?.classList.add('is-invalid');
             return;
        }
         El.umbralInput?.classList.remove('is-invalid');
        const button = El.humidityForm.querySelector('button[type="submit"]');
        setLoadingState(button, true);
        try {
            await postData('/threshold', { umbral: parseInt(value) });
            showToast(`Humidity threshold set to ${value}%`, 'success');
             // Optionally clear input or update display immediately
            // El.umbralInput.value = '';
            await updateUI(); // Refresh UI to show new threshold potentially reflected in /data
        } catch (error) {
            // Error toast already shown by postData
        } finally {
            setLoadingState(button, false);
        }
    });

    // Add real-time validation feedback
     El.umbralInput?.addEventListener('input', () => {
         const value = El.umbralInput.value;
         if (value === '' || isNaN(value) || value < 0 || value > 100) {
             El.umbralInput.classList.add('is-invalid');
         } else {
             El.umbralInput.classList.remove('is-invalid');
         }
     });

    El.manualStageForm?.addEventListener('submit', async (e) => {
        e.preventDefault();
        const value = parseInt(El.manualStageSelect?.value);
        if (isNaN(value)) {
            showToast('Invalid stage selected.', 'warning');
            return;
        }
        const button = El.manualStageForm.querySelector('button[type="submit"]');
        setLoadingState(button, true);
        try {
            await postData('/setManualStage', { stage: value });
            showToast('Manual stage set successfully.', 'success');
            await updateCurrentStageLabel(); // Refresh stage display
            await updateUI(); // Refresh all data potentially affected by stage change
        } catch (error) {
            // Error toast already shown by postData
        } finally {
            setLoadingState(button, false);
        }
    });

    // --- Buttons ---
    El.activatePumpBtn?.addEventListener('click', async () => {
        const durationStr = prompt('Enter pump activation duration in seconds (e.g., 30):', '30');
        const duration = parseInt(durationStr);
        if (isNaN(duration) || duration <= 0) {
            showToast('Invalid duration entered. Please enter a positive number.', 'warning');
            return;
        }
        setLoadingState(El.activatePumpBtn, true);
        try {
            const response = await postData('/controlPump', { action: 'on', duration: duration });
             // Check response type and content
            if (typeof response === 'object' && response.status === 'pump_on') {
                showToast(`Pump activated for ${duration} seconds.`, 'success');
                 await updateUI(); // Refresh UI to show pump status
                 // Start visual timer if possible
            } else if (typeof response === 'string' && response.includes('pump_on')) { // Fallback for text response
                 showToast(`Pump activated for ${duration} seconds.`, 'success');
                 await updateUI();
            }
             else {
                 showToast('Failed to activate pump (device response).', 'warning');
                 console.warn('Unexpected response from /controlPump (on):', response);
            }
        } catch (error) {
            // Error toast shown by postData
        } finally {
            // setLoadingState might be handled by the next updateUI, but ensure it's off if pump is off
            // We might need to call updateUI again here or rely on the interval refresh
             setLoadingState(El.activatePumpBtn, false); // Ensure button is re-enabled
        }
    });

    El.deactivatePumpBtn?.addEventListener('click', async () => {
        if (!confirm('Are you sure you want to manually deactivate the pump?')) return;
        setLoadingState(El.deactivatePumpBtn, true);
        try {
            const response = await postData('/controlPump', { action: 'off' });
            if (typeof response === 'object' && response.status === 'pump_off') {
                showToast('Pump deactivated successfully.', 'success');
                 await updateUI(); // Refresh UI to show pump status
            } else if (typeof response === 'string' && response.includes('pump_off')) { // Fallback
                 showToast('Pump deactivated successfully.', 'success');
                 await updateUI();
            } else {
                showToast('Failed to deactivate pump (device response).', 'warning');
                 console.warn('Unexpected response from /controlPump (off):', response);
            }
        } catch (error) {
            // Error toast shown by postData
        } finally {
            setLoadingState(El.deactivatePumpBtn, false); // Ensure button is re-enabled
        }
    });

    El.takeMeasurementBtn?.addEventListener('click', async () => {
        setLoadingState(El.takeMeasurementBtn, true);
        try {
            // Assuming /takeMeasurement is POST and might return success/failure or trigger data update
            await postData('/takeMeasurement', {}, 'POST'); // Use POST as defined in C++
            showToast('Manual measurement triggered.', 'info');
            // Give device a moment to process before fetching new data
            await new Promise(resolve => setTimeout(resolve, 1000));
            await updateUI(); // Refresh UI immediately to fetch potentially new data
        } catch (error) {
             // Error toast shown by postData
        } finally {
            setLoadingState(El.takeMeasurementBtn, false);
        }
    });

    El.clearMeasurementsBtn?.addEventListener('click', async () => {
        if (!confirm('Are you sure you want to delete ALL measurement history? This cannot be undone.')) return;
        setLoadingState(El.clearMeasurementsBtn, true);
        try {
            await postData('/clearHistory', {}, 'POST'); // Use POST
            showToast('Measurement history cleared.', 'success');
            await updateUI(); // Refresh UI (history and chart should be empty)
        } catch (error) {
            // Error toast shown by postData
        } finally {
            setLoadingState(El.clearMeasurementsBtn, false);
        }
    });

    El.restartSystemBtn?.addEventListener('click', async () => {
        if (!confirm('Are you sure you want to restart the Green Nanny device? It will be unavailable for a moment.')) return;
        setLoadingState(El.restartSystemBtn, true);
        showToast('Restarting system...', 'warning');
        try {
            // Don't wait for response, system will likely disconnect
             postData('/restartSystem', {}, 'POST'); // Use POST
             // Maybe disable UI elements or show overlay?
             document.body.classList.add('system-restarting'); // Add a class for overlay/disabling
             setTimeout(() => {
                 showToast('System should be restarting. Please wait and refresh the page manually in a minute.', 'info');
                 // Don't re-enable the button automatically
             }, 5000); // 5 seconds delay
        } catch (error) {
            // This might not be caught if the server shuts down immediately
            showToast('Failed to send restart command. You may need to restart manually.', 'error');
            setLoadingState(El.restartSystemBtn, false);
             document.body.classList.remove('system-restarting');
        }
        // No finally here for the button state as the page context might be lost
    });

    El.refreshStageBtn?.addEventListener('click', async () => {
         setLoadingState(El.refreshStageBtn, true);
         try {
             await updateCurrentStageLabel();
              showToast('Stage information refreshed.', 'info');
         } catch (error) {
              // Error toast shown by postData inside updateCurrentStageLabel->fetchData
              // showToast('Failed to refresh stage info.', 'error'); // Avoid double toast
         } finally {
             setLoadingState(El.refreshStageBtn, false);
         }
    });

    El.resetManualStageBtn?.addEventListener('click', async () => {
        if (!confirm('Are you sure you want to disable manual stage control and return to automatic mode?')) return;
        setLoadingState(El.resetManualStageBtn, true);
        try {
            await postData('/resetManualStage', {}, 'POST'); // Use POST
            showToast('Manual stage control reset to automatic.', 'success');
            await updateCurrentStageLabel(); // Refresh stage display
            await updateUI(); // Refresh all data
        } catch (error) {
             // Error toast shown by postData
        } finally {
            setLoadingState(El.resetManualStageBtn, false);
        }
    });

    // Initialize tooltips (assuming Bootstrap 5 JS is loaded)
    if (typeof bootstrap !== 'undefined' && bootstrap.Tooltip) {
        const tooltipTriggerList = [].slice.call(document.querySelectorAll('[data-bs-toggle="tooltip"]'));
        tooltipTriggerList.map(function (tooltipTriggerEl) {
            return new bootstrap.Tooltip(tooltipTriggerEl);
        });
    } else {
        console.warn("Bootstrap Tooltip component not found. Tooltips will not work.");
    }
}


// --- Initialization ---
document.addEventListener('DOMContentLoaded', async () => {
    console.log("DOM loaded, initializing Green Nanny UI...");
    showGlobalLoader(true); // Show loader during initial setup

    // Check if critical elements exist
    if (!El.measurementChartCtx) {
         console.warn("Chart canvas not found. Chart will be disabled.");
    }

    setupEventListeners(); // Setup listeners first
    await populateStageSelector(); // Then populate dynamic elements like the selector

    try {
        await updateUI(); // Perform initial data load and UI population
        // updateUI now handles the initial chart creation via updateChart

    } catch (error) {
        console.error("Initialization failed:", error);
        showToast("Failed to initialize dashboard. Please check connection.", "error");
        // Display a more prominent error message?
        const mainContent = document.querySelector('.content-wrapper');
        if(mainContent) {
             // Avoid wiping out content if only some data failed
            mainContent.insertAdjacentHTML('afterbegin', `<div class="alert alert-danger m-3"><strong>Initialization Error:</strong> Could not load initial data. Please check the device connection and refresh the page. Some features may be unavailable.</div>`);
        }
    } finally {
         showGlobalLoader(false); // Hide loader after initial load attempt
    }

    // Chart creation is now handled within the first call to updateChart() inside updateUI()
    // No need for a separate createChart() call here.

    console.log("Initialization complete.");
});