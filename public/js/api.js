import { initializeApp } from 'firebase/app';
import { getDatabase, ref, get, set, push, remove } from 'firebase/database';
import firebaseConfig from './firebase-config.js';

const app = initializeApp(firebaseConfig);
const db = getDatabase(app);

const BASE_URL = 'http://192.168.0.78'; // Ajusta esta URL si es necesario

export async function fetchData(endpoint) {
    try {
        const response = await fetch(`${BASE_URL}${endpoint}`);
        if (!response.ok) {
            throw new Error(`HTTP error! status: ${response.status}`);
        }
        return await response.json();
    } catch (error) {
        console.error('Error fetching data: ', error);
        throw error;
    }
}

export async function postData(endpoint, data) {
    try {
        const response = await fetch(`${BASE_URL}${endpoint}`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify(data)
        });
        if (!response.ok) {
            throw new Error(`HTTP error! status: ${response.status}`);
        }
        return await response.json();
    } catch (error) {
        console.error('Error posting data: ', error);
        throw error;
    }
}

export async function clearMeasurementHistory() {
    try {
        const response = await fetch(`${BASE_URL}/clearHistory`, {
            method: 'POST'
        });
        if (!response.ok) {
            throw new Error(`HTTP error! status: ${response.status}`);
        }
        return await response.json();
    } catch (error) {
        console.error('Error clearing history: ', error);
        throw error;
    }
}
