// js/app.js completo
// ==================

// BASE URL para el ESP8266
const BASE_URL = 'http://192.168.0.78';

// Funciones de API
async function fetchData(endpoint) {
  try {
    const res = await fetch(`${BASE_URL}${endpoint}`);
    if (!res.ok) throw new Error(`HTTP error! status: ${res.status}`);
    return await res.json();
  } catch (e) {
    console.error('Error fetching data:', e);
    throw e;
  }
}

async function postData(endpoint, data) {
  try {
    const res = await fetch(`${BASE_URL}${endpoint}`, {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify(data)
    });
    if (!res.ok) throw new Error(`HTTP error! status: ${res.status}`);
    return await res.json();
  } catch (e) {
    console.error('Error posting data:', e);
    throw e;
  }
}

async function clearMeasurementHistory() {
  try {
    const res = await fetch(`${BASE_URL}/clearHistory`, { method: 'POST' });
    if (!res.ok) throw new Error(`HTTP error! status: ${res.status}`);
    return await res.json();
  } catch (e) {
    console.error('Error clearing measurement history:', e);
    throw e;
  }
}

// Función para actualizar la gráfica
function updateChart(measurements) {
  const ctx = document.getElementById('measurementChart').getContext('2d');
  const timestamps = measurements.map(m => parseFloat(m.timestamp)).filter(v => !isNaN(v));
  const humidity = measurements.map(m => m.humidity);
  const temperature = measurements.map(m => m.temperature);
  const pumpVals = measurements.map(m => m.pumpActivated ? m.temperature : null);

  if (!timestamps.length) {
    console.warn('No valid timestamps');
    return;
  }

  new Chart(ctx, {
    type: 'line',
    data: {
      labels: timestamps,
      datasets: [
        { label: 'Temperature (°C)', data: temperature, borderColor: 'rgba(255,99,132,1)', fill: false },
        { label: 'Humidity (%)', data: humidity, borderColor: 'rgba(54,162,235,1)', fill: false },
        { label: 'Pump Activation', data: pumpVals, borderColor: 'rgba(255,206,86,1)', fill: false }
      ]
    }
  });
}

// Función para actualizar la UI
async function updateUI(dataUrl, measurementUrl) {
  const tEl = document.getElementById('temperature');
  const hEl = document.getElementById('humidity');
  const thEl = document.getElementById('humidityThreshold');
  const pacEl = document.getElementById('pumpActivationCount');
  const vpdEl = document.getElementById('vpd');
  const tetEl = document.getElementById('totalElapsedTime');
  const histEl = document.getElementById('measurementHistory');

  try {
    const data = await fetchData(dataUrl);
    const meas = await fetchData(measurementUrl);

    tEl.innerText = data.temperature != null ? `${data.temperature.toFixed(2)} °C` : 'N/A';
    hEl.innerText = data.humidity != null ? `${data.humidity.toFixed(2)} %` : 'N/A';
    thEl.innerText = data.humidityThreshold != null ? `${data.humidityThreshold} %` : 'N/A';
    pacEl.innerText = data.pumpActivationCount != null ? `${data.pumpActivationCount}` : 'N/A';
    vpdEl.innerText = data.vpd != null ? `${data.vpd.toFixed(2)} Kpa` : 'N/A';
    tetEl.innerText = data.totalElapsedTime != null ? `${parseFloat(data.totalElapsedTime).toFixed(2)} Horas` : 'N/A';

    if (histEl) {
      histEl.innerHTML = meas && meas.length
        ? meas.map((m,i) => `<div class="measurement-history-entry">${i+1}. Humidity: ${m.humidity}%, Temp: ${m.temperature}°C, Time: ${m.timestamp}, Pump: ${m.pumpActivated?'Yes':'No'}</div>`).join('')
        : "<div class='measurement-history-entry'>No measurement data found.</div>";
    }

    if (meas) updateChart(meas);
  } catch (e) {
    console.error('Error updating UI:', e);
  }
}

// Setup event listeners de UI.js
function setupEventListeners() {
  const form = document.getElementById('humidityForm');
  const btnPumpOn = document.getElementById('activatePump');
  const btnMeasure = document.getElementById('takeMeasurement');
  const btnClear = document.getElementById('clearMeasurements');
  const btnRestart = document.getElementById('restartSystem');

  form.addEventListener('submit', e => {
    e.preventDefault();
    const val = document.getElementById('umbral').value;
    postData('/threshold', { umbral: val });
  });

  btnPumpOn.addEventListener('click', () => {
    postData('/controlPump', { action: 'on', duration: 30 });
  });

  btnMeasure.addEventListener('click', () => {
    postData('/takeMeasurement');
  });

  btnClear.addEventListener('click', () => {
    postData('/clearHistory');
  });

  btnRestart.addEventListener('click', () => {
    postData('/restartSystem');
  });
}

// Función para actualizar la etiqueta de etapa actual
async function updateCurrentStageLabel() {
  const label = document.getElementById('currentStageLabel');
  try {
    const d = await fetchData('/getCurrentStage');
    if (d && d.currentStage) label.textContent = `${d.currentStage} (índice ${d.stageIndex})`;
    else label.textContent = '-';
  } catch {
    label.textContent = 'Error';
  }
}

// DOMContentLoaded: inicializar todo
document.addEventListener('DOMContentLoaded', async () => {
  setupEventListeners();
  await updateUI('/data', '/loadMeasurement');
  await updateCurrentStageLabel();

  // Lógica de control manual de etapas
  document.getElementById('refreshStage').addEventListener('click', updateCurrentStageLabel);

  document.getElementById('manualStageForm').addEventListener('submit', async e => {
    e.preventDefault();
    const v = parseInt(document.getElementById('manualStageSelect').value);
    try {
      await postData('/setManualStage', { stage: v });
      alert('Etapa manual establecida correctamente.');
      await updateCurrentStageLabel();
    } catch {
      alert('Ocurrió un error al establecer la etapa manual.');
    }
  });

  document.getElementById('resetManualStageButton').addEventListener('click', async () => {
    if (!confirm('¿Estás seguro de resetear el control manual de etapas?')) return;
    try {
      await fetch(`${location.origin}/resetManualStage`, { method: 'POST' });
      alert('Control manual de etapa desactivado. Se vuelve a modo automático.');
      await updateCurrentStageLabel();
    } catch {
      alert('No se pudo resetear el control manual de etapa.');
    }
  });

  // Lógica avanzada para activar/apagar bomba
  document.getElementById('activatePump').addEventListener('click', async () => {
    const btn = document.getElementById('activatePump');
    const dur = parseInt(prompt('Ingrese la duración en segundos para activar la bomba:', '30'));
    if (isNaN(dur) || dur <= 0) { alert('Ingrese una duración válida.'); return; }
    btn.disabled = true;
    const loader = document.createElement('span'); loader.classList.add('loader');
    btn.appendChild(loader);
    try {
      const r = await postData('/controlPump', { action: 'on', duration: dur });
      alert(r.status==='pump_on'?`Bomba activada por ${dur} segundos.`:'No se pudo activar la bomba.');
    } catch {
      alert('Error al activar la bomba.');
    } finally {
      btn.disabled = false;
      btn.removeChild(loader);
    }
  });

  document.getElementById('deactivatePump').addEventListener('click', async () => {
    if (!confirm('¿Estás seguro de apagar la bomba?')) return;
    const btn = document.getElementById('deactivatePump');
    btn.disabled = true;
    const loader = document.createElement('span'); loader.classList.add('loader');
    btn.appendChild(loader);
    try {
      const r = await postData('/controlPump', { action: 'off' });
      alert(r.status==='pump_off'?'Bomba apagada correctamente.':'No se pudo apagar la bomba.');
    } catch {
      alert('Error al apagar la bomba.');
    } finally {
      btn.disabled = false;
      btn.removeChild(loader);
    }
  });
});
