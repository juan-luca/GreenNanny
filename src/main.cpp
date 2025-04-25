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
#define STAGES_CONFIG_FILE "/stages_config.json" // File for custom stage config

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

// Configuración de red estática (opcional)
IPAddress ip(192, 168, 0, 73);
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

// Definición de etapas fenológicas - REMOVE CONST HERE
struct Stage {
    const char* name;      // Keep name const
    int duration_days;     // Can be modified if desired, but not requested yet. Keep as is for now.
    int humidityThreshold; // EDITABLE
    int wateringTimeSec;   // EDITABLE
};

// Array de Etapas - REMOVE CONST, this is now the default/initial config
Stage stages[] = {
    {"Germinacion", 7, 65, 15},
    {"Vegetativo", 14, 60, 25},
    {"Prefloracion", 7, 55, 35},
    {"Floracion", 30, 50, 35},
    {"Maduracion", 10, 45, 20}
};
const int numStages = sizeof(stages) / sizeof(stages[0]);

// Variables para control manual de etapas
bool manualStageControl = false;
int manualStageIndex = 0; // Index in the stages array
uint32_t rtcBootEpoch = 0;
// Variables de estado
uint64_t lastMeasurementTimestamp = 0; // Track last successful measurement time (epoch ms)

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
void controlIndependiente(); // Main automatic control logic (includes taking measurement)
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
void handleListStages(); // Now returns current/modified stages
void loadStagesConfig(); // NEW: Load custom stage config from file
bool saveStagesConfig(); // NEW: Save current stages to file
void handleUpdateStage(); // NEW: API endpoint to update a stage
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
// HTTP Handlers
void handleRoot(); // Redirects / to index.html or config.html
// Utilities
float getHumidity();
float getTemperature();
float calculateVPD(float temperature, float humidity);
void handleSerialCommands(); // Optional serial control


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
        while (true) { delay(1000); } // Halt on FS error
    } else {
        Serial.println("[INFO] LittleFS montado correctamente.");
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
    sprintf(buf, "%04u-%02u-%02u %02u:%02u:%02u", now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());
    Serial.print("[INFO] Hora RTC actual: "); Serial.println(buf);

    rtcBootEpoch = rtc.now().unixtime();   // Store boot time epoch
    startTime    = millis();               // Store boot time millis
    lastMeasurementTimestamp = (uint64_t)rtcBootEpoch * 1000ULL; // Initialize last measurement time
    Serial.print("[INFO] Hora de inicio del sistema (millis): "); Serial.println(startTime);
    Serial.print("[INFO] Hora de inicio del sistema (epoch ms): "); Serial.println((uint64_t)lastMeasurementTimestamp);

    // Configurar el pin de la bomba
    setupBomba();

    // Inicializar el sensor DHT
    setupDHTSensor();

    // ***** NEW: Load custom stage configuration *****
    loadStagesConfig();

    // Cargar mediciones previas
    jsonIndex = parseData(loadMeasurements(), measurements);
    Serial.print("[INFO] Cargadas "); Serial.print(jsonIndex); Serial.println(" mediciones previas.");

    // Cargar intervalo de medición
    loadMeasurementInterval();
    nextMeasureTimestamp = startTime + (measurementInterval * 3600000UL);
    Serial.print("[INFO] Próxima medición programada inicialmente alrededor de millis: "); Serial.println(nextMeasureTimestamp);

    // Cargar etapa manual si existe
    loadManualStage();

    // Cargar credenciales WiFi y conectar si existen
    loadWifiCredentials();

    // Iniciar modo AP si no está conectado a WiFi
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[INFO] No conectado a WiFi. Iniciando modo AP.");
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
    // Manejar clientes HTTP
    server.handleClient();

    // Procesar solicitudes DNS si está en modo AP
    if (WiFi.getMode() == WIFI_AP) {
        dnsServer.processNextRequest();
    }

    // Comandos seriales (opcional)
    handleSerialCommands();

    // --- Auto-apagado de la bomba ---
    unsigned long current = millis();  // Recalcular timestamp aquí
    if (pumpAutoOff && pumpActivated && (current - pumpOnTime >= pumpDurationMs)) {
        Serial.println("[AUTO] Tiempo de riego (autoOff) cumplido.");
        deactivatePump();
    }

    // Conteo de segundos para debug si la bomba está encendida con auto-off
    if (pumpActivated && pumpAutoOff && (current - lastSecondPrint >= 1000)) {
        pumpSecondsCount++;
        lastSecondPrint = current;
    }

    // Verificar si es momento de tomar una medición programada
    if (current >= nextMeasureTimestamp) {
        Serial.println("[INFO] Hora de medición programada alcanzada.");
        controlIndependiente();  // Lógica automática de medición y riego
        // Programar la siguiente medición
        nextMeasureTimestamp = current + (measurementInterval * 3600000UL);
        Serial.print("[INFO] Próxima medición programada para millis: ");
        Serial.println(nextMeasureTimestamp);
    }

    // Mensajes de debug periódicos (cada 10 minutos)
    if (current - lastDebugPrint >= 600000) {
        lastDebugPrint = current;
        uint32_t rtcElapsedSeconds = rtc.now().unixtime() - rtcBootEpoch;

        unsigned long remainingMs = (nextMeasureTimestamp > current)
                                    ? (nextMeasureTimestamp - current)
                                    : 0;
        unsigned long remainingSeconds = remainingMs / 1000;
        unsigned long remainingHours   = remainingSeconds / 3600;
        unsigned long remainingMinutes = (remainingSeconds % 3600) / 60;

        Serial.println("--- DEBUG STATUS ---");
        Serial.print("[DEBUG] Uptime (RTC): ");
        Serial.print(rtcElapsedSeconds / 86400); Serial.print("d ");
        Serial.print((rtcElapsedSeconds % 86400) / 3600); Serial.print("h ");
        Serial.print((rtcElapsedSeconds % 3600) / 60);     Serial.println("m");

        Serial.print("[DEBUG] Próxima medición en aprox: ");
        Serial.print(remainingHours);   Serial.print("h ");
        Serial.print(remainingMinutes); Serial.println("m");

        Serial.print("[DEBUG] Estado Bomba: ");
        Serial.print(pumpActivated ? "ON" : "OFF");
        if (pumpAutoOff) Serial.print(" (Auto-Off)");
        Serial.println();

        Serial.print("[DEBUG] Memoria Libre: ");
        Serial.println(ESP.getFreeHeap());

        if (WiFi.status() == WL_CONNECTED) {
            Serial.print("[DEBUG] WiFi RSSI: ");
            Serial.print(WiFi.RSSI()); Serial.println(" dBm");
        } else {
            Serial.println("[DEBUG] WiFi: Desconectado/AP");
        }

        char buf[20];
        DateTime dtNow = rtc.now();
        sprintf(buf, "%04u-%02u-%02u %02u:%02u:%02u",
                dtNow.year(), dtNow.month(), dtNow.day(),
                dtNow.hour(), dtNow.minute(), dtNow.second());
        Serial.print("[DEBUG] Hora RTC: "); Serial.println(buf);
        Serial.println("--------------------");
    }
}


