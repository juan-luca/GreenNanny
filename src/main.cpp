
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <DHT.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
// #include <Wire.h>    // REMOVED - No RTC
// #include <RTClib.h>  // REMOVED - No RTC
#include <time.h> // <-- LIBRERÍA PARA NTP Y FUNCIONES DE TIEMPO ESTÁNDAR

// --- Añadir configuración NTP ---
const char* ntpServer1 = "pool.ntp.org";
const char* ntpServer2 = "time.nist.gov";
// AJUSTA ESTOS VALORES PARA TU ZONA HORARIA
// EJEMPLO PARA ESPAÑA PENINSULAR:
//   - Horario de Invierno (CET, UTC+1): gmtOffset_sec = 3600; daylightOffset_sec = 0;
//   - Horario de Verano   (CEST, UTC+2): gmtOffset_sec = 3600; daylightOffset_sec = 3600; (Sistema suma ambos)
// ¡¡¡ ASEGÚRATE DE PONER LOS VALORES CORRECTOS PARA TU UBICACIÓN Y ÉPOCA DEL AÑO !!!
const long gmtOffset_sec     = -3 * 3600;  // UTC-3 = -10800 segundos
const int  daylightOffset_sec = 0;         // nada de DST

// Alternativa: Usar POSIX Timezone String (más robusto para cambios automáticos verano/invierno si tu SDK lo soporta bien)
// const char* tzInfo = "CET-1CEST,M3.5.0,M10.5.0/3"; // Ejemplo para Europa Central/Madrid
// ---------------------------------

// Definiciones de pines y tipos
#define DHTPIN D2
#define DHTTYPE DHT11
#define BOMBA_PIN D3

#define MAX_JSON_OBJECTS 500 // Max measurements to store
#define STAGES_CONFIG_FILE "/stages_config.json" // File for custom stage config

// RTC - REMOVED
// RTC_DS1307 rtc;

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
unsigned long startTime = 0; // millis() at boot
unsigned long lastDebugPrint = 0;
unsigned long lastNtpSyncAttempt = 0; // Track last NTP sync attempt
const unsigned long ntpSyncInterval = 24 * 3600 * 1000UL; // Sync cada 24h

// NEW Time Variables (NTP based)
bool ntpTimeSynchronized = false; // Flag: True once NTP sync is successful
time_t ntpBootEpoch = 0;          // Epoch time (seconds) of the first successful NTP sync

// Simulación de sensores (poner en false para usar el sensor real)
bool simulateSensors = false; // CAMBIAR A false PARA USO REAL
float simulatedHumidity = 55.0;
float simulatedTemperature = 25.0;

// Inicialización del sensor DHT
DHT dht(DHTPIN, DHTTYPE);

// Control de la bomba
int pumpActivationCount = 0;
unsigned long pumpOnTime = 0;      // Timestamp (millis) when pump was turned on (for auto-off)
unsigned long pumpDurationMs = 0;  // How long the pump should stay on (set by activatePump)
bool pumpAutoOff = false;          // Flag: Is the pump expected to turn off automatically?
bool pumpActivated = false;        // Flag: Is the pump currently ON?
unsigned long lastSecondPrint = 0; // For printing debug seconds while pump is on
int pumpSecondsCount = 0;          // Counter for debug seconds

// Intervalo de mediciones (en horas)
int measurementInterval = 3; // Default 3 hours
unsigned long nextMeasureTimestamp = 0; // Timestamp (millis) for next measurement check

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
// uint32_t rtcBootEpoch = 0; // REMOVED - Replaced by ntpBootEpoch (time_t)

// Variables de estado
uint64_t lastMeasurementTimestamp = 0; // Track last successful measurement time (epoch ms, UTC from NTP)

// --- Prototipos de Funciones ---
// Core Logic & Setup
void setup();
void loop();
void setupDHTSensor();
void setupBomba();
void setupServer();
void startAPMode();
bool syncNtpTime(); // PROTOTIPO NTP (Modified: no RTC interaction)
// Network & Config
void loadWifiCredentials();
void handleConnectWifi(); // Manual connection attempt via UI
void handleSaveWifiCredentials(); // Save credentials from UI
void handleWifiListRequest(); // Scan and list networks
// Time & Measurement
void loadMeasurementInterval();
void saveMeasurementInterval(int interval);
void handleMeasurementInterval(); // Set/Get interval via API
void controlIndependiente(); // Main automatic control logic (includes taking measurement, uses NTP time)
// Pump Control
void activatePump(unsigned long durationMs);
void deactivatePump();
void handlePumpControl(); // Manual pump control via API
// Stage Control
int getCurrentStageIndex(unsigned long daysElapsed); // Accepts daysElapsed as arg
void loadManualStage();
void saveManualStage(int index);
void handleSetManualStage();
void handleGetCurrentStage(); // Uses NTP time
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
void handleData(); // Main data endpoint (uses NTP time)
void handleLoadMeasurement(); // Endpoint to get all stored measurements
void handleClearMeasurementHistory(); // Endpoint to clear measurements
// HTTP Handlers
void handleRoot(); // Redirects / to index.html or config.html
// Utilities
float getHumidity();
float getTemperature();
float calculateVPD(float temperature, float humidity);
void handleSerialCommands(); // Optional serial control (updated for NTP time)


// --- IMPLEMENTACIONES ---

// *** MODIFIED FUNCTION: Sincroniza la hora con NTP y la establece en el sistema ***
bool syncNtpTime() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[NTP] WiFi no conectado. Imposible sincronizar.");
        return false;
    }

    Serial.print("[NTP] Intentando sincronizar hora del sistema con "); Serial.println(ntpServer1);
    // Configurar NTP - Usar gmtOffset_sec y daylightOffset_sec
    configTime(gmtOffset_sec, daylightOffset_sec, "129.6.15.28", "132.163.4.101");
    // Alternativa con POSIX string: configTime(tzInfo, ntpServer1, ntpServer2);

    // Esperar a que la hora se sincronice (máx 15 segundos)
    int attempts = 0;
    time_t now_time_t = time(nullptr);
    while (now_time_t < (24 * 3600) && attempts < 30) { // Esperar hasta obtener un timestamp válido (superior a 1 día en segundos)
        delay(500);
        now_time_t = time(nullptr);
        Serial.print(".");
        attempts++;
    }
    Serial.println(""); // Nueva línea después de los puntos

    if (now_time_t < (24 * 3600)) { // Si sigue siendo un valor bajo (no sincronizó)
        Serial.println("[NTP ERROR] Falló la obtención de hora NTP.");
        ntpTimeSynchronized = false; // Asegurar que el flag esté en false
        return false;
    }

    // Convertir time_t a estructura tm para mostrarla
    struct tm timeinfo;
    localtime_r(&now_time_t, &timeinfo); // Usar localtime_r para obtener la hora local configurada

    Serial.print("[NTP INFO] Hora del sistema obtenida y establecida: ");
    char buf[30];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z", &timeinfo); // Formato estándar con zona horaria si está disponible
    Serial.println(buf);

    // --- NO RTC ADJUSTMENT NEEDED ---
    // Serial.println("[NTP INFO] Ajustando RTC con la hora obtenida."); NO LONGER NEEDED
    // Crear objeto DateTime para RTClib - NO LONGER NEEDED
    // DateTime dtToSet(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
    //                  timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    // Ajustar el RTC DS1307 - NO LONGER NEEDED
    // rtc.adjust(dtToSet);
    // Verificar si el RTC está corriendo después del ajuste - NO LONGER NEEDED
    // if (!rtc.isrunning()) { ... }

    Serial.println("[NTP SUCCESS] Hora del sistema establecida correctamente.");
    ntpTimeSynchronized = true; // Marcar que la hora es válida

    // Si es la primera sincronización exitosa, guardar el epoch de referencia
    if (ntpBootEpoch == 0) {
        ntpBootEpoch = now_time_t;
        Serial.print("[INFO] Primera sincronización NTP. Epoch de referencia guardado: "); Serial.println(ntpBootEpoch);
    }

    // Actualizar timestamp de la última medición si aún no se ha hecho ninguna válida
    // Esto asegura que la próxima medición se calcule desde un punto de tiempo conocido
    if (lastMeasurementTimestamp == 0) {
         lastMeasurementTimestamp = (uint64_t)now_time_t * 1000ULL;
         Serial.print("[INFO] Timestamp de última medición inicializado a: "); Serial.println(lastMeasurementTimestamp);
    }

    return true;
}


void setup() {
    Serial.begin(115200);
    while (!Serial) { ; }
    Serial.println("\n\n[SETUP] Iniciando Green Nanny v1.2 (NTP Only)...");

    // Montar el sistema de archivos
    if (!LittleFS.begin()) {
        Serial.println("[ERROR] Falló al montar LittleFS. Verifica formato.");
        while (true) { delay(1000); }
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
    // Iniciar I2C y RTC - REMOVED
    // Wire.begin(D6 /* SDA */, D5 /* SCL */);
    // if (!rtc.begin()) { ... } else { ... } - REMOVED

    // Obtener la hora actual del RTC - REMOVED (Ahora depende de NTP)
    // DateTime now = rtc.now(); ... - REMOVED
    Serial.println("[INFO] Hora inicial del sistema depende de NTP. Sincronizando...");

    // rtcBootEpoch = now.unixtime(); // REMOVED
    startTime    = millis(); // Guardar millis de inicio
    lastMeasurementTimestamp = 0; // Se actualizará tras la primera sincronización NTP o medición
    Serial.print("[INFO] Hora de inicio del sistema (millis): "); Serial.println(startTime);
    // Serial.print("[INFO] Hora de inicio del sistema (epoch RTC inicial): "); // REMOVED

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
    // Calcular nextMeasureTimestamp DESPUÉS de posible sincronización NTP
    // Se hará más abajo después del intento de sync

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

         // *** INTENTAR SINCRONIZAR HORA NTP ***
         if (syncNtpTime()) {
             // Hora sincronizada, recalcular próxima medición basado en la hora ACTUAL
             time_t now_t = time(nullptr);
             struct tm timeinfo;
             localtime_r(&now_t, &timeinfo);
             char buf[30];
             strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
             Serial.print("[INFO] Hora sistema después de NTP: "); Serial.println(buf);
             unsigned long currentMillis = millis(); // Obtener millis actuales
             // Ajustar la marca de tiempo de la próxima medición relativa a la hora actual.
             // Calcular cuándo debería ser la próxima medición basada en el intervalo y la hora actual.
             // Ejemplo simple: la próxima será 'interval' horas después de AHORA (en millis).
             nextMeasureTimestamp = currentMillis + (measurementInterval * 3600000UL);
             Serial.print("[INFO] Próxima medición ajustada post-NTP para millis ~: "); Serial.println(nextMeasureTimestamp);
         } else {
             // No sincronizó, usar la hora del sistema no sincronizada (cercana a epoch 0)
             Serial.println("[WARN] No se pudo sincronizar con NTP. La hora del sistema NO es correcta.");
             // Calcular próximo intervalo basado en millis() desde el inicio
             nextMeasureTimestamp = startTime + (measurementInterval * 3600000UL);
             Serial.print("[INFO] Próxima medición programada (sin NTP sync) alrededor de millis: "); Serial.println(nextMeasureTimestamp);
             // El tiempo transcurrido (elapsedDays) será incorrecto hasta que NTP sincronice.
         }
         lastNtpSyncAttempt = millis(); // Marcar el último intento (exitoso o no)
    }

    // Si no hubo conexión WiFi en setup y no se calculó nextMeasureTimestamp, calcularlo ahora basado en startTime
    if (nextMeasureTimestamp == 0) {
        nextMeasureTimestamp = startTime + (measurementInterval * 3600000UL);
        Serial.print("[INFO] Próxima medición (sin WiFi/NTP inicial) programada para millis ~: "); Serial.println(nextMeasureTimestamp);
    }

    // Configurar el servidor web y endpoints
    setupServer();

    Serial.println("[INFO] === Setup completo. Sistema listo. ===");
}

