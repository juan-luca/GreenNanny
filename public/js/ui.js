import { fetchData, postData } from './api.js';
import { updateChart } from './chart.js';

export async function updateUI(dataUrl, measurementUrl) {
    const temperatureDisplay = document.getElementById('temperature');
    const humidityDisplay = document.getElementById('humidity');
    const humidityThresholdDisplay = document.getElementById('humidityThreshold');
    const pumpActivationCountDisplay = document.getElementById('pumpActivationCount');
    const vpdDisplay = document.getElementById('vpd');
    const totalElapsedTimeDisplay = document.getElementById('totalElapsedTime');
    const measurementHistoryDisplay = document.getElementById('measurementHistory');

    try {
        const data = await fetchData(dataUrl);
        const measurements = await fetchData(measurementUrl);

        if (temperatureDisplay) temperatureDisplay.innerText = data.temperature ? `${data.temperature.toFixed(2)} °C` : "N/A";
        if (humidityDisplay) humidityDisplay.innerText = data.humidity ? `${data.humidity.toFixed(2)} %` : "N/A";
        if (humidityThresholdDisplay) humidityThresholdDisplay.innerText = data.humidityThreshold ? `${data.humidityThreshold} %` : "N/A";
        if (pumpActivationCountDisplay) pumpActivationCountDisplay.innerText = data.pumpActivationCount != null ? `${data.pumpActivationCount}` : "N/A";
        if (vpdDisplay) vpdDisplay.innerText = data.vpd != null ? `${data.vpd.toFixed(2)} Kpa` : "N/A";
        if (totalElapsedTimeDisplay) totalElapsedTimeDisplay.innerText = data.totalElapsedTime ? `${data.totalElapsedTime.toFixed(2)} Horas` : "N/A";

        if (measurementHistoryDisplay) {
            let history = measurements && measurements.length > 0
                ? measurements.map((m, index) => 
                    `<div class="measurement-history-entry">
                        ${index + 1}. Humidity: ${m.humidity}%, Temperature: ${m.temperature}°C, Time: ${m.timestamp}, Pump Activated: ${m.pumpActivated ? 'Yes' : 'No'}
                    </div>`
                  ).join('')
                : "<div class='measurement-history-entry'>No measurement data found.</div>";
            measurementHistoryDisplay.innerHTML = history;
        }

        if (measurements) {
            updateChart(measurements);
        }
    } catch (error) {
        console.error("Error updating UI: ", error);
    }
}



export function setupEventListeners() {
    const humidityForm = document.getElementById('humidityForm');
    const activatePumpButton = document.getElementById('activatePump');
    const takeMeasurementButton = document.getElementById('takeMeasurement');
    const clearMeasurementsButton = document.getElementById('clearMeasurements');
    const restartSystemButton = document.getElementById('restartSystem');

    humidityForm.addEventListener('submit', event => {
        event.preventDefault();
        const threshold = document.getElementById('umbral').value;
        postData('/threshold', { umbral: threshold });
    });

    activatePumpButton.addEventListener('click', () => {
        postData('/activatePump');
    });

    takeMeasurementButton.addEventListener('click', () => {
        postData('/takeMeasurement');
    });

    clearMeasurementsButton.addEventListener('click', () => {
        postData('/clearMeasurements');
    });

    restartSystemButton.addEventListener('click', () => {
        postData('/restartSystem');
    });
}