// Configura el sensor DHT
void setupDHTSensor() {
    Serial.println("[SETUP] Inicializando sensor DHT...");
    dht.begin();
    float initial_h = dht.readHumidity();
    float initial_t = dht.readTemperature();
    if (isnan(initial_h) || isnan(initial_t)) {
        Serial.println("[WARN] No se pudo leer del sensor DHT al inicio. ¿Está conectado?");
        if (!simulateSensors) Serial.println("[WARN] La simulación NO está activa. Las lecturas fallarán.");
    } else {
         Serial.println("[INFO] Sensor DHT inicializado correctamente.");
    }
    if(simulateSensors) Serial.println("[INFO] LA SIMULACIÓN DE SENSORES ESTÁ ACTIVA.");
}

// Configura el pin de la bomba
void setupBomba() {
    Serial.println("[SETUP] Configurando pin de la bomba (D3)...");
    pinMode(BOMBA_PIN, OUTPUT);
    digitalWrite(BOMBA_PIN, LOW);
    pumpActivated = false;
    pumpAutoOff = false;
}

// Obtiene la humedad (real o simulada)
float getHumidity() {
    if (simulateSensors) {
        simulatedHumidity += (random(-20, 21) / 10.0); // +/- 2.0
        if (simulatedHumidity < 30) simulatedHumidity = 30 + (random(0, 50) / 10.0);
        if (simulatedHumidity > 95) simulatedHumidity = 95 - (random(0, 50) / 10.0);
        return simulatedHumidity;
    } else {
        float h = dht.readHumidity();
        int retry = 0;
        while (isnan(h) && retry < 3) {
             Serial.println("[WARN] Falla lectura de humedad, reintentando...");
             delay(500);
             h = dht.readHumidity();
             retry++;
        }
        if (isnan(h)) {
             Serial.println("[ERROR] Falla lectura de humedad después de reintentos.");
             return -1.0; // Error indicator
        }
        return h;
    }
}

// Obtiene la temperatura (real o simulada)
float getTemperature() {
    if (simulateSensors) {
        simulatedTemperature += (random(-10, 11) / 10.0); // +/- 1.0
        if (simulatedTemperature < 15) simulatedTemperature = 15 + (random(0, 20) / 10.0);
        if (simulatedTemperature > 35) simulatedTemperature = 35 - (random(0, 20) / 10.0);
        return simulatedTemperature;
    } else {
        float t = dht.readTemperature();
        int retry = 0;
        while (isnan(t) && retry < 3) {
             Serial.println("[WARN] Falla lectura de temperatura, reintentando...");
             delay(500);
             t = dht.readTemperature();
             retry++;
        }
        if (isnan(t)) {
             Serial.println("[ERROR] Falla lectura de temperatura después de reintentos.");
             return -99.0; // Error indicator
        }
        return t;
    }
}

// Calcula el VPD (Déficit de Presión de Vapor) en kPa
float calculateVPD(float temperature, float humidity) {
    if (temperature <= -90.0 || humidity < 0.0) return -1.0; // Invalid input
    float clampedHumidity = (humidity > 100.0) ? 100.0 : humidity;
    float svp = 0.6108 * exp((17.27 * temperature) / (temperature + 237.3));
    float avp = (clampedHumidity / 100.0) * svp;
    float vpd = svp - avp;
    return (vpd < 0) ? 0.0 : vpd;
}

// Determina el índice de la etapa actual basado en días transcurridos (RTC)
int getCurrentStageIndex(unsigned long daysElapsed) {
    if (manualStageControl) return manualStageIndex;
    unsigned long cumulativeDays = 0;
    // Uses the potentially modified 'stages' array
    for (int i = 0; i < numStages; i++) {
        cumulativeDays += stages[i].duration_days;
        if (daysElapsed <= cumulativeDays) return i;
    }
    return numStages - 1; // Stay in last stage if time exceeds total duration
}

// Activa la bomba con auto-apagado
void activatePump(unsigned long durationMs) {
    if (pumpActivated) {
        Serial.println("[WARN] Bomba ya activada. Ignorando.");
        return;
    }
    if (durationMs <= 0) {
        Serial.println("[WARN] Duración de riego inválida. No se activará.");
        return;
    }
    Serial.print("[ACCION] Activando bomba por "); Serial.print(durationMs / 1000); Serial.println(" segundos (Auto-Off).");
    digitalWrite(BOMBA_PIN, HIGH);
    pumpActivated = true;
    pumpAutoOff = true;
    pumpOnTime = millis();
    pumpDurationMs = durationMs;
    pumpSecondsCount = 0;
    lastSecondPrint = millis();
    pumpActivationCount++;
    Serial.print("[INFO] Contador activaciones bomba: "); Serial.println(pumpActivationCount);
}

// Desactiva la bomba
void deactivatePump() {
    if (!pumpActivated) return;
    Serial.println("[ACCION] Desactivando bomba.");
    digitalWrite(BOMBA_PIN, LOW);
    pumpActivated = false;
    pumpAutoOff = false;
    pumpDurationMs = 0;
    pumpOnTime = 0;
    pumpSecondsCount = 0;
}

// Carga el intervalo de medición desde archivo
void loadMeasurementInterval() {
    File file = LittleFS.open("/interval.txt", "r");
    if (!file) {
        Serial.println("[INFO] No 'interval.txt', usando default (3h).");
        measurementInterval = 3;
        saveMeasurementInterval(measurementInterval);
        return;
    }
    String valStr = file.readStringUntil('\n');
    file.close();
    valStr.trim();
    int val = valStr.toInt();
    if (val > 0 && val < 168) {
        measurementInterval = val;
        Serial.print("[INFO] Intervalo cargado: "); Serial.print(measurementInterval); Serial.println("h.");
    } else {
        Serial.print("[WARN] Intervalo inválido ('"); Serial.print(valStr); Serial.println("'). Usando default (3h).");
        measurementInterval = 3;
        saveMeasurementInterval(measurementInterval);
    }
}

// Guarda el intervalo de medición en archivo
void saveMeasurementInterval(int interval) {
    if (interval <= 0 || interval >= 168) {
         Serial.print("[ERROR] Intento guardar intervalo inválido: "); Serial.println(interval);
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
    Serial.print("[INFO] Intervalo guardado: "); Serial.print(measurementInterval); Serial.println("h.");

    // Recalcular próxima medición basado en la última medición REALIZADA (usando millis estimado)
    unsigned long lastEventTimeMillis = startTime; // Default to boot time
    if (lastMeasurementTimestamp > (uint64_t)rtcBootEpoch * 1000ULL) {
        // Estimate millis corresponding to last measurement epoch
        uint64_t msSinceBootAtLastMeasurement = lastMeasurementTimestamp - (uint64_t)rtcBootEpoch * 1000ULL;
        lastEventTimeMillis = startTime + (unsigned long)(msSinceBootAtLastMeasurement);
    }
    nextMeasureTimestamp = lastEventTimeMillis + (measurementInterval * 3600000UL);
    Serial.print("[INFO] Próxima medición recalculada para millis ~: "); Serial.println(nextMeasureTimestamp);
}

// Carga credenciales WiFi desde archivo
void loadWifiCredentials() {
    File file = LittleFS.open("/WifiConfig.txt", "r");
    if (!file || !file.available()) {
        if(file) file.close();
        Serial.println("[INFO] No 'WifiConfig.txt' o vacío. No autoconexión.");
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
    bool useStaticIP = true; // Descomentar y poner true para forzar IP estática
    if (useStaticIP && !WiFi.config(ip, gateway, subnet)) {
        Serial.println("[WARN] Falló aplicación IP estática.");
    }
    WiFi.begin(ssid.c_str(), password.c_str());
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500); Serial.print("."); attempts++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n[INFO] Conexión WiFi exitosa.");
        Serial.print("[INFO] IP: "); Serial.println(WiFi.localIP());
        Serial.print("[INFO] RSSI: "); Serial.print(WiFi.RSSI()); Serial.println(" dBm");
    } else {
        Serial.println("\n[ERROR] Falló conexión WiFi con credenciales guardadas.");
        WiFi.disconnect();
    }
}