void loop() {
    unsigned long currentMillis = millis(); // Obtener millis al inicio del loop

    // Manejar clientes HTTP
    server.handleClient();

    // Procesar solicitudes DNS si está en modo AP
    if (WiFi.getMode() == WIFI_AP) {
        dnsServer.processNextRequest();
    }

    // Comandos seriales (opcional)
    handleSerialCommands();

    // --- Auto-apagado de la bomba ---
    if (pumpAutoOff && pumpActivated && (currentMillis - pumpOnTime >= pumpDurationMs)) {
        Serial.println("[AUTO] Tiempo de riego (autoOff) cumplido.");
        deactivatePump();
    }

    // Conteo de segundos para debug si la bomba está encendida con auto-off
    if (pumpActivated && pumpAutoOff && (currentMillis - lastSecondPrint >= 1000)) {
        pumpSecondsCount++;
        Serial.print("[DEBUG PUMP] Riego en curso, segundo: "); Serial.println(pumpSecondsCount); // <-- Más informativo
        lastSecondPrint = currentMillis;
    }

    // Verificar si es momento de tomar una medición programada
    if (currentMillis >= nextMeasureTimestamp) {
        Serial.println("[INFO] Hora de medición programada alcanzada.");
        controlIndependiente();  // Lógica automática de medición y riego
        // Programar la siguiente medición
        nextMeasureTimestamp = currentMillis + (measurementInterval * 3600000UL); // Usar currentMillis como base
        Serial.print("[INFO] Próxima medición programada para millis: ");
        Serial.println(nextMeasureTimestamp);
    }

    // --- Sincronización NTP Periódica ---
    // Intentar sincronizar solo si no está sincronizado O si ha pasado el intervalo
    if (WiFi.status() == WL_CONNECTED && (!ntpTimeSynchronized || (currentMillis - lastNtpSyncAttempt >= ntpSyncInterval))) {
        if (!ntpTimeSynchronized) {
             Serial.println("[LOOP] Hora no sincronizada, intentando NTP sync...");
        } else {
             Serial.println("[LOOP] Intervalo de sincronización NTP (" + String(ntpSyncInterval / 3600000UL) + "h) alcanzado.");
        }
        syncNtpTime(); // Intentar sincronizar
        lastNtpSyncAttempt = currentMillis; // Actualizar marca de tiempo del intento
    }

    // Mensajes de debug periódicos (cada 10 minutos)
    if (currentMillis - lastDebugPrint >= 600000) { // Usar currentMillis
        lastDebugPrint = currentMillis;

        time_t now_t = time(nullptr);
        struct tm timeinfo;
        bool timeValid = ntpTimeSynchronized && (now_t >= ntpBootEpoch);
        char buf[30];

        Serial.println("--- DEBUG STATUS ---");

        if(timeValid) {
            localtime_r(&now_t, &timeinfo);
            strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
            uint32_t elapsedSeconds = now_t - ntpBootEpoch;
            Serial.print("[DEBUG] Uptime (NTP based): "); // Uptime desde la primera sincronización NTP
            Serial.print(elapsedSeconds / 86400); Serial.print("d ");
            Serial.print((elapsedSeconds % 86400) / 3600); Serial.print("h ");
            Serial.print((elapsedSeconds % 3600) / 60);     Serial.println("m");
        } else {
            strcpy(buf, "N/A (NTP Pending)");
            Serial.println("[DEBUG] Uptime (NTP based): N/A (Waiting for NTP sync)");
        }
        Serial.print("[DEBUG] System Time: "); Serial.println(buf);


        unsigned long remainingMs = (nextMeasureTimestamp > currentMillis)
                                    ? (nextMeasureTimestamp - currentMillis)
                                    : 0;
        unsigned long remainingSeconds = remainingMs / 1000;
        unsigned long remainingHours   = remainingSeconds / 3600;
        unsigned long remainingMinutes = (remainingSeconds % 3600) / 60;
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

        Serial.print("[DEBUG] NTP Synced: "); Serial.println(ntpTimeSynchronized ? "Yes" : "No");
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
        Serial.print("[SIM] Humedad simulada: "); Serial.println(simulatedHumidity); // Debug simulación
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
        Serial.print("[SIM] Temperatura simulada: "); Serial.println(simulatedTemperature); // Debug simulación
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

// Determina el índice de la etapa actual basado en días transcurridos (desde primer NTP sync)
// MODIFIED: Accepts daysElapsed calculated externally
int getCurrentStageIndex(unsigned long daysElapsed) {
    if (manualStageControl) return manualStageIndex;

    // Si el tiempo no está sincronizado (daysElapsed será probablemente 0 o inválido),
    // o si daysElapsed es 0 (aún no ha pasado un día desde el sync), estar en la primera etapa.
    if (!ntpTimeSynchronized || daysElapsed == 0) {
        // Optional: Add a warning log here if needed
        if (!ntpTimeSynchronized) {
             // Serial.println("[WARN] getCurrentStageIndex: NTP no sincronizado, usando etapa 0.");
        } else {
             // Serial.println("[DEBUG] getCurrentStageIndex: Menos de 1 día transcurrido, usando etapa 0.");
        }
        return 0;
    }

    unsigned long cumulativeDays = 0;
    // Uses the potentially modified 'stages' array
    for (int i = 0; i < numStages; i++) {
        cumulativeDays += stages[i].duration_days;
        if (daysElapsed <= cumulativeDays) return i;
    }
    // Si el tiempo transcurrido supera la duración total de todas las etapas
    return numStages - 1; // Permanecer en la última etapa
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
    pumpOnTime = millis(); // Usa millis() para el temporizador de auto-apagado
    pumpDurationMs = durationMs;
    pumpSecondsCount = 0; // Reset counter for new activation
    lastSecondPrint = millis(); // Reset timer for debug prints
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
    if (pumpSecondsCount > 0) { // Log total time if it was running
        Serial.print("[INFO] Bomba estuvo activa por ~"); Serial.print(pumpSecondsCount); Serial.println("s.");
    }
    pumpSecondsCount = 0;
}

// Carga el intervalo de medición desde archivo
void loadMeasurementInterval() {
    File file = LittleFS.open("/interval.txt", "r");
    if (!file) {
        Serial.println("[INFO] No 'interval.txt', usando default (3h).");
        measurementInterval = 3;
        saveMeasurementInterval(measurementInterval); // Save default if file doesn't exist
        return;
    }
    String valStr = file.readStringUntil('\n');
    file.close();
    valStr.trim();
    int val = valStr.toInt();
    if (val > 0 && val < 168) { // Valid range 1 to 167 hours
        measurementInterval = val;
        Serial.print("[INFO] Intervalo cargado: "); Serial.print(measurementInterval); Serial.println("h.");
    } else {
        Serial.print("[WARN] Intervalo inválido ('"); Serial.print(valStr); Serial.println("'). Usando default (3h).");
        measurementInterval = 3;
        saveMeasurementInterval(measurementInterval); // Save valid default
    }
}

// Guarda el intervalo de medición en archivo
void saveMeasurementInterval(int interval) {
    if (interval <= 0 || interval >= 168) { // Validate range before saving
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
    measurementInterval = interval; // Update in-memory value immediately

    // Recalcular próxima medición basado en la última medición REALIZADA (usando millis)
    // O usar el tiempo de inicio si no hay mediciones aún
    unsigned long lastEventTimeMillis = startTime; // Default to boot time (millis)

    // Si tenemos un timestamp de la última medición válido (post-NTP), estimar los millis correspondientes
    if (lastMeasurementTimestamp > 0 && ntpTimeSynchronized && ntpBootEpoch > 0) {
         // Calcular cuánto tiempo (segundos) ha pasado desde el boot epoch hasta la última medición
         time_t lastMeasurementEpochSec = lastMeasurementTimestamp / 1000ULL;
         if (lastMeasurementEpochSec >= ntpBootEpoch) {
             time_t secondsSinceBootEpoch = lastMeasurementEpochSec - ntpBootEpoch;
             // Estimar los millis en los que ocurrió la última medición:
             // millis al inicio + milisegundos transcurridos desde el inicio (aproximado por epoch diff)
             // Esta es una aproximación, ya que millis() puede derivar respecto a NTP.
             // Usar currentMillis como base si la última medición es reciente es más simple.
             // Optemos por lo simple: basar la siguiente medición en la hora ACTUAL (millis).
             lastEventTimeMillis = millis(); // Base recalculation on current time
             Serial.println("[INFO] Recalculando próximo intervalo basado en la hora actual (millis).");
         } else {
              Serial.println("[WARN] Calculando próximo intervalo: timestamp última medición parece anterior al boot epoch. Usando hora actual.");
              lastEventTimeMillis = millis(); // Fallback to current time
         }
    } else {
        // Si no hay timestamp válido o no hay NTP, basar en la hora actual
        lastEventTimeMillis = millis();
         Serial.println("[INFO] Recalculando próximo intervalo basado en la hora actual (millis) - NTP/LastTimestamp no disponible.");
    }


    // Schedule next measurement from the last event time (millis) + new interval
    nextMeasureTimestamp = lastEventTimeMillis + (measurementInterval * 3600000UL);
    Serial.print("[INFO] Intervalo guardado: "); Serial.print(measurementInterval); Serial.println("h.");
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
    bool useStaticIP = true; // Poner a true para intentar IP estática (definida globalmente)
    if (useStaticIP && !WiFi.config(ip, gateway, subnet)) {
        Serial.println("[WARN] Falló aplicación IP estática.");
    }
    WiFi.begin(ssid.c_str(), password.c_str());
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) { // 10 segundos de espera
        delay(500); Serial.print("."); attempts++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n[INFO] Conexión WiFi exitosa.");
        Serial.print("[INFO] IP: "); Serial.println(WiFi.localIP());
        Serial.print("[INFO] RSSI: "); Serial.print(WiFi.RSSI()); Serial.println(" dBm");
    } else {
        Serial.println("\n[ERROR] Falló conexión WiFi con credenciales guardadas.");
        WiFi.disconnect(false); // No borrar credenciales internas
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
    // Usa la constante numStages para la validación
    if (stageIdx >= 0 && stageIdx < numStages) {
        manualStageIndex = stageIdx;
        manualStageControl = true;
        Serial.print("[INFO] Control manual cargado. Etapa: "); Serial.println(stages[manualStageIndex].name);
    } else {
        Serial.print("[WARN] Índice etapa manual inválido ('"); Serial.print(stageStr); Serial.println("'). Usando control automático.");
        manualStageControl = false;
        manualStageIndex = 0;
        LittleFS.remove("/ManualStage.txt"); // Borrar archivo inválido
    }
}

// Guarda etapa manual en archivo
void saveManualStage(int index) {
     // Usa la constante numStages para la validación
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
     manualStageIndex = index; // Actualizar valor en memoria
     manualStageControl = true; // Activar control manual
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
    // Podría añadirse validación básica del contenido aquí si es necesario
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
         Serial.println("[ERROR] Error al escribir historial completo en 'Measurements.txt'. Bytes esperados: " + String(allMeasurementsString.length()) + ", escritos: " + String(bytesWritten));
    } else {
         Serial.println("[INFO] Historial guardado en archivo (" + String(bytesWritten) + " bytes).");
    }
}

// Parsea string de historial (formato {j1},{j2},..) en array
int parseData(String input, String output[]) {
    int count = 0;
    int startIndex = 0;
    int endIndex = 0;
    input.trim(); // Quitar espacios al inicio/fin
    while (startIndex < input.length() && count < MAX_JSON_OBJECTS) {
        startIndex = input.indexOf('{', startIndex);
        if (startIndex == -1) break; // No más objetos JSON
        endIndex = input.indexOf('}', startIndex);
        if (endIndex == -1) {
            Serial.println("[PARSE WARN] Objeto JSON incompleto encontrado. Deteniendo parseo.");
            break; // Malformed JSON object
        }
        // Extraer el objeto JSON
        output[count++] = input.substring(startIndex, endIndex + 1);
        // Mover al siguiente carácter después del '}'
        startIndex = endIndex + 1;
        // Omitir la coma y espacios opcionales antes del siguiente objeto
        if (startIndex < input.length() && input.charAt(startIndex) == ',') {
             startIndex++;
        }
        while(startIndex < input.length() && isspace(input.charAt(startIndex))) {
             startIndex++; // Skip whitespace
        }
    }
    if (count >= MAX_JSON_OBJECTS) {
       Serial.println("[PARSE WARN] Se alcanzó el límite MAX_JSON_OBJECTS durante el parseo.");
    }
    return count;
}

// Convierte array de historial a String (formato {j1},{j2},..)
String arrayToString(String array[], size_t arraySize) {
    String result = "";
    bool first = true;
    for (size_t i = 0; i < arraySize; i++) {
        // Asegurarse que el string no es nulo y tiene contenido JSON válido básico
        if (array[i] != nullptr && array[i].length() > 2 && array[i].startsWith("{") && array[i].endsWith("}")) {
            if (!first) {
                result += ","; // Añadir coma separadora
            }
            result += array[i];
            first = false;
        } else if (array[i] != nullptr && array[i].length() > 0) {
            // Loguear si hay un elemento inválido en el array que no sea vacío
            Serial.print("[ARRAY2STR WARN] Ignorando elemento inválido en índice "); Serial.print(i); Serial.print(": "); Serial.println(array[i]);
        }
    }
    return result;
}

// Guarda una nueva medición en array y archivo (con deslizamiento)
void saveMeasurement(const String& jsonString) {
    Serial.println("[ACCION] Guardando nueva medición:");
    Serial.println(jsonString);
    // Validación básica del formato JSON
    if (!jsonString.startsWith("{") || !jsonString.endsWith("}")) {
        Serial.println("[ERROR] Intento guardar medición inválida (no es un objeto JSON).");
        return;
    }

    if (jsonIndex < MAX_JSON_OBJECTS) {
        measurements[jsonIndex++] = jsonString;
    } else {
        // Array lleno, desplazar todos los elementos una posición hacia la izquierda
        Serial.println("[WARN] Array mediciones lleno. Desplazando historial...");
        for (int i = 0; i < MAX_JSON_OBJECTS - 1; i++) {
            measurements[i] = measurements[i + 1];
        }
        // Añadir la nueva medición al final
        measurements[MAX_JSON_OBJECTS - 1] = jsonString;
        // jsonIndex ya está en MAX_JSON_OBJECTS, no necesita incrementarse
    }
    // Guardar el estado actual completo del array en el archivo
    saveMeasurementFile(arrayToString(measurements, jsonIndex));
}

// Formatea array de historial a JSON Array String "[{j1},{j2},..]"
void formatMeasurementsToString(String& formattedString) {
    formattedString = "["; // Iniciar array JSON
    bool first = true;
    for (int i = 0; i < jsonIndex; i++) {
         // Asegurarse que el string no es nulo y tiene contenido JSON válido básico
        if (measurements[i] != nullptr && measurements[i].length() > 2 && measurements[i].startsWith("{") && measurements[i].endsWith("}")) {
            if (!first) {
                formattedString += ","; // Coma separadora
            }
            formattedString += measurements[i]; // Añadir el objeto JSON
            first = false;
        }
    }
    formattedString += "]"; // Cerrar array JSON
}


// NEW: Load custom stage configuration from LittleFS
void loadStagesConfig() {
    if (LittleFS.exists(STAGES_CONFIG_FILE)) {
        File configFile = LittleFS.open(STAGES_CONFIG_FILE, "r");
        if (!configFile) {
            Serial.println("[ERROR] No se pudo abrir el archivo de configuración de etapas existente.");
            return;
        }

        // Aumentar tamaño si hay muchas etapas o nombres largos (poco probable aquí)
        StaticJsonDocument<512> doc; // Ajustar tamaño si es necesario

        DeserializationError error = deserializeJson(doc, configFile);
        configFile.close(); // Cerrar archivo después de leer

        if (error) {
            Serial.print("[ERROR] Falló al parsear JSON de configuración de etapas: ");
            Serial.println(error.c_str());
            return;
        }

        if (!doc.is<JsonArray>()) {
             Serial.println("[ERROR] El archivo de configuración de etapas no contiene un array JSON.");
             return;
        }

        JsonArray stageArray = doc.as<JsonArray>();
        int loadedCount = 0;
        // Iterar sobre el array JSON y actualizar el array 'stages' en memoria
        for (JsonObject stageConfig : stageArray) {
            if (loadedCount >= numStages) {
                Serial.println("[WARN] Más etapas en archivo de config que en array 'stages'. Ignorando extras.");
                break;
            }
            // Usar valores por defecto del array 'stages' si falta alguna clave en el JSON
            // No actualizamos 'name' ni 'index' desde el archivo. Solo parámetros editables.
            // stages[loadedCount].duration_days     = stageConfig["duration_days"]      | stages[loadedCount].duration_days; // Opcional si quieres permitir cambiar duración
            stages[loadedCount].humidityThreshold = stageConfig["humidityThreshold"]  | stages[loadedCount].humidityThreshold;
            stages[loadedCount].wateringTimeSec   = stageConfig["wateringTimeSec"]    | stages[loadedCount].wateringTimeSec;
            loadedCount++;
        }
        Serial.print("[INFO] Cargados "); Serial.print(loadedCount); Serial.println(" parámetros de etapas desde " STAGES_CONFIG_FILE);

    } else {
        Serial.println("[INFO] No existe " STAGES_CONFIG_FILE ". Usando configuración de etapas por defecto.");
        // Opcional: Guardar la configuración por defecto al archivo la primera vez
        // saveStagesConfig();
    }
}

// NEW: Save the current state of the 'stages' array to LittleFS
bool saveStagesConfig() {
    // Aumentar tamaño si es necesario
    StaticJsonDocument<512> doc;
    JsonArray stageArray = doc.to<JsonArray>();

    // Crear un objeto JSON para cada etapa en el array 'stages'
    for (int i = 0; i < numStages; i++) {
        JsonObject stageConfig = stageArray.createNestedObject();
        // Guardar solo los parámetros editables. No es necesario guardar nombre/index.
        // stageConfig["name"] = stages[i].name; // Opcional si se quisiera guardar
        // stageConfig["duration_days"] = stages[i].duration_days; // Opcional
        stageConfig["humidityThreshold"] = stages[i].humidityThreshold;
        stageConfig["wateringTimeSec"]   = stages[i].wateringTimeSec;
    }

    File configFile = LittleFS.open(STAGES_CONFIG_FILE, "w");
    if (!configFile) {
        Serial.println("[ERROR] No se pudo abrir " STAGES_CONFIG_FILE " para escritura.");
        return false;
    }

    // Serializar el JSON al archivo
    size_t bytesWritten = serializeJson(doc, configFile);
    configFile.close(); // Cerrar el archivo

    if (bytesWritten == 0) {
        Serial.println("[ERROR] Falló al escribir JSON de configuración de etapas en archivo (0 bytes escritos).");
        return false;
    } else {
        Serial.print("[INFO] Configuración de etapas guardada en " STAGES_CONFIG_FILE " (");
        Serial.print(bytesWritten); Serial.println(" bytes).");
        return true;
    }
}

// Lógica principal de control (sensores, decisión riego, registro) - MODIFIED FOR NTP
void controlIndependiente() {
    unsigned long startMillis = millis(); // Millis al inicio del ciclo

    // 1. Obtener Hora del Sistema (NTP)
    time_t now_time_t = time(nullptr);
    struct tm timeinfo;
    uint64_t epochMs = 0; // Usar 0 como indicador de tiempo no válido
    uint32_t elapsedDays = 0; // Días transcurridos desde el primer sync NTP
    bool timeValid = ntpTimeSynchronized && (now_time_t >= ntpBootEpoch); // Check if time is valid and after initial sync

    Serial.println("\n[CONTROL] Iniciando ciclo...");
    char buf[30];

    if (timeValid) {
        localtime_r(&now_time_t, &timeinfo); // Obtener desglose hora local
        epochMs = (uint64_t)now_time_t * 1000ULL; // Calcular epoch ms UTC
        elapsedDays = (now_time_t - ntpBootEpoch) / 86400UL; // Calcular días transcurridos
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo); // Formatear hora para log
        Serial.print("[INFO] Hora Sistema: "); Serial.print(buf);
        Serial.print(", Dia (desde 1er sync): "); Serial.println(elapsedDays);
    } else {
        Serial.println("[CONTROL WARN] NTP time not yet synchronized. Timestamp será 0, Día será 0.");
        // No podemos calcular días transcurridos ni obtener hora válida
        strcpy(buf, "N/A (NTP Pending)");
        Serial.print("[INFO] Hora Sistema: "); Serial.println(buf);
        Serial.print(", Dia (desde 1er sync): "); Serial.println(elapsedDays); // Será 0
    }

    // Estado de fallo de sensor (basado en ciclos, no en tiempo absoluto si NTP falla)
    static uint32_t failureStartTimeEpoch = 0; // Usará epoch=0 si NTP no ha sincronizado
    static bool previousSensorValid = true;

    // 2. Leer sensores
    float humidity    = getHumidity();
    float temperature = getTemperature();

    // Actualizar timestamp de última medición SOLO si la hora es válida
    if (timeValid) {
        lastMeasurementTimestamp = epochMs;
    }

    // 3. Validar sensor y manejar fallos
    bool sensorValid = !(humidity < 0.0 || temperature <= -90.0);
    if (!sensorValid) {
        if (previousSensorValid) { // Si es la primera vez que falla
            Serial.println("[WARN] Falla sensor detectada.");
            // Marcar inicio fallo con epoch actual (o 0 si NTP no sync)
            failureStartTimeEpoch = timeValid ? now_time_t : 0;
        } else {
             // Sensor ya estaba fallando, no hacer nada especial
        }
    } else { // Sensor válido
        if (!previousSensorValid) { // Si se acaba de recuperar
            Serial.println("[INFO] Sensor recuperado.");
        }
        failureStartTimeEpoch = 0; // Resetear contador de fallo si el sensor es válido
    }
    previousSensorValid = sensorValid; // Guardar estado actual para la próxima iteración

    // 4. Determinar etapa y parámetros (usa potentially modified 'stages' array)
    // Pasa los días calculados (0 si NTP no sync) a la función
    int stageIndex = getCurrentStageIndex(elapsedDays);
    const Stage& currentStage = stages[stageIndex]; // Obtener referencia a la etapa actual
    int currentThreshold = currentStage.humidityThreshold;
    unsigned long wateringTimeMs = currentStage.wateringTimeSec * 1000UL;
    Serial.print("[INFO] Etapa: "); Serial.print(currentStage.name); Serial.print(" ("); Serial.print(manualStageControl ? "Manual" : "Auto"); Serial.println(")");
    Serial.print("       Umbral: "); Serial.print(currentThreshold); Serial.print("%, Riego Config: "); Serial.print(currentStage.wateringTimeSec); Serial.println("s");

    // 5. Decidir riego (solo si bomba no está ya en modo auto-apagado)
    bool needsWatering = false;
    if (!pumpAutoOff) {
        if (sensorValid) {
            Serial.print("[DECISION] Sensor OK. Humedad: "); Serial.print(humidity, 1); Serial.print("%, Umbral: "); Serial.println(currentThreshold);
            if (humidity < currentThreshold) { // Comparar con el umbral actual
                Serial.println("           -> Humedad bajo umbral. Regar.");
                needsWatering = true;
            } else {
                Serial.println("           -> Humedad OK. No regar.");
            }
        } else { // Sensor inválido
            Serial.println("[DECISION] Sensor inválido.");
            // Verificar fallo prolongado SOLO si tenemos hora válida para comparar
            if (timeValid && failureStartTimeEpoch > 0) {
                uint32_t downTimeSeconds = now_time_t - failureStartTimeEpoch;
                Serial.print("           -> Tiempo fallo: "); Serial.print(downTimeSeconds); Serial.print("s ("); Serial.print(downTimeSeconds / 3600); Serial.println("h)");
                if (downTimeSeconds >= 86400UL) { // >= 24 horas de fallo continuo
                    Serial.println("           -> Falla sensor >= 24h. Forzar riego por seguridad.");
                    needsWatering = true;
                    // Resetear el timer de fallo DESPUÉS de decidir regar, para que no riegue continuamente
                    // si el sensor sigue fallando. Regará una vez cada 24h de fallo.
                    failureStartTimeEpoch = now_time_t;
                } else {
                    Serial.println("           -> Falla sensor < 24h. No regar.");
                }
            } else if (!timeValid && failureStartTimeEpoch == 0 && !previousSensorValid) {
                // Sensor falla, pero no podemos medir duración (NTP no sync)
                Serial.println("           -> Falla sensor detectada, pero duración desconocida (NTP no sincronizado). No regar.");
            }
             else {
                // Primera detección de fallo o NTP no sincronizado la primera vez que falla
                Serial.println("           -> Primera detección fallo sensor (o NTP no sync). No regar.");
                // failureStartTimeEpoch ya debería haberse establecido si timeValid=true
            }
        }

        // Actuar: activar la bomba solo si es necesario Y si no está ya activada
        if (needsWatering && !pumpActivated) {
             activatePump(wateringTimeMs); // Usa el tiempo de riego de la etapa actual
        } else if (needsWatering && pumpActivated) {
             Serial.println("[INFO] Riego necesario, pero la bomba ya está activa (posiblemente manual). No se reactiva.");
        }

    } else { // pumpAutoOff es true
        Serial.println("[INFO] Bomba en ciclo Auto-Off. Omitiendo decisión de riego en este ciclo.");
        // Muestra cuánto tiempo queda aproximadamente (basado en millis)
        unsigned long elapsedPumpMs = millis() - pumpOnTime;
        unsigned long remainingPumpMs = (pumpDurationMs > elapsedPumpMs) ? (pumpDurationMs - elapsedPumpMs) : 0;
        Serial.print("       Tiempo restante riego ~: "); Serial.print(remainingPumpMs / 1000); Serial.println("s");
    }

    // 6. Registrar medición
    StaticJsonDocument<256> doc; // Tamaño suficiente
    if (sensorValid) {
        doc["temperature"] = serialized(String(temperature, 1));
        doc["humidity"]    = serialized(String(humidity, 1));
    } else {
        doc["temperature"] = nullptr;
        doc["humidity"]    = nullptr;
    }
    doc["pumpActivated"] = pumpActivated; // Estado REAL de la bomba al momento de guardar
    doc["stage"]         = currentStage.name; // Nombre de la etapa actual
    doc["epoch_ms"]      = epochMs; // Timestamp en milisegundos UTC (será 0 si NTP no ha sincronizado)
    String measurementString;
    serializeJson(doc, measurementString);
    saveMeasurement(measurementString); // Guardar en array y archivo

    unsigned long duration = millis() - startMillis;
    Serial.print("[CONTROL] Ciclo finalizado en "); Serial.print(duration); Serial.println(" ms.");
}


// --- Handlers HTTP ---

// Handler para / - Redirige a index.html o config.html
void handleRoot() {
    Serial.println("[HTTP] Solicitud / recibida.");
    // Evitar caché para asegurar que la redirección correcta ocurra siempre
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "-1");

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[HTTP] WiFi no conectado. Redirigiendo a /config.html");
        server.sendHeader("Location", "/config.html", true);
        server.send(302, "text/plain", "Redirecting to config...");
    } else {
        Serial.println("[HTTP] WiFi conectado. Redirigiendo a /index.html");
        server.sendHeader("Location", "/index.html", true);
        server.send(302, "text/plain", "Redirecting to dashboard...");
    }
}

// Handler para /data - Datos principales y estado (MODIFIED FOR NTP)
void handleData() {
    Serial.println("[HTTP] Solicitud /data recibida.");
    server.sendHeader("Access-Control-Allow-Origin", "*"); // Permitir CORS
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "-1");

    // Obtener datos actuales
    float humidity = getHumidity();
    float temperature = getTemperature();
    float vpd = calculateVPD(temperature, humidity);

    // Obtener hora y calcular tiempo transcurrido (NTP based)
    time_t now_t = time(nullptr);
    uint64_t epochNowMs = 0;
    uint32_t elapsedSeconds = 0;
    uint32_t elapsedDays = 0;
    bool timeValid = ntpTimeSynchronized && (now_t >= ntpBootEpoch);

    if (timeValid) {
        epochNowMs = (uint64_t)now_t * 1000ULL;
        elapsedSeconds = now_t - ntpBootEpoch;
        elapsedDays = elapsedSeconds / 86400;
    } // Si no, permanecen en 0

    // Determinar etapa actual usando los días calculados
    int stageIndex = getCurrentStageIndex(elapsedDays);
    const Stage& currentStage = stages[stageIndex]; // Usar datos de etapa actuales

    // Crear JSON de respuesta
    StaticJsonDocument<768> doc; // Aumentar tamaño si es necesario

    // Sensores
    if (temperature > -90.0) doc["temperature"] = serialized(String(temperature, 1)); else doc["temperature"] = nullptr;
    if (humidity >= 0.0) doc["humidity"] = serialized(String(humidity, 1)); else doc["humidity"] = nullptr;
    if (vpd >= 0.0) doc["vpd"] = serialized(String(vpd, 2)); else doc["vpd"] = nullptr;

    // Estado Bomba
    doc["pumpStatus"] = pumpActivated;
    doc["pumpActivationCount"] = pumpActivationCount;
    doc["pumpAutoOff"] = pumpAutoOff; // Informar si está en modo auto-off
    if (pumpAutoOff && pumpActivated) {
        unsigned long currentMillis = millis();
        unsigned long elapsedPumpMs = currentMillis - pumpOnTime;
        unsigned long remainingPumpMs = (pumpDurationMs > elapsedPumpMs) ? (pumpDurationMs - elapsedPumpMs) : 0;
        doc["pumpRemainingSec"] = remainingPumpMs / 1000;
    } else {
        doc["pumpRemainingSec"] = 0;
    }

    // Tiempo
    doc["elapsedTime"] = elapsedSeconds; // Tiempo transcurrido desde primer sync NTP (segundos)
    doc["currentTime"] = epochNowMs;     // Hora actual del sistema (epoch ms UTC, 0 if not synced)
    doc["lastMeasurementTimestamp"] = lastMeasurementTimestamp; // Última vez que se guardó medición (epoch ms UTC)
    doc["ntpSynced"] = ntpTimeSynchronized; // Informar si NTP está sincronizado

    // Etapa Actual
    doc["currentStageName"] = currentStage.name;
    doc["currentStageIndex"] = stageIndex;
    doc["currentStageThreshold"] = currentStage.humidityThreshold; // Umbral actual
    doc["currentStageWateringSec"] = currentStage.wateringTimeSec; // Tiempo de riego actual
    doc["manualStageControl"] = manualStageControl;

    // Red y Sistema
    if (WiFi.status() == WL_CONNECTED) {
       doc["deviceIP"] = WiFi.localIP().toString();
       doc["wifiRSSI"] = WiFi.RSSI();
       doc["wifiStatus"] = "Connected";
    } else if (WiFi.getMode() == WIFI_AP) {
        doc["deviceIP"] = WiFi.softAPIP().toString();
        doc["wifiRSSI"] = 0; // No aplicable en modo AP
        doc["wifiStatus"] = "AP Mode";
    } else {
        doc["deviceIP"] = "N/A";
        doc["wifiRSSI"] = 0;
        doc["wifiStatus"] = "Disconnected";
    }
    doc["freeHeap"] = ESP.getFreeHeap();
    doc["measurementInterval"] = measurementInterval; // Informar intervalo configurado

    // Serializar y enviar
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

// Handler para /wifiList - Listar redes WiFi
void handleWifiListRequest() {
    Serial.println("[HTTP] Solicitud /wifiList. Escaneando...");
    server.sendHeader("Access-Control-Allow-Origin", "*"); // Permitir CORS
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");

    // WiFi.scanNetworks() puede tardar unos segundos
    int numNetworks = WiFi.scanNetworks(); // true = async, false = sync (default)
    Serial.print("[INFO] Escaneo WiFi completado. Redes encontradas: "); Serial.println(numNetworks);

    if (numNetworks == WIFI_SCAN_FAILED) { // -1
         Serial.println("[ERROR] Falló el escaneo WiFi.");
         server.send(500, "application/json", "{\"error\":\"Scan failed\"}");
         return;
    }
    if (numNetworks == 0) {
         Serial.println("[INFO] No se encontraron redes WiFi.");
         server.send(200, "application/json", "[]"); // Enviar array vacío
         return;
    }

    // Estimar tamaño JSON: ~70 bytes por red + overhead
    // Usar DynamicJsonDocument para tamaño variable
    DynamicJsonDocument wifiJson(numNetworks * 80 + 50); // Ajustar tamaño si es necesario
    JsonArray networks = wifiJson.to<JsonArray>();

    // Limitar el número de redes enviadas para no sobrecargar JSON/memoria
    int maxNetworksToSend = (numNetworks > 20) ? 20 : numNetworks;
    Serial.print("[INFO] Enviando información de las (hasta) "); Serial.print(maxNetworksToSend); Serial.println(" redes más fuertes.");

    for (int i = 0; i < maxNetworksToSend; ++i) {
        JsonObject network = networks.createNestedObject();
        network["ssid"] = WiFi.SSID(i);
        network["rssi"] = WiFi.RSSI(i);
        // Mapear tipo de encriptación a string legible
        switch (WiFi.encryptionType(i)) {
            // Basado en ESP8266WiFiScan.h
            case ENC_TYPE_WEP:  network["encryption"] = "WEP"; break;       // 5
            case ENC_TYPE_TKIP: network["encryption"] = "WPA/PSK"; break;   // 2 (WPA-PSK)
            case ENC_TYPE_CCMP: network["encryption"] = "WPA2/PSK"; break;  // 4 (WPA2-PSK)
            case ENC_TYPE_NONE: network["encryption"] = "Open"; break;      // 7
            case ENC_TYPE_AUTO: network["encryption"] = "WPA/WPA2/PSK"; break; // 8 (WPA/WPA2-PSK) - Puede requerir ajustes
            // Podrían existir otros valores, mapear los más comunes
            default:            network["encryption"] = "Unknown (" + String(WiFi.encryptionType(i)) + ")"; break;
        }
    }

    // Serializar y enviar
    String response;
    serializeJson(wifiJson, response);
    server.send(200, "application/json", response);
}

// Handler para /connectWifi - Conectar manualmente (sin guardar)
void handleConnectWifi() {
    Serial.println("[HTTP] Solicitud /connectWifi (POST).");
    server.sendHeader("Access-Control-Allow-Origin", "*");
    if (!server.hasArg("plain")) {
        server.send(400, "text/plain", "Bad Request: Missing request body");
        return;
    }
    String body = server.arg("plain");
    StaticJsonDocument<256> doc; // Suficiente para SSID y password
    DeserializationError error = deserializeJson(doc, body);

    if (error) {
        Serial.print("[ERROR] JSON inválido en /connectWifi: "); Serial.println(error.c_str());
        server.send(400, "text/plain", "Bad Request: Invalid JSON format");
        return;
    }

    if (!doc.containsKey("ssid") || !doc["ssid"].is<const char*>()) {
        server.send(400, "text/plain", "Bad Request: Missing or invalid 'ssid'");
        return;
    }
    const char* ssid_c = doc["ssid"];
    if (strlen(ssid_c) == 0) {
        server.send(400, "text/plain", "Bad Request: SSID cannot be empty");
        return;
    }
    String ssid = String(ssid_c);
    // Password es opcional (redes abiertas)
    String password = doc["password"] | ""; // Usar "" si no se proporciona "password"

    Serial.print("[ACCION] Intentando conectar (manual, sin guardar): '"); Serial.print(ssid); Serial.println("'...");

    // Poner en modo STA si no lo está ya
    if (WiFi.getMode() != WIFI_STA) {
        WiFi.mode(WIFI_STA);
        delay(100); // Pequeña pausa para que el modo cambie
    }
    WiFi.begin(ssid.c_str(), password.c_str());

    // Esperar conexión (máximo ~15 segundos)
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    Serial.println(); // Nueva línea

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("[INFO] Conexión WiFi manual exitosa.");
        Serial.print("[INFO] IP: "); Serial.println(WiFi.localIP());
        dnsServer.stop(); // Detener DNS si estaba en modo AP
        server.send(200, "application/json", "{\"status\":\"success\", \"ip\":\"" + WiFi.localIP().toString() + "\"}");
        // Intentar sincronizar NTP ahora que hay conexión
        syncNtpTime(); // Intentar sincronizar ahora
        lastNtpSyncAttempt = millis(); // Resetear timer de sync NTP
    } else {
        Serial.println("[ERROR] Conexión WiFi manual falló.");
        Serial.print("[DEBUG] Estado WiFi final: "); Serial.println(WiFi.status());
        // Importante: Desconectar explícitamente para limpiar el intento fallido
        WiFi.disconnect(false); // false = no borrar config interna del SDK
        // Volver a modo AP solo si no hay credenciales guardadas,
        // de lo contrario, en el próximo reinicio intentará conectar con las guardadas.
        if (!LittleFS.exists("/WifiConfig.txt")) {
            Serial.println("[INFO] No hay credenciales guardadas, volviendo a modo AP.");
            startAPMode();
        } else {
             Serial.println("[INFO] Hay credenciales guardadas, permanecerá en modo STA para reintentar al reiniciar.");
             // Opcionalmente, podrías forzar modo AP aquí si prefieres
             // startAPMode();
        }
        server.send(401, "application/json", "{\"status\":\"failed\", \"message\":\"Connection failed\"}");
    }
}

// Handler para /saveWifiCredentials - Validar, guardar y reiniciar
void handleSaveWifiCredentials() {
     Serial.println("[HTTP] Solicitud /saveWifiCredentials (POST).");
     server.sendHeader("Access-Control-Allow-Origin", "*");
     if (!server.hasArg("plain")) {
         server.send(400, "text/plain", "Bad Request: Missing request body");
         return;
     }
     String body = server.arg("plain");
     StaticJsonDocument<256> doc;
     DeserializationError error = deserializeJson(doc, body);

     if (error) {
         Serial.print("[ERROR] JSON inválido en /saveWifiCredentials: "); Serial.println(error.c_str());
         server.send(400, "text/plain", "Bad Request: Invalid JSON format");
         return;
     }

     if (!doc.containsKey("ssid") || !doc["ssid"].is<const char*>()) {
         server.send(400, "text/plain", "Bad Request: Missing or invalid 'ssid'");
         return;
     }
     const char* ssid_c = doc["ssid"];
     if (strlen(ssid_c) == 0) {
         server.send(400, "text/plain", "Bad Request: SSID cannot be empty");
         return;
     }
     String ssid = String(ssid_c);
     String password = doc["password"] | "";

     Serial.print("[ACCION] Validando y guardando credenciales para: '"); Serial.print(ssid); Serial.println("'...");

     // Intentar conectar para validar las credenciales antes de guardar
     if (WiFi.getMode() != WIFI_STA) {
         WiFi.mode(WIFI_STA);
         delay(100);
     }
     WiFi.begin(ssid.c_str(), password.c_str());
     int attempts = 0;
     while (WiFi.status() != WL_CONNECTED && attempts < 30) { // Esperar ~15s
         delay(500);
         Serial.print(".");
         attempts++;
     }
     Serial.println();

     if (WiFi.status() == WL_CONNECTED) {
         Serial.println("[INFO] Conexión de prueba exitosa. Guardando credenciales...");
         File file = LittleFS.open("/WifiConfig.txt", "w");
         if (!file) {
             Serial.println("[ERROR] No se pudo abrir 'WifiConfig.txt' para escritura.");
             server.send(500, "application/json", "{\"status\":\"error\", \"message\":\"Cannot save configuration file\"}");
             return;
         }
         file.println(ssid);
         file.println(password);
         file.close();
         Serial.println("[INFO] Credenciales guardadas. Reiniciando el sistema para aplicar...");
         // Enviar respuesta ANTES de reiniciar
         server.send(200, "application/json", "{\"status\":\"success\", \"message\":\"Credentials saved. Restarting device...\"}");
         delay(1000); // Dar tiempo para que la respuesta HTTP se envíe
         ESP.restart();
     } else {
         Serial.println("[ERROR] Falló la conexión de prueba con las nuevas credenciales. No se guardaron.");
         Serial.print("[DEBUG] Estado WiFi final: "); Serial.println(WiFi.status());
         WiFi.disconnect(false);
         // Volver a modo AP para que el usuario pueda intentarlo de nuevo desde la página de config
         startAPMode();
         server.send(401, "application/json", "{\"status\":\"failed\", \"message\":\"Connection test failed. Credentials not saved.\"}");
     }
}

// Handler para /loadMeasurement - Cargar historial
void handleLoadMeasurement() {
    Serial.println("[HTTP] Solicitud /loadMeasurement (GET).");
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    String formattedJsonArray;
    // Usa la función que formatea el array en memoria a un string JSON "[{},{},...]"
    formatMeasurementsToString(formattedJsonArray);
    server.send(200, "application/json", formattedJsonArray);
}

// Handler para /clearHistory - Borrar historial
void handleClearMeasurementHistory() {
     Serial.println("[HTTP] Solicitud /clearHistory (POST).");
     server.sendHeader("Access-Control-Allow-Origin", "*");
     Serial.println("[ACCION] Borrando historial de mediciones...");

     // Limpiar el array en memoria
     for (int i = 0; i < MAX_JSON_OBJECTS; i++) {
         measurements[i] = ""; // O asignar nullptr si se usa String*
     }
     jsonIndex = 0; // Resetear el índice

     // Borrar el archivo del sistema de archivos
     if (LittleFS.exists("/Measurements.txt")) {
        if (LittleFS.remove("/Measurements.txt")) {
            Serial.println("[INFO] Archivo 'Measurements.txt' borrado.");
        } else {
            Serial.println("[ERROR] No se pudo borrar 'Measurements.txt'.");
            // Continuar de todos modos, al menos el array en memoria está limpio
        }
     } else {
         Serial.println("[INFO] 'Measurements.txt' no existía, no se requiere borrado.");
     }

     Serial.println("[INFO] Historial de mediciones borrado (memoria y archivo).");
     server.send(200, "application/json", "{\"status\":\"success\", \"message\":\"Measurement history cleared\"}");
}

// Handler para /getMeasurementInterval y /setMeasurementInterval
void handleMeasurementInterval() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");

    if (server.method() == HTTP_POST) {
        // --- SET INTERVAL ---
        Serial.println("[HTTP] Solicitud /setMeasurementInterval (POST).");
        if (!server.hasArg("plain")) {
            server.send(400, "text/plain", "Bad Request: Missing request body");
            return;
        }
        StaticJsonDocument<128> doc; // Suficiente para {"interval": 123}
        DeserializationError error = deserializeJson(doc, server.arg("plain"));
        if (error) {
            server.send(400, "text/plain", "Bad Request: Invalid JSON");
            return;
        }
        if (!doc.containsKey("interval") || !doc["interval"].is<int>()) {
            server.send(400, "text/plain", "Bad Request: Missing or invalid 'interval' field (must be integer)");
            return;
        }

        int newInterval = doc["interval"];
        // Validar el rango del intervalo (ej: 1 hora a 1 semana en horas)
        if (newInterval > 0 && newInterval < (7 * 24 + 1)) { // 1 a 168 horas
            Serial.print("[ACCION] Ajustando intervalo de medición a: "); Serial.print(newInterval); Serial.println(" horas.");
            saveMeasurementInterval(newInterval); // Guarda en archivo y actualiza variable global
            server.send(200, "application/json", "{\"status\":\"success\", \"interval\": " + String(measurementInterval) + "}");
        } else {
            Serial.print("[ERROR] Intervalo inválido recibido: "); Serial.println(newInterval);
            server.send(400, "application/json", "{\"status\":\"error\", \"message\":\"Invalid interval value. Must be between 1 and 168 hours.\"}");
        }

    } else if (server.method() == HTTP_GET) {
         // --- GET INTERVAL ---
         Serial.println("[HTTP] Solicitud /getMeasurementInterval (GET).");
         StaticJsonDocument<64> doc;
         doc["interval"] = measurementInterval; // Devolver el valor actual en memoria
         String response;
         serializeJson(doc, response);
         server.send(200, "application/json", response);
    } else {
         // Método no soportado (ej: PUT, DELETE)
         server.send(405, "text/plain", "Method Not Allowed");
    }
}

// Handler para /controlPump - Control manual bomba
void handlePumpControl() {
    Serial.println("[HTTP] Solicitud /controlPump (POST).");
     server.sendHeader("Access-Control-Allow-Origin", "*");
     if (!server.hasArg("plain")) {
         server.send(400, "text/plain", "Bad Request: Missing request body");
         return;
     }
     StaticJsonDocument<128> doc; // Suficiente para action y duration
     DeserializationError error = deserializeJson(doc, server.arg("plain"));
     if (error) {
         server.send(400, "text/plain", "Bad Request: Invalid JSON");
         return;
     }
     if (!doc.containsKey("action") || !doc["action"].is<String>()) {
         server.send(400, "text/plain", "Bad Request: Missing or invalid 'action' field (must be string 'on' or 'off')");
         return;
     }
     String action = doc["action"].as<String>();

     if (action.equalsIgnoreCase("on")) {
         // Acción ON requiere duración
         if (!doc.containsKey("duration") || !doc["duration"].is<int>()) {
             server.send(400, "application/json", "{\"status\":\"error\", \"message\":\"Missing or invalid 'duration' field for action 'on' (must be integer seconds)\"}");
             return;
         }
         int durationSec = doc["duration"];
         // Validar duración (ej: 1 segundo a 10 minutos)
         if (durationSec <= 0 || durationSec > 600) {
             server.send(400, "application/json", "{\"status\":\"error\", \"message\":\"Invalid duration. Must be between 1 and 600 seconds.\"}");
             return;
         }
         Serial.print("[ACCION] Encendiendo bomba (manual HTTP) por "); Serial.print(durationSec); Serial.println("s.");
         activatePump(durationSec * 1000UL); // Llama a la función que activa con auto-off
         server.send(200, "application/json", "{\"status\":\"success\", \"pumpStatus\":\"on\", \"duration\":" + String(durationSec) + "}");
     } else if (action.equalsIgnoreCase("off")) {
         // Acción OFF no necesita duración
         Serial.println("[ACCION] Apagando bomba (manual HTTP).");
         deactivatePump(); // Llama a la función que desactiva
         server.send(200, "application/json", "{\"status\":\"success\", \"pumpStatus\":\"off\"}");
     } else {
         // Acción desconocida
         Serial.print("[ERROR] Acción de bomba inválida recibida: "); Serial.println(action);
         server.send(400, "application/json", "{\"status\":\"error\", \"message\":\"Invalid action specified. Use 'on' or 'off'.\"}");
     }
}

// Handler para /setManualStage - Establecer etapa manual
void handleSetManualStage() {
    Serial.println("[HTTP] Solicitud /setManualStage (POST).");
    server.sendHeader("Access-Control-Allow-Origin", "*");
     if (!server.hasArg("plain")) {
         server.send(400, "text/plain", "Bad Request: Missing body");
         return;
     }
     StaticJsonDocument<128> doc;
     DeserializationError error = deserializeJson(doc, server.arg("plain"));
     if (error) {
         server.send(400, "text/plain", "Bad Request: Invalid JSON");
         return;
     }
     // Espera un JSON como {"stage": index}
     if (!doc.containsKey("stage") || !doc["stage"].is<int>()) {
         server.send(400, "text/plain", "Bad Request: Missing or invalid 'stage' index (must be integer)");
         return;
     }
     int stageIndex = doc["stage"];
     // Validar índice contra el número de etapas definidas
     if (stageIndex >= 0 && stageIndex < numStages) {
         Serial.print("[ACCION] Estableciendo etapa manual (HTTP) a índice: "); Serial.println(stageIndex);
         saveManualStage(stageIndex); // Guarda en archivo y activa flag
         server.send(200, "application/json", "{\"status\":\"success\", \"message\":\"Manual stage set to index " + String(stageIndex) + "\"}");
     } else {
         Serial.print("[ERROR] Índice de etapa manual inválido recibido: "); Serial.println(stageIndex);
         server.send(400, "application/json", "{\"status\":\"error\", \"message\":\"Invalid stage index provided.\"}");
     }
}

// Handler para /getCurrentStage - Obtener etapa actual y sus parámetros (MODIFIED FOR NTP)
void handleGetCurrentStage() {
     Serial.println("[HTTP] Solicitud /getCurrentStage (GET).");
     server.sendHeader("Access-Control-Allow-Origin", "*");
     server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");

     // Calcular etapa actual basada en tiempo transcurrido (NTP) o modo manual
     time_t now_t = time(nullptr);
     uint32_t elapsedDays = 0;
     bool timeValid = ntpTimeSynchronized && (now_t >= ntpBootEpoch);

     if(timeValid){
         elapsedDays = (now_t - ntpBootEpoch) / 86400UL;
     } // Si no, elapsedDays permanece 0

     int stageIndex = getCurrentStageIndex(elapsedDays); // Esta función ya considera manualStageControl y NTP sync state
     const Stage& currentStage = stages[stageIndex]; // Obtener referencia a la etapa actual (puede tener valores modificados)

     // Crear JSON de respuesta
     StaticJsonDocument<256> doc; // Suficiente para los datos de la etapa
     doc["currentStageName"] = currentStage.name;
     doc["currentStageIndex"] = stageIndex;
     doc["manualControlActive"] = manualStageControl; // Indicar si el modo manual está activo
     doc["calculatedElapsedDays"] = elapsedDays; // Informar los días calculados (puede ser 0)
     doc["isTimeSynchronized"] = ntpTimeSynchronized; // Informar si la hora es válida

     // Incluir parámetros actuales de la etapa (los que pueden haber sido modificados)
     JsonObject params = doc.createNestedObject("currentParams");
     params["humidityThreshold"] = currentStage.humidityThreshold;
     params["wateringTimeSec"] = currentStage.wateringTimeSec;
     params["duration_days"] = currentStage.duration_days; // Incluir duración también

     String response;
     serializeJson(doc, response);
     server.send(200, "application/json", response);
}

// Handler para /resetManualStage - Volver a control automático de etapas
void handleResetManualStage() {
     Serial.println("[HTTP] Solicitud /resetManualStage (POST).");
     server.sendHeader("Access-Control-Allow-Origin", "*");

     if (manualStageControl) { // Solo actuar si estaba en modo manual
        manualStageControl = false; // Desactivar flag en memoria
        manualStageIndex = 0; // Resetear índice (aunque no se use en auto)

        // Borrar el archivo que activa el modo manual al inicio
        if (LittleFS.exists("/ManualStage.txt")) {
            if (LittleFS.remove("/ManualStage.txt")) {
                 Serial.println("[INFO] Archivo 'ManualStage.txt' borrado. Control automático activado.");
            } else {
                 Serial.println("[ERROR] No se pudo borrar 'ManualStage.txt'. Control automático activado en memoria.");
            }
        } else {
             Serial.println("[INFO] Control manual desactivado (archivo no existía).");
        }
        server.send(200, "application/json", "{\"status\":\"success\", \"message\":\"Manual stage control deactivated. Automatic control enabled.\"}");
     } else {
        Serial.println("[INFO] Control manual ya estaba desactivado. No se realizó ninguna acción.");
        server.send(200, "application/json", "{\"status\":\"success\", \"message\":\"Manual stage control was already inactive.\"}");
     }
}

// Handler para /listStages - Listar todas las etapas (CON SU CONFIGURACIÓN ACTUAL)
void handleListStages() {
    Serial.println("[HTTP] Solicitud /listStages (GET).");
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");

    // Crear un array JSON para las etapas
    StaticJsonDocument<768> doc; // Ajustar tamaño si hay muchas etapas o datos extra
    JsonArray stageArray = doc.to<JsonArray>();

    // Iterar sobre el array 'stages' en memoria (que puede tener valores modificados)
    for (int i = 0; i < numStages; i++) {
        JsonObject stageObj = stageArray.createNestedObject();
        stageObj["index"]             = i; // Índice numérico
        stageObj["name"]              = stages[i].name; // Nombre (constante)
        stageObj["duration_days"]     = stages[i].duration_days; // Duración (actualmente no editable por UI, pero se lista)
        stageObj["humidityThreshold"] = stages[i].humidityThreshold; // Umbral (editable)
        stageObj["wateringTimeSec"]   = stages[i].wateringTimeSec; // Tiempo riego (editable)
    }

    String response;
    serializeJson(stageArray, response);
    server.send(200, "application/json", response);
}

// Handler para /updateStage - Actualizar parámetros de una etapa específica
void handleUpdateStage() {
    Serial.println("[HTTP] Solicitud /updateStage (POST).");
    server.sendHeader("Access-Control-Allow-Origin", "*");

    if (!server.hasArg("plain")) {
        server.send(400, "text/plain", "Bad Request: Missing request body");
        return;
    }
    StaticJsonDocument<192> doc; // Tamaño para index y 2-3 parámetros
    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    if (error) {
        server.send(400, "text/plain", "Bad Request: Invalid JSON");
        return;
    }

    // Validar que el índice existe y es válido
    if (!doc.containsKey("index") || !doc["index"].is<int>()) {
        server.send(400, "text/plain", "Bad Request: Missing or invalid 'index'");
        return;
    }
    int idx = doc["index"];
    if (idx < 0 || idx >= numStages) {
        server.send(400, "text/plain", "Bad Request: Invalid stage index value");
        return;
    }

    // Actualizar parámetros si están presentes en el JSON y son válidos
    bool updated = false;
    if (doc.containsKey("humidityThreshold") && doc["humidityThreshold"].is<int>()) {
        int newThreshold = doc["humidityThreshold"];
        if (newThreshold >= 0 && newThreshold <= 100) { // Validar rango
            stages[idx].humidityThreshold = newThreshold;
            Serial.print("[UPDATE STAGE] Etapa "); Serial.print(idx); Serial.print(" - Nuevo umbral: "); Serial.println(newThreshold);
            updated = true;
        } else { Serial.println("[UPDATE STAGE WARN] Umbral inválido ignorado."); }
    }
    if (doc.containsKey("wateringTimeSec") && doc["wateringTimeSec"].is<int>()) {
        int newWateringTime = doc["wateringTimeSec"];
        if (newWateringTime >= 0 && newWateringTime <= 600) { // Validar rango (0 a 10 min)
            stages[idx].wateringTimeSec = newWateringTime;
             Serial.print("[UPDATE STAGE] Etapa "); Serial.print(idx); Serial.print(" - Nuevo tiempo riego: "); Serial.println(newWateringTime);
            updated = true;
        } else { Serial.println("[UPDATE STAGE WARN] Tiempo riego inválido ignorado."); }
    }
    // Opcional: Actualizar duración si se permite (descomentar si se implementa)
    /*
    if (doc.containsKey("duration_days") && doc["duration_days"].is<int>()) { ... }
    */

    if (updated) {
        // Guardar la configuración actualizada en el archivo
        if (saveStagesConfig()) {
            // Devolver la lista completa de etapas actualizada como confirmación
            handleListStages();
        } else {
            server.send(500, "application/json", "{\"status\":\"error\", \"message\":\"Failed to save updated stage configuration to file.\"}");
        }
    } else {
        Serial.println("[UPDATE STAGE] No se proporcionaron parámetros válidos para actualizar.");
        server.send(400, "application/json", "{\"status\":\"error\", \"message\":\"No valid parameters provided to update.\"}");
    }
}


// Inicia modo AP (Access Point)
void startAPMode() {
    const char* ap_ssid = "GreenNanny-Setup";
    const char* ap_password = "password123"; // Contraseña WPA2-PSK
    Serial.print("[ACCION] Iniciando Modo AP: SSID '"); Serial.print(ap_ssid); Serial.println("'...");

    WiFi.persistent(false); // No guardar la configuración de AP en memoria flash
    WiFi.disconnect(true); // Desconectar de cualquier red anterior y borrar SSID/Pass
    WiFi.mode(WIFI_AP);
    delay(100); // Pequeña pausa

    // Intentar iniciar el AP
    if(WiFi.softAP(ap_ssid, ap_password)) {
        IPAddress apIP = WiFi.softAPIP(); // IP por defecto suele ser 192.168.4.1
        Serial.print("[INFO] AP iniciado correctamente.");
        Serial.print(" Conéctate a la red WiFi '"); Serial.print(ap_ssid);
        Serial.print("' y visita http://"); Serial.println(apIP);

        // Iniciar el servidor DNS para el portal cautivo
        // Redirige todas las solicitudes DNS a la IP del AP
        if (dnsServer.start(DNS_PORT, "*", apIP)) {
            Serial.println("[INFO] Servidor DNS para portal cautivo iniciado.");
        } else {
            Serial.println("[ERROR] Falló el inicio del servidor DNS.");
        }
    } else {
        Serial.println("[ERROR] Falló el inicio del modo AP!");
    }
}

// Configura servidor web y endpoints
void setupServer() {
    Serial.println("[SETUP] Configurando servidor web...");

    // --- Página Principal (Redirección) ---
    server.on("/", HTTP_GET, handleRoot);

    // --- Endpoints API GET (Obtener datos) ---
    server.on("/data", HTTP_GET, handleData);                   // Estado general, sensores, bomba, etapa actual, red (NTP BASED)
    server.on("/loadMeasurement", HTTP_GET, handleLoadMeasurement); // Historial de mediciones
    server.on("/getMeasurementInterval", HTTP_GET, handleMeasurementInterval); // Intervalo de medición actual
    server.on("/getCurrentStage", HTTP_GET, handleGetCurrentStage); // Detalles de la etapa actual (NTP BASED)
    server.on("/listStages", HTTP_GET, handleListStages);       // Lista todas las etapas con su config actual
    server.on("/wifiList", HTTP_GET, handleWifiListRequest);    // Escanea y lista redes WiFi cercanas

    // --- Endpoints API POST (Modificar estado o configuración) ---
    server.on("/setMeasurementInterval", HTTP_POST, handleMeasurementInterval); // Establecer intervalo
    server.on("/controlPump", HTTP_POST, handlePumpControl);             // Encender/apagar bomba manualmente
    server.on("/setManualStage", HTTP_POST, handleSetManualStage);       // Activar control manual de etapa
    server.on("/resetManualStage", HTTP_POST, handleResetManualStage);   // Desactivar control manual
    server.on("/updateStage", HTTP_POST, handleUpdateStage);             // Modificar parámetros de una etapa
    server.on("/clearHistory", HTTP_POST, handleClearMeasurementHistory); // Borrar historial mediciones
    server.on("/connectWifi", HTTP_POST, handleConnectWifi);             // Probar conexión WiFi (sin guardar)
    server.on("/saveWifiCredentials", HTTP_POST, handleSaveWifiCredentials); // Guardar creds WiFi y reiniciar

    // --- Acciones Directas (POST sin cuerpo JSON complejo) ---
    // Forzar una medición ahora mismo
    server.on("/takeMeasurement", HTTP_POST, [](){
         Serial.println("[HTTP] Solicitud /takeMeasurement (POST).");
         server.sendHeader("Access-Control-Allow-Origin", "*");
         controlIndependiente(); // Ejecuta el ciclo de control principal (NTP BASED)
         server.send(200, "application/json", "{\"status\":\"success\", \"message\":\"Measurement cycle triggered successfully.\"}");
    });
    // Reiniciar el dispositivo
    server.on("/restartSystem", HTTP_POST, []() {
        Serial.println("[HTTP] Solicitud /restartSystem (POST).");
        server.sendHeader("Access-Control-Allow-Origin", "*");
        // Enviar respuesta ANTES de reiniciar
        server.send(200, "application/json", "{\"status\":\"restarting\", \"message\":\"Device is restarting...\"}");
        Serial.println("[ACCION] Reiniciando sistema por petición HTTP...");
        delay(1000); // Dar tiempo a enviar la respuesta
        ESP.restart();
    });

    // --- Servir Archivos Estáticos (onNotFound con Gzip y Cache) ---
    server.onNotFound([]() {
        String path = server.uri();
        if (path.indexOf("..") != -1) {
             server.send(400, "text/plain", "Bad Request");
             return;
        }
        Serial.print("[HTTP Static] Request: "); Serial.println(path);
        if (path.endsWith("/")) path += "index.html";

        String contentType = "text/plain";
        if (path.endsWith(".html")) contentType = "text/html";
        else if (path.endsWith(".css")) contentType = "text/css";
        else if (path.endsWith(".js")) contentType = "application/javascript";
        else if (path.endsWith(".png")) contentType = "image/png";
        else if (path.endsWith(".jpg")) contentType = "image/jpeg";
        else if (path.endsWith(".ico")) contentType = "image/x-icon";
        else if (path.endsWith(".svg")) contentType = "image/svg+xml";
        else if (path.endsWith(".json")) contentType = "application/json";
        else if (path.endsWith(".gz")) contentType = "application/x-gzip"; // For serving .gz explicitly if needed, though handled below

        String pathWithGz = path + ".gz";
        bool clientAcceptsGzip = server.hasHeader("Accept-Encoding") && server.header("Accept-Encoding").indexOf("gzip") != -1;

        if (clientAcceptsGzip && LittleFS.exists(pathWithGz)) {
            Serial.print("[HTTP Static] Serving Gzipped: "); Serial.println(pathWithGz);
            File file = LittleFS.open(pathWithGz, "r");
            if (file) {
                server.sendHeader("Content-Encoding", "gzip");
                server.sendHeader("Cache-Control", "max-age=86400");
                server.streamFile(file, contentType); // Pass original content type
                file.close();
                return;
            } else {
                 Serial.print("[ERROR] Could not open Gzipped: "); Serial.println(pathWithGz);
            }
        }

        if (LittleFS.exists(path)) {
            Serial.print("[HTTP Static] Serving Ungzipped: "); Serial.println(path);
            File file = LittleFS.open(path, "r");
            if (file) {
                server.sendHeader("Cache-Control", "max-age=86400");
                server.streamFile(file, contentType);
                file.close();
                return;
            } else {
                 Serial.print("[ERROR] Could not open Ungzipped: "); Serial.println(path);
            }
        }

        Serial.print("[HTTP Static] File Not Found: "); Serial.println(path);
        if (WiFi.getMode() == WIFI_AP) {
             Serial.println("[CAPTIVE] Redirecting to captive portal (/).");
             server.sendHeader("Location", "/", true);
             server.send(302, "text/plain", "Redirecting to captive portal...");
        } else {
            server.send(404, "text/plain", "404: File Not Found");
        }
    });

    // Iniciar el servidor
    server.begin();
    Serial.println("[INFO] Servidor HTTP iniciado en puerto 80.");
}

// Maneja comandos seriales (opcional, para debug) - MODIFIED FOR NTP
void handleSerialCommands() {
    if (Serial.available() > 0) {
        String command = Serial.readStringUntil('\n');
        command.trim();
        Serial.print("\n[SERIAL CMD] Comando recibido: '"); Serial.print(command); Serial.println("'");

        if (command.equalsIgnoreCase("STATUS")) {
             time_t now_t = time(nullptr);
             struct tm timeinfo;
             bool timeValid = ntpTimeSynchronized && (now_t >= ntpBootEpoch);
             char buf[40]; // Increased size slightly for safety

             Serial.println("--- SERIAL STATUS ---");
             if(timeValid) {
                 localtime_r(&now_t, &timeinfo);
                 strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z", &timeinfo);
                 uint32_t elapsedSeconds = now_t - ntpBootEpoch;
                 Serial.print("System Time: "); Serial.println(buf);
                 Serial.print("Uptime (NTP): "); Serial.print(elapsedSeconds / 86400); Serial.print("d "); Serial.print((elapsedSeconds % 86400) / 3600); Serial.print("h "); Serial.print((elapsedSeconds % 3600) / 60); Serial.println("m");
             } else {
                 Serial.println("System Time: N/A (NTP Pending)");
                 Serial.println("Uptime (NTP): N/A");
             }
             Serial.print("NTP Synced: "); Serial.println(ntpTimeSynchronized ? "Yes" : "No");
             Serial.print("Uptime (millis): "); Serial.print(millis() / 1000); Serial.println("s");
             float h = getHumidity(); float t = getTemperature();
             Serial.print("Sensor: Temp="); Serial.print(t, 1); Serial.print("C, Hum="); Serial.print(h, 1); Serial.print("%, VPD~="); Serial.print(calculateVPD(t, h), 2); Serial.println("kPa");
             Serial.print("Pump: "); Serial.print(pumpActivated ? "ON" : "OFF");
             if (pumpAutoOff) {
                 unsigned long currentMillis = millis();
                 unsigned long elapsedPumpMs = currentMillis - pumpOnTime;
                 unsigned long remainingPumpMs = (pumpDurationMs > elapsedPumpMs) ? (pumpDurationMs - elapsedPumpMs) : 0;
                 Serial.print(" (Auto-Off, remaining ~"); Serial.print(remainingPumpMs / 1000); Serial.print("s)");
             }
             Serial.println();
             Serial.print("Pump Activations: "); Serial.println(pumpActivationCount);

             uint32_t elapsedDays = 0;
             if(timeValid) elapsedDays = (now_t - ntpBootEpoch) / 86400UL;
             int stageIdx = getCurrentStageIndex(elapsedDays);
             Serial.print("Stage: ["); Serial.print(stageIdx); Serial.print("] "); Serial.print(stages[stageIdx].name);
             Serial.print(" ("); Serial.print(manualStageControl ? "Manual" : "Auto"); Serial.print(")");
             Serial.print(" - Thr: "); Serial.print(stages[stageIdx].humidityThreshold); Serial.print("%, Wat: "); Serial.print(stages[stageIdx].wateringTimeSec); Serial.println("s");

             Serial.print("WiFi Mode: "); Serial.print(WiFi.getMode() == WIFI_AP ? "AP" : (WiFi.getMode() == WIFI_STA ? "STA" : "OFF"));
             Serial.print(", Status: "); Serial.print(WiFi.status() == WL_CONNECTED ? "Connected" : (WiFi.status() == WL_IDLE_STATUS ? "Idle" : (WiFi.status() == WL_CONNECT_FAILED ? "Failed" : String(WiFi.status()))));
             if (WiFi.status() == WL_CONNECTED) { Serial.print(", IP: "); Serial.print(WiFi.localIP()); Serial.print(", RSSI: "); Serial.print(WiFi.RSSI()); Serial.print("dBm"); }
             else if (WiFi.getMode() == WIFI_AP) { Serial.print(", AP IP: "); Serial.print(WiFi.softAPIP()); }
             Serial.println();
             Serial.print("Measurement Interval: "); Serial.print(measurementInterval); Serial.println("h");
             unsigned long currentMillis = millis();
             unsigned long remainingMs = (nextMeasureTimestamp > currentMillis) ? (nextMeasureTimestamp - currentMillis) : 0;
             Serial.print("Next Measurement ~: "); Serial.print(remainingMs / 3600000UL); Serial.print("h "); Serial.print((remainingMs % 3600000UL) / 60000UL); Serial.println("m");
             Serial.print("History Records: "); Serial.println(jsonIndex);
             Serial.print("Free Heap: "); Serial.println(ESP.getFreeHeap());
             Serial.println("--- Current Stages Config ---");
             for(int i=0; i<numStages; i++) {
                Serial.printf(" [%d] %-12s: Dur=%3dd, Thr=%3d%%, Wat=%3ds\n",
                              i, stages[i].name, stages[i].duration_days,
                              stages[i].humidityThreshold, stages[i].wateringTimeSec);
             }
             Serial.println("--- END STATUS ---");
        }
        else if (command.equalsIgnoreCase("MEASURE")) {
            Serial.println("[SERIAL] Forzando ciclo de medición y control...");
            controlIndependiente();
        }
        else if (command.startsWith("PUMP ON ")) {
            int duration = command.substring(8).toInt();
            if (duration > 0 && duration <= 600) {
                 Serial.print("[SERIAL] Encendiendo bomba por "); Serial.print(duration); Serial.println(" segundos...");
                 activatePump(duration * 1000UL);
            } else {
                 Serial.println("[ERROR] Duración inválida para PUMP ON (1-600s). Usando 30s por defecto.");
                 activatePump(30000UL);
            }
        }
        else if (command.equalsIgnoreCase("PUMP ON")) { // Sin duración, usar default
            Serial.println("[SERIAL] Encendiendo bomba por 30 segundos (default)...");
            activatePump(30000UL);
        }
        else if (command.equalsIgnoreCase("PUMP OFF")) {
            Serial.println("[SERIAL] Apagando bomba...");
            deactivatePump();
        }
        else if (command.startsWith("SET STAGE ")) {
            int stage = command.substring(10).toInt();
            if (stage >= 0 && stage < numStages) {
                 Serial.print("[SERIAL] Estableciendo Etapa Manual a: ["); Serial.print(stage); Serial.print("] "); Serial.println(stages[stage].name);
                 saveManualStage(stage);
            } else { Serial.println("[ERROR] Índice de etapa inválido."); }
        }
        else if (command.equalsIgnoreCase("RESET STAGE")) {
            Serial.println("[SERIAL] Cambiando a control de etapa automático.");
            handleResetManualStage(); // Llama a la misma función que el handler HTTP
        }
        else if (command.equalsIgnoreCase("CLEAR")) {
            Serial.println("[SERIAL] Borrando historial de mediciones...");
            handleClearMeasurementHistory(); // Llama a la misma función que el handler HTTP
        }
        else if (command.equalsIgnoreCase("RESTART")) {
            Serial.println("[SERIAL] Reiniciando sistema...");
            ESP.restart();
        }
        else if (command.equalsIgnoreCase("NTP SYNC")) {
            Serial.println("[SERIAL] Forzando sincronización NTP...");
            if(syncNtpTime()) {
                Serial.println("[SERIAL] Sincronización NTP exitosa.");
                time_t now_t = time(nullptr);
                struct tm timeinfo;
                localtime_r(&now_t, &timeinfo);
                char buf[30];
                strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
                Serial.print("[SERIAL] Nueva hora sistema: "); Serial.println(buf);
            } else {
                 Serial.println("[SERIAL] Falló la sincronización NTP.");
            }
        }
        else if (command.startsWith("SET INTERVAL ")) {
            int interval = command.substring(13).toInt();
            if (interval > 0 && interval < 168) {
                 Serial.print("[SERIAL] Estableciendo intervalo de medición a "); Serial.print(interval); Serial.println("h...");
                 saveMeasurementInterval(interval);
            } else { Serial.println("[ERROR] Intervalo inválido (1-167h)."); }
        }
        else if (command.equalsIgnoreCase("LIST FILES")) {
            Serial.println("[SERIAL] Listando archivos en LittleFS:");
            #ifdef ESP8266
              Dir dir = LittleFS.openDir("/");
              while (dir.next()) {
                Serial.print("  - "); Serial.print(dir.fileName());
                File f = dir.openFile("r");
                Serial.print(" ("); Serial.print(f.size()); Serial.println(" bytes)");
                f.close();
              }
            #else // ESP32 API might differ slightly
              File root = LittleFS.open("/");
              File file = root.openNextFile();
              while(file){
                Serial.print("  - "); Serial.print(file.name());
                Serial.print(" ("); Serial.print(file.size()); Serial.println(" bytes)");
                file.close(); // Close the file handle
                file = root.openNextFile();
              }
              root.close(); // Close the root directory handle
            #endif
            Serial.println("--- Fin Listado ---");
        }
        else if (command.equalsIgnoreCase("FORMAT")) {
             Serial.println("\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
             Serial.println("!! ADVERTENCIA: ESTO BORRARÁ TODOS LOS DATOS Y   !!");
             Serial.println("!! CONFIGURACIONES (WiFi, historial, etapas...)!!");
             Serial.println("!! Escribe 'CONFIRM FORMAT' para proceder.       !!");
             Serial.println("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
        }
        else if (command.equalsIgnoreCase("CONFIRM FORMAT")) {
             Serial.println("[SERIAL] Iniciando formateo de LittleFS...");
             bool formatted = LittleFS.format();
             Serial.println(formatted ? "[SERIAL] LittleFS formateado correctamente. Reiniciando sistema..." : "[SERIAL] [ERROR] Falló el formateo de LittleFS.");
             delay(2000); // Pausa para leer mensaje
             ESP.restart();
        }
        else {
             Serial.println("[WARN] Comando desconocido. Comandos disponibles:");
             Serial.println("  STATUS         - Muestra estado actual detallado");
             Serial.println("  MEASURE        - Forza un ciclo de medición");
             Serial.println("  PUMP ON [secs] - Enciende bomba por X seg (default 30)");
             Serial.println("  PUMP OFF       - Apaga bomba");
             Serial.println("  SET STAGE <idx>- Establece etapa manual (ej: SET STAGE 0)");
             Serial.println("  RESET STAGE    - Vuelve a control automático de etapa");
             Serial.println("  SET INTERVAL <h> - Establece intervalo medición (1-167h)");
             Serial.println("  CLEAR          - Borra historial de mediciones");
             Serial.println("  NTP SYNC       - Forza sincronización hora NTP");
             Serial.println("  LIST FILES     - Lista archivos en LittleFS");
             Serial.println("  RESTART        - Reinicia el dispositivo");
             Serial.println("  FORMAT         - (PRECAUCIÓN) Inicia proceso de formateo");
        }
    }
}