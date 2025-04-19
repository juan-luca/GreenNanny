#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <DHT.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <RTClib.h>


// Definiciones de pines y tipos
#define DHTPIN D2
#define DHTTYPE DHT11
#define BOMBA_PIN D3

#define MAX_JSON_OBJECTS 500 // Max measurements to store

// RTC
RTC_DS1307 rtc;
// Configuración de DNS y Captive Portal
DNSServer dnsServer;
const byte DNS_PORT = 53;

// Servidor web en el puerto 80
ESP8266WebServer server(80);

// Almacenamiento de mediciones
String measurements[MAX_JSON_OBJECTS];
int jsonIndex = 0;

// Configuración de red estática (si se usan credenciales guardadas)
IPAddress ip(192, 168, 0, 75);
IPAddress gateway(192, 168, 0, 1);
IPAddress subnet(255, 255, 255, 0);

// Variables de tiempo
unsigned long startTime = 0;
unsigned long lastDebugPrint = 0;

// Simulación de sensores (poner en false para usar el sensor real)
bool simulateSensors = false; // CAMBIAR A false PARA USO REAL
float simulatedHumidity = 55.0;
float simulatedTemperature = 25.0;

// Inicialización del sensor DHT
DHT dht(DHTPIN, DHTTYPE);

// Control de la bomba
int pumpActivationCount = 0;
unsigned long pumpOnTime = 0;      // Timestamp when pump was turned on (for auto-off)
unsigned long pumpDurationMs = 0;  // How long the pump should stay on (set by activatePump)
bool pumpAutoOff = false;          // Flag: Is the pump expected to turn off automatically?
bool pumpActivated = false;        // Flag: Is the pump currently ON?
unsigned long lastSecondPrint = 0; // For printing debug seconds while pump is on
int pumpSecondsCount = 0;          // Counter for debug seconds

// Intervalo de mediciones (en horas)
int measurementInterval = 3; // Default 3 hours
unsigned long nextMeasureTimestamp = 0; // Timestamp for next measurement check

// Definición de etapas fenológicas
struct Stage {
    const char* name;
    int duration_days;     // Approx. days the stage lasts
    int humidityThreshold; // Minimum humidity threshold (%) for this stage
    int wateringTimeSec;   // Default watering time in seconds for this stage
};

// Array de Etapas
Stage stages[] = {
    {"Germinacion", 7, 65, 15},   // Higher humidity needed initially
    {"Vegetativo", 14, 60, 25},
    {"Prefloracion", 7, 55, 35},
    {"Floracion", 30, 50, 35},    // Lower humidity often preferred
    {"Maduracion", 10, 45, 20}
};
const int numStages = sizeof(stages) / sizeof(stages[0]);

// Variables para control manual de etapas
bool manualStageControl = false;
int manualStageIndex = 0; // Index in the stages array
uint32_t rtcBootEpoch = 0;
// Variables de estado
unsigned long lastMeasurementTimestamp = 0; // Track last successful measurement time

// --- Prototipos de Funciones ---
// Core Logic & Setup
void setup();
void loop();
void setupDHTSensor();
void setupBomba();
void setupServer();
void startAPMode();
// Network & Config
void loadWifiCredentials();
void handleConnectWifi(); // Manual connection attempt via UI
void handleSaveWifiCredentials(); // Save credentials from UI
void handleWifiListRequest(); // Scan and list networks
// Time & Measurement
void loadMeasurementInterval();
void saveMeasurementInterval(int interval);
void handleMeasurementInterval(); // Set/Get interval via API
void takeMeasurement(); // Triggered by controlIndependiente or manually
// Pump Control
void activatePump(unsigned long durationMs);
void deactivatePump();
void handlePumpControl(); // Manual pump control via API
// Stage Control
int getCurrentStageIndex(unsigned long daysElapsed);
void loadManualStage();
void saveManualStage(int index);
void handleSetManualStage();
void handleGetCurrentStage();
void handleResetManualStage();
void handleListStages();
// Data Handling
String loadMeasurements();
void saveMeasurement(const String& jsonString);
void saveMeasurementFile(const String& allMeasurementsString);
int parseData(String input, String output[]); // Parses stored measurements string
String arrayToString(String array[], size_t arraySize); // Converts array back to string for saving
void formatMeasurementsToString(String& formattedString); // Formats for JSON array response
void handleData(); // Main data endpoint
void handleLoadMeasurement(); // Endpoint to get all stored measurements
void handleClearMeasurementHistory(); // Endpoint to clear measurements
// Utilities
float getHumidity();
float getTemperature();
float calculateVPD(float temperature, float humidity);
void handleSerialCommands(); // Optional serial control
void controlIndependiente(); // Main automatic control logic


// --- IMPLEMENTACIONES ---

void setup() {
    Serial.begin(115200);
    while (!Serial) {
        ; // wait for serial port to connect. Needed for native USB
    }
    Serial.println("\n\n[SETUP] Iniciando Green Nanny v1.1...");

    // Montar el sistema de archivos
    if (!LittleFS.begin()) {
        Serial.println("[ERROR] Falló al montar LittleFS. Verifica formato.");
        // No continuar sin FS
        while (true) { delay(1000); }
    } else {
        Serial.println("[INFO] LittleFS montado correctamente.");
        // List directory content for debugging
        #ifdef ESP8266
          Dir dir = LittleFS.openDir("/");
          while (dir.next()) {
            Serial.print("  FS File: "); Serial.print(dir.fileName());
            File f = dir.openFile("r");
            Serial.print(", Size: "); Serial.println(f.size());
            f.close();
          }
        #endif
    }
// Iniciar I2C y RTC
Wire.begin(D6 /* SDA */, D5 /* SCL */);
if (!rtc.begin()) {
    Serial.println("[ERROR] No se encontró el módulo RTC DS1307.");
} else {
    if (!rtc.isrunning()) {
        Serial.println("[WARN] RTC no está corriendo, ajustando con fecha/hora de compilación.");
        rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }
    Serial.println("[INFO] Módulo RTC DS1307 inicializado correctamente.");
}
DateTime now = rtc.now();
char buf[20];
// Formato YYYY-MM-DD HH:MM:SS
sprintf(buf, "%04u-%02u-%02u %02u:%02u:%02u",
        now.year(), now.month(), now.day(),
        now.hour(), now.minute(), now.second());
Serial.println(buf);

rtcBootEpoch = rtc.now().unixtime();   // ← guardo epoch en segundos
startTime    = millis();              // mantengo tu millisecond counter para timers

    // Configurar el pin de la bomba
    setupBomba();

    // Inicializar el sensor DHT
    setupDHTSensor();

    // Tiempo de inicio
    startTime = millis();
    lastMeasurementTimestamp = startTime; // Initialize last measurement time
    Serial.print("[INFO] Hora de inicio del sistema (millis): "); Serial.println(startTime);


    // Cargar mediciones previas
    jsonIndex = parseData(loadMeasurements(), measurements);
    Serial.print("[INFO] Cargadas "); Serial.print(jsonIndex); Serial.println(" mediciones previas.");

    // Cargar intervalo de medición
    loadMeasurementInterval();
    // Calcular la primera hora de medición programada (basado en el intervalo)
    // We'll calculate nextMeasureTimestamp inside loop based on interval initially
    nextMeasureTimestamp = startTime + (measurementInterval * 3600000UL); // First measure after interval from start
    Serial.print("[INFO] Próxima medición programada inicialmente alrededor de millis: "); Serial.println(nextMeasureTimestamp);


    // Cargar etapa manual si existe
    loadManualStage();

    // Cargar credenciales WiFi y conectar si existen
    loadWifiCredentials();

    // Iniciar modo AP si no está conectado a WiFi después de intentar con credenciales guardadas
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[INFO] No conectado a WiFi después de intentar credenciales. Iniciando modo AP.");
        startAPMode();
    } else {
         Serial.println("[INFO] Conectado a WiFi.");
         Serial.print("[INFO] IP Address: "); Serial.println(WiFi.localIP());
         Serial.print("[INFO] RSSI: "); Serial.print(WiFi.RSSI()); Serial.println(" dBm");
         dnsServer.stop(); // Stop DNS server if connected to WiFi
    }

    // Configurar el servidor web y endpoints
    setupServer();

    Serial.println("[INFO] === Setup completo. Sistema listo. ===");
}

void loop() {
    unsigned long now = millis();

    // Manejar clientes del servidor web
    server.handleClient();

    // Procesar solicitudes DNS (solo si está en modo AP)
    if (WiFi.getMode() == WIFI_AP) {
      dnsServer.processNextRequest();
    }

    // Manejar comandos seriales para control manual (si está habilitado)
    handleSerialCommands();

    // Apagar la bomba automáticamente si se cumplió el tiempo (autoOff)
    if (pumpAutoOff && pumpActivated && (now - pumpOnTime >= pumpDurationMs)) {
        Serial.println("[AUTO] Tiempo de riego (autoOff) cumplido.");
        deactivatePump();
    }

    // Contar segundos si la bomba está activada con autoOff (para debug)
    if (pumpActivated && pumpAutoOff && (now - lastSecondPrint >= 1000)) {
        pumpSecondsCount++;
        // Commented out for less verbose logging
        // Serial.print("[DEBUG] Bomba activa (autoOff), segundo "); Serial.println(pumpSecondsCount);
        lastSecondPrint = now;
    }

    // Verificar si es momento de tomar una medición programada
    if (now >= nextMeasureTimestamp) {
        Serial.println("[INFO] Hora de medición programada alcanzada.");
        controlIndependiente(); // Ejecuta la lógica de medición y riego
        // Programar la siguiente medición
        nextMeasureTimestamp = now + (measurementInterval * 3600000UL); // Interval in milliseconds
        Serial.print("[INFO] Próxima medición programada para millis: "); Serial.println(nextMeasureTimestamp);
    }

    // Mensajes de debug periódicos (cada 10 minutos)
    if (now - lastDebugPrint >= 600000) { // 600000 ms = 10 minutos
        lastDebugPrint = now;
        unsigned long elapsedSeconds = (now - startTime) / 1000;
        unsigned long remainingSeconds = (nextMeasureTimestamp > now) ? (nextMeasureTimestamp - now) / 1000 : 0;
        unsigned long remainingHours = remainingSeconds / 3600;
        unsigned long remainingMinutes = (remainingSeconds % 3600) / 60;

        Serial.print("[DEBUG] Uptime: ");
        Serial.print(elapsedSeconds / 86400); Serial.print("d ");
        Serial.print((elapsedSeconds % 86400) / 3600); Serial.print("h ");
        Serial.print((elapsedSeconds % 3600) / 60); Serial.println("m");

        Serial.print("[DEBUG] Próxima medición en aprox: ");
        Serial.print(remainingHours); Serial.print("h ");
        Serial.print(remainingMinutes); Serial.println("m");
        Serial.print("[DEBUG] Estado Bomba: "); Serial.print(pumpActivated ? "ON" : "OFF");
        if (pumpAutoOff) Serial.print(" (Auto-Off)");
        Serial.println();
        Serial.print("[DEBUG] Memoria Libre: "); Serial.println(ESP.getFreeHeap());
        if (WiFi.status() == WL_CONNECTED) {
             Serial.print("[DEBUG] WiFi RSSI: "); Serial.print(WiFi.RSSI()); Serial.println(" dBm");
        } else {
             Serial.println("[DEBUG] WiFi: Desconectado");
        }

    }
}

// Configura el sensor DHT
void setupDHTSensor() {
    Serial.println("[SETUP] Inicializando sensor DHT...");
    dht.begin();
    // Perform a dummy read to check sensor
    float initial_h = dht.readHumidity();
    float initial_t = dht.readTemperature();
    if (isnan(initial_h) || isnan(initial_t)) {
        Serial.println("[WARN] No se pudo leer del sensor DHT al inicio. ¿Está conectado?");
        if (!simulateSensors) {
             Serial.println("[WARN] La simulación NO está activa. Las lecturas fallarán.");
        }
    } else {
         Serial.println("[INFO] Sensor DHT inicializado correctamente.");
    }

    if(simulateSensors) {
        Serial.println("[INFO] LA SIMULACIÓN DE SENSORES ESTÁ ACTIVA.");
    }
}

// Configura el pin de la bomba
void setupBomba() {
    Serial.println("[SETUP] Configurando pin de la bomba (D3)...");
    pinMode(BOMBA_PIN, OUTPUT);
    digitalWrite(BOMBA_PIN, LOW); // Ensure pump is off initially
    pumpActivated = false;
    pumpAutoOff = false;
}

// Obtiene la humedad (real o simulada)
float getHumidity() {
    if (simulateSensors) {
        // Simulate some fluctuation
        simulatedHumidity += (random(-20, 21) / 10.0); // +/- 2.0
        if (simulatedHumidity < 30) simulatedHumidity = 30 + (random(0, 50) / 10.0);
        if (simulatedHumidity > 95) simulatedHumidity = 95 - (random(0, 50) / 10.0);
        Serial.print("[SIM] Humedad simulada: "); Serial.println(simulatedHumidity);
        return simulatedHumidity;
    } else {
        float h = dht.readHumidity();
        int retry = 0;
        // Retry reading a few times if NaN
        while (isnan(h) && retry < 3) {
             Serial.println("[WARN] Falla lectura de humedad, reintentando...");
             delay(500); // Wait before retry
             h = dht.readHumidity();
             retry++;
        }
        if (isnan(h)) {
             Serial.println("[ERROR] Falla lectura de humedad después de reintentos.");
             return -1.0; // Return an error indicator
        } else {
             Serial.print("[SENSOR] Humedad leída: "); Serial.println(h);
             return h;
        }
    }
}

// Obtiene la temperatura (real o simulada)
float getTemperature() {
    if (simulateSensors) {
        simulatedTemperature += (random(-10, 11) / 10.0); // +/- 1.0
        if (simulatedTemperature < 15) simulatedTemperature = 15 + (random(0, 20) / 10.0);
        if (simulatedTemperature > 35) simulatedTemperature = 35 - (random(0, 20) / 10.0);
        Serial.print("[SIM] Temperatura simulada: "); Serial.println(simulatedTemperature);
        return simulatedTemperature;
    } else {
        float t = dht.readTemperature();
        int retry = 0;
        // Retry reading a few times if NaN
        while (isnan(t) && retry < 3) {
             Serial.println("[WARN] Falla lectura de temperatura, reintentando...");
             delay(500); // Wait before retry
             t = dht.readTemperature();
             retry++;
        }
        if (isnan(t)) {
             Serial.println("[ERROR] Falla lectura de temperatura después de reintentos.");
             return -99.0; // Return an error indicator
        } else {
             Serial.print("[SENSOR] Temperatura leída: "); Serial.println(t);
             return t;
        }
    }
}

// Calcula el VPD (Déficit de Presión de Vapor) en kPa
float calculateVPD(float temperature, float humidity) {
    if (temperature <= -90.0 || humidity < 0.0) { // Check for invalid sensor readings
        return -1.0; // Indicate error
    }
    // Formula using Magnus formula approximation
    // Saturation Vapor Pressure (SVP) in kPa
    float svp = 0.6108 * exp((17.27 * temperature) / (temperature + 237.3));
    // Actual Vapor Pressure (AVP) in kPa
    float avp = (humidity / 100.0) * svp;
    // Vapor Pressure Deficit (VPD) in kPa
    float vpd = svp - avp;
    // Ensure VPD is not negative (can happen with minor sensor inaccuracies at high humidity)
    return (vpd < 0) ? 0.0 : vpd;
}


// Función para determinar el índice de la etapa actual basado en días transcurridos
int getCurrentStageIndex(unsigned long daysElapsed) {
    if (manualStageControl) {
        Serial.print("[INFO] Control manual activado. Usando etapa: ");
        Serial.println(stages[manualStageIndex].name);
        return manualStageIndex;
    }

    unsigned long cumulativeDays = 0;
    for (int i = 0; i < numStages; i++) {
        cumulativeDays += stages[i].duration_days;
        if (daysElapsed <= cumulativeDays) {
             Serial.print("[INFO] Calculado automático. Días: "); Serial.print(daysElapsed);
             Serial.print(" -> Etapa: "); Serial.println(stages[i].name);
            return i; // Return the index of the current stage
        }
    }
    // If daysElapsed exceeds all stage durations, stay in the last stage
    Serial.print("[INFO] Calculado automático. Días: "); Serial.print(daysElapsed);
    Serial.print(" -> Etapa (última): "); Serial.println(stages[numStages - 1].name);
    return numStages - 1;
}

// Función para activar la bomba
void activatePump(unsigned long durationMs) {
    if (pumpActivated) {
        Serial.println("[WARN] La bomba ya está activada. Ignorando nueva activación.");
        return;
    }
    if (durationMs <= 0) {
        Serial.println("[WARN] Duración de riego inválida (<= 0 ms). No se activará la bomba.");
        return;
    }

    Serial.print("[ACCION] Activando bomba por "); Serial.print(durationMs / 1000); Serial.println(" segundos (Auto-Off).");
    digitalWrite(BOMBA_PIN, HIGH);
    pumpActivated = true;
    pumpAutoOff = true; // Always use auto-off when activated programmatically
    pumpOnTime = millis();
    pumpDurationMs = durationMs;
    pumpSecondsCount = 0; // Reset debug counter
    lastSecondPrint = millis();
    pumpActivationCount++; // Increment global counter
    Serial.print("[INFO] Contador de activaciones de bomba: "); Serial.println(pumpActivationCount);
}

// Función para desactivar la bomba
void deactivatePump() {
    if (!pumpActivated) {
        // Serial.println("[INFO] La bomba ya está desactivada."); // Maybe too verbose
        return;
    }
    Serial.println("[ACCION] Desactivando bomba.");
    digitalWrite(BOMBA_PIN, LOW);
    pumpActivated = false;
    pumpAutoOff = false; // Reset auto-off flag
    pumpDurationMs = 0;
    pumpOnTime = 0;
    pumpSecondsCount = 0;
}

// Carga el intervalo de medición desde el archivo
void loadMeasurementInterval() {
    File file = LittleFS.open("/interval.txt", "r");
    if (!file) {
        Serial.println("[INFO] No se encontró 'interval.txt', usando valor por defecto (3 horas).");
        measurementInterval = 3;
        saveMeasurementInterval(measurementInterval); // Save the default value
        return;
    }
    String valStr = file.readStringUntil('\n');
    file.close();
    valStr.trim();
    int val = valStr.toInt();
    if (val > 0 && val < 168) { // Sanity check (e.g., 1 hour to 1 week)
        measurementInterval = val;
        Serial.print("[INFO] Intervalo de medición cargado: ");
        Serial.print(measurementInterval); Serial.println(" horas.");
    } else {
        Serial.print("[WARN] Intervalo inválido en 'interval.txt' ('"); Serial.print(valStr);
        Serial.println("'). Usando valor por defecto (3 horas).");
        measurementInterval = 3;
        saveMeasurementInterval(measurementInterval); // Save the default value
    }
}

// Guarda el intervalo de medición en el archivo
void saveMeasurementInterval(int interval) {
    if (interval <= 0 || interval >= 168) {
         Serial.print("[ERROR] Intento de guardar intervalo inválido: "); Serial.println(interval);
         return;
    }
    File file = LittleFS.open("/interval.txt", "w");
    if (!file) {
        Serial.println("[ERROR] No se pudo abrir 'interval.txt' para escritura.");
        return;
    }
    file.println(interval);
    file.close();
    measurementInterval = interval; // Update in-memory value
    Serial.print("[INFO] Intervalo de medición guardado: ");
    Serial.print(measurementInterval); Serial.println(" horas.");

    // Recalcular la próxima hora de medición basada en la última medición o el inicio
    unsigned long lastEventTime = (lastMeasurementTimestamp > startTime) ? lastMeasurementTimestamp : startTime;
    nextMeasureTimestamp = lastEventTime + (measurementInterval * 3600000UL);
     Serial.print("[INFO] Próxima medición recalculada para millis: "); Serial.println(nextMeasureTimestamp);
}

// Carga las credenciales WiFi desde el archivo
void loadWifiCredentials() {
    File file = LittleFS.open("/WifiConfig.txt", "r");
    if (!file) {
        Serial.println("[INFO] No se encontró 'WifiConfig.txt'. No se intentará autoconexión.");
        return;
    }
    if (!file.available()) {
         Serial.println("[INFO] 'WifiConfig.txt' está vacío.");
         file.close();
         return;
    }

    String ssid = file.readStringUntil('\n');
    String password = file.readStringUntil('\n');
    file.close();
    ssid.trim();
    password.trim();

    if (ssid.length() == 0) {
         Serial.println("[WARN] SSID vacío en 'WifiConfig.txt'.");
         return;
    }

    Serial.print("[ACCION] Intentando conectar a WiFi guardada: '"); Serial.print(ssid); Serial.println("'...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());

    // Intentar configuración estática (descomentar si se desea IP fija)
     WiFi.config(ip, gateway, subnet);
    Serial.println("[INFO] Intentando configuración IP estática.");


    int attempts = 0;
    const int maxAttempts = 20; // Wait up to 20 * 500ms = 10 seconds
    while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n[INFO] Conexión WiFi exitosa con credenciales guardadas.");
        Serial.print("[INFO] IP Address: "); Serial.println(WiFi.localIP());
        Serial.print("[INFO] Subnet Mask: "); Serial.println(WiFi.subnetMask());
        Serial.print("[INFO] Gateway IP: "); Serial.println(WiFi.gatewayIP());
        Serial.print("[INFO] DNS IP: "); Serial.println(WiFi.dnsIP());
        Serial.print("[INFO] RSSI: "); Serial.print(WiFi.RSSI()); Serial.println(" dBm");
    } else {
        Serial.println("\n[ERROR] Falló la conexión WiFi con credenciales guardadas.");
        WiFi.disconnect(); // Ensure disconnected state
    }
}

// Carga la etapa manual desde el archivo al iniciar
void loadManualStage() {
    File file = LittleFS.open("/ManualStage.txt", "r");
    if (!file) {
        Serial.println("[INFO] No se encontró 'ManualStage.txt'. Control de etapa automático activado.");
        manualStageControl = false;
        manualStageIndex = 0; // Default to first stage if file doesn't exist
        return;
    }

    if (!file.available()) {
         Serial.println("[INFO] 'ManualStage.txt' está vacío. Control de etapa automático activado.");
         file.close();
         manualStageControl = false;
         manualStageIndex = 0;
         return;
    }

    String stageStr = file.readStringUntil('\n');
    file.close();
    stageStr.trim();
    int stageIdx = stageStr.toInt();

    // Validate the loaded index
    if (stageIdx >= 0 && stageIdx < numStages) {
        manualStageIndex = stageIdx;
        manualStageControl = true;
        Serial.print("[INFO] Control manual de etapa cargado desde archivo. Etapa: ");
        Serial.println(stages[manualStageIndex].name);
    } else {
        Serial.print("[WARN] Índice de etapa manual inválido ('"); Serial.print(stageStr);
        Serial.println("') en 'ManualStage.txt'. Usando control automático.");
        manualStageControl = false;
        manualStageIndex = 0; // Reset to default
        // Optionally delete the invalid file
        LittleFS.remove("/ManualStage.txt");
    }
}

// Guarda la etapa manual seleccionada
void saveManualStage(int index) {
     if (index < 0 || index >= numStages) {
         Serial.print("[ERROR] Intento de guardar índice de etapa manual inválido: "); Serial.println(index);
         return;
     }
     File file = LittleFS.open("/ManualStage.txt", "w");
     if (!file) {
         Serial.println("[ERROR] No se pudo abrir 'ManualStage.txt' para escritura.");
         return;
     }
     file.println(index);
     file.close();
     manualStageIndex = index; // Update in-memory value
     manualStageControl = true; // Ensure manual control is active
     Serial.print("[INFO] Etapa manual guardada: "); Serial.println(stages[manualStageIndex].name);
}


// Carga las mediciones previas desde el archivo
String loadMeasurements() {
    File file = LittleFS.open("/Measurements.txt", "r");
    if (!file) {
        Serial.println("[INFO] No se encontró 'Measurements.txt'. No hay historial previo.");
        return "";
    }
     if (!file.available()) {
        Serial.println("[INFO] 'Measurements.txt' está vacío.");
        file.close();
        return "";
     }
    // Assuming measurements are stored one JSON object per line or comma-separated
    // Let's assume comma-separated for parsing simplicity with parseData
    String measurementsStr = file.readString();
    file.close();
    Serial.println("[INFO] Historial de mediciones cargado desde archivo.");
    // Serial.println(measurementsStr); // Can be very long, comment out usually
    return measurementsStr;
}

// Guarda TODAS las mediciones actuales en el archivo (sobrescribe)
void saveMeasurementFile(const String& allMeasurementsString) {
    File file = LittleFS.open("/Measurements.txt", "w");
    if (!file) {
        Serial.println("[ERROR] No se pudo abrir 'Measurements.txt' para escritura.");
        return;
    }
    size_t bytesWritten = file.print(allMeasurementsString);
    file.close();
    if (bytesWritten == allMeasurementsString.length()) {
         // Serial.println("[DEBUG] Historial de mediciones guardado en archivo."); // Verbose
    } else {
         Serial.println("[ERROR] Error al escribir historial completo en 'Measurements.txt'.");
    }
}

// Parsea la cadena de mediciones cargada del archivo en el array
// Asume formato: {json1},{json2},...
int parseData(String input, String output[]) {
    int count = 0;
    int startIndex = 0;
    int endIndex = 0;
    input.trim(); // Remove leading/trailing whitespace

    while (startIndex < input.length() && count < MAX_JSON_OBJECTS) {
        startIndex = input.indexOf('{', startIndex);
        if (startIndex == -1) break; // No more opening braces

        endIndex = input.indexOf('}', startIndex);
        if (endIndex == -1) break; // No closing brace found (malformed)

        // Extract the JSON object string
        String entry = input.substring(startIndex, endIndex + 1);
        output[count++] = entry;

        // Move startIndex past the current object for the next search
        startIndex = endIndex + 1;
        // Skip potential comma separator
        if (startIndex < input.length() && input.charAt(startIndex) == ',') {
            startIndex++;
        }
         // Skip potential whitespace after comma
         while(startIndex < input.length() && isspace(input.charAt(startIndex))) {
             startIndex++;
         }
    }
    return count; // Return the number of measurements parsed
}


// Convierte el array de Strings de mediciones a un solo String para guardar
// Formato: {json1},{json2},...
String arrayToString(String array[], size_t arraySize) {
    String result = "";
    bool first = true;
    for (size_t i = 0; i < arraySize; i++) {
        // Only add non-empty entries
        if (array[i] != nullptr && array[i].length() > 2) { // Check basic validity { }
            if (!first) {
                result += ","; // Comma separator
            }
            result += array[i];
            first = false;
        }
    }
    return result;
}

// Guarda una nueva medición en el array y en el archivo
void saveMeasurement(const String& jsonString) {
    Serial.println("[ACCION] Guardando nueva medición:");
    Serial.println(jsonString);

    // Basic validation: check if it looks like a JSON object
    if (!jsonString.startsWith("{") || !jsonString.endsWith("}")) {
        Serial.println("[ERROR] Intento de guardar medición inválida (no es JSON object).");
        return;
    }

    if (jsonIndex < MAX_JSON_OBJECTS) {
        // Add to the next available slot
        measurements[jsonIndex++] = jsonString;
        Serial.print("[INFO] Medición agregada al índice: "); Serial.println(jsonIndex - 1);
    } else {
        // Array is full, implement a sliding window: shift old ones, add new one at the end
        Serial.println("[WARN] Array de mediciones lleno. Desplazando historial...");
        for (int i = 0; i < MAX_JSON_OBJECTS - 1; i++) {
            measurements[i] = measurements[i + 1];
        }
        measurements[MAX_JSON_OBJECTS - 1] = jsonString;
        // jsonIndex remains MAX_JSON_OBJECTS
         Serial.print("[INFO] Medición agregada al final (índice "); Serial.print(MAX_JSON_OBJECTS - 1); Serial.println("), la más antigua eliminada.");
    }
    // Save the entire updated history back to the file
    saveMeasurementFile(arrayToString(measurements, jsonIndex)); // Use jsonIndex as size
}

// Formatea las mediciones para enviarlas como un array JSON [{},{}]
void formatMeasurementsToString(String& formattedString) {
    formattedString = "[";
    bool first = true;
    // Iterate only up to jsonIndex, which holds the count of valid entries
    for (int i = 0; i < jsonIndex; i++) {
        if (measurements[i] != nullptr && measurements[i].length() > 2) {
            if (!first) {
                formattedString += ",";
            }
            formattedString += measurements[i];
            first = false;
        }
    }
    formattedString += "]";
}

// Función principal de control automático basada en etapas y tiempo
void controlIndependiente() {
    unsigned long now = millis();
    unsigned long elapsedMillis = now - startTime;
    unsigned long elapsedSeconds = elapsedMillis / 1000;
    unsigned long elapsedHours   = elapsedSeconds / 3600;
    uint32_t elapsedDays = (rtc.now().unixtime() - rtcBootEpoch) / 86400UL;



    static unsigned long failureStartTime = 0; // Timestamp de primer fallo

    Serial.println("\n[CONTROL] Iniciando ciclo de control independiente...");
    Serial.print("[INFO] Tiempo transcurrido: "); Serial.print(elapsedDays); Serial.print("d ");
    Serial.print(elapsedHours % 24); Serial.print("h ");
    Serial.print((elapsedSeconds % 3600) / 60); Serial.println("m");

    // 1. Tomar lecturas
    DateTime dtNow   = rtc.now();
    uint64_t epochMs = (uint64_t)dtNow.unixtime() * 1000ULL;
    float humidity   = getHumidity();
    float temperature = getTemperature();
    lastMeasurementTimestamp = epochMs;   // <-- AHORA ES EPOCH REAL

    // 2. Detectar fallo de sensor
    bool sensorValid = !(humidity < 0.0 || temperature <= -90.0);
    if (!sensorValid) {
        Serial.println("[WARN] Falla en lectura de sensor. Se registrará medición de fallo.");
        if (failureStartTime == 0) {
            failureStartTime = now; // Marca inicio de la serie de fallos
        }
    } else {
        // Si recupera, resetea el contador de fallo
        failureStartTime = 0;
    }

    // 3. Determinar etapa y parámetros
    int stageIndex = getCurrentStageIndex(elapsedDays);
    const Stage& currentStage   = stages[stageIndex];
    int currentThreshold        = currentStage.humidityThreshold;
    unsigned long wateringTimeMs = currentStage.wateringTimeSec * 1000UL;

    Serial.print("[INFO] Etapa: "); Serial.println(currentStage.name);
    Serial.print("       Umbral humedad: "); Serial.print(currentThreshold); Serial.println("%");
    Serial.print("       Duración riego: "); Serial.print(currentStage.wateringTimeSec); Serial.println("s");

    // 4. Decidir riego
    bool needsWatering = false;
    if (!pumpAutoOff) {
        if (sensorValid && humidity < currentThreshold) {
            // Humedad bajo umbral → riego normal
            Serial.println("[DECISION] Humedad bajo umbral. Se activa riego.");
            needsWatering = true;
        }
        else if (!sensorValid) {
            // Sensor fuera de servicio
            unsigned long downTime = now - failureStartTime;
            if (downTime >= 86400000UL) { // 24 h en ms
                Serial.println("[DECISION] 24 h de fallo continuado. Forzando riego.");
                needsWatering = true;
            } else {
                Serial.println("[DECISION] Sensor inválido pero no han pasado 24 h de fallo. No riega.");
            }
        } else {
            // Sensor OK y humedad suficiente
            Serial.println("[DECISION] Humedad suficiente. No riega.");
        }

        if (needsWatering) {
            activatePump(wateringTimeMs);
        } else {
            deactivatePump();
        }
    } else {
        Serial.println("[INFO] Bomba en modo Auto‑Off, se omite lógica de riego.");
    }

   // 5. Registrar la medición (incluyendo fallos)
StaticJsonDocument<256> doc;

// Humedad y temperatura: número formateado o JSON null
if (sensorValid) {
    doc["temperature"] = temperature;  // a float → JSON number
doc["humidity"]    = humidity;     // a float → JSON number

} else {
    doc["humidity"]    = nullptr;  // esto genera un JSON null
    doc["temperature"] = nullptr;
}

// Resto de campos siempre presentes
doc["timestamp"]    = String(elapsedHours) + "h" + String((elapsedSeconds % 3600) / 60) + "m";
doc["pumpActivated"] = needsWatering;
doc["stage"]         = currentStage.name;
doc["epoch_ms"]      = epochMs;     // número, no string
doc["timestamp_str"] = dtNow.timestamp(DateTime::TIMESTAMP_DATE) + String(" ") +
                       dtNow.timestamp(DateTime::TIMESTAMP_TIME);   // legible


// Serializar y guardar
String measurementString;
serializeJson(doc, measurementString);
saveMeasurement(measurementString);

    Serial.println("[CONTROL] Ciclo de control finalizado.");
}


// --- Handlers para Endpoints HTTP ---

// Handler para la página principal o config
void handleRoot() {
     String path = "/";
     String contentType = "text/html";
     bool isIndex = true;

     if (WiFi.status() != WL_CONNECTED) {
          path = "/config.html";
          Serial.println("[HTTP] Sirviendo página de configuración WiFi (config.html)");
     } else {
          path = "/index.html";
          Serial.println("[HTTP] Sirviendo página principal (index.html)");
     }

    if (!LittleFS.exists(path)) {
        Serial.print("[ERROR] No se encontró el archivo: "); Serial.println(path);
        server.send(404, "text/plain", "Error 404: Archivo no encontrado (" + path + ")");
        return;
    }

    File file = LittleFS.open(path, "r");
    if (!file) {
         Serial.print("[ERROR] No se pudo abrir el archivo: "); Serial.println(path);
         server.send(500, "text/plain", "Error interno del servidor: No se pudo abrir el archivo.");
         return;
    }

    // Usar streamFile para eficiencia con archivos grandes
    server.streamFile(file, contentType);
    file.close();
}


// Handler para /data
void handleData() {
    Serial.println("[HTTP] Solicitud /data recibida.");

    // Obtener lecturas actuales
    float humidity = getHumidity();
    float temperature = getTemperature();
    float vpd = calculateVPD(temperature, humidity);

    // Tiempo transcurrido
    unsigned long now = millis();
    unsigned long elapsedSeconds = (now - startTime) / 1000;

    // Determinar estado actual (basado en tiempo o manual)
    unsigned long elapsedDays = elapsedSeconds / 86400;
    int stageIndex = getCurrentStageIndex(elapsedDays);
    const Stage& currentStage = stages[stageIndex];

    DateTime dtNow = rtc.now();
    uint64_t epochNowMs = (uint64_t)dtNow.unixtime() * 1000ULL;
   
    // Crear respuesta JSON
    StaticJsonDocument<512> doc; // Sufficient size for data payload
    doc["temperature"] = serialized(String(temperature, 1));
    doc["humidity"] = serialized(String(humidity, 1));
    doc["vpd"] = serialized(String(vpd, 2)); // VPD usually shown with 2 decimals
    doc["pumpStatus"] = pumpActivated;
    doc["pumpActivationCount"] = pumpActivationCount;
    doc["elapsedTime"] = elapsedSeconds; // Total seconds elapsed
    doc["startTime"] = startTime; // System start time in ms
    doc["currentTime"]           = epochNowMs;
    doc["lastMeasurementTimestamp"] = lastMeasurementTimestamp; // ya contiene epoch real
    // Current stage info
    doc["currentStageName"] = currentStage.name;
    doc["currentStageIndex"] = stageIndex;
    doc["currentStageThreshold"] = currentStage.humidityThreshold; // Threshold for the CURRENT stage
    doc["currentStageWateringSec"] = currentStage.wateringTimeSec; // Watering time for the CURRENT stage
    doc["manualStageControl"] = manualStageControl;
    // Network Info (if connected)
    if (WiFi.status() == WL_CONNECTED) {
       doc["deviceIP"] = WiFi.localIP().toString();
       doc["wifiRSSI"] = WiFi.RSSI();
    } else {
        doc["deviceIP"] = (WiFi.getMode() == WIFI_AP) ? WiFi.softAPIP().toString() : "N/A";
        doc["wifiRSSI"] = 0;
    }


    String response;
    serializeJson(doc, response);

    server.sendHeader("Access-Control-Allow-Origin", "*"); // Allow CORS for development
    server.send(200, "application/json", response);
    // Serial.println("[HTTP] Respuesta /data enviada:"); // Verbose
    // Serial.println(response); // Verbose
}

// Handler para listar redes WiFi disponibles
void handleWifiListRequest() {
    Serial.println("[HTTP] Solicitud /wifiList recibida. Escaneando redes...");
    server.sendHeader("Access-Control-Allow-Origin", "*");
    int numNetworks = WiFi.scanNetworks();
    Serial.print("[INFO] Escaneo completado. Redes encontradas: "); Serial.println(numNetworks);

    if (numNetworks == -1) {
         Serial.println("[ERROR] Escaneo WiFi falló.");
         server.send(500, "application/json", "{\"error\":\"Scan failed\"}");
         return;
    }
    if (numNetworks == 0) {
         Serial.println("[INFO] No se encontraron redes WiFi.");
         server.send(200, "application/json", "[]"); // Send empty array
         return;
    }

    // Estimate JSON size needed: ~70 bytes per network + array overhead
    DynamicJsonDocument wifiJson(numNetworks * 70 + 50);
    JsonArray networks = wifiJson.to<JsonArray>();

    // Limit number of networks reported if too many?
    int maxNetworksToSend = (numNetworks > 20) ? 20 : numNetworks; // Limit to e.g., 20

    for (int i = 0; i < maxNetworksToSend; ++i) {
        JsonObject network = networks.createNestedObject();
        network["ssid"] = WiFi.SSID(i);
        network["rssi"] = WiFi.RSSI(i);
        // network["encryption"] = WiFi.encryptionType(i); // Could add encryption type
    }

    String response;
    serializeJson(wifiJson, response);
    server.send(200, "application/json", response);
}

// Handler para conectar a WiFi (intento manual desde UI, no guarda)
void handleConnectWifi() {
    Serial.println("[HTTP] Solicitud /connectWifi (POST) recibida.");
    if (!server.hasArg("plain")) {
        server.send(400, "text/plain", "Bad Request: Missing POST body");
        return;
    }

    String body = server.arg("plain");
    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, body);

    if (error) {
        server.send(400, "text/plain", "Bad Request: Invalid JSON");
        return;
    }

    const char* ssid_c = doc["ssid"];
    const char* password_c = doc["password"];

    if (!ssid_c || strlen(ssid_c) == 0) {
        server.send(400, "text/plain", "Bad Request: Missing SSID");
        return;
    }
     // Password can be empty for open networks

    String ssid = String(ssid_c);
    String password = String(password_c ? password_c : ""); // Handle null password


    Serial.print("[ACCION] Intentando conectar a WiFi (manual): '"); Serial.print(ssid); Serial.println("'...");

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());

    int attempts = 0;
    const int maxAttempts = 20; // 10 seconds
    while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n[INFO] Conexión WiFi manual exitosa.");
        dnsServer.stop(); // Stop DNS if we connected
        String successMsg = "{\"status\":\"success\", \"ip\":\"" + WiFi.localIP().toString() + "\"}";
        server.send(200, "application/json", successMsg);
    } else {
        Serial.println("\n[ERROR] Conexión WiFi manual falló.");
        WiFi.disconnect(false); // Don't erase config on manual fail
        // Go back to AP mode? Or stay STA trying? Let's revert to AP if saved creds failed too
        if (!LittleFS.exists("/WifiConfig.txt")) { // If no saved creds exist, go back to AP
             startAPMode();
        }
        server.send(401, "application/json", "{\"status\":\"failed\", \"message\":\"Connection failed\"}");
    }
}


// Handler para guardar credenciales WiFi y reiniciar
void handleSaveWifiCredentials() {
     Serial.println("[HTTP] Solicitud /saveWifiCredentials (POST) recibida.");
     if (!server.hasArg("plain")) {
         server.send(400, "text/plain", "Bad Request: Missing POST body");
         return;
     }

     String body = server.arg("plain");
     StaticJsonDocument<200> doc;
     DeserializationError error = deserializeJson(doc, body);

     if (error) {
         server.send(400, "text/plain", "Bad Request: Invalid JSON");
         return;
     }

    const char* ssid_c = doc["ssid"];
    const char* password_c = doc["password"];

    if (!ssid_c || strlen(ssid_c) == 0) {
        server.send(400, "text/plain", "Bad Request: Missing SSID");
        return;
    }
     // Password can be empty

    String ssid = String(ssid_c);
    String password = String(password_c ? password_c : "");


     Serial.print("[ACCION] Validando y guardando credenciales WiFi para: '"); Serial.print(ssid); Serial.println("'...");

     // 1. Try connecting with the new credentials FIRST
     WiFi.mode(WIFI_STA);
     WiFi.begin(ssid.c_str(), password.c_str());

     int attempts = 0;
     const int maxAttempts = 20; // 10 seconds
     while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts) {
         delay(500);
         Serial.print(".");
         attempts++;
     }

     // 2. If connection successful, save them
     if (WiFi.status() == WL_CONNECTED) {
         Serial.println("\n[INFO] Conexión exitosa con nuevas credenciales. Guardando...");
         File file = LittleFS.open("/WifiConfig.txt", "w");
         if (!file) {
             Serial.println("[ERROR] No se pudo abrir 'WifiConfig.txt' para escritura.");
             server.send(500, "application/json", "{\"status\":\"error\", \"message\":\"Internal Server Error: Cannot save config\"}");
             return;
         }
         file.println(ssid);
         file.println(password);
         file.close();
         Serial.println("[INFO] Credenciales guardadas. Reiniciando el sistema...");
         server.send(200, "application/json", "{\"status\":\"success\", \"message\":\"Credentials saved. Restarting system...\"}");
         delay(1000); // Allow time for response to send
         ESP.restart();
     } else {
         // 3. If connection failed, report error and DO NOT save
         Serial.println("\n[ERROR] Falló la conexión con las credenciales proporcionadas. No se guardarán.");
         WiFi.disconnect(false); // Don't erase existing config if any
         // Revert to AP mode?
         startAPMode();
         server.send(401, "application/json", "{\"status\":\"failed\", \"message\":\"Connection failed with provided credentials. Not saved.\"}");
     }
}


// Handler para guardar una medición (obsoleto? Measurements are saved internally now)
/*
void handleSaveMeasurement() {
    Serial.println("[HTTP] Solicitud /saveMeasurement (POST) recibida.");
     String jsonString = server.arg("plain");
     if (jsonString.length() > 2) {
         saveMeasurement(jsonString); // Use the internal save function
         server.send(200, "application/json", "{\"status\":\"success\"}");
     } else {
         server.send(400, "text/plain", "Bad Request: Invalid measurement data");
     }
}*/

// Handler para cargar las mediciones
void handleLoadMeasurement() {
    Serial.println("[HTTP] Solicitud /loadMeasurement (GET) recibida.");
    server.sendHeader("Access-Control-Allow-Origin", "*");
    String formattedJsonArray;
    formatMeasurementsToString(formattedJsonArray); // Format the in-memory array
    // Serial.println("[HTTP] Enviando historial de mediciones:"); // Verbose
    // Serial.println(formattedJsonArray); // Verbose
    server.send(200, "application/json", formattedJsonArray);
}

// Handler para limpiar el historial de mediciones
void handleClearMeasurementHistory() {
     Serial.println("[HTTP] Solicitud /clearHistory (POST) recibida.");
     server.sendHeader("Access-Control-Allow-Origin", "*");
     Serial.println("[ACCION] Borrando historial de mediciones...");

     // Clear the in-memory array
     for (int i = 0; i < MAX_JSON_OBJECTS; i++) {
         measurements[i] = ""; // Or assign nullptr if using String pointers
     }
     jsonIndex = 0; // Reset index

     // Clear the file
     if (LittleFS.exists("/Measurements.txt")) {
        if (!LittleFS.remove("/Measurements.txt")) {
             Serial.println("[ERROR] No se pudo borrar 'Measurements.txt'.");
             // Send error but proceed with clearing memory
             server.send(500, "application/json", "{\"status\":\"error\", \"message\":\"Could not delete history file\"}");
             return; // Maybe don't return, just warn?
        }
     }
      // Optionally recreate an empty file? saveMeasurementFile("");

     Serial.println("[INFO] Historial de mediciones borrado.");
     server.send(200, "application/json", "{\"status\":\"success\"}");
}

// Handler para establecer y obtener el intervalo de medición
void handleMeasurementInterval() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    if (server.method() == HTTP_POST) {
        Serial.println("[HTTP] Solicitud /setMeasurementInterval (POST) recibida.");
        if (!server.hasArg("plain")) {
            server.send(400, "text/plain", "Bad Request: Missing JSON body");
            return;
        }
        String body = server.arg("plain");
        StaticJsonDocument<100> doc;
        DeserializationError error = deserializeJson(doc, body);
        if (error) {
            server.send(400, "text/plain", "Bad Request: Invalid JSON");
            return;
        }
        if (!doc.containsKey("interval")) {
             server.send(400, "text/plain", "Bad Request: Missing 'interval' key");
             return;
        }
        int newInterval = doc["interval"];
        if (newInterval > 0 && newInterval < 168) { // Basic validation (1h to 1 week)
            Serial.print("[ACCION] Ajustando intervalo de medición a: "); Serial.print(newInterval); Serial.println(" horas");
            saveMeasurementInterval(newInterval); // Save and update internal value
            server.send(200, "application/json", "{\"status\":\"success\"}");
        } else {
            server.send(400, "text/plain", "Bad Request: Interval must be between 1 and 167 hours");
        }
    } else if (server.method() == HTTP_GET) {
         Serial.println("[HTTP] Solicitud /getMeasurementInterval (GET) recibida.");
         StaticJsonDocument<50> doc;
         doc["interval"] = measurementInterval;
         String response;
         serializeJson(doc, response);
         server.send(200, "application/json", response);
    } else {
         server.send(405, "text/plain", "Method Not Allowed");
    }
}

// Handler para controlar la bomba manualmente
void handlePumpControl() {
    Serial.println("[HTTP] Solicitud /controlPump (POST) recibida.");
     server.sendHeader("Access-Control-Allow-Origin", "*");
     if (!server.hasArg("plain")) {
         server.send(400, "text/plain", "Bad Request: Missing JSON body");
         return;
     }
     String body = server.arg("plain");
     StaticJsonDocument<128> doc; // Increased size for duration
     DeserializationError error = deserializeJson(doc, body);
     if (error) {
         server.send(400, "text/plain", "Bad Request: Invalid JSON");
         return;
     }

     if (!doc.containsKey("action")) {
         server.send(400, "text/plain", "Bad Request: Missing 'action' key");
         return;
     }
     String action = doc["action"].as<String>();

     if (action.equalsIgnoreCase("on")) {
         // Duration is optional, use a default from a stage if not provided? Or a fixed default.
         // Let's use a fixed default of 30s if not provided.
         int durationSec = doc["duration"] | 30; // Default to 30 seconds if "duration" is missing or null
         if (durationSec <= 0) {
             server.send(400, "application/json", "{\"status\":\"error\", \"message\":\"Invalid duration\"}");
             return;
         }
         unsigned long durationMs = durationSec * 1000UL;
         Serial.print("[ACCION] Encendiendo bomba manualmente por "); Serial.print(durationSec); Serial.println(" segundos.");
         activatePump(durationMs);
         server.send(200, "application/json", "{\"status\":\"pump_on\"}");

     } else if (action.equalsIgnoreCase("off")) {
         Serial.println("[ACCION] Apagando bomba manualmente.");
         deactivatePump();
         server.send(200, "application/json", "{\"status\":\"pump_off\"}");
     } else {
         Serial.print("[ERROR] Acción de bomba inválida recibida: "); Serial.println(action);
         server.send(400, "application/json", "{\"status\":\"invalid_action\"}");
     }
}

// Handler para establecer etapa manual
void handleSetManualStage() {
    Serial.println("[HTTP] Solicitud /setManualStage (POST) recibida.");
    server.sendHeader("Access-Control-Allow-Origin", "*");
     if (!server.hasArg("plain")) {
         server.send(400, "text/plain", "Bad Request: Missing JSON body");
         return;
     }
     String body = server.arg("plain");
     StaticJsonDocument<100> doc;
     DeserializationError error = deserializeJson(doc, body);
     if (error) {
         server.send(400, "text/plain", "Bad Request: Invalid JSON");
         return;
     }
     if (!doc.containsKey("stage")) {
         server.send(400, "text/plain", "Bad Request: Missing 'stage' key (index)");
         return;
     }
     int stageIndex = doc["stage"];

     if (stageIndex >= 0 && stageIndex < numStages) {
         saveManualStage(stageIndex); // Save and update state
         server.send(200, "application/json", "{\"status\":\"success\", \"message\":\"Manual stage set\"}");
     } else {
         server.send(400, "application/json", "{\"status\":\"error\", \"message\":\"Invalid stage index\"}");
     }
}

// Handler para obtener la etapa actual (manual o automática)
void handleGetCurrentStage() {
     Serial.println("[HTTP] Solicitud /getCurrentStage (GET) recibida.");
     server.sendHeader("Access-Control-Allow-Origin", "*");

     unsigned long elapsedSeconds = (millis() - startTime) / 1000;
     unsigned long elapsedDays = elapsedSeconds / 86400;
     int stageIndex = getCurrentStageIndex(elapsedDays); // Gets manual or auto index
     const Stage& currentStage = stages[stageIndex];

     StaticJsonDocument<256> doc;
     doc["currentStage"] = currentStage.name;
     doc["stageIndex"] = stageIndex;
     doc["manualControl"] = manualStageControl;
     // Add parameters for the current stage
     JsonObject params = doc.createNestedObject("params");
     params["threshold"] = currentStage.humidityThreshold;
     params["watering"] = currentStage.wateringTimeSec;
     params["duration_days"] = currentStage.duration_days; // Add duration too


     String response;
     serializeJson(doc, response);
     server.send(200, "application/json", response);
}

// Handler para resetear el control manual de etapas
void handleResetManualStage() {
     Serial.println("[HTTP] Solicitud /resetManualStage (POST) recibida.");
     server.sendHeader("Access-Control-Allow-Origin", "*");
     manualStageControl = false;
     // Delete the config file to make it permanent
     if (LittleFS.exists("/ManualStage.txt")) {
         if (!LittleFS.remove("/ManualStage.txt")) {
             Serial.println("[ERROR] No se pudo borrar 'ManualStage.txt'.");
             // Report error but continue logic
         }
     }
     Serial.println("[INFO] Control manual de etapa desactivado (automático activado).");
     server.send(200, "application/json", "{\"status\":\"success\", \"message\":\"Manual stage reset to automatic\"}");
}

// Handler para listar todas las etapas definidas
void handleListStages() {
    Serial.println("[HTTP] Solicitud /listStages (GET) recibida.");
    server.sendHeader("Access-Control-Allow-Origin", "*");

    // Calculate JSON size: base array + object per stage (~100 bytes each?)
    DynamicJsonDocument doc(JSON_ARRAY_SIZE(numStages) + numStages * JSON_OBJECT_SIZE(5) + 100); // Estimate size
    JsonArray stagesArray = doc.to<JsonArray>();

    for (int i = 0; i < numStages; i++) {
        JsonObject stageObj = stagesArray.createNestedObject();
        stageObj["index"] = i;
        stageObj["name"] = stages[i].name;
        stageObj["duration_days"] = stages[i].duration_days;
        stageObj["humidityThreshold"] = stages[i].humidityThreshold;
        stageObj["wateringTimeSec"] = stages[i].wateringTimeSec;
    }

    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}


// Inicia el modo AP para configuración
void startAPMode() {
    const char* ap_ssid = "GreenNanny-Setup";
    const char* ap_password = "password123"; // Use a simple password for setup AP

    Serial.print("[ACCION] Iniciando Modo Access Point (AP): SSID '");
    Serial.print(ap_ssid); Serial.println("'...");

    WiFi.mode(WIFI_AP);
    bool result = WiFi.softAP(ap_ssid, ap_password);

    if(result) {
        Serial.println("[INFO] Punto de acceso WiFi iniciado correctamente.");
        IPAddress apIP = WiFi.softAPIP();
        Serial.print("[INFO] IP del AP (conectar a esta IP): "); Serial.println(apIP);

        // Iniciar servidor DNS para Captive Portal
        // Respond to all DNS requests with the AP IP address
        dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
        dnsServer.start(DNS_PORT, "*", apIP);
        Serial.println("[INFO] Servidor DNS para portal cautivo iniciado.");
    } else {
        Serial.println("[ERROR] Falló al iniciar el punto de acceso WiFi!");
        // Maybe try restarting?
    }
}

// Configura el servidor web y los endpoints
void setupServer() {
    Serial.println("[SETUP] Configurando servidor web y endpoints...");

    // --- Páginas Principales ---
    server.on("/", HTTP_GET, handleRoot); // Sirve index.html o config.html
    server.on("/index.html", HTTP_GET, handleRoot);
    server.on("/config.html", HTTP_GET, handleRoot);

    // --- Endpoints API ---
    server.on("/data", HTTP_GET, handleData); // Datos principales sensores y estado
    server.on("/loadMeasurement", HTTP_GET, handleLoadMeasurement); // Historial de mediciones
    server.on("/clearHistory", HTTP_POST, handleClearMeasurementHistory); // Borrar historial
    server.on("/wifiList", HTTP_GET, handleWifiListRequest); // Listar redes WiFi
    server.on("/connectWifi", HTTP_POST, handleConnectWifi); // Intentar conectar (manual)
    server.on("/saveWifiCredentials", HTTP_POST, handleSaveWifiCredentials); // Guardar credenciales y reiniciar
    server.on("/controlPump", HTTP_POST, handlePumpControl); // Control manual bomba on/off
    server.on("/setMeasurementInterval", HTTP_POST, handleMeasurementInterval); // Establecer intervalo
    server.on("/getMeasurementInterval", HTTP_GET, handleMeasurementInterval); // Obtener intervalo
    server.on("/listStages", HTTP_GET, handleListStages); // Listar etapas definidas
    server.on("/getCurrentStage", HTTP_GET, handleGetCurrentStage); // Obtener etapa actual
    server.on("/setManualStage", HTTP_POST, handleSetManualStage); // Establecer etapa manual
    server.on("/resetManualStage", HTTP_POST, handleResetManualStage); // Volver a modo automático

    // --- Acciónes ---
    server.on("/takeMeasurement", HTTP_POST, [](){ // Trigger manual measurement cycle
         Serial.println("[HTTP] Solicitud /takeMeasurement (POST) recibida.");
         server.sendHeader("Access-Control-Allow-Origin", "*");
         controlIndependiente(); // Run the control logic now
         server.send(200, "application/json", "{\"status\":\"success\", \"message\":\"Measurement cycle triggered\"}");
    });
    server.on("/restartSystem", HTTP_POST, []() { // Reiniciar el ESP
        Serial.println("[HTTP] Solicitud /restartSystem (POST) recibida.");
        server.sendHeader("Access-Control-Allow-Origin", "*");
        server.send(200, "application/json", "{\"status\":\"restarting\"}");
        Serial.println("[ACCION] Reiniciando el sistema...");
        delay(500); // Allow time for response
        ESP.restart();
    });

     // --- Servir Archivos Estáticos desde LittleFS ---
     // Handler para servir cualquier archivo no encontrado en las rutas anteriores
     server.onNotFound([]() {
        String path = server.uri();
        Serial.print("[HTTP] Solicitud no encontrada, intentando servir archivo estático: "); Serial.println(path);
        if (path.endsWith("/")) path += "index.html"; // Si es un directorio, intentar servir index.html

        String contentType = "text/plain"; // Default content type
        if(path.endsWith(".html")) contentType = "text/html";
        else if(path.endsWith(".css")) contentType = "text/css";
        else if(path.endsWith(".js")) contentType = "application/javascript";
        else if(path.endsWith(".png")) contentType = "image/png";
        else if(path.endsWith(".jpg")) contentType = "image/jpeg";
        else if(path.endsWith(".ico")) contentType = "image/x-icon";
        else if(path.endsWith(".svg")) contentType = "image/svg+xml";


        if (LittleFS.exists(path)) {
             Serial.print("[HTTP] Sirviendo archivo: "); Serial.println(path);
             File file = LittleFS.open(path, "r");
             server.streamFile(file, contentType);
             file.close();
        } else {
             Serial.print("[HTTP] Archivo no encontrado en LittleFS: "); Serial.println(path);
             // Captive portal redirection (if in AP mode)
             if (WiFi.getMode() == WIFI_AP) {
                   Serial.println("[CAPTIVE] Redirigiendo cliente a la página de configuración.");
                   // Redirect to the root which should serve config.html in AP mode
                   server.sendHeader("Location", "/", true);
                   server.send(302, "text/plain", "Redirecting to configuration page");
             } else {
                  server.send(404, "text/plain", "404 Not Found");
             }

        }
    });


    // Iniciar el servidor
    server.begin();
    Serial.println("[INFO] Servidor HTTP iniciado en puerto 80.");
}


// Función para manejar comandos seriales (opcional)
void handleSerialCommands() {
    if (Serial.available() > 0) {
        String command = Serial.readStringUntil('\n');
        command.trim();
        Serial.print("[SERIAL] Comando recibido: "); Serial.println(command);

        if (command.equalsIgnoreCase("STATUS")) {
            // Print current status summary
             unsigned long now = millis();
             unsigned long elapsedSeconds = (now - startTime) / 1000;
             Serial.println("--- STATUS ---");
             Serial.print("Uptime: "); Serial.print(elapsedSeconds / 86400); Serial.print("d ");
             Serial.print((elapsedSeconds % 86400) / 3600); Serial.print("h ");
             Serial.print((elapsedSeconds % 3600) / 60); Serial.println("m");
             float h = getHumidity(); float t = getTemperature();
             Serial.print("Temp: "); Serial.print(t, 1); Serial.print(" C, Humid: "); Serial.print(h, 1); Serial.println(" %");
             Serial.print("Pump: "); Serial.print(pumpActivated ? "ON" : "OFF"); if (pumpAutoOff) Serial.print(" (Auto)"); Serial.println();
             int stageIdx = getCurrentStageIndex(elapsedSeconds / 86400);
             Serial.print("Stage: "); Serial.print(stages[stageIdx].name); Serial.print(" ("); Serial.print(manualStageControl ? "Manual" : "Auto"); Serial.println(")");
             Serial.print("WiFi: "); Serial.println(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "Disconnected");
             Serial.print("Measurements: "); Serial.println(jsonIndex);
             Serial.print("Next Measure (ms): "); Serial.println(nextMeasureTimestamp);
             Serial.println("--------------");
        } else if (command.equalsIgnoreCase("MEASURE")) {
             Serial.println("[SERIAL] Forzando ciclo de medición...");
             controlIndependiente();
        } else if (command.equalsIgnoreCase("PUMP ON")) {
            Serial.println("[SERIAL] Encendiendo bomba manualmente (30s)...");
             activatePump(30000); // Activate for 30 seconds
        } else if (command.equalsIgnoreCase("PUMP OFF")) {
             Serial.println("[SERIAL] Apagando bomba manualmente...");
             deactivatePump();
        } else if (command.startsWith("SET STAGE ")) {
             int stage = command.substring(10).toInt();
             if (stage >= 0 && stage < numStages) {
                 Serial.print("[SERIAL] Estableciendo etapa manual a: "); Serial.println(stages[stage].name);
                 saveManualStage(stage);
             } else {
                 Serial.println("[ERROR] Índice de etapa inválido.");
             }
        } else if (command.equalsIgnoreCase("RESET STAGE")) {
            Serial.println("[SERIAL] Desactivando control manual de etapa.");
            manualStageControl = false;
            if (LittleFS.exists("/ManualStage.txt")) LittleFS.remove("/ManualStage.txt");
        } else if (command.equalsIgnoreCase("CLEAR")) {
            Serial.println("[SERIAL] Borrando historial de mediciones...");
             handleClearMeasurementHistory(); // Call the same logic as the HTTP handler
        } else if (command.equalsIgnoreCase("RESTART")) {
             Serial.println("[SERIAL] Reiniciando sistema...");
             ESP.restart();
        }
         else {
            Serial.println("[WARN] Comando serial desconocido.");
            Serial.println(" Comandos disponibles: STATUS, MEASURE, PUMP ON, PUMP OFF, SET STAGE <index>, RESET STAGE, CLEAR, RESTART");
        }
    }
}