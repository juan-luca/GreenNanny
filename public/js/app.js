import { fetchData, postData, clearMeasurementHistory } from './api.js';
import { updateUI, setupEventListeners } from './ui.js';

document.addEventListener("DOMContentLoaded", function() {
    setupEventListeners();
    updateUI("/data", "/loadMeasurement");
});

document.getElementById('humidityForm').addEventListener('submit', async (event) => {
    event.preventDefault();
    const umbral = document.getElementById('umbral').value;
    try {
        await postData('/threshold', { umbral });
        alert('Umbral de humedad actualizado con éxito');
    } catch (error) {
        console.error('Error updating humidity threshold: ', error);
    }
});

document.getElementById('clearMeasurements').addEventListener('click', async () => {
    try {
        await clearMeasurementHistory();
        alert('Historial de mediciones borrado con éxito');
        const data = await fetchData('/data');
        updateUI(data, '/loadMeasurement');
    } catch (error) {
        console.error('Error clearing measurement history: ', error);
    }
});