// Carga etapa manual desde archivo
void loadManualStage() {
    File file = LittleFS.open("/ManualStage.txt", "r");
    if (!file || !file.available()) {
        if(file) file.close();
        Serial.println("[INFO] No 'ManualStage.txt' o vacío. Control automático.");
        manualStageControl = false;
        manualStageIndex = 0;
        return;
    }
    String stageStr = file.readStringUntil('\n');
    file.close();
    stageStr.trim();
    int stageIdx = stageStr.toInt();
    if (stageIdx >= 0 && stageIdx < numStages) {
        manualStageIndex = stageIdx;
        manualStageControl = true;
        Serial.print("[INFO] Control manual cargado. Etapa: "); Serial.println(stages[manualStageIndex].name);
    } else {
        Serial.print("[WARN] Índice etapa manual inválido ('"); Serial.print(stageStr); Serial.println("'). Usando control automático.");
        manualStageControl = false;
        manualStageIndex = 0;
        LittleFS.remove("/ManualStage.txt");
    }
}

// Guarda etapa manual en archivo
void saveManualStage(int index) {
     if (index < 0 || index >= numStages) {
         Serial.print("[ERROR] Intento guardar índice etapa manual inválido: "); Serial.println(index);
         return;
     }
     File file = LittleFS.open("/ManualStage.txt", "w");
     if (!file) {
         Serial.println("[ERROR] No se pudo abrir 'ManualStage.txt' para escritura.");
         return;
     }
     file.println(index);
     file.close();
     manualStageIndex = index;
     manualStageControl = true;
     Serial.print("[INFO] Etapa manual guardada: "); Serial.println(stages[manualStageIndex].name);
}

// Carga historial desde archivo
String loadMeasurements() {
    File file = LittleFS.open("/Measurements.txt", "r");
    if (!file || !file.available()) {
        if(file) file.close();
        Serial.println("[INFO] No 'Measurements.txt' o vacío.");
        return "";
    }
    String measurementsStr = file.readString();
    file.close();
    Serial.println("[INFO] Historial cargado desde archivo.");
    return measurementsStr;
}

// Guarda TODO el historial actual en archivo (sobrescribe)
void saveMeasurementFile(const String& allMeasurementsString) {
    File file = LittleFS.open("/Measurements.txt", "w");
    if (!file) {
        Serial.println("[ERROR] No se pudo abrir 'Measurements.txt' para escritura.");
        return;
    }
    size_t bytesWritten = file.print(allMeasurementsString);
    file.close();
    if (bytesWritten != allMeasurementsString.length()) {
         Serial.println("[ERROR] Error al escribir historial completo en 'Measurements.txt'.");
    }
}

// Parsea string de historial (formato {j1},{j2},..) en array
int parseData(String input, String output[]) {
    int count = 0;
    int startIndex = 0;
    int endIndex = 0;
    input.trim();
    while (startIndex < input.length() && count < MAX_JSON_OBJECTS) {
        startIndex = input.indexOf('{', startIndex);
        if (startIndex == -1) break;
        endIndex = input.indexOf('}', startIndex);
        if (endIndex == -1) break; // Malformed
        output[count++] = input.substring(startIndex, endIndex + 1);
        startIndex = endIndex + 1;
        if (startIndex < input.length() && input.charAt(startIndex) == ',') startIndex++;
        while(startIndex < input.length() && isspace(input.charAt(startIndex))) startIndex++;
    }
    return count;
}

// Convierte array de historial a String (formato {j1},{j2},..)
String arrayToString(String array[], size_t arraySize) {
    String result = "";
    bool first = true;
    for (size_t i = 0; i < arraySize; i++) {
        if (array[i] != nullptr && array[i].length() > 2) { // Basic validity { }
            if (!first) result += ",";
            result += array[i];
            first = false;
        }
    }
    return result;
}

// Guarda una nueva medición en array y archivo (con deslizamiento)
void saveMeasurement(const String& jsonString) {
    Serial.println("[ACCION] Guardando nueva medición:");
    Serial.println(jsonString);
    if (!jsonString.startsWith("{") || !jsonString.endsWith("}")) {
        Serial.println("[ERROR] Intento guardar medición inválida (no JSON object).");
        return;
    }
    if (jsonIndex < MAX_JSON_OBJECTS) {
        measurements[jsonIndex++] = jsonString;
    } else {
        Serial.println("[WARN] Array mediciones lleno. Desplazando...");
        for (int i = 0; i < MAX_JSON_OBJECTS - 1; i++) measurements[i] = measurements[i + 1];
        measurements[MAX_JSON_OBJECTS - 1] = jsonString;
    }
    saveMeasurementFile(arrayToString(measurements, jsonIndex));
}

// Formatea array de historial a JSON Array String "[{j1},{j2},..]"
void formatMeasurementsToString(String& formattedString) {
    formattedString = "[";
    bool first = true;
    for (int i = 0; i < jsonIndex; i++) {
        if (measurements[i] != nullptr && measurements[i].length() > 2) {
            if (!first) formattedString += ",";
            formattedString += measurements[i];
            first = false;
        }
    }
    formattedString += "]";
}


// NEW: Load custom stage configuration from LittleFS
void loadStagesConfig() {
    Serial.print("[SETUP] Intentando cargar configuración de etapas desde "); Serial.println(STAGES_CONFIG_FILE);
    if (!LittleFS.exists(STAGES_CONFIG_FILE)) {
        Serial.println("[INFO] No existe archivo de configuración de etapas. Usando defaults.");
        return;
    }

    File configFile = LittleFS.open(STAGES_CONFIG_FILE, "r");
    if (!configFile) {
        Serial.println("[ERROR] No se pudo abrir el archivo de configuración de etapas para lectura.");
        return;
    }

    // Increase size if necessary, depends on stage names and number of stages
    StaticJsonDocument<1024> doc; // Adjust size as needed
    DeserializationError error = deserializeJson(doc, configFile);
    configFile.close();

    if (error) {
        Serial.print(F("[ERROR] Falló al deserializar stages_config.json: "));
        Serial.println(error.f_str());
        Serial.println("[WARN] Usando configuración de etapas default.");
        // Optional: Delete corrupted file?
        // LittleFS.remove(STAGES_CONFIG_FILE);
        return;
    }

    if (!doc.is<JsonArray>()) {
        Serial.println("[ERROR] stages_config.json no contiene un array JSON. Usando defaults.");
        return;
    }

    JsonArray loadedStages = doc.as<JsonArray>();
    if (loadedStages.size() != numStages) {
         Serial.print("[WARN] El número de etapas en config (");
         Serial.print(loadedStages.size());
         Serial.print(") no coincide con el default (");
         Serial.print(numStages);
         Serial.println("). Usando defaults.");
         // Optional: Delete mismatched file?
         // LittleFS.remove(STAGES_CONFIG_FILE);
         return;
    }

    int updatedCount = 0;
    // Update the global 'stages' array with loaded values
    for (int i = 0; i < numStages; ++i) {
        JsonObject loadedStage = loadedStages[i];
        if (!loadedStage) continue;

        // Verify name matches to prevent issues if default order changes
        const char* loadedName = loadedStage["name"];
        if (loadedName && strcmp(loadedName, stages[i].name) == 0) {
            // Update editable fields if present and valid
            if (loadedStage.containsKey("humidityThreshold")) {
                int newThreshold = loadedStage["humidityThreshold"];
                if (newThreshold >= 0 && newThreshold <= 100) {
                    stages[i].humidityThreshold = newThreshold;
                } else {
                    Serial.print("[WARN] Umbral humedad inválido para '"); Serial.print(stages[i].name); Serial.println("' en config. Usando default.");
                }
            }
            if (loadedStage.containsKey("wateringTimeSec")) {
                int newWateringTime = loadedStage["wateringTimeSec"];
                 if (newWateringTime > 0 && newWateringTime <= 600) { // Example validation
                    stages[i].wateringTimeSec = newWateringTime;
                 } else {
                     Serial.print("[WARN] Tiempo riego inválido para '"); Serial.print(stages[i].name); Serial.println("' en config. Usando default.");
                 }
            }
            // Add duration_days update here if you want it editable later
            updatedCount++;
        } else {
            Serial.print("[WARN] Discrepancia de nombre o entrada inválida en índice ");
            Serial.print(i); Serial.print(" en config. Usando default para '");
            Serial.print(stages[i].name); Serial.println("'.");
             // If names mismatch, probably safest to revert to all defaults
             // loadStagesConfig(); // Re-read defaults essentially (or handle differently)
        }
    }

    if (updatedCount == numStages) {
        Serial.println("[INFO] Configuración de etapas personalizada cargada correctamente.");
    } else {
         Serial.println("[WARN] Configuración de etapas cargada parcialmente debido a errores/discrepancias.");
    }
}

// NEW: Save the current state of the 'stages' array to LittleFS
bool saveStagesConfig() {
    Serial.print("[ACTION] Guardando configuración de etapas en "); Serial.println(STAGES_CONFIG_FILE);
    StaticJsonDocument<1024> doc; // Adjust size as needed
    JsonArray stagesArray = doc.to<JsonArray>();

    // Populate the JSON array from the current 'stages' data
    for (int i = 0; i < numStages; ++i) {
        JsonObject stageObj = stagesArray.createNestedObject();
        stageObj["name"] = stages[i].name; // Include name for verification on load
        stageObj["humidityThreshold"] = stages[i].humidityThreshold;
        stageObj["wateringTimeSec"] = stages[i].wateringTimeSec;
        // Add duration_days here if it becomes editable
        // stageObj["duration_days"] = stages[i].duration_days;
    }

    File configFile = LittleFS.open(STAGES_CONFIG_FILE, "w");
    if (!configFile) {
        Serial.println("[ERROR] No se pudo abrir archivo de configuración de etapas para escritura.");
        return false;
    }

    size_t bytesWritten = serializeJson(doc, configFile);
    configFile.close();

    if (bytesWritten > 0) {
        Serial.println("[INFO] Configuración de etapas guardada correctamente.");
        return true;
    } else {
        Serial.println("[ERROR] Falló al escribir configuración de etapas en archivo.");
        return false;
    }
}

// Lógica principal de control (sensores, decisión riego, registro)
void controlIndependiente() {
    unsigned long startMillis = millis();
    DateTime dtNow   = rtc.now();
    uint64_t epochMs = (uint64_t)dtNow.unixtime() * 1000ULL;
    uint32_t elapsedDays = (dtNow.unixtime() - rtcBootEpoch) / 86400UL;
    static uint32_t failureStartTimeEpoch = 0; // Epoch de inicio de fallo sensor
    static bool previousSensorValid = true;

    Serial.println("\n[CONTROL] Iniciando ciclo...");
    char buf[20];
    sprintf(buf, "%04u-%02u-%02u %02u:%02u:%02u", dtNow.year(), dtNow.month(), dtNow.day(), dtNow.hour(), dtNow.minute(), dtNow.second());
    Serial.print("[INFO] Hora: "); Serial.print(buf); Serial.print(", Dia: "); Serial.println(elapsedDays);

    // 1. Leer sensores
    float humidity    = getHumidity();
    float temperature = getTemperature();
    lastMeasurementTimestamp = epochMs; // Actualizar timestamp de última medición

    // 2. Validar sensor y manejar fallos
    bool sensorValid = !(humidity < 0.0 || temperature <= -90.0);
    if (!sensorValid) {
        if (previousSensorValid) {
            Serial.println("[WARN] Falla sensor detectada.");
            failureStartTimeEpoch = dtNow.unixtime(); // Marcar inicio fallo
        }
    } else {
        if (!previousSensorValid) Serial.println("[INFO] Sensor recuperado.");
        failureStartTimeEpoch = 0; // Resetear si válido
    }
    previousSensorValid = sensorValid;

    // 3. Determinar etapa y parámetros (uses potentially modified 'stages' array)
    int stageIndex = getCurrentStageIndex(elapsedDays);
    const Stage& currentStage = stages[stageIndex];
    int currentThreshold = currentStage.humidityThreshold; // Use potentially modified threshold
    unsigned long wateringTimeMs = currentStage.wateringTimeSec * 1000UL; // Use potentially modified watering time
    Serial.print("[INFO] Etapa: "); Serial.print(currentStage.name); Serial.print(" ("); Serial.print(manualStageControl ? "Manual" : "Auto"); Serial.println(")");
    Serial.print("       Umbral: "); Serial.print(currentThreshold); Serial.print("%, Riego: "); Serial.print(currentStage.wateringTimeSec); Serial.println("s");

    // 4. Decidir riego (solo si bomba no está en auto-off)
    bool needsWatering = false;
    if (!pumpAutoOff) {
        if (sensorValid && humidity < currentThreshold) { // Uses currentThreshold
            Serial.println("[DECISION] Humedad bajo umbral -> Regar.");
            needsWatering = true;
        } else if (!sensorValid && failureStartTimeEpoch > 0) {
            uint32_t downTimeSeconds = dtNow.unixtime() - failureStartTimeEpoch;
            if (downTimeSeconds >= 86400UL) { // >= 24h
                Serial.println("[DECISION] Falla sensor >= 24h -> Forzar riego.");
                needsWatering = true;
                failureStartTimeEpoch = dtNow.unixtime(); // Resetear para evitar riego continuo
            } else {
                Serial.print("[DECISION] Sensor inválido < 24h ("); Serial.print(downTimeSeconds / 3600); Serial.println("h). No regar.");
            }
        } else if (sensorValid) {
             Serial.println("[DECISION] Humedad OK. No regar.");
        } else {
             Serial.println("[DECISION] Sensor inválido (1ra vez). No regar.");
        }
        // Actuar solo si es necesario y bomba no está ya ON
        if (needsWatering && !pumpActivated) activatePump(wateringTimeMs); // Uses wateringTimeMs
    } else {
        Serial.println("[INFO] Bomba en Auto-Off. Omitiendo decisión riego.");
    }

    // 5. Registrar medición
    StaticJsonDocument<256> doc;
    if (sensorValid) {
        doc["temperature"] = serialized(String(temperature, 1));
        doc["humidity"]    = serialized(String(humidity, 1));
    } else {
        doc["temperature"] = nullptr;
        doc["humidity"]    = nullptr;
    }
    doc["pumpActivated"] = pumpActivated; // Estado REAL de la bomba al guardar
    doc["stage"]         = currentStage.name;
    doc["epoch_ms"]      = epochMs;
    String measurementString;
    serializeJson(doc, measurementString);
    saveMeasurement(measurementString);

    unsigned long duration = millis() - startMillis;
    Serial.print("[CONTROL] Ciclo finalizado en "); Serial.print(duration); Serial.println(" ms.");
}


// --- Handlers HTTP ---

// Handler para / - Redirige a index.html o config.html
void handleRoot() {
    Serial.println("[HTTP] Solicitud / recibida.");
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[HTTP] WiFi no conectado. Redirigiendo a /config.html");
        server.sendHeader("Location", "/config.html", true);
        server.send(302, "text/plain", "Redirecting to config...");
    } else {
        Serial.println("[HTTP] WiFi conectado. Redirigiendo a /index.html");
        server.sendHeader("Location", "/index.html", true);
        server.send(302, "text/plain", "Redirecting to dashboard...");
    }
    // El navegador hará una NUEVA solicitud a la URL redirigida,
    // que será manejada por onNotFound.
}

// Handler para /data - Datos principales y estado
void handleData() {
    Serial.println("[HTTP] Solicitud /data recibida.");
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");

    float humidity = getHumidity();
    float temperature = getTemperature();
    float vpd = calculateVPD(temperature, humidity);
    DateTime dtNow = rtc.now();
    uint64_t epochNowMs = (uint64_t)dtNow.unixtime() * 1000ULL;
    uint32_t elapsedSeconds = dtNow.unixtime() - rtcBootEpoch;
    uint32_t elapsedDays = elapsedSeconds / 86400;
    int stageIndex = getCurrentStageIndex(elapsedDays);
    const Stage& currentStage = stages[stageIndex]; // Use potentially modified stage data

    StaticJsonDocument<512> doc;
    if (temperature > -90.0) doc["temperature"] = serialized(String(temperature, 1)); else doc["temperature"] = nullptr;
    if (humidity >= 0.0) doc["humidity"] = serialized(String(humidity, 1)); else doc["humidity"] = nullptr;
    if (vpd >= 0.0) doc["vpd"] = serialized(String(vpd, 2)); else doc["vpd"] = nullptr;
    doc["pumpStatus"] = pumpActivated;
    doc["pumpActivationCount"] = pumpActivationCount;
    doc["elapsedTime"] = elapsedSeconds;
    doc["currentTime"] = epochNowMs;
    doc["lastMeasurementTimestamp"] = lastMeasurementTimestamp;
    doc["currentStageName"] = currentStage.name;
    doc["currentStageIndex"] = stageIndex;
    doc["currentStageThreshold"] = currentStage.humidityThreshold; // Return current threshold
    doc["currentStageWateringSec"] = currentStage.wateringTimeSec; // Return current watering time
    doc["manualStageControl"] = manualStageControl;
    if (WiFi.status() == WL_CONNECTED) {
       doc["deviceIP"] = WiFi.localIP().toString();
       doc["wifiRSSI"] = WiFi.RSSI();
    } else {
        doc["deviceIP"] = (WiFi.getMode() == WIFI_AP) ? WiFi.softAPIP().toString() : "N/A";
        doc["wifiRSSI"] = 0;
    }
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

// Handler para /wifiList - Listar redes WiFi
void handleWifiListRequest() {
    Serial.println("[HTTP] Solicitud /wifiList. Escaneando...");
    server.sendHeader("Access-Control-Allow-Origin", "*");
    int numNetworks = WiFi.scanNetworks();
    Serial.print("[INFO] Redes encontradas: "); Serial.println(numNetworks);
    if (numNetworks == -1) {
         server.send(500, "application/json", "{\"error\":\"Scan failed\"}"); return;
    }
    if (numNetworks == 0) {
         server.send(200, "application/json", "[]"); return;
    }
    DynamicJsonDocument wifiJson(numNetworks * 70 + 50);
    JsonArray networks = wifiJson.to<JsonArray>();
    int maxNetworksToSend = (numNetworks > 20) ? 20 : numNetworks;
    for (int i = 0; i < maxNetworksToSend; ++i) {
        JsonObject network = networks.createNestedObject();
        network["ssid"] = WiFi.SSID(i);
        network["rssi"] = WiFi.RSSI(i);
        switch (WiFi.encryptionType(i)) {
            case ENC_TYPE_WEP:  network["encryption"] = "WEP"; break;
            case ENC_TYPE_TKIP: network["encryption"] = "WPA"; break;
            case ENC_TYPE_CCMP: network["encryption"] = "WPA2"; break;
            case ENC_TYPE_NONE: network["encryption"] = "Open"; break;
            case ENC_TYPE_AUTO: network["encryption"] = "Auto"; break;
            default:            network["encryption"] = "Unknown"; break;
        }
    }
    String response;
    serializeJson(wifiJson, response);
    server.send(200, "application/json", response);
}

// Handler para /connectWifi - Conectar manualmente (sin guardar)
void handleConnectWifi() {
    Serial.println("[HTTP] Solicitud /connectWifi (POST).");
    if (!server.hasArg("plain")) { server.send(400, "text/plain", "Bad Request: Missing body"); return; }
    String body = server.arg("plain");
    StaticJsonDocument<200> doc;
    if (deserializeJson(doc, body)) { server.send(400, "text/plain", "Bad Request: Invalid JSON"); return; }
    const char* ssid_c = doc["ssid"];
    if (!ssid_c || strlen(ssid_c) == 0) { server.send(400, "text/plain", "Bad Request: Missing SSID"); return; }
    String ssid = String(ssid_c);
    String password = doc["password"] | ""; // Default a "" si no existe

    Serial.print("[ACCION] Intentando conectar (manual): '"); Serial.print(ssid); Serial.println("'...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) { delay(500); Serial.print("."); attempts++; }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n[INFO] Conexión WiFi manual exitosa.");
        dnsServer.stop();
        server.send(200, "application/json", "{\"status\":\"success\", \"ip\":\"" + WiFi.localIP().toString() + "\"}");
    } else {
        Serial.println("\n[ERROR] Conexión WiFi manual falló.");
        WiFi.disconnect(false);
        if (!LittleFS.exists("/WifiConfig.txt")) startAPMode(); // Volver a AP si no hay creds guardadas
        server.send(401, "application/json", "{\"status\":\"failed\", \"message\":\"Connection failed\"}");
    }
}

// Handler para /saveWifiCredentials - Validar, guardar y reiniciar
void handleSaveWifiCredentials() {
     Serial.println("[HTTP] Solicitud /saveWifiCredentials (POST).");
     if (!server.hasArg("plain")) { server.send(400, "text/plain", "Bad Request: Missing body"); return; }
     String body = server.arg("plain");
     StaticJsonDocument<200> doc;
     if (deserializeJson(doc, body)) { server.send(400, "text/plain", "Bad Request: Invalid JSON"); return; }
     const char* ssid_c = doc["ssid"];
     if (!ssid_c || strlen(ssid_c) == 0) { server.send(400, "text/plain", "Bad Request: Missing SSID"); return; }
     String ssid = String(ssid_c);
     String password = doc["password"] | "";

     Serial.print("[ACCION] Validando y guardando creds para: '"); Serial.print(ssid); Serial.println("'...");
     WiFi.mode(WIFI_STA);
     WiFi.begin(ssid.c_str(), password.c_str());
     int attempts = 0;
     while (WiFi.status() != WL_CONNECTED && attempts < 20) { delay(500); Serial.print("."); attempts++; }

     if (WiFi.status() == WL_CONNECTED) {
         Serial.println("\n[INFO] Conexión OK. Guardando...");
         File file = LittleFS.open("/WifiConfig.txt", "w");
         if (!file) {
             Serial.println("[ERROR] No se pudo abrir 'WifiConfig.txt' para escritura.");
             server.send(500, "application/json", "{\"status\":\"error\", \"message\":\"Cannot save config\"}");
             return;
         }
         file.println(ssid); file.println(password); file.close();
         Serial.println("[INFO] Creds guardadas. Reiniciando...");
         server.send(200, "application/json", "{\"status\":\"success\", \"message\":\"Credentials saved. Restarting...\"}");
         delay(1000); ESP.restart();
     } else {
         Serial.println("\n[ERROR] Falló conexión con nuevas creds. No guardadas.");
         WiFi.disconnect(false);
         startAPMode(); // Volver a AP
         server.send(401, "application/json", "{\"status\":\"failed\", \"message\":\"Connection failed. Not saved.\"}");
     }
}

// Handler para /loadMeasurement - Cargar historial
void handleLoadMeasurement() {
    Serial.println("[HTTP] Solicitud /loadMeasurement (GET).");
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    String formattedJsonArray;
    formatMeasurementsToString(formattedJsonArray);
    server.send(200, "application/json", formattedJsonArray);
}

// Handler para /clearHistory - Borrar historial
void handleClearMeasurementHistory() {
     Serial.println("[HTTP] Solicitud /clearHistory (POST).");
     server.sendHeader("Access-Control-Allow-Origin", "*");
     Serial.println("[ACCION] Borrando historial...");
     for (int i = 0; i < MAX_JSON_OBJECTS; i++) measurements[i] = "";
     jsonIndex = 0;
     bool removed = LittleFS.remove("/Measurements.txt");
     if (!removed) Serial.println("[ERROR] No se pudo borrar 'Measurements.txt'.");
     // Recrear archivo vacío
     File file = LittleFS.open("/Measurements.txt", "w");
     if (file) { file.print(""); file.close(); } else { Serial.println("[ERROR] No se pudo recrear 'Measurements.txt'."); }
     Serial.println("[INFO] Historial borrado.");
     server.send(200, "application/json", "{\"status\":\"success\"}");
}

// Handler para /getMeasurementInterval y /setMeasurementInterval
void handleMeasurementInterval() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    if (server.method() == HTTP_POST) {
        Serial.println("[HTTP] Solicitud /setMeasurementInterval (POST).");
        if (!server.hasArg("plain")) { server.send(400, "text/plain", "Bad Request: Missing body"); return; }
        StaticJsonDocument<100> doc;
        if (deserializeJson(doc, server.arg("plain"))) { server.send(400, "text/plain", "Bad Request: Invalid JSON"); return; }
        if (!doc.containsKey("interval")) { server.send(400, "text/plain", "Bad Request: Missing 'interval'"); return; }
        int newInterval = doc["interval"];
        if (newInterval > 0 && newInterval < 168) {
            Serial.print("[ACCION] Ajustando intervalo a: "); Serial.print(newInterval); Serial.println("h");
            saveMeasurementInterval(newInterval);
            server.send(200, "application/json", "{\"status\":\"success\"}");
        } else {
            server.send(400, "text/plain", "Bad Request: Interval must be 1-167h");
        }
    } else if (server.method() == HTTP_GET) {
         Serial.println("[HTTP] Solicitud /getMeasurementInterval (GET).");
         StaticJsonDocument<50> doc;
         doc["interval"] = measurementInterval;
         String response; serializeJson(doc, response);
         server.send(200, "application/json", response);
    } else {
         server.send(405, "text/plain", "Method Not Allowed");
    }
}

// Handler para /controlPump - Control manual bomba
void handlePumpControl() {
    Serial.println("[HTTP] Solicitud /controlPump (POST).");
     server.sendHeader("Access-Control-Allow-Origin", "*");
     if (!server.hasArg("plain")) { server.send(400, "text/plain", "Bad Request: Missing body"); return; }
     StaticJsonDocument<128> doc;
     if (deserializeJson(doc, server.arg("plain"))) { server.send(400, "text/plain", "Bad Request: Invalid JSON"); return; }
     if (!doc.containsKey("action")) { server.send(400, "text/plain", "Bad Request: Missing 'action'"); return; }
     String action = doc["action"].as<String>();

     if (action.equalsIgnoreCase("on")) {
         if (!doc.containsKey("duration") || !doc["duration"].is<int>()) { server.send(400, "application/json", "{\"status\":\"error\", \"message\":\"Missing/invalid duration\"}"); return; }
         int durationSec = doc["duration"];
         if (durationSec <= 0 || durationSec > 600) { server.send(400, "application/json", "{\"status\":\"error\", \"message\":\"Invalid duration (1-600s)\"}"); return; }
         Serial.print("[ACCION] Encendiendo bomba (manual) por "); Serial.print(durationSec); Serial.println("s.");
         activatePump(durationSec * 1000UL);
         server.send(200, "application/json", "{\"status\":\"pump_on\"}");
     } else if (action.equalsIgnoreCase("off")) {
         Serial.println("[ACCION] Apagando bomba (manual).");
         deactivatePump();
         server.send(200, "application/json", "{\"status\":\"pump_off\"}");
     } else {
         server.send(400, "application/json", "{\"status\":\"invalid_action\"}");
     }
}

// Handler para /setManualStage - Establecer etapa manual
void handleSetManualStage() {
    Serial.println("[HTTP] Solicitud /setManualStage (POST).");
    server.sendHeader("Access-Control-Allow-Origin", "*");
     if (!server.hasArg("plain")) { server.send(400, "text/plain", "Bad Request: Missing body"); return; }
     StaticJsonDocument<100> doc;
     if (deserializeJson(doc, server.arg("plain"))) { server.send(400, "text/plain", "Bad Request: Invalid JSON"); return; }
     if (!doc.containsKey("stage")) { server.send(400, "text/plain", "Bad Request: Missing 'stage' index"); return; }
     int stageIndex = doc["stage"];
     if (stageIndex >= 0 && stageIndex < numStages) {
         saveManualStage(stageIndex);
         server.send(200, "application/json", "{\"status\":\"success\", \"message\":\"Manual stage set\"}");
     } else {
         server.send(400, "application/json", "{\"status\":\"error\", \"message\":\"Invalid stage index\"}");
     }
}

// Handler para /getCurrentStage - Obtener etapa actual
void handleGetCurrentStage() {
     Serial.println("[HTTP] Solicitud /getCurrentStage (GET).");
     server.sendHeader("Access-Control-Allow-Origin", "*");
     server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
     uint32_t elapsedSeconds = rtc.now().unixtime() - rtcBootEpoch;
     uint32_t elapsedDays = elapsedSeconds / 86400;
     int stageIndex = getCurrentStageIndex(elapsedDays);
     const Stage& currentStage = stages[stageIndex]; // Use potentially modified stage data
     StaticJsonDocument<256> doc;
     doc["currentStage"] = currentStage.name;
     doc["stageIndex"] = stageIndex;
     doc["manualControl"] = manualStageControl;
     JsonObject params = doc.createNestedObject("params");
     params["threshold"] = currentStage.humidityThreshold; // Return current threshold
     params["watering"] = currentStage.wateringTimeSec;     // Return current watering time
     params["duration_days"] = currentStage.duration_days;
     String response; serializeJson(doc, response);
     server.send(200, "application/json", response);
}

// Handler para /resetManualStage - Volver a control automático
void handleResetManualStage() {
     Serial.println("[HTTP] Solicitud /resetManualStage (POST).");
     server.sendHeader("Access-Control-Allow-Origin", "*");
     manualStageControl = false;
     if (LittleFS.exists("/ManualStage.txt")) LittleFS.remove("/ManualStage.txt");
     Serial.println("[INFO] Control manual desactivado.");
     server.send(200, "application/json", "{\"status\":\"success\", \"message\":\"Manual stage reset\"}");
}

// Handler para /listStages - Listar todas las etapas (FROM CURRENT STATE)
void handleListStages() {
    Serial.println("[HTTP] Solicitud /listStages (GET).");
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate"); // Don't cache stage list as it can be edited
    DynamicJsonDocument doc(JSON_ARRAY_SIZE(numStages) + numStages * JSON_OBJECT_SIZE(5) + 100);
    JsonArray stagesArray = doc.to<JsonArray>();
    // Use the potentially modified global 'stages' array
    for (int i = 0; i < numStages; i++) {
        JsonObject stageObj = stagesArray.createNestedObject();
        stageObj["index"] = i;
        stageObj["name"] = stages[i].name;
        stageObj["duration_days"] = stages[i].duration_days;
        stageObj["humidityThreshold"] = stages[i].humidityThreshold; // Return current value
        stageObj["wateringTimeSec"] = stages[i].wateringTimeSec;     // Return current value
    }
    String response; serializeJson(doc, response);
    server.send(200, "application/json", response);
}

// NEW: Handler for /updateStage - Update parameters for a specific stage
void handleUpdateStage() {
    Serial.println("[HTTP] Solicitud /updateStage (POST).");
    server.sendHeader("Access-Control-Allow-Origin", "*");

    if (!server.hasArg("plain")) {
        server.send(400, "text/plain", "Bad Request: Missing body");
        return;
    }
    String body = server.arg("plain");
    StaticJsonDocument<256> doc; // JSON should be small for a single stage update
    DeserializationError error = deserializeJson(doc, body);

    if (error) {
        server.send(400, "text/plain", "Bad Request: Invalid JSON");
        return;
    }

    if (!doc.containsKey("index") || !doc["index"].is<int>()) {
        server.send(400, "text/plain", "Bad Request: Missing or invalid 'index'");
        return;
    }
    int index = doc["index"];

    if (index < 0 || index >= numStages) {
        server.send(400, "text/plain", "Bad Request: Invalid stage index");
        return;
    }

    // Validate and update humidityThreshold
    if (!doc.containsKey("humidityThreshold") || !doc["humidityThreshold"].is<int>()) {
        server.send(400, "text/plain", "Bad Request: Missing or invalid 'humidityThreshold'");
        return;
    }
    int newThreshold = doc["humidityThreshold"];
    if (newThreshold < 0 || newThreshold > 100) {
        server.send(400, "text/plain", "Bad Request: Invalid humidity threshold (0-100)");
        return;
    }

    // Validate and update wateringTimeSec
    if (!doc.containsKey("wateringTimeSec") || !doc["wateringTimeSec"].is<int>()) {
        server.send(400, "text/plain", "Bad Request: Missing or invalid 'wateringTimeSec'");
        return;
    }
    int newWateringTime = doc["wateringTimeSec"];
    if (newWateringTime <= 0 || newWateringTime > 600) { // Example range validation
        server.send(400, "text/plain", "Bad Request: Invalid watering time (1-600s)");
        return;
    }

    // Update the in-memory stage data
    stages[index].humidityThreshold = newThreshold;
    stages[index].wateringTimeSec = newWateringTime;
    Serial.print("[ACCION] Etapa '"); Serial.print(stages[index].name);
    Serial.print("' actualizada -> Umbral: "); Serial.print(newThreshold);
    Serial.print("%, Riego: "); Serial.print(newWateringTime); Serial.println("s");

    // Save the updated configuration to file
    if (saveStagesConfig()) {
        server.send(200, "application/json", "{\"status\":\"success\", \"message\":\"Stage updated and saved\"}");
    } else {
        server.send(500, "application/json", "{\"status\":\"error\", \"message\":\"Stage updated in memory, but failed to save to file\"}");
    }
}


// Inicia modo AP
void startAPMode() {
    const char* ap_ssid = "GreenNanny-Setup";
    const char* ap_password = "password123";
    Serial.print("[ACCION] Iniciando Modo AP: SSID '"); Serial.print(ap_ssid); Serial.println("'...");
    WiFi.mode(WIFI_AP);
    if(WiFi.softAP(ap_ssid, ap_password)) {
        IPAddress apIP = WiFi.softAPIP();
        Serial.print("[INFO] AP iniciado. IP: http://"); Serial.println(apIP);
        if (dnsServer.start(DNS_PORT, "*", apIP)) Serial.println("[INFO] Servidor DNS cautivo iniciado.");
        else Serial.println("[ERROR] Falló inicio servidor DNS.");
    } else {
        Serial.println("[ERROR] Falló inicio AP!");
    }
}

// Configura servidor web y endpoints
void setupServer() {
    Serial.println("[SETUP] Configurando servidor web...");

    // --- Página Principal (Redirección) ---
    server.on("/", HTTP_GET, handleRoot);

    // --- Endpoints API GET ---
    server.on("/data", HTTP_GET, handleData);
    server.on("/loadMeasurement", HTTP_GET, handleLoadMeasurement);
    server.on("/getMeasurementInterval", HTTP_GET, handleMeasurementInterval);
    server.on("/getCurrentStage", HTTP_GET, handleGetCurrentStage);
    server.on("/listStages", HTTP_GET, handleListStages); // Returns current/modified stages
    server.on("/wifiList", HTTP_GET, handleWifiListRequest);

    // --- Endpoints API POST ---
    server.on("/clearHistory", HTTP_POST, handleClearMeasurementHistory);
    server.on("/connectWifi", HTTP_POST, handleConnectWifi);
    server.on("/saveWifiCredentials", HTTP_POST, handleSaveWifiCredentials);
    server.on("/controlPump", HTTP_POST, handlePumpControl);
    server.on("/setMeasurementInterval", HTTP_POST, handleMeasurementInterval);
    server.on("/setManualStage", HTTP_POST, handleSetManualStage);
    server.on("/resetManualStage", HTTP_POST, handleResetManualStage);
    server.on("/updateStage", HTTP_POST, handleUpdateStage); // NEW: Endpoint to update stage config

    // --- Acciones Directas (POST) ---
    server.on("/takeMeasurement", HTTP_POST, [](){
         Serial.println("[HTTP] Solicitud /takeMeasurement (POST).");
         server.sendHeader("Access-Control-Allow-Origin", "*");
         controlIndependiente();
         server.send(200, "application/json", "{\"status\":\"success\", \"message\":\"Measurement triggered\"}");
    });
    server.on("/restartSystem", HTTP_POST, []() {
        Serial.println("[HTTP] Solicitud /restartSystem (POST).");
        server.sendHeader("Access-Control-Allow-Origin", "*");
        server.send(200, "application/json", "{\"status\":\"restarting\"}");
        Serial.println("[ACCION] Reiniciando sistema...");
        delay(500); ESP.restart();
    });

    // --- Servir Archivos Estáticos (onNotFound con Gzip) ---
    server.onNotFound([]() {
        String path = server.uri();
        if (path.indexOf("..") != -1) { server.send(400, "text/plain", "Bad Request"); return; }
        Serial.print("[HTTP Static] Request: "); Serial.println(path);
        if (path.endsWith("/")) path += "index.html";

        String contentType = "text/plain";
        if (path.endsWith(".html")) contentType = "text/html";
        else if (path.endsWith(".css")) contentType = "text/css";
        else if (path.endsWith(".js")) contentType = "application/javascript";
        else if (path.endsWith(".png")) contentType = "image/png";
        else if (path.endsWith(".jpg") || path.endsWith(".jpeg")) contentType = "image/jpeg";
        else if (path.endsWith(".ico")) contentType = "image/x-icon";
        else if (path.endsWith(".svg")) contentType = "image/svg+xml";
        else if (path.endsWith(".json")) contentType = "application/json";

        String pathWithGz = path + ".gz";
        bool clientAcceptsGzip = server.hasHeader("Accept-Encoding") && server.header("Accept-Encoding").indexOf("gzip") != -1;

        if (clientAcceptsGzip && LittleFS.exists(pathWithGz)) {
            Serial.print("[HTTP Static] Serving Gzipped: "); Serial.println(pathWithGz);
            File file = LittleFS.open(pathWithGz, "r");
            if (file) {
                server.sendHeader("Content-Encoding", "gzip");
                server.sendHeader("Cache-Control", "max-age=86400"); // Cache 1 day
                server.streamFile(file, contentType);
                file.close(); return;
            } else { Serial.print("[ERROR] Failed open Gzipped: "); Serial.println(pathWithGz); }
        }

        if (LittleFS.exists(path)) {
            Serial.print("[HTTP Static] Serving Ungzipped: "); Serial.println(path);
            File file = LittleFS.open(path, "r");
            if (file) {
                server.sendHeader("Cache-Control", "max-age=86400"); // Cache 1 day
                server.streamFile(file, contentType);
                file.close(); return;
            } else { Serial.print("[ERROR] Failed open Ungzipped: "); Serial.println(path); }
        }

        Serial.print("[HTTP Static] File Not Found: "); Serial.println(path);
        if (WiFi.getMode() == WIFI_AP) {
             Serial.println("[CAPTIVE] Redirecting to configuration page (/).");
             server.sendHeader("Location", "/", true);
             server.send(302, "text/plain", "Redirecting...");
        } else {
            server.send(404, "text/plain", "404 Not Found");
        }
    });

    server.begin();
    Serial.println("[INFO] Servidor HTTP iniciado en puerto 80.");
}

// Maneja comandos seriales (opcional)
void handleSerialCommands() {
    if (Serial.available() > 0) {
        String command = Serial.readStringUntil('\n'); command.trim();
        Serial.print("[SERIAL] Comando: "); Serial.println(command);
        if (command.equalsIgnoreCase("STATUS")) {
             DateTime dtNow = rtc.now(); uint32_t elapsedSeconds = dtNow.unixtime() - rtcBootEpoch;
             Serial.println("--- SERIAL STATUS ---");
             char buf[20]; sprintf(buf, "%04u-%02u-%02u %02u:%02u:%02u", dtNow.year(), dtNow.month(), dtNow.day(), dtNow.hour(), dtNow.minute(), dtNow.second());
             Serial.print("RTC: "); Serial.println(buf);
             Serial.print("Uptime: "); Serial.print(elapsedSeconds / 86400); Serial.print("d "); Serial.print((elapsedSeconds % 86400) / 3600); Serial.print("h "); Serial.print((elapsedSeconds % 3600) / 60); Serial.println("m");
             float h = getHumidity(); float t = getTemperature(); Serial.print("Sensor: T="); Serial.print(t, 1); Serial.print("C H="); Serial.print(h, 1); Serial.println("%");
             Serial.print("Pump: "); Serial.print(pumpActivated ? "ON" : "OFF"); if (pumpAutoOff) Serial.print(" (Auto)"); Serial.println();
             int stageIdx = getCurrentStageIndex(elapsedSeconds / 86400); Serial.print("Stage: "); Serial.print(stages[stageIdx].name); Serial.print(" ("); Serial.print(manualStageControl ? "Manual" : "Auto"); Serial.print(") - Threshold: "); Serial.print(stages[stageIdx].humidityThreshold); Serial.print("%, Water: "); Serial.print(stages[stageIdx].wateringTimeSec); Serial.println("s");
             Serial.print("WiFi: "); Serial.println(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : (WiFi.getMode() == WIFI_AP ? "AP Mode" : "Disconnected"));
             Serial.print("History: "); Serial.println(jsonIndex);
             unsigned long remainingMs = (nextMeasureTimestamp > millis()) ? (nextMeasureTimestamp - millis()) : 0; Serial.print("Next ~: "); Serial.print(remainingMs / 3600000UL); Serial.print("h "); Serial.print((remainingMs % 3600000UL) / 60000UL); Serial.println("m");
             Serial.print("Heap: "); Serial.println(ESP.getFreeHeap());
             Serial.println("--- Current Stages ---");
             for(int i=0; i<numStages; i++) { Serial.printf(" [%d] %s: Dur=%dd, Thr=%d%%, Wat=%ds\n", i, stages[i].name, stages[i].duration_days, stages[i].humidityThreshold, stages[i].wateringTimeSec); }
             Serial.println("---------------------");
        } else if (command.equalsIgnoreCase("MEASURE")) { Serial.println("[SERIAL] Forzando medición..."); controlIndependiente(); }
        else if (command.equalsIgnoreCase("PUMP ON")) { Serial.println("[SERIAL] Bomba ON (30s)..."); activatePump(30000); }
        else if (command.equalsIgnoreCase("PUMP OFF")) { Serial.println("[SERIAL] Bomba OFF..."); deactivatePump(); }
        else if (command.startsWith("SET STAGE ")) { int stage = command.substring(10).toInt(); if (stage >= 0 && stage < numStages) { Serial.print("[SERIAL] Set Manual Stage: "); Serial.println(stages[stage].name); saveManualStage(stage); } else { Serial.println("[ERROR] Índice etapa inválido."); } }
        else if (command.equalsIgnoreCase("RESET STAGE")) { Serial.println("[SERIAL] Reset a etapa automática."); handleResetManualStage(); }
        else if (command.equalsIgnoreCase("CLEAR")) { Serial.println("[SERIAL] Borrando historial..."); handleClearMeasurementHistory(); }
        else if (command.equalsIgnoreCase("RESTART")) { Serial.println("[SERIAL] Reiniciando..."); ESP.restart(); }
        else if (command.equalsIgnoreCase("FORMAT")) { Serial.println("[SERIAL] BORRAR TODO? Escribe 'CONFIRM FORMAT'"); }
        else if (command.equalsIgnoreCase("CONFIRM FORMAT")) { Serial.println("[SERIAL] Formateando LittleFS..."); bool formatted = LittleFS.format(); Serial.println(formatted ? "[SERIAL] Formateado OK. Reiniciando." : "[SERIAL] [ERROR] Formato falló."); delay(1000); ESP.restart(); }
        else { Serial.println("[WARN] Comando desconocido. Ver STATUS."); }
    }
}