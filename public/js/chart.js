export function updateChart(measurements) {
    const ctx = document.getElementById('measurementChart').getContext('2d');

    // Ensure timestamps are valid decimal values
    const timestamps = measurements.map(m => parseFloat(m.timestamp)).filter(value => !isNaN(value));
    const humidityValues = measurements.map(m => m.humidity);
    const temperatureValues = measurements.map(m => m.temperature);
    const pumpActivationValues = measurements.map(m => m.pumpActivated ? m.temperature : null); // or humidity if preferred

    if (timestamps.length === 0) {
        console.warn("No valid timestamps found for chart data.");
        return;
    }

    // Calculate the range of timestamps
    const minTimestamp = Math.min(...timestamps);
    const maxTimestamp = Math.max(...timestamps);
    const timeRange = maxTimestamp - minTimestamp;

    // Determine appropriate step size
    let unit = 'minute';
    if (timeRange > 60) { // More than 60 minutes
        unit = 'hour';
    }
    if (timeRange > 1440) { // More than 24 hours (1440 minutes)
        unit = 'day';
    }
    if (timeRange > 10080) { // More than 1 week (10080 minutes)
        unit = 'week';
    }
    if (timeRange > 43200) { // More than 1 month (43200 minutes)
        unit = 'month';
    }

    new Chart(ctx, {
        type: 'line',
        data: {
            labels: timestamps,
            datasets: [
                {
                    label: 'Temperature (°C)',
                    data: temperatureValues,
                    borderColor: 'rgba(255, 99, 132, 1)',
                    borderWidth: 1,
                    fill: false
                },
                {
                    label: 'Humidity (%)',
                    data: humidityValues,
                    borderColor: 'rgba(54, 162, 235, 1)',
                    borderWidth: 1,
                    fill: false
                },
                {
                    label: 'Pump Activation',
                    data: pumpActivationValues,
                    borderColor: 'rgba(255, 206, 86, 1)',
                    pointBackgroundColor: 'rgba(255, 206, 86, 1)',
                    pointBorderColor: 'rgba(255, 206, 86, 1)',
                    pointRadius: 5,
                    fill: false,
                    showLine: false
                }
            ]
        },
        options: {
            scales: {
                x: {
                    type: 'linear',
                    position: 'bottom',
                    title: {
                        display: true,
                        text: 'Timestamp'
                    }
                },
                y: {
                    title: {
                        display: true,
                        text: 'Value'
                    }
                }
            }
        }
    });
    
}
