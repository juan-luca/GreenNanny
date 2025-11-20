
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266NetBIOS.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#ifndef DISABLE_OTA
#include <ArduinoOTA.h>  // Conditional: saves ~2-3KB RAM when disabled
#endif
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
#define FAN_PIN D4          // Pin para ventilador
#define EXTRACTOR_PIN D5    // Pin para turbina de extracción

#define MAX_JSON_OBJECTS 30 // Reducido a 30 para liberar más RAM (~2.4KB total, ~1.2KB extra vs 50). Con mediciones cada 3h, 30 = 90 horas de historial
#define STAGES_CONFIG_FILE "/stages_config.json" // File for custom stage config
#define THRESHOLDS_CONFIG_FILE "/thresholds_config.json" // File for fan/extractor thresholds
#define DISCORD_CONFIG_FILE "/discord_config.json" // File for Discord webhook configuration

// Debug log buffer (reducido a 20 para liberar más RAM - cada slot ~80 bytes = 1.6KB total vs 2.4KB con 30)
#define DEBUG_LOG_SIZE 20
String debugLogBuffer[DEBUG_LOG_SIZE];
int debugLogIndex = 0;
int debugLogCount = 0;
bool discordProcessing = false;

// Health log skip counter (NEW)
uint32_t healthLogSkipCount = 0;

// Heap emergency thresholds (ajustados por requests HTTP agresivos)
#define HEAP_EMERGENCY_THRESHOLD 12000  // Crítico: reinicio inmediato (subido de 10KB para evitar loop)
#define HEAP_WARNING_THRESHOLD 15000    // Warning: purgar y desactivar funciones no críticas
#define HEAP_SAFE_THRESHOLD 16000       // Nivel ideal de heap libre
#define HEAP_LOGGING_THRESHOLD 14000    // Logging ultra-compacto si heap < 14KB

// RTC - REMOVED
// RTC_DS1307 rtc;

// Configuración de DNS y Captive Portal
DNSServer dnsServer;
const byte DNS_PORT = 53;

// Servidor web en el puerto 80
ESP8266WebServer server(80);

// HTTP Update Server para OTA
ESP8266HTTPUpdateServer httpUpdater;

// OTA Configuration
const char* OTA_PASSWORD = "greennanny2024"; // Cambiar esto por una contraseña segura
bool otaInProgress = false;

// Log file paths and limits (NEW)
static const char* HEALTH_LOG_FILE = "/health_log.txt";
static const char* HEALTH_LOG_PREV_FILE = "/health_log.prev";
static const char* LAST_RESTART_FILE = "/last_restart_reason.txt";
static const size_t HEALTH_LOG_MAX_BYTES = 64 * 1024; // 64KB rotation threshold

// Almacenamiento de mediciones
String measurements[MAX_JSON_OBJECTS];
int jsonIndex = 0;

// Configuración de red estática (opcional)
/*IPAddress ip(192, 168, 0, 73);
IPAddress gateway(192, 168, 0, 1);
IPAddress subnet(255, 255, 255, 0);*/

// Variables para la conexión WiFi asíncrona
enum WifiConnectionState { IDLE, SENDING_INSTRUCTIONS, ATTEMPTING_CONNECTION, CONNECTION_IN_PROGRESS };
WifiConnectionState wifiState = IDLE;
String targetSsid = "";
String targetPass = "";
unsigned long connectionAttemptStartMillis = 0;

const char* HOSTNAME = "greennanny"; // Nombre base preferido
// REMOVED: actualHostname and mdnsAdvertisedName to save ~160 bytes RAM
// These will be created as local variables only when needed

// WiFi por defecto si no hay configuracin guardada
const char* DEFAULT_WIFI_SSID = "Personal-244";
const char* DEFAULT_WIFI_PASSWORD = "0040640653";
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

// Caché de lecturas del sensor (actualizadas solo en mediciones programadas/manuales)
float cachedHumidity = -1.0;
float cachedTemperature = -99.0;

// Modo test para simular diferentes condiciones sin sensor
bool testModeEnabled = false;
unsigned long testModeStartTime = 0;
const unsigned long testCycleDuration = 60000; // 60 segundos por ciclo completo

// Discord webhook configuration
String discordWebhookUrl = "";
bool discordAlertsEnabled = false;
unsigned long lastDiscordAlert = 0;
const unsigned long discordAlertCooldown = 10000; // 10 segundos para testing (cambiar a 300000 en producción)

// Discord alert thresholds
struct DiscordAlertConfig {
    bool tempHighAlert;      // Alerta por temperatura alta
    float tempHighThreshold; // Umbral de temperatura alta (°C)
    bool tempLowAlert;       // Alerta por temperatura baja
    float tempLowThreshold;  // Umbral de temperatura baja (°C)
    bool humHighAlert;       // Alerta por humedad alta
    float humHighThreshold;  // Umbral de humedad alta (%)
    bool humLowAlert;        // Alerta por humedad baja
    float humLowThreshold;   // Umbral de humedad baja (%)
    bool sensorFailAlert;    // Alerta por fallo de sensor
    bool deviceActivationAlert; // Alerta cuando se activan dispositivos
};

DiscordAlertConfig discordAlerts = {
    true,   // tempHighAlert
    35.0,   // tempHighThreshold
    true,   // tempLowAlert
    15.0,   // tempLowThreshold
    true,   // humHighAlert
    85.0,   // humHighThreshold
    true,   // humLowAlert
    30.0,   // humLowThreshold
    true,   // sensorFailAlert
    false   // deviceActivationAlert (deshabilitado por defecto para no spam)
};

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

// Control de ventilador y turbina de extracción
bool fanActivated = false;         // Flag: Is the fan currently ON?
bool extractorActivated = false;   // Flag: Is the extractor currently ON?

// Umbrales para activación automática de ventilador y turbina
struct EnvironmentThresholds {
    float fanTempOn;        // Temperatura para encender ventilador (°C)
    float fanHumOn;         // Humedad para encender ventilador (%)
    float extractorTempOn;  // Temperatura para encender turbina (°C)
    float extractorHumOn;   // Humedad para encender turbina (%)
};

// Valores por defecto de umbrales
EnvironmentThresholds thresholds = {
    28.0,  // fanTempOn - ventilador se enciende si temp >= 28°C
    70.0,  // fanHumOn - ventilador se enciende si humedad >= 70%
    32.0,  // extractorTempOn - turbina se enciende si temp >= 32°C
    85.0   // extractorHumOn - turbina se enciende si humedad >= 85%
};

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
void setupFanAndExtractor();  // NEW: Setup for fan and extractor pins
void setupServer();
void setupOTA();               // NEW: Setup OTA updates
void startAPMode();
bool syncNtpTime(); // PROTOTIPO NTP (Modified: no RTC interaction)
void startNameServices(); // Inicia mDNS/NBNS con greennanny, greennanny2, ...
// Health Logging & Restart Management (NEW)
void appendHealthLog(const char* eventType = "PERIODIC");
void handleHealthLog(); // Tail health log
void handleDownloadLogs(); // Download aggregated logs
void scheduleRestart(const char* reason); // Persist reason & restart
void diagnoseHeapUsage(); // NEW: Diagnose which component consumes most heap
bool resetComponentProactive(const char* component); // NEW: RCP - Reset component to free memory
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
// Fan and Extractor Control (NEW)
void activateFan();
void deactivateFan();
void activateExtractor();
void deactivateExtractor();
void handleFanControl();       // Manual fan control via API
void handleExtractorControl(); // Manual extractor control via API
void controlFanAndExtractor(); // Automatic control based on thresholds
void loadThresholds();         // Load thresholds from file
void saveThresholds();         // Save thresholds to file
void handleGetThresholds();    // API endpoint to get thresholds
void handleSetThresholds();    // API endpoint to set thresholds
// Test Mode
void handleTestMode();         // API endpoint to toggle test mode
void updateTestModeSimulation(); // Update simulated values in test mode
void logEvent(const String& eventType, const String& details); // Log events to history
// Discord Alerts
void loadDiscordConfig();      // Load Discord webhook config from file
void saveDiscordConfig();      // Save Discord webhook config to file
void sendDiscordAlert(const String& title, const String& message, const String& color); // Send alert to Discord
void sendDiscordAlertTest(const String& title, const String& message, const String& color); // Send test alert (no cooldown)
void checkAndSendAlerts(float temp, float hum, bool sensorValid); // Check conditions and send alerts
void handleGetDiscordConfig(); // API endpoint to get Discord config
void handleSetDiscordConfig(); // API endpoint to set Discord config
void handleTestDiscordAlert(); // API endpoint to send test alert
void handleGetLogs(); // API endpoint to get debug logs
void handleHealth(); // API endpoint for system health monitoring
void handleDiskInfo(); // API endpoint for filesystem information
void handleHealthLog(); // API endpoint to tail health log (NEW)
void handleDownloadLogs(); // API endpoint to download logs (NEW)
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
void appendMeasurementToFile(const String& jsonString);
void logEvent(const String& eventType, const String& details); // NEW: Log events to history
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
float updateHumidityCache();    // Updates sensor cache - called ONLY in controlIndependiente()
float updateTemperatureCache(); // Updates sensor cache - called ONLY in controlIndependiente()
float calculateVPD(float temperature, float humidity);
void handleSerialCommands(); // Optional serial control (updated for NTP time)


// --- IMPLEMENTACIONES ---

// Comprueba si un nombre .local ya existe en la red
static bool isMdnsNameTaken(const String& hostNoSuffix) {
    if (WiFi.status() != WL_CONNECTED) return false; // Sin WiFi, no podemos comprobar, asumir libre
    
    // Usar WiFi.hostByName con múltiples intentos para mayor confiabilidad
    String fqdn = hostNoSuffix + ".local";
    
    for (int attempt = 0; attempt < 3; attempt++) {
        IPAddress ip;
        int res = WiFi.hostByName(fqdn.c_str(), ip);
        if (res == 1 && (ip[0] != 0 || ip[1] != 0 || ip[2] != 0 || ip[3] != 0)) {
            Serial.print("[mDNS CHECK] "); Serial.print(fqdn); 
            Serial.print(" resuelve a: "); Serial.println(ip);
            return true;
        }
        if (attempt < 2) {
            delay(400); // Más tiempo entre intentos para propagación mDNS
            yield(); // Permitir que WiFi procese
        }
    }
    
    return false;
}

// Inicia mDNS/NBNS probando greennanny, greennanny2, greennanny3...
// Prueba SIEMPRE secuencialmente para garantizar orden predecible
void startNameServices() {
    const char* base = HOSTNAME; // "greennanny"
    const int maxSuffix = 9;     // greennanny .. greennanny9

    uint32_t chipId = ESP.getChipId();
    Serial.print("[mDNS] ChipID: 0x"); Serial.println(chipId, HEX);
    
    // Esperar un momento para que la red se estabilice y mDNS se propague
    Serial.println("[mDNS] Esperando 2s para estabilización de red...");
    delay(2000);
    
    Serial.println("[mDNS] Probando nombres secuencialmente: greennanny, greennanny2, greennanny3...");

    String chosen = "";
    
    // Probar secuencialmente: greennanny, greennanny2, greennanny3... hasta greennanny9
    for (int i = 1; i <= maxSuffix && chosen.length() == 0; i++) {
        String candidate = (i == 1) ? String(base) : String(base) + String(i);
        Serial.print("[mDNS] Probando: "); Serial.print(candidate); Serial.print(".local ... ");
        
        // Verificar si el nombre está ocupado
        if (isMdnsNameTaken(candidate)) {
            Serial.println("OCUPADO");
            continue;
        }
        
        Serial.print("LIBRE, intentando asignar... ");
        
        // Intentar iniciar mDNS con este nombre
        if (MDNS.begin(candidate.c_str())) {
            chosen = candidate;
            MDNS.addService("http", "tcp", 80);
            Serial.println("✓ ASIGNADO");
            Serial.print("[mDNS SUCCESS] Anunciado como: "); Serial.print(chosen); Serial.println(".local");
            break;
        } else {
            Serial.println("✗ FALLO MDNS.begin()");
        }
        
        delay(150); // Dar tiempo entre intentos
    }
    
    // Si no se pudo asignar ningún nombre del 1-9, usar fallback con ChipID
    if (chosen.length() == 0) {
        Serial.println("[mDNS ERROR] Todos los nombres (1-9) están ocupados o fallaron.");
        String fallback = String(base) + "-" + String(chipId & 0xFFFF, HEX);
        Serial.print("[mDNS] Intentando nombre de último recurso: "); Serial.println(fallback);
        if (MDNS.begin(fallback.c_str())) {
            chosen = fallback;
            MDNS.addService("http", "tcp", 80);
            Serial.print("[mDNS] Anunciado como: "); Serial.print(chosen); Serial.println(".local");
        }
    }
    
    // NBNS para Windows - using chosen directly (no global storage)
    if (chosen.length()) {
        if (NBNS.begin(chosen.c_str())) {
            Serial.print("[NBNS SUCCESS] Anunciado como: http://"); Serial.print(chosen); Serial.println("/");
        } else {
            Serial.println("[NBNS WARN] No se pudo iniciar NBNS.");
        }
    }
}

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
        Serial.println("[FATAL ERROR] Falló al montar LittleFS. Reiniciando en 3s...");
        Serial.println("[INFO] Si el problema persiste, reflashea el filesystem.");
        delay(3000);
        ESP.restart();  // Restart limpio en lugar de hard lock
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
    
    Serial.println("[INFO] Hora inicial del sistema depende de NTP. Sincronizando...");

    startTime    = millis(); // Guardar millis de inicio
    lastMeasurementTimestamp = 0; // Se actualizará tras la primera sincronización NTP o medición
    Serial.print("[INFO] Hora de inicio del sistema (millis): "); Serial.println(startTime);
    
    // Configurar el pin de la bomba
    setupBomba();

    // Configurar pines de ventilador y turbina
    setupFanAndExtractor();

    // Inicializar el sensor DHT
    setupDHTSensor();

    // ***** NEW: Load custom stage configuration *****
    loadStagesConfig();

    // ***** Load fan/extractor thresholds *****
    loadThresholds();

    // ***** Load Discord webhook configuration *****
    loadDiscordConfig();

    // Cargar mediciones previas
    {
        String raw = loadMeasurements();
        jsonIndex = parseData(raw, measurements);
        Serial.print("[INFO] Cargadas "); Serial.print(jsonIndex); Serial.println(" mediciones previas.");
        // Reparación automática si el contenido del archivo parece corrupto/truncado
        String cleaned = arrayToString(measurements, jsonIndex);
        if (cleaned.length() > 0 && cleaned.length() != raw.length()) {
            Serial.println("[REPAIR] Detectada posible corrupción/truncado en historial. Reescribiendo archivo limpio...");
            saveMeasurementFile(cleaned);
        }
    }

    // Cargar intervalo de medición
    loadMeasurementInterval();
    
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

         // *** INICIAR mDNS/NBNS con nombres secuenciales ***
         startNameServices();

         // *** CONFIGURAR OTA UPDATES ***
         setupOTA();

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
             nextMeasureTimestamp = currentMillis + (measurementInterval * 3600000UL);
             Serial.print("[INFO] Próxima medición ajustada post-NTP para millis ~: "); Serial.println(nextMeasureTimestamp);
         } else {
             // No sincronizó, usar la hora del sistema no sincronizada (cercana a epoch 0)
             Serial.println("[WARN] No se pudo sincronizar con NTP. La hora del sistema NO es correcta.");
             nextMeasureTimestamp = startTime + (measurementInterval * 3600000UL);
             Serial.print("[INFO] Próxima medición programada (sin NTP sync) alrededor de millis: "); Serial.println(nextMeasureTimestamp);
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
    // Crear archivo de health log con encabezado si no existe (NEW)
    if (!LittleFS.exists(HEALTH_LOG_FILE)) {
        File hf = LittleFS.open(HEALTH_LOG_FILE, "w");
        if (hf) { hf.println("{\"evt\":\"HEADER\",\"ms\":0,\"epoch\":0,\"wifi\":-1,\"rssi\":0,\"heap\":0,\"frag\":0,\"maxblk\":0,\"fs_used\":0,\"fs_tot\":0,\"meas\":0}"); hf.close(); }
    }
}

void loop() {
    // ===== HARDWARE WATCHDOG FEED =====
    ESP.wdtFeed(); // Feed del watchdog hardware al inicio de cada loop
    
    unsigned long currentMillis = millis(); // Obtener millis al inicio del loop
    
    // ===== GESTIÓN AUTOMÁTICA DE HEAP CON DIAGNÓSTICO Y RCP (NEW) =====
    static unsigned long lastHeapCheck = 0;
    static bool rcpAttempted = false;  // Bandera GLOBAL - solo se resetea al arrancar
    static uint32_t rcpAttemptCount = 0;  // Contador de intentos de RCP
    
    if (currentMillis - lastHeapCheck > 60000UL) {  // Cada 60 segundos
        uint32_t freeHeap = ESP.getFreeHeap();
        
        // NIVEL CRÍTICO: <10KB - Diagnóstico + RCP (solo 1 vez) + Reinicio
        if (freeHeap < HEAP_EMERGENCY_THRESHOLD) {
            Serial.print("[HEAP CRITICAL] Solo ");
            Serial.print(freeHeap);
            Serial.println(" bytes libres! Ejecutando protocolo de emergencia...");
            
            // 1. Ejecutar diagnóstico para identificar el culpable
            diagnoseHeapUsage();
            
            // 2. Intentar RCP solo UNA VEZ en toda la vida del boot
            if (!rcpAttempted) {
                rcpAttempted = true;  // NUNCA se resetea hasta el próximo boot
                rcpAttemptCount++;
                
                Serial.println("[HEAP] Primera y única oportunidad de RCP en este boot.");
                
                // Determinar qué componente limpiar
                uint32_t measEstimate = jsonIndex * 150;
                uint32_t debugEstimate = DEBUG_LOG_SIZE * 80;
                uint8_t frag = ESP.getHeapFragmentation();
                
                String componentToReset = "UNKNOWN";
                if (frag > 40) {
                    componentToReset = "FRAGMENTATION";
                } else if (measEstimate > 5000) {
                    componentToReset = "MEASUREMENTS";
                } else if (debugEstimate > measEstimate && debugEstimate > 2000) {
                    componentToReset = "DEBUG_BUFFER";
                } else {
                    componentToReset = "UNKNOWN";
                }
                
                Serial.print("[HEAP] Intentando RCP en componente: ");
                Serial.println(componentToReset);
                
                uint32_t heapBefore = ESP.getFreeHeap();
                bool rcpSuccess = resetComponentProactive(componentToReset.c_str());
                uint32_t heapAfter = ESP.getFreeHeap();
                int32_t recovered = (int32_t)heapAfter - (int32_t)heapBefore;
                
                // Si el RCP no recuperó al menos 2KB O el heap sigue <10KB, reiniciar
                if (!rcpSuccess || recovered < 2000 || heapAfter < HEAP_EMERGENCY_THRESHOLD) {
                    Serial.print("[HEAP] RCP recuperó solo ");
                    Serial.print(recovered);
                    Serial.println(" bytes. Insuficiente, reiniciando...");
                    appendHealthLog("HEAP_CRITICAL_RCP_FAILED");
                    scheduleRestart("HEAP_EMERGENCY_POST_RCP");
                } else {
                    Serial.print("[HEAP] RCP exitoso! Recuperados ");
                    Serial.print(recovered);
                    Serial.println(" bytes. Continuando operación.");
                }
            } else {
                // Ya se intentó RCP antes en este boot, reiniciar sin más intentos
                Serial.println("[HEAP] RCP ya intentado anteriormente en este boot. Reiniciando...");
                appendHealthLog("HEAP_CRITICAL_PERSISTENT");
                scheduleRestart("HEAP_EMERGENCY_PERSISTENT");
            }
        }
        // NIVEL WARNING: <14KB - Limpieza preventiva suave
        else if (freeHeap < HEAP_WARNING_THRESHOLD) {
            // NO resetear rcpAttempted aquí, solo advertir
            if (jsonIndex > 100) {
                Serial.print("[HEAP WARNING] ");
                Serial.print(freeHeap);
                Serial.print(" bytes libres, purgando mediciones antiguas (total: ");
                Serial.print(jsonIndex);
                Serial.println(")...");
                
                int toRemove = min(50, jsonIndex - 80);  // Mantener al menos 80
                for (int i = toRemove; i < jsonIndex; i++) {
                    measurements[i - toRemove] = measurements[i];
                }
                jsonIndex -= toRemove;
                saveMeasurementFile(arrayToString(measurements, jsonIndex));
                Serial.print("[HEAP] Mediciones reducidas a ");
                Serial.println(jsonIndex);
            }
        }
        // Heap está bien, no hacer nada (NO resetear rcpAttempted - permanece true hasta próximo boot)
        
        lastHeapCheck = currentMillis;
    }
    
    // ===== WATCHDOG SOFTWARE + HANG DETECTOR =====
    static unsigned long lastLoopTime = 0;
    static unsigned long hangCounter = 0;
    unsigned long loopDuration = millis() - lastLoopTime;
    
    if (loopDuration > 30000 && lastLoopTime > 0) {  // Loop tomó más de 30s
        hangCounter++;
        Serial.print("[WATCHDOG] Loop bloqueado por ");
        Serial.print(loopDuration / 1000);
        Serial.print("s! (hang #");
        Serial.print(hangCounter);
        Serial.println(") Reiniciando...");
        delay(500);
        ESP.restart();
    }
    lastLoopTime = millis();
    
    currentMillis = millis(); // Actualizar después de operaciones

    // ===== PERSISTENT HEALTH CONDITIONS (NEW) =====
    static unsigned long lowHeapSince = 0;
    static unsigned long wifiDownSince = 0;
    uint32_t freeHeapInstant = ESP.getFreeHeap();
    if (freeHeapInstant < 9000) {
        if (lowHeapSince == 0) lowHeapSince = currentMillis; // start tracking
    } else {
        lowHeapSince = 0; // recovered
    }
    if (WiFi.status() != WL_CONNECTED && WiFi.getMode() == WIFI_STA) {
        if (wifiDownSince == 0) wifiDownSince = currentMillis;
    } else {
        wifiDownSince = 0;
    }
    // Preventive restarts
    if (lowHeapSince && (currentMillis - lowHeapSince > 600000UL)) { // 10 min
        if (currentMillis > 10000) appendHealthLog("LOW_HEAP"); // only log if stable
        scheduleRestart("LOW_HEAP_PERSISTENT");
    }
    if (wifiDownSince && (currentMillis - wifiDownSince > 7200000UL)) { // 2h
        if (currentMillis > 10000) appendHealthLog("WIFI_DOWN"); // only log if stable
        scheduleRestart("WIFI_DOWN_LONG");
    }

    // ===== PERIODIC HEALTH LOG SNAPSHOT =====
    static unsigned long lastHealthLog = 0;
    static bool healthLogReady = false;
    // Wait 10 seconds after boot before first health log to ensure everything is stable
    if (!healthLogReady && currentMillis > 10000) {
        healthLogReady = true;
        lastHealthLog = currentMillis; // Initialize timer
    }
    if (healthLogReady && (currentMillis - lastHealthLog >= 300000UL)) { // cada 5 minutos
        appendHealthLog("PERIODIC");
        lastHealthLog = currentMillis;
    }

    // ===== AUTO-RECONNECT WIFI MEJORADO =====
    static unsigned long lastWiFiCheck = 0;
    static int wifiFailCount = 0;
    
    if (currentMillis - lastWiFiCheck > 30000) {  // Cada 30s
        if (WiFi.status() != WL_CONNECTED && wifiState == IDLE && WiFi.getMode() == WIFI_STA) {
            wifiFailCount++;
            Serial.print("[WIFI] Desconectado! (fallo #");
            Serial.print(wifiFailCount);
            Serial.println(") Intentando reconectar...");
            
            // Si han fallado más de 3 intentos, hacer reset completo del WiFi
            if (wifiFailCount > 3) {
                Serial.println("[WIFI] Demasiados fallos. Reset completo de WiFi...");
                WiFi.disconnect(true);
                delay(500);
                WiFi.mode(WIFI_OFF);
                delay(500);
                WiFi.mode(WIFI_STA);
                delay(500);
                
                // Recargar credenciales y reconectar
                loadWifiCredentials();
                wifiFailCount = 0; // Resetear contador después de reset completo
            } else {
                WiFi.reconnect();
            }
            
            // Si llevan 12 horas desconectado (24 checks), reiniciar el ESP
            if (wifiFailCount >= 1440) { // 24 checks/hora * 60 horas = 1440
                Serial.println("[WIFI CRITICAL] 12 horas sin WiFi. Reiniciando dispositivo...");
                delay(1000);
                ESP.restart();
            }
        } else if (WiFi.status() == WL_CONNECTED) {
            // Resetear contador si la conexión se restableció
            if (wifiFailCount > 0) {
                Serial.println("[WIFI] Conexión restablecida! Reseteando contador.");
                wifiFailCount = 0;
            }
        }
        lastWiFiCheck = currentMillis;
    }

    // Manejar OTA updates (solo si está conectado a WiFi)
#ifndef DISABLE_OTA
    if (WiFi.status() == WL_CONNECTED && !otaInProgress) {
        ArduinoOTA.handle();
    }
#endif

    // Manejar clientes HTTP
    server.handleClient();
    MDNS.update();

    // Refresh timestamp after serving requests to prevent immediate
    // auto-off when the pump is activated from the dashboard.
    currentMillis = millis();

    // --- MÁQUINA DE ESTADOS PARA CONEXIÓN WIFI ASÍNCRONA (VERSIÓN FINAL) ---
    if (wifiState == SENDING_INSTRUCTIONS) {
        // Este estado da un "período de gracia" para que el servidor web termine de enviar la página
        // antes de iniciar el bloqueo de WiFi.begin(). 500ms es más que suficiente.
        if (currentMillis - connectionAttemptStartMillis >= 500UL) {
            Serial.println("[WIFI_STATE] Período de gracia finalizado. Intentando conectar ahora...");
            wifiState = ATTEMPTING_CONNECTION; // Pasar al siguiente estado
        }
    }
    else if (wifiState == ATTEMPTING_CONNECTION) {
        Serial.println("[WIFI_STATE] Iniciando intento de conexión...");
        
        WiFi.disconnect();
        WiFi.mode(WIFI_STA);
        // Generate hostname locally (no global storage)
        String localHostname = String(HOSTNAME) + "-" + String(ESP.getChipId() & 0xFFF, HEX);
        WiFi.hostname(localHostname);
        WiFi.begin(targetSsid.c_str(), targetPass.c_str());
        
        wifiState = CONNECTION_IN_PROGRESS;
        connectionAttemptStartMillis = currentMillis; // Reiniciar el temporizador para el timeout

    } else if (wifiState == CONNECTION_IN_PROGRESS) {
        // Verificar si la conexión fue exitosa
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("\n[WIFI_STATE] ¡Éxito! Conectado a la red.");
            
            File file = LittleFS.open("/WifiConfig.txt", "w");
            if (file) {
                file.println(targetSsid);
                file.println(targetPass);
                file.close();
                Serial.println("[INFO] Credenciales guardadas permanentemente. Reiniciando...");
                delay(1000);
                ESP.restart();
            } else {
                Serial.println("[FATAL ERROR] No se pudo abrir 'WifiConfig.txt' para escritura!");
                wifiState = IDLE;
            }
        }
        // Verificar si se agotó el tiempo de espera (30 segundos)
        else if (currentMillis - connectionAttemptStartMillis >= 30000UL) {
            Serial.println("\n[WIFI_STATE] Falló la conexión (timeout).");
            WiFi.disconnect(true);
            
            targetSsid = "";
            targetPass = "";
            wifiState = IDLE;
            
            Serial.println("[INFO] Reiniciando para volver al modo AP.");
            delay(1000);
            ESP.restart();
        }
    }
    // --- FIN DE LA MÁQUINA DE ESTADOS ---

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
        int totalSeconds = pumpDurationMs / 1000;
        int remaining = totalSeconds - pumpSecondsCount;
        Serial.print("[DEBUG PUMP] Riego en curso, segundo ");
        Serial.print(pumpSecondsCount);
        Serial.print("/");
        Serial.print(totalSeconds);
        Serial.print(" (restan ");
        Serial.print(remaining);
        Serial.println("s)");
        lastSecondPrint = currentMillis;
    }

    // Verificar si es momento de tomar una medición programada
    if (currentMillis >= nextMeasureTimestamp) {
        Serial.println("[INFO] Hora de medición programada alcanzada.");
        controlIndependiente();  // Lógica automática de medición y riego
        
        // Control de ventilador/extractor SOLO durante las mediciones programadas
        controlFanAndExtractor();
        
        // Programar la siguiente medición
        nextMeasureTimestamp = currentMillis + (measurementInterval * 3600000UL);
        Serial.print("[INFO] Próxima medición programada para millis: ");
        Serial.println(nextMeasureTimestamp);
    }

    // --- Actualización del modo test ---
    if (testModeEnabled) {
        updateTestModeSimulation();
    }

    // --- Sincronización NTP Periódica ---
    if (WiFi.status() == WL_CONNECTED && (!ntpTimeSynchronized || (currentMillis - lastNtpSyncAttempt >= ntpSyncInterval))) {
        if (!ntpTimeSynchronized) {
             Serial.println("[LOOP] Hora no sincronizada, intentando NTP sync...");
        } else {
             Serial.println("[LOOP] Intervalo de sincronización NTP (" + String(ntpSyncInterval / 3600000UL) + "h) alcanzado.");
        }
        syncNtpTime();
        lastNtpSyncAttempt = currentMillis;
    }

    // Mensajes de debug periódicos (cada 10 minutos)
    if (currentMillis - lastDebugPrint >= 600000) {
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
            Serial.print("[DEBUG] Uptime (NTP based): ");
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

        uint32_t freeHeap = ESP.getFreeHeap();
        uint8_t heapFrag = ESP.getHeapFragmentation();
        Serial.print("[DEBUG] Memoria Libre: ");
        Serial.print(freeHeap);
        Serial.print(" bytes, Fragmentación: ");
        Serial.print(heapFrag);
        Serial.println("%");
        
        // ADVERTENCIA SI HAY POCA MEMORIA
        if (freeHeap < 10000) {
            Serial.println("[WARN] ⚠️  HEAP BAJO! < 10KB - Sistema en riesgo");
        } else if (freeHeap < 15000) {
            Serial.println("[WARN] Heap moderadamente bajo < 15KB");
        }
        
        if (heapFrag > 50) {
            Serial.println("[WARN] ⚠️  HEAP MUY FRAGMENTADO! > 50%");
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.print("[DEBUG] WiFi RSSI: ");
            Serial.print(WiFi.RSSI()); Serial.println(" dBm");
        } else {
            Serial.println("[DEBUG] WiFi: Desconectado/AP");
        }

        Serial.print("[DEBUG] NTP Synced: "); Serial.println(ntpTimeSynchronized ? "Yes" : "No");
        
        // Info del filesystem
        FSInfo fs_info;
        LittleFS.info(fs_info);
        uint32_t usedBytes = fs_info.usedBytes;
        uint32_t totalBytes = fs_info.totalBytes;
        float usedPercent = (totalBytes > 0) ? (usedBytes * 100.0 / totalBytes) : 0;
        Serial.print("[DEBUG] Filesystem: ");
        Serial.print(usedBytes);
        Serial.print("/");
        Serial.print(totalBytes);
        Serial.print(" bytes (");
        Serial.print(usedPercent, 1);
        Serial.println("% usado)");
        
        Serial.println("--------------------");
        // Extra: snapshot log right after debug block for richer context (only after stable boot)
        if (currentMillis > 15000) appendHealthLog("DEBUG");
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

// Configura los pines de ventilador y turbina
void setupFanAndExtractor() {
    Serial.println("[SETUP] Configurando pines de ventilador (D4) y turbina (D5)...");
    pinMode(FAN_PIN, OUTPUT);
    pinMode(EXTRACTOR_PIN, OUTPUT);
    digitalWrite(FAN_PIN, LOW);
    digitalWrite(EXTRACTOR_PIN, LOW);
    fanActivated = false;
    extractorActivated = false;
}

// Obtiene la humedad (real o simulada)
// MODIFIED: NUNCA lee del sensor, solo retorna caché actualizado por controlIndependiente()
float getHumidity() {
    if (testModeEnabled) return simulatedHumidity;
    if (simulateSensors) return simulatedHumidity;
    return cachedHumidity;
}

// Función interna para actualizar cache de humedad (llamada SOLO desde controlIndependiente y /takeMeasurement)
float updateHumidityCache() {
    if (testModeEnabled) {
        cachedHumidity = simulatedHumidity;
        return cachedHumidity;
    }
    
    if (simulateSensors) {
        cachedHumidity = simulatedHumidity;
        return cachedHumidity;
    }
    
    float h = dht.readHumidity();
    if (!isnan(h)) {
        cachedHumidity = h;
    }
    // Si es NaN, mantener último valor válido
    
    return cachedHumidity;
}

// Obtiene la temperatura (real o simulada)
// MODIFIED: NUNCA lee del sensor, solo retorna caché actualizado por controlIndependiente()
float getTemperature() {
    if (testModeEnabled) return simulatedTemperature;
    if (simulateSensors) return simulatedTemperature;
    return cachedTemperature;
}

// Función interna para actualizar cache de temperatura (llamada SOLO desde controlIndependiente y /takeMeasurement)
float updateTemperatureCache() {
    if (testModeEnabled) {
        cachedTemperature = simulatedTemperature;
        return cachedTemperature;
    }
    
    if (simulateSensors) {
        cachedTemperature = simulatedTemperature;
        return cachedTemperature;
    }
    
    float t = dht.readTemperature();
    if (!isnan(t)) {
        cachedTemperature = t;
    }
    // Si es NaN, mantener último valor válido
    
    return cachedTemperature;
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

// ===== ENDPOINT /health - MONITOREO DE SALUD DEL SISTEMA =====
void handleHealth() {
    StaticJsonDocument<512> doc;
    
    // Métricas básicas
    doc["uptime_ms"] = millis();
    doc["free_heap"] = ESP.getFreeHeap();
    doc["heap_fragmentation"] = ESP.getHeapFragmentation();
    doc["max_free_block"] = ESP.getMaxFreeBlockSize();
    
    // WiFi
    if (WiFi.status() == WL_CONNECTED) {
        doc["wifi_rssi"] = WiFi.RSSI();
        doc["wifi_status"] = "connected";
    } else {
        doc["wifi_rssi"] = 0;
        doc["wifi_status"] = "disconnected";
    }
    
    // Contadores
    doc["measurements_count"] = jsonIndex;
    doc["logs_count"] = debugLogCount;
    doc["discord_processing"] = discordProcessing;
    doc["ntp_synced"] = ntpTimeSynchronized;
    doc["health_log_skipped"] = healthLogSkipCount;  // Agregar contador de SKIP al endpoint
    
    // Reset reason (current) and persisted restart reason if any
    doc["reset_reason"] = ESP.getResetReason();
    if (LittleFS.exists(LAST_RESTART_FILE)) {
        File rf = LittleFS.open(LAST_RESTART_FILE, "r");
        if (rf) {
            String reason = rf.readStringUntil('\n');
            reason.trim();
            rf.close();
            if (reason.length()) doc["last_restart_reason"] = reason;
        }
    }
    
    // Indicadores de salud
    bool healthy = true;
    String issues = "";
    
    if (ESP.getFreeHeap() < 10000) {
        healthy = false;
        issues += "LOW_HEAP,";
    }
    if (ESP.getHeapFragmentation() > 50) {
        healthy = false;
        issues += "HEAP_FRAGMENTED,";
    }
    if (WiFi.status() != WL_CONNECTED && WiFi.getMode() == WIFI_STA) {
        healthy = false;
        issues += "WIFI_DOWN,";
    }
    if (!ntpTimeSynchronized) {
        issues += "NTP_NOT_SYNCED,";
    }
    
    doc["healthy"] = healthy;
    doc["issues"] = issues.length() > 0 ? issues.substring(0, issues.length()-1) : "none";
    
    String response;
    serializeJson(doc, response);
    
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(healthy ? 200 : 503, "application/json", response);
}

// ===== HEALTH LOGGING UTILITIES (NEW) =====
// Append a health snapshot line in compact JSON. eventType may be PERIODIC, LOW_HEAP, WIFI_DOWN, FORCED_RESTART, MANUAL_RESTART
void appendHealthLog(const char* eventType) {
    // MODO ULTRA-COMPACTO: Si heap <14KB, usar formato mínimo sin Strings temporales
    bool lowHeapMode = (ESP.getFreeHeap() < HEAP_LOGGING_THRESHOLD);
    if (lowHeapMode) {
        healthLogSkipCount++;  // Contador para distinguir logs ultra-compactos
    }
    int fsTries = 0;
    while (fsTries < 2) {
        if (LittleFS.begin() && HEALTH_LOG_FILE && HEALTH_LOG_PREV_FILE) break;
        fsTries++;
        delay(100);
    }
    if (fsTries == 2) {
        Serial.println("[HEALTH LOG] ERROR: LittleFS no inicia, formateando...");
        LittleFS.format();
        ESP.restart();
        return;
    }
    
    // Collect metrics
    uint32_t nowMs = millis();
    time_t now_t = time(nullptr);
    bool timeValid = ntpTimeSynchronized && (now_t >= ntpBootEpoch);
    uint64_t epochMs = timeValid ? (uint64_t)now_t * 1000ULL : 0ULL;
    int wifiStatus = (WiFi.status() == WL_CONNECTED) ? 1 : 0;
    int32_t rssi = wifiStatus ? WiFi.RSSI() : 0;
    uint32_t freeHeap = ESP.getFreeHeap();
    uint8_t heapFrag = ESP.getHeapFragmentation();
    uint32_t maxBlock = ESP.getMaxFreeBlockSize();
    FSInfo fs_info;
    uint32_t usedBytes = 0;
    uint32_t totalBytes = 0;
    if (LittleFS.info(fs_info)) {
        usedBytes = fs_info.usedBytes;
        totalBytes = fs_info.totalBytes;
    }
    uint32_t measCount = jsonIndex;

    // Rotate if oversized, with error handling
    bool rotated = false;
    if (LittleFS.exists(HEALTH_LOG_FILE)) {
        File fsize = LittleFS.open(HEALTH_LOG_FILE, "r");
        if (fsize) {
            size_t sz = fsize.size();
            fsize.close();
            if (sz > HEALTH_LOG_MAX_BYTES) {
                LittleFS.remove(HEALTH_LOG_PREV_FILE);
                LittleFS.rename(HEALTH_LOG_FILE, HEALTH_LOG_PREV_FILE);
                rotated = true;
            }
        } else {
            // Corrupt file, remove it
            Serial.println("[HEALTH LOG] ERROR: cannot open log file for size check, removing!");
            LittleFS.remove(HEALTH_LOG_FILE);
            // Log cleanup event after removal
            File f2 = LittleFS.open(HEALTH_LOG_FILE, "a");
            if (f2) {
                f2.println("{\"evt\":\"LOG_CLEANUP\",\"msg\":\"health_log.txt removed (open fail)\"}");
                f2.close();
            }
        }
    }

    File f = LittleFS.open(HEALTH_LOG_FILE, "a");
    if (!f) {
        Serial.println("[HEALTH LOG] ERROR: cannot open log file for append, removing!");
        LittleFS.remove(HEALTH_LOG_FILE);
        // Try to create a new file and log cleanup event
        File f2 = LittleFS.open(HEALTH_LOG_FILE, "a");
        if (f2) {
            f2.println("{\"evt\":\"LOG_CLEANUP\",\"msg\":\"health_log.txt recreated (append fail)\"}");
            f2.close();
        }
        return;
    }

    // Write log: ultra-compact mode when heap low, normal mode when safe
    if (lowHeapMode) {
        // Ultra-compact: direct writes, no String temporaries to save RAM
        f.print("{\"evt\":\"");
        f.print(eventType);
        f.print("\",\"ms\":");
        f.print(nowMs);
        f.print(",\"epoch\":");
        f.print((unsigned long)(epochMs & 0xFFFFFFFF)); // Low 32 bits
        f.print(",\"wifi\":");
        f.print(wifiStatus);
        f.print(",\"rssi\":");
        f.print(rssi);
        f.print(",\"heap\":");
        f.print(freeHeap);
        f.print(",\"frag\":");
        f.print(heapFrag);
        f.print(",\"meas\":");
        f.print(measCount);
        f.print(",\"skipped\":");
        f.print(healthLogSkipCount);
        f.println("}");
    } else {
        // Normal mode: full String construction with all fields
        String line = "{";
        line += "\"evt\":\"" + String(eventType) + "\"";
        line += ",\"ms\":" + String(nowMs);
        line += ",\"epoch\":" + String(epochMs);
        line += ",\"wifi\":" + String(wifiStatus);
        line += ",\"rssi\":" + String(rssi);
        line += ",\"heap\":" + String(freeHeap);
        line += ",\"frag\":" + String(heapFrag);
        line += ",\"maxblk\":" + String(maxBlock);
        line += ",\"fs_used\":" + String(usedBytes);
        line += ",\"fs_tot\":" + String(totalBytes);
        line += ",\"meas\":" + String(measCount);
        line += ",\"skipped\":" + String(healthLogSkipCount);
        line += "}";
        
        if (line.length() > 512) {
            Serial.println("[HEALTH LOG] WARN: línea de log demasiado larga, ignorada");
            f.close();
            return;
        }
        f.println(line);
    }
    
    f.close();
    ESP.wdtFeed(); // Alimentar watchdog tras operación de log
}

// Persist a restart reason and schedule restart shortly
void scheduleRestart(const char* reason) {
    Serial.print("[RESTART] Scheduling restart. Reason: "); Serial.println(reason);
    // Write reason file
    File rf = LittleFS.open(LAST_RESTART_FILE, "w");
    if (rf) {
        rf.println(reason);
        rf.close();
    }
    appendHealthLog(reason); // log reason before restart
    delay(500);
    ESP.restart();
}

// ===== DIAGNÓSTICO DE USO DE HEAP POR COMPONENTE (OPTIMIZADO) =====
void diagnoseHeapUsage() {
    // Diagnóstico simplificado para minimizar consumo de heap
    uint32_t heapStart = ESP.getFreeHeap();
    uint32_t measurementsEstimate = jsonIndex * 150;
    uint32_t debugBufferEstimate = DEBUG_LOG_SIZE * 80;
    uint8_t fragPercent = ESP.getHeapFragmentation();
    
    // Identificar el mayor consumidor sin usar String
    const char* biggestConsumer = "SYSTEM";
    uint32_t biggestSize = 0;
    
    if (fragPercent > 40) {
        biggestConsumer = "FRAGMENTATION";
        biggestSize = fragPercent;
    } else if (measurementsEstimate > debugBufferEstimate && measurementsEstimate > 3000) {
        biggestConsumer = "MEASUREMENTS";
        biggestSize = measurementsEstimate;
    } else if (debugBufferEstimate > 2000) {
        biggestConsumer = "DEBUG_BUFFER";
        biggestSize = debugBufferEstimate;
    }
    
    Serial.print("[DIAG] Heap:");
    Serial.print(heapStart);
    Serial.print(" Meas:");
    Serial.print(measurementsEstimate);
    Serial.print(" Debug:");
    Serial.print(debugBufferEstimate);
    Serial.print(" Frag:");
    Serial.print(fragPercent);
    Serial.print("% Culprit:");
    Serial.println(biggestConsumer);
    
    // Log compacto solo si hay suficiente heap
    if (heapStart > HEAP_LOGGING_THRESHOLD) {
        File f = LittleFS.open(HEALTH_LOG_FILE, "a");
        if (f) {
            f.print("{\"evt\":\"DIAG\",\"h\":");
            f.print(heapStart);
            f.print(",\"m\":");
            f.print(measurementsEstimate);
            f.print(",\"d\":");
            f.print(debugBufferEstimate);
            f.print(",\"f\":");
            f.print(fragPercent);
            f.print(",\"c\":\"");
            f.print(biggestConsumer);
            f.println("\"}");
            f.close();
        }
    }
}

// ===== RCP: RESET DE COMPONENTE PROACTIVO (OPTIMIZADO) =====
// Intenta liberar memoria limpiando el componente más problemático
// Retorna true si logró liberar >2KB, false si necesita restart
bool resetComponentProactive(const char* component) {
    uint32_t heapBefore = ESP.getFreeHeap();
    
    // Limpiar según componente identificado
    if (strcmp(component, "MEASUREMENTS") == 0 && jsonIndex > 10) {
        // Purgar mediciones, mantener solo últimas 10
        for (int i = 0; i < 10; i++) {
            measurements[i] = measurements[jsonIndex - 10 + i];
        }
        jsonIndex = 10;
        saveMeasurementFile(arrayToString(measurements, jsonIndex));
    }
    else if (strcmp(component, "DEBUG_BUFFER") == 0) {
        // Limpiar debug buffer
        for (int i = 0; i < DEBUG_LOG_SIZE; i++) {
            debugLogBuffer[i] = "";
        }
        debugLogIndex = 0;
        debugLogCount = 0;
    }
    else {
        // SYSTEM o FRAGMENTATION: limpiar todo lo que se pueda
        // Debug buffer
        for (int i = 0; i < DEBUG_LOG_SIZE; i++) {
            debugLogBuffer[i] = "";
        }
        debugLogIndex = 0;
        debugLogCount = 0;
        
        // Mediciones (mantener solo 5)
        if (jsonIndex > 5) {
            for (int i = 0; i < 5; i++) {
                measurements[i] = measurements[jsonIndex - 5 + i];
            }
            jsonIndex = 5;
            saveMeasurementFile(arrayToString(measurements, jsonIndex));
        }
    }
    
    uint32_t heapAfter = ESP.getFreeHeap();
    int32_t recovered = (int32_t)heapAfter - (int32_t)heapBefore;
    
    Serial.print("[RCP] ");
    Serial.print(component);
    Serial.print(" Before:");
    Serial.print(heapBefore);
    Serial.print(" After:");
    Serial.print(heapAfter);
    Serial.print(" +");
    Serial.println(recovered);
    
    // Log solo si hay suficiente heap
    if (heapAfter > HEAP_LOGGING_THRESHOLD) {
        File f = LittleFS.open(HEALTH_LOG_FILE, "a");
        if (f) {
            f.print("{\"evt\":\"RCP\",\"c\":\"");
            f.print(component);
            f.print("\",\"b\":");
            f.print(heapBefore);
            f.print(",\"a\":");
            f.print(heapAfter);
            f.print(",\"r\":");
            f.print(recovered);
            f.println("}");
            f.close();
        }
    }
    
    // Éxito si recuperamos >2KB o llegamos a nivel seguro
    return (recovered > 2000 || heapAfter >= HEAP_WARNING_THRESHOLD);
}

// ===== ENDPOINT /healthLog?lines=N (tail) =====
void handleHealthLog() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    int lines = 100; // default tail size
    if (server.hasArg("lines")) {
        int req = server.arg("lines").toInt();
        if (req > 0 && req <= 500) lines = req; // clamp to 500 max (heap safety)
    }
    if (!LittleFS.exists(HEALTH_LOG_FILE)) {
        server.send(200, "application/json", "{\"lines\":[]}");
        return;
    }
    File f = LittleFS.open(HEALTH_LOG_FILE, "r");
    if (!f) {
        // Corrupt file, remove and return empty
        Serial.println("[HEALTH LOG] ERROR: cannot open log for read, removing!");
        LittleFS.remove(HEALTH_LOG_FILE);
        server.send(200, "application/json", "{\"lines\":[\"LOG_CLEANUP: health_log.txt removed (read fail)\"]}");
        return;
    }
    // Allocate ring buffer on heap to avoid stack overflow
    String* ring = new String[lines];
    if (!ring) {
        f.close();
        server.send(500, "application/json", "{\"error\":\"out of memory\"}");
        return;
    }
    int count = 0;
    while (f.available()) {
        String l = f.readStringUntil('\n');
        l.trim();
        if (!l.length() || l.length() > 512) continue;
        // Validar que sea JSON (empieza y termina con llaves)
        if (!(l.startsWith("{") && l.endsWith("}"))) {
            Serial.println("[HEALTH LOG] WARN: línea corrupta, borrando log!");
            f.close();
            LittleFS.remove(HEALTH_LOG_FILE);
            delete[] ring;
            server.send(200, "application/json", "{\"lines\":[\"LOG_CLEANUP: health_log.txt removed (bad format)\"]}");
            return;
        }
        if (count < lines) {
            ring[count++] = l;
        } else {
            // Circular shift to keep last N lines
            for (int i=1;i<lines;i++) ring[i-1]=ring[i];
            ring[lines-1]=l;
        }
        if ((count % 50)==0) ESP.wdtFeed();
    }
    f.close();
    // If all lines are empty, treat as corrupt and auto-cleanup
    bool allEmpty = true;
    for (int i=0;i<count;i++) {
        if (ring[i].length() > 0) { allEmpty = false; break; }
    }
    if (allEmpty && count > 0) {
        Serial.println("[HEALTH LOG] WARN: all log lines empty, removing log!");
        LittleFS.remove(HEALTH_LOG_FILE);
        delete[] ring;
        server.send(200, "application/json", "{\"lines\":[\"LOG_CLEANUP: health_log.txt removed (all lines empty)\"]}");
        return;
    }
    String resp = "{\"lines\":[";
    for (int i=0;i<count;i++) {
        if (i) resp += ",";
        String esc = ring[i];
        esc.replace("\\", "\\\\");
        esc.replace("\"", "\\\"");
        resp += "\"" + esc + "\"";
    }
    resp += "]}";
    delete[] ring; // Free heap memory
    server.send(200, "application/json", resp);
}

// ===== ENDPOINT /downloadLogs - aggregated logs (health + prev + debug circular) =====
void handleDownloadLogs() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Cache-Control", "no-cache");
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/plain", "");
    server.sendContent("GreenNanny Logs Export\n");
    server.sendContent("Generated: ");
    server.sendContent(String(millis()).c_str());
    server.sendContent(" ms since boot\n\n");
    // Current debug circular buffer
    server.sendContent("=== DEBUG RING (most recent first) ===\n");
    if (debugLogCount==0) server.sendContent("<empty>\n");
    else {
        int startIdx = (debugLogCount < DEBUG_LOG_SIZE) ? 0 : debugLogIndex;
        for (int i=0;i<debugLogCount;i++) {
            int idx = (startIdx + i) % DEBUG_LOG_SIZE;
            server.sendContent(debugLogBuffer[idx] + "\n");
            if ((i % 20)==0) yield();
        }
    }
    // Health logs
    auto streamFile = [&](const char* path, const char* title){
        if (!LittleFS.exists(path)) return;
        File f = LittleFS.open(path, "r");
        if (!f) {
            Serial.printf("[DOWNLOAD LOGS] ERROR: cannot open %s, removing!\n", path);
            LittleFS.remove(path);
            server.sendContent(String("\n=== ") + title + " ===\nLOG_CLEANUP: file removed (open fail)\n");
            return;
        }
        server.sendContent(String("\n=== ") + title + " ===\n");
        int lineCount = 0;
        while (f.available()) {
            String chunk = f.readStringUntil('\n');
            server.sendContent(chunk + "\n");
            if ((millis() & 0xF)==0) yield();
            lineCount++;
        }
        f.close();
        if (lineCount == 0) {
            Serial.printf("[DOWNLOAD LOGS] WARN: %s empty, removing!\n", path);
            LittleFS.remove(path);
            server.sendContent("LOG_CLEANUP: file removed (empty)\n");
        }
    };
    streamFile(HEALTH_LOG_PREV_FILE, "HEALTH LOG (previous rotated)");
    streamFile(HEALTH_LOG_FILE, "HEALTH LOG (current)");
    // Measurements count summary only (full history already accessible separately)
    server.sendContent("\n=== SUMMARY ===\nMeasurements in RAM: ");
    server.sendContent(String(jsonIndex).c_str());
    server.sendContent("\nHealth Log Events Skipped: ");
    server.sendContent(String(healthLogSkipCount).c_str());
    server.sendContent("\nFree Heap: ");
    server.sendContent(String(ESP.getFreeHeap()).c_str());
    server.sendContent("\nHeap Fragmentation: ");
    server.sendContent(String(ESP.getHeapFragmentation()).c_str());
    server.sendContent("%\nReset Reason: ");
    server.sendContent(ESP.getResetReason().c_str());
    if (LittleFS.exists(LAST_RESTART_FILE)) {
        File rf = LittleFS.open(LAST_RESTART_FILE, "r");
        if (rf) {
            String rr = rf.readStringUntil('\n'); rr.trim(); rf.close();
            server.sendContent("\nLast Restart Reason (persisted): ");
            server.sendContent(rr.c_str());
        }
    }
    server.sendContent("\n-- END --\n");
    server.client().stop();
}

// ===== ENDPOINT /diskInfo - INFORMACIÓN DEL FILESYSTEM =====
void handleDiskInfo() {
    Serial.println("[HTTP] Solicitud /diskInfo recibida.");
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    
    FSInfo fs_info;
    if (!LittleFS.info(fs_info)) {
        Serial.println("[ERROR] No se pudo obtener información del filesystem.");
        server.send(500, "application/json", "{\"error\":\"Failed to get filesystem info\"}");
        return;
    }
    
    StaticJsonDocument<256> doc;
    
    uint32_t totalBytes = fs_info.totalBytes;
    uint32_t usedBytes = fs_info.usedBytes;
    uint32_t freeBytes = totalBytes - usedBytes;
    float usedPercent = (totalBytes > 0) ? (usedBytes * 100.0 / totalBytes) : 0;
    float freePercent = 100.0 - usedPercent;
    
    doc["total_bytes"] = totalBytes;
    doc["used_bytes"] = usedBytes;
    doc["free_bytes"] = freeBytes;
    doc["used_percent"] = serialized(String(usedPercent, 1));
    doc["free_percent"] = serialized(String(freePercent, 1));
    doc["block_size"] = fs_info.blockSize;
    doc["page_size"] = fs_info.pageSize;
    doc["max_open_files"] = fs_info.maxOpenFiles;
    doc["max_path_length"] = fs_info.maxPathLength;
    
    String response;
    response.reserve(256); // Pre-allocar para JSON pequeño
    serializeJson(doc, response);
    
    server.send(200, "application/json", response);
    
    // OPTIMIZACIÓN CRÍTICA: Liberar buffers HTTP inmediatamente
    server.client().flush();
    yield();
}

// Función para agregar mensajes al buffer de debug
void addDebugLog(const String& message) {
    // Obtener timestamp
    time_t now = time(nullptr);
    struct tm timeinfo;
    char timestamp[20];
    
    if (ntpTimeSynchronized && localtime_r(&now, &timeinfo)) {
        strftime(timestamp, sizeof(timestamp), "%H:%M:%S", &timeinfo);
    } else {
        snprintf(timestamp, sizeof(timestamp), "%lu", millis() / 1000);
    }
    
    // Agregar al buffer circular
    debugLogBuffer[debugLogIndex] = String(timestamp) + " | " + message;
    debugLogIndex = (debugLogIndex + 1) % DEBUG_LOG_SIZE;
    if (debugLogCount < DEBUG_LOG_SIZE) {
        debugLogCount++;
    }
    
    // También imprimir en Serial
    Serial.println(message);
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
    
    // Enviar notificación a Discord si está habilitada
    if (discordAlerts.deviceActivationAlert) {
        sendDiscordAlert(
            "💧 Pump Activated",
            String("The water pump has been activated for ") + String(durationMs / 1000) + " seconds.",
            "4169E1" // Azul royal
        );
    }
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

// Activa el ventilador
void activateFan() {
    if (fanActivated) return;
    Serial.println("[ACCION] Activando ventilador.");
    digitalWrite(FAN_PIN, HIGH);
    fanActivated = true;
    
    // Registrar evento en historial
    float temp = getTemperature();
    float hum = getHumidity();
    String details = "Temp: " + String(temp, 1) + "°C, Hum: " + String(hum, 1) + "%";
    logEvent("FAN_ON", details);
    
    // Enviar notificación a Discord si está habilitada
    if (discordAlerts.deviceActivationAlert) {
        sendDiscordAlert(
            "🌀 Fan Activated",
            "The fan has been turned ON due to environmental conditions.\n" + details,
            "00CED1" // Turquesa
        );
    }
}

// Desactiva el ventilador
void deactivateFan() {
    if (!fanActivated) return;
    Serial.println("[ACCION] Desactivando ventilador.");
    digitalWrite(FAN_PIN, LOW);
    fanActivated = false;
    
    // Registrar evento en historial
    logEvent("FAN_OFF", "Ventilador desactivado");
}

// Activa la turbina de extracción
void activateExtractor() {
    if (extractorActivated) return;
    Serial.println("[ACCION] Activando turbina de extracción.");
    digitalWrite(EXTRACTOR_PIN, HIGH);
    extractorActivated = true;
    
    // Registrar evento en historial
    float temp = getTemperature();
    float hum = getHumidity();
    String details = "Temp: " + String(temp, 1) + "°C, Hum: " + String(hum, 1) + "%";
    logEvent("EXTRACTOR_ON", details);
    
    // Enviar notificación a Discord si está habilitada
    if (discordAlerts.deviceActivationAlert) {
        sendDiscordAlert(
            "💨 Extractor Activated",
            "The extractor has been turned ON due to environmental conditions.\n" + details,
            "9370DB" // Púrpura
        );
    }
}

// Desactiva la turbina de extracción
void deactivateExtractor() {
    if (!extractorActivated) return;
    Serial.println("[ACCION] Desactivando turbina de extracción.");
    digitalWrite(EXTRACTOR_PIN, LOW);
    extractorActivated = false;
    
    // Registrar evento en historial
    logEvent("EXTRACTOR_OFF", "Turbina desactivada");
}

// Control automático del ventilador y turbina basado en umbrales
void controlFanAndExtractor() {
    float temp = getTemperature();
    float hum = getHumidity();
    
    // Validar lecturas
    if (temp <= -90.0 || hum < 0.0) {
        // Sensores no válidos, no cambiar estado
        return;
    }

    // Control del ventilador
    if (temp >= thresholds.fanTempOn || hum >= thresholds.fanHumOn) {
        if (!fanActivated) activateFan();
    } else {
        // Agregar histéresis de 2 unidades para evitar oscilaciones
        if (temp < (thresholds.fanTempOn - 2.0) && hum < (thresholds.fanHumOn - 2.0)) {
            if (fanActivated) deactivateFan();
        }
    }

    // Control de la turbina de extracción
    if (temp >= thresholds.extractorTempOn || hum >= thresholds.extractorHumOn) {
        if (!extractorActivated) activateExtractor();
    } else {
        // Agregar histéresis de 2 unidades para evitar oscilaciones
        if (temp < (thresholds.extractorTempOn - 2.0) && hum < (thresholds.extractorHumOn - 2.0)) {
            if (extractorActivated) deactivateExtractor();
        }
    }
}

// Carga umbrales desde archivo
void loadThresholds() {
    if (LittleFS.exists(THRESHOLDS_CONFIG_FILE)) {
        File file = LittleFS.open(THRESHOLDS_CONFIG_FILE, "r");
        if (!file) {
            Serial.println("[ERROR] No se pudo abrir archivo de umbrales.");
            return;
        }

        StaticJsonDocument<256> doc;
        DeserializationError error = deserializeJson(doc, file);
        file.close();

        if (error) {
            Serial.print("[ERROR] Error al parsear JSON de umbrales: ");
            Serial.println(error.c_str());
            return;
        }

        thresholds.fanTempOn = doc["fanTempOn"] | thresholds.fanTempOn;
        thresholds.fanHumOn = doc["fanHumOn"] | thresholds.fanHumOn;
        thresholds.extractorTempOn = doc["extractorTempOn"] | thresholds.extractorTempOn;
        thresholds.extractorHumOn = doc["extractorHumOn"] | thresholds.extractorHumOn;

        Serial.println("[INFO] Umbrales cargados desde archivo.");
    } else {
        Serial.println("[INFO] No existe archivo de umbrales. Usando valores por defecto.");
        saveThresholds(); // Guardar valores por defecto
    }
}

// Guarda umbrales en archivo
void saveThresholds() {
    StaticJsonDocument<256> doc;
    doc["fanTempOn"] = thresholds.fanTempOn;
    doc["fanHumOn"] = thresholds.fanHumOn;
    doc["extractorTempOn"] = thresholds.extractorTempOn;
    doc["extractorHumOn"] = thresholds.extractorHumOn;

    File file = LittleFS.open(THRESHOLDS_CONFIG_FILE, "w");
    if (!file) {
        Serial.println("[ERROR] No se pudo abrir archivo de umbrales para escritura.");
        return;
    }

    size_t bytesWritten = serializeJson(doc, file);
    file.close();

    if (bytesWritten == 0) {
        Serial.println("[ERROR] Error al escribir umbrales en archivo.");
    } else {
        Serial.print("[INFO] Umbrales guardados en archivo (");
        Serial.print(bytesWritten);
        Serial.println(" bytes).");
    }
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
// Carga credenciales WiFi desde archivo
void loadWifiCredentials() {
    File file = LittleFS.open("/WifiConfig.txt", "r");
    String ssid = "";
    String password = "";
    bool hadFile = (file && file.available());
    if (hadFile) {
        ssid = file.readStringUntil('\n');
        password = file.readStringUntil('\n');
        file.close();
        ssid.trim();
        password.trim();
    } else {
        if (file) file.close();
        Serial.println("[INFO] No 'WifiConfig.txt' o vacío. Se probará WiFi por defecto.");
    }

    auto tryConnect = [&](const String& s, const String& p, const char* label) -> bool {
        if (s.length() == 0) return false;
        Serial.print("[ACCION] Intentando conectar (" ); Serial.print(label); Serial.print(") a '"); Serial.print(s); Serial.println("'");
        // Generate hostname locally (no global storage)
        String localHostname = String(HOSTNAME) + "-" + String(ESP.getChipId() & 0xFFF, HEX);
        WiFi.mode(WIFI_STA);
        WiFi.hostname(localHostname);
        WiFi.begin(s.c_str(), p.c_str());
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 30) { // ~15s
            delay(500); Serial.print("."); attempts++;
        }
        Serial.println("");
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("[INFO] Conexión WiFi exitosa.");
            Serial.print("[INFO] IP: "); Serial.println(WiFi.localIP());
            Serial.print("[INFO] RSSI: "); Serial.print(WiFi.RSSI()); Serial.println(" dBm");
            return true;
        }
        Serial.println("[WARN] No se pudo conectar con esas credenciales.");
        WiFi.disconnect(false);
        return false;
    };

    bool connected = false;
    if (hadFile && ssid.length() > 0) {
        connected = tryConnect(ssid, password, "guardadas");
    }
    if (!connected) {
        // Intento por defecto
        connected = tryConnect(String(DEFAULT_WIFI_SSID), String(DEFAULT_WIFI_PASSWORD), "por defecto");
        if (connected && !hadFile) {
            // Guardar para futuros reinicios
            File wf = LittleFS.open("/WifiConfig.txt", "w");
            if (wf) { wf.println(DEFAULT_WIFI_SSID); wf.println(DEFAULT_WIFI_PASSWORD); wf.close(); }
        }
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
    // Escribir de forma atómica usando archivo temporal y rename
    const char* tmpPath = "/Measurements.tmp";
    const char* finalPath = "/Measurements.txt";

    File tmp = LittleFS.open(tmpPath, "w");
    if (!tmp) {
        Serial.println("[ERROR] No se pudo abrir archivo temporal para escritura.");
        return;
    }
    size_t bytesWritten = tmp.print(allMeasurementsString);
    tmp.flush();
    tmp.close();
    if (bytesWritten != allMeasurementsString.length()) {
        Serial.println("[ERROR] Error al escribir historial completo en temporal. Abortando reemplazo.");
        LittleFS.remove(tmpPath);
        return;
    }
    // Reemplazo atómico: borrar final y renombrar temporal
    if (LittleFS.exists(finalPath)) {
        LittleFS.remove(finalPath);
        yield();
    }
    if (!LittleFS.rename(tmpPath, finalPath)) {
        Serial.println("[ERROR] Falló rename de temporal a Measurements.txt");
        // Si falla rename, intentar limpieza del temporal
        LittleFS.remove(tmpPath);
        return;
    }
    Serial.println("[INFO] Historial guardado en archivo (reemplazo atómico).");
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
        String jsonObj = input.substring(startIndex, endIndex + 1);
        
        // Validación: debe tener contenido y al menos un campo con comillas
        if (jsonObj.length() > 5 && jsonObj.indexOf("\"") > 0) {
            output[count++] = jsonObj;
        } else {
            Serial.print("[PARSE WARN] Saltando objeto JSON vacío/inválido: '");
            Serial.print(jsonObj.substring(0, 30));
            Serial.println("...'");
        }
        
        // Mover al siguiente carácter después del '}'
        startIndex = endIndex + 1;
        
        // Omitir la coma y espacios opcionales antes del siguiente objeto
        while(startIndex < input.length() && 
              (input.charAt(startIndex) == ',' || isspace(input.charAt(startIndex)))) {
            startIndex++;
        }
        
        if ((count % 20) == 0) { yield(); }
    }
    
    if (count >= MAX_JSON_OBJECTS) {
       Serial.println("[PARSE WARN] Se alcanzó el límite MAX_JSON_OBJECTS durante el parseo.");
    }
    
    Serial.print("[PARSE INFO] Parseados ");
    Serial.print(count);
    Serial.println(" objetos JSON válidos.");
    
    return count;
}

// Convierte array de historial a String (formato {j1},{j2},..)
String arrayToString(String array[], size_t arraySize) {
    String result = "";
    bool first = true;
    int validCount = 0;
    
    for (size_t i = 0; i < arraySize; i++) {
        // Validación estricta: verificar que tiene contenido JSON válido
        if (array[i].length() > 5 && 
            array[i].startsWith("{") && 
            array[i].endsWith("}") &&
            array[i].indexOf("\"") > 0) {
            
            if (!first) {
                result += ","; // Añadir coma separadora
            }
            result += array[i];
            first = false;
            validCount++;
        } else if (array[i].length() > 0) {
            // Loguear si hay un elemento inválido en el array que no sea vacío
            Serial.print("[ARRAY2STR WARN] Ignorando elemento inválido en índice ");
            Serial.print(i);
            Serial.print(": '");
            Serial.print(array[i].substring(0, 30));
            Serial.println("...'");
        }
        if ((i % 20) == 0) { yield(); }
    }
    
    if (validCount != arraySize && arraySize > 0) {
        Serial.print("[ARRAY2STR INFO] Convertidos ");
        Serial.print(validCount);
        Serial.print(" de ");
        Serial.print(arraySize);
        Serial.println(" elementos válidos");
    }
    
    return result;
}

// Guarda una nueva medición en array y archivo (con deslizamiento)
void saveMeasurement(const String& jsonString) {
    // Validación estricta del formato JSON
    if (jsonString.length() < 5 || 
        !jsonString.startsWith("{") || 
        !jsonString.endsWith("}") ||
        jsonString.indexOf("\"") < 0) { // Debe contener al menos un campo
        Serial.print("[ERROR] Intento guardar medición inválida: '");
        Serial.print(jsonString.substring(0, 50));
        Serial.println("...'");
        return;
    }
    
    Serial.println("[ACCION] Guardando nueva medición:");
    Serial.println(jsonString);

    if (jsonIndex < MAX_JSON_OBJECTS) {
        measurements[jsonIndex++] = jsonString;
        // Modo eficiente: anexar al archivo existente con separador si corresponde
        appendMeasurementToFile(jsonString);
    } else {
        // Array lleno, desplazar todos los elementos una posición hacia la izquierda
        Serial.println("[WARN] Array mediciones lleno. Desplazando historial...");
        for (int i = 0; i < MAX_JSON_OBJECTS - 1; i++) {
            measurements[i] = measurements[i + 1];
            if ((i % 20) == 0) { yield(); }
        }
        // Añadir la nueva medición al final
        measurements[MAX_JSON_OBJECTS - 1] = jsonString;
        // jsonIndex ya está en MAX_JSON_OBJECTS, no necesita incrementarse
        // Reescribir archivo completo de forma atómica solo cuando se alcanza el límite
        saveMeasurementFile(arrayToString(measurements, jsonIndex));
    }
}

// Añade una medición al final del archivo, insertando coma si hay contenido previo
void appendMeasurementToFile(const String& jsonString) {
    const char* path = "/Measurements.txt";
    bool addComma = false;
    if (LittleFS.exists(path)) {
        File rf = LittleFS.open(path, "r");
        if (rf) {
            size_t sz = rf.size();
            addComma = (sz > 0);
            rf.close();
        }
    }
    File af = LittleFS.open(path, "a");
    if (!af) {
        Serial.println("[ERROR] No se pudo abrir 'Measurements.txt' en modo append. Reintentando con reescritura completa.");
        // Fallback: reescribir completo
        saveMeasurementFile(arrayToString(measurements, jsonIndex));
        return;
    }
    if (addComma) {
        af.print(",");
    }
    size_t w1 = af.print(jsonString);
    af.flush();
    af.close();
    if (w1 != jsonString.length()) {
        Serial.println("[ERROR] Fallo al anexar medición (bytes incompletos). Se intentará reparar en próximo arranque.");
    } else {
        Serial.println("[INFO] Medición anexada al archivo.");
    }
}

// Formatea array de historial a JSON Array String "[{j1},{j2},..]"
void formatMeasurementsToString(String& formattedString) {
    formattedString = "["; // Iniciar array JSON
    bool first = true;
    int validCount = 0;
    for (int i = 0; i < jsonIndex; i++) {
         // Validación estricta: verificar que no esté vacío, tenga llaves, y contenga al menos un campo
        if (measurements[i].length() > 5 && 
            measurements[i].startsWith("{") && 
            measurements[i].endsWith("}") &&
            measurements[i].indexOf("\"") > 0) { // Debe tener al menos un campo con comillas
            
            if (!first) {
                formattedString += ","; // Coma separadora
            }
            formattedString += measurements[i]; // Añadir el objeto JSON
            first = false;
            validCount++;
        } else if (measurements[i].length() > 0) {
            // Log de elementos inválidos para debugging
            Serial.print("[FORMAT WARN] Saltando medición inválida idx=");
            Serial.print(i);
            Serial.print(": '");
            Serial.print(measurements[i].substring(0, 30));
            Serial.println("...'");
        }
        if ((i % 20) == 0) { yield(); }
    }
    formattedString += "]"; // Cerrar array JSON
    
    if (validCount != jsonIndex && jsonIndex > 0) {
        Serial.print("[FORMAT INFO] Formateadas ");
        Serial.print(validCount);
        Serial.print(" de ");
        Serial.print(jsonIndex);
        Serial.println(" mediciones (algunas se saltaron por ser inválidas)");
    }
}

// Registra un evento en el historial (activación de ventilador/extractor, etc.)
void logEvent(const String& eventType, const String& details) {
    Serial.print("[EVENT] ");
    Serial.print(eventType);
    Serial.print(": ");
    Serial.println(details);
    
    // Obtener timestamp actual
    time_t now_t = time(nullptr);
    bool timeValid = ntpTimeSynchronized && (now_t >= ntpBootEpoch);
    uint64_t epochMs = timeValid ? ((uint64_t)now_t * 1000ULL) : 0ULL;
    
    // Crear JSON del evento
    StaticJsonDocument<256> doc;
    doc["eventType"] = eventType;
    doc["details"] = details;
    doc["epoch_ms"] = epochMs;
    doc["testMode"] = testModeEnabled;
    
    // Agregar valores actuales de sensores para contexto
    float temp = getTemperature();
    float hum = getHumidity();
    if (temp > -90.0) doc["temperature"] = serialized(String(temp, 1));
    if (hum >= 0.0) doc["humidity"] = serialized(String(hum, 1));
    
    String eventString;
    serializeJson(doc, eventString);
    saveMeasurement(eventString);
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

    // 2. Leer sensores - ACTUALIZAR CACHE desde el sensor físico
    Serial.println("[SENSOR] Leyendo DHT (actualización programada)...");
    float humidity    = updateHumidityCache();    // Lee físicamente y actualiza cache
    float temperature = updateTemperatureCache(); // Lee físicamente y actualiza cache

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
    doc["fanActivated"] = fanActivated;   // Estado del ventilador
    doc["extractorActivated"] = extractorActivated; // Estado del extractor
    doc["testMode"] = testModeEnabled;    // Indicar si está en modo test
    doc["stage"]         = currentStage.name; // Nombre de la etapa actual
    doc["epoch_ms"]      = epochMs; // Timestamp en milisegundos UTC (será 0 si NTP no ha sincronizado)
    String measurementString;
    serializeJson(doc, measurementString);
    saveMeasurement(measurementString); // Guardar en array y archivo

    // 7. Verificar y enviar alertas de Discord si están habilitadas
    checkAndSendAlerts(temperature, humidity, sensorValid);

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

    // OPTIMIZACIÓN: Usar solo valores en caché, NO leer del sensor directamente
    // El sensor solo se lee en controlIndependiente() o /takeMeasurement
    float humidity, temperature;
    
    if (testModeEnabled) {
        // Usar valores simulados en modo test
        humidity = simulatedHumidity;
        temperature = simulatedTemperature;
    } else if (simulateSensors) {
        // Usar valores simulados si la simulación está activa
        humidity = simulatedHumidity;
        temperature = simulatedTemperature;
    } else {
        // Usar valores en caché - NO leer del sensor aquí para conservar heap y evitar bloqueos
        humidity = getHumidity();
        temperature = getTemperature();
    }
    
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

    // Crear JSON de respuesta - reducido de 768 a 512 bytes (suficiente para campos actuales)
    StaticJsonDocument<512> doc;

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

    // Estado Ventilador y Turbina
    doc["fanStatus"] = fanActivated;
    doc["extractorStatus"] = extractorActivated;

    // Modo Test
    doc["testModeEnabled"] = testModeEnabled;
    if (testModeEnabled) {
        unsigned long elapsedCycle = millis() - testModeStartTime;
        unsigned long cyclePos = elapsedCycle % testCycleDuration;
        float cycleProgress = (float)cyclePos / (float)testCycleDuration * 100.0;
        doc["testCycleProgress"] = serialized(String(cycleProgress, 1));
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
    doc["deviceHostname"] = HOSTNAME; // Use const directly
    doc["mdnsName"] = String(HOSTNAME) + ".local";
    doc["freeHeap"] = ESP.getFreeHeap();
    doc["measurementInterval"] = measurementInterval; // Informar intervalo configurado

    // Serializar y enviar - OPTIMIZACIÓN: reserve() para evitar reallocaciones múltiples
    String response;
    response.reserve(512); // Pre-allocar espacio estimado para evitar fragmentación
    serializeJson(doc, response);
    server.send(200, "application/json", response);
    
    // OPTIMIZACIÓN CRÍTICA: Liberar buffers HTTP inmediatamente
    server.client().flush(); // Enviar todo lo pendiente
    yield(); // Permitir que WiFi stack procese
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

    // OPTIMIZACIÓN: Limitar a 10 redes máximo para reducir uso de heap
    int maxNetworksToSend = (numNetworks > 10) ? 10 : numNetworks;
    
    // Usar DynamicJsonDocument con tamaño limitado (10 redes * 80 bytes = 800 + overhead)
    DynamicJsonDocument wifiJson(maxNetworksToSend * 80 + 50);
    JsonArray networks = wifiJson.to<JsonArray>();

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

    // Preparar hostname único y poner en modo STA
    if (WiFi.getMode() != WIFI_STA) {
        WiFi.mode(WIFI_STA);
        delay(100); // Pequeña pausa para que el modo cambie
    }
    // Generate hostname locally (no global storage)
    String localHostname = String(HOSTNAME) + "-" + String(ESP.getChipId() & 0xFFF, HEX);
    WiFi.hostname(localHostname);
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
        
        // *** INICIAR mDNS/NBNS (manual) con nombres secuenciales ***
        startNameServices();

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
        }
        server.send(401, "application/json", "{\"status\":\"failed\", \"message\":\"Connection failed\"}");
    }
}

void sendFinalInstructionsPage(ESP8266WebServer& server) {
    // Iniciar el envío con código 200 y tipo de contenido.
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "-1");
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html", ""); // Envío inicial vacío

    // Envío del contenido HTML exacto que solicitaste, con la sintaxis corregida.
    // Use HOSTNAME directly (mdnsAdvertisedName was removed to save RAM)
    String urlLocal = String("http://") + HOSTNAME + ".local";
    String urlNbns  = String("http://") + HOSTNAME + "/";
    String html = "<!DOCTYPE html><html lang=\"es\"><head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
        "<title>Conectando... - Green Nanny</title>"
        "<link href=\"https://cdn.jsdelivr.net/npm/bootstrap@5.1.3/dist/css/bootstrap.min.css\" rel=\"stylesheet\">"
        "<style>"
        "body{background-color:#222;color:#fff;display:flex;align-items:center;justify-content:center;height:100vh;text-align:center;}"
        ".container{max-width:500px;}"
        ".card{background-color:#333;border:1px solid #444;border-radius:10px;padding:30px;}"
        "</style></head><body><div class=\"container\"><div class=\"card\">"
        "<p>Haga clic en el bot&oacute;n de abajo para acceder al dashboard.</p>";
    html += String("<a href=\"") + urlLocal + "\" class=\\\"btn btn-success mt-4 w-100 fs-5\\\">Ir al Dashboard</a>";
    html += String("<p class=\\\"text-muted small mt-2\\\">Si el bot&oacute;n no funciona, pruebe tambi&eacute;n: <strong>") + urlNbns + "</strong> (Windows)</p>";
    html += "<p class=\\\"text-muted small\\\">O use la IP del dispositivo (desde su router/DHCP).</p>";
    html += "</div></div></body></html>";
    server.sendContent(html);

    server.sendContent(""); // Finalizar el envío
}

// Handler para /saveWifiCredentials - Solo inicia el proceso asíncrono
void handleSaveWifiCredentials() {
    Serial.println("[HTTP] Solicitud /saveWifiCredentials recibida.");
    
    if (!server.hasArg("ssid") || server.arg("ssid").length() == 0) {
        server.send(400, "text/plain", "Error: SSID no puede estar vacío.");
        return;
    }
    
    // Guardar credenciales en variables globales
    targetSsid = server.arg("ssid");
    targetPass = server.arg("password");
    
    // Cambiar el estado para que el loop() empiece a enviar la página
    wifiState = SENDING_INSTRUCTIONS;
    connectionAttemptStartMillis = millis(); // Registrar la hora de inicio
    
    Serial.println("[HTTP] Petición aceptada. Enviando página de instrucciones...");
    
    // Enviar la página de instrucciones y terminar la solicitud HTTP.
    sendFinalInstructionsPage(server);
}

// Handler para /loadMeasurement - Cargar historial
void handleLoadMeasurement() {
    Serial.println("[HTTP] Solicitud /loadMeasurement (GET).");
    
    // OPTIMIZACIÓN CRÍTICA: Stream directo sin construir String gigante
    // Esto ahorra ~15KB de heap en sistemas con muchas mediciones
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "application/json", "");
    
    // Enviar array JSON por partes directamente al cliente
    server.sendContent("[");
    bool first = true;
    int validCount = 0;
    
    for (int i = 0; i < jsonIndex; i++) {
        // Validación estricta: verificar que no esté vacío, tenga llaves, y contenga al menos un campo
        if (measurements[i].length() > 5 && 
            measurements[i].startsWith("{") && 
            measurements[i].endsWith("}") &&
            measurements[i].indexOf("\"") > 0) {
            
            if (!first) {
                server.sendContent(",");
            }
            server.sendContent(measurements[i]);
            first = false;
            validCount++;
        }
        if ((i % 10) == 0) { yield(); } // Yield cada 10 items
    }
    
    server.sendContent("]");
    // Finalizar el envío correctamente: enviar chunk vacío para terminar la codificación chunked
    server.sendContent("");
    // Cerrar la conexión del cliente de forma ordenada
    server.client().stop(); // Cerrar conexión
    
    Serial.print("[HTTP] /loadMeasurement enviado (stream): ");
    Serial.print(validCount);
    Serial.println(" mediciones");
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


// MODIFIED: Switched from JSON body to URL parameters for robustness
void handlePumpControl() {
    Serial.println("[HTTP] Solicitud /controlPump (POST).");
    server.sendHeader("Access-Control-Allow-Origin", "*");

    // Check if the required 'action' argument exists in the URL
    if (!server.hasArg("action")) {
        server.send(400, "application/json", "{\"status\":\"error\", \"message\":\"Missing 'action' parameter\"}");
        return;
    }
    String action = server.arg("action");

    if (action.equalsIgnoreCase("on")) {
        // For 'on' action, the 'duration' argument is also required
        if (!server.hasArg("duration")) {
            server.send(400, "application/json", "{\"status\":\"error\", \"message\":\"Missing 'duration' parameter for action 'on'\"}");
            return;
        }

        int durationSec = server.arg("duration").toInt();

        // Validate the converted duration
        if (durationSec <= 0 || durationSec > 600) {
            Serial.print("[ERROR] Duración de bomba inválida recibida: "); Serial.println(durationSec);
            server.send(400, "application/json", "{\"status\":\"error\", \"message\":\"Invalid duration. Must be between 1 and 600 seconds.\"}");
            return;
        }

        Serial.print("[ACCION] Encendiendo bomba (manual HTTP) por "); Serial.print(durationSec); Serial.println("s.");
        activatePump(durationSec * 1000UL);
        server.send(200, "application/json", "{\"status\":\"success\", \"pumpStatus\":\"on\", \"duration\":" + String(durationSec) + "}");

    } else if (action.equalsIgnoreCase("off")) {
        Serial.println("[ACCION] Apagando bomba (manual HTTP).");
        deactivatePump();
        server.send(200, "application/json", "{\"status\":\"success\", \"pumpStatus\":\"off\"}");

    } else {
        Serial.print("[ERROR] Acción de bomba inválida recibida: "); Serial.println(action);
        server.send(400, "application/json", "{\"status\":\"error\", \"message\":\"Invalid action specified. Use 'on' or 'off'.\"}");
    }
}

// Handler para /controlFan - Control manual del ventilador
void handleFanControl() {
    Serial.println("[HTTP] Solicitud /controlFan (POST).");
    server.sendHeader("Access-Control-Allow-Origin", "*");

    if (!server.hasArg("action")) {
        server.send(400, "application/json", "{\"status\":\"error\", \"message\":\"Missing 'action' parameter\"}");
        return;
    }
    String action = server.arg("action");

    if (action.equalsIgnoreCase("on")) {
        Serial.println("[ACCION] Encendiendo ventilador (manual HTTP).");
        activateFan();
        server.send(200, "application/json", "{\"status\":\"success\", \"fanStatus\":\"on\"}");
    } else if (action.equalsIgnoreCase("off")) {
        Serial.println("[ACCION] Apagando ventilador (manual HTTP).");
        deactivateFan();
        server.send(200, "application/json", "{\"status\":\"success\", \"fanStatus\":\"off\"}");
    } else {
        Serial.print("[ERROR] Acción de ventilador inválida: "); Serial.println(action);
        server.send(400, "application/json", "{\"status\":\"error\", \"message\":\"Invalid action. Use 'on' or 'off'.\"}");
    }
}

// Handler para /controlExtractor - Control manual de la turbina
void handleExtractorControl() {
    Serial.println("[HTTP] Solicitud /controlExtractor (POST).");
    server.sendHeader("Access-Control-Allow-Origin", "*");

    if (!server.hasArg("action")) {
        server.send(400, "application/json", "{\"status\":\"error\", \"message\":\"Missing 'action' parameter\"}");
        return;
    }
    String action = server.arg("action");

    if (action.equalsIgnoreCase("on")) {
        Serial.println("[ACCION] Encendiendo turbina (manual HTTP).");
        activateExtractor();
        server.send(200, "application/json", "{\"status\":\"success\", \"extractorStatus\":\"on\"}");
    } else if (action.equalsIgnoreCase("off")) {
        Serial.println("[ACCION] Apagando turbina (manual HTTP).");
        deactivateExtractor();
        server.send(200, "application/json", "{\"status\":\"success\", \"extractorStatus\":\"off\"}");
    } else {
        Serial.print("[ERROR] Acción de turbina inválida: "); Serial.println(action);
        server.send(400, "application/json", "{\"status\":\"error\", \"message\":\"Invalid action. Use 'on' or 'off'.\"}");
    }
}

// Handler para /getThresholds - Obtener umbrales actuales
void handleGetThresholds() {
    Serial.println("[HTTP] Solicitud /getThresholds (GET).");
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");

    StaticJsonDocument<256> doc;
    doc["fanTempOn"] = thresholds.fanTempOn;
    doc["fanHumOn"] = thresholds.fanHumOn;
    doc["extractorTempOn"] = thresholds.extractorTempOn;
    doc["extractorHumOn"] = thresholds.extractorHumOn;

    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

// Handler para /setThresholds - Configurar umbrales
void handleSetThresholds() {
    Serial.println("[HTTP] Solicitud /setThresholds (POST).");
    server.sendHeader("Access-Control-Allow-Origin", "*");

    if (!server.hasArg("plain")) {
        server.send(400, "text/plain", "Bad Request: Missing body");
        return;
    }

    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    if (error) {
        server.send(400, "text/plain", "Bad Request: Invalid JSON");
        return;
    }

    // Actualizar umbrales si están presentes y son válidos
    bool updated = false;
    if (doc.containsKey("fanTempOn") && doc["fanTempOn"].is<float>()) {
        float val = doc["fanTempOn"];
        if (val >= 0 && val <= 50) {
            thresholds.fanTempOn = val;
            updated = true;
        }
    }
    if (doc.containsKey("fanHumOn") && doc["fanHumOn"].is<float>()) {
        float val = doc["fanHumOn"];
        if (val >= 0 && val <= 100) {
            thresholds.fanHumOn = val;
            updated = true;
        }
    }
    if (doc.containsKey("extractorTempOn") && doc["extractorTempOn"].is<float>()) {
        float val = doc["extractorTempOn"];
        if (val >= 0 && val <= 50) {
            thresholds.extractorTempOn = val;
            updated = true;
        }
    }
    if (doc.containsKey("extractorHumOn") && doc["extractorHumOn"].is<float>()) {
        float val = doc["extractorHumOn"];
        if (val >= 0 && val <= 100) {
            thresholds.extractorHumOn = val;
            updated = true;
        }
    }

    if (updated) {
        saveThresholds();
        server.send(200, "application/json", "{\"status\":\"success\", \"message\":\"Thresholds updated\"}");
    } else {
        server.send(400, "application/json", "{\"status\":\"error\", \"message\":\"No valid parameters provided\"}");
    }
}

// ============================================
// MODO TEST - Simulación de condiciones
// ============================================

// Actualiza los valores simulados en modo test
void updateTestModeSimulation() {
    if (!testModeEnabled) return;
    
    // Actualizar valores aleatorios cada 10 segundos
    static unsigned long lastUpdate = 0;
    unsigned long now = millis();
    
    if (now - lastUpdate >= 10000) { // Cada 10 segundos
        // Generar temperatura aleatoria entre 18°C y 38°C
        // Con mayor probabilidad en el rango normal (22-30°C)
        float tempRandom = random(0, 100) / 100.0; // 0.0 a 1.0
        if (tempRandom < 0.7) {
            // 70% de probabilidad: temperatura normal (22-30°C)
            simulatedTemperature = 22.0 + random(0, 81) / 10.0; // 22.0 a 30.0
        } else if (tempRandom < 0.85) {
            // 15% de probabilidad: temperatura alta (30-38°C)
            simulatedTemperature = 30.0 + random(0, 81) / 10.0; // 30.0 a 38.0
        } else {
            // 15% de probabilidad: temperatura baja (18-22°C)
            simulatedTemperature = 18.0 + random(0, 41) / 10.0; // 18.0 a 22.0
        }
        
        // Generar humedad aleatoria entre 30% y 95%
        // Con mayor probabilidad en el rango normal (50-75%)
        float humRandom = random(0, 100) / 100.0; // 0.0 a 1.0
        if (humRandom < 0.7) {
            // 70% de probabilidad: humedad normal (50-75%)
            simulatedHumidity = 50.0 + random(0, 251) / 10.0; // 50.0 a 75.0
        } else if (humRandom < 0.85) {
            // 15% de probabilidad: humedad alta (75-95%)
            simulatedHumidity = 75.0 + random(0, 201) / 10.0; // 75.0 a 95.0
        } else {
            // 15% de probabilidad: humedad baja (30-50%)
            simulatedHumidity = 30.0 + random(0, 201) / 10.0; // 30.0 a 50.0
        }
        
        Serial.print("[TEST MODE] Valores actualizados (solo simulación) | Temp: ");
        Serial.print(simulatedTemperature, 1);
        Serial.print("°C | Hum: ");
        Serial.print(simulatedHumidity, 1);
        Serial.print("% | Las mediciones se guardan cada ");
        Serial.print(measurementInterval);
        Serial.println("h según intervalo configurado");
        
        lastUpdate = now;
    }
}

// Handler para /testMode - Activar/desactivar modo test
void handleTestMode() {
    Serial.println("[HTTP] Solicitud /testMode (GET/POST).");
    server.sendHeader("Access-Control-Allow-Origin", "*");
    
    if (server.method() == HTTP_GET) {
        // Retornar estado actual
        StaticJsonDocument<128> doc;
        doc["testModeEnabled"] = testModeEnabled;
        doc["currentTemp"] = simulatedTemperature;
        doc["currentHum"] = simulatedHumidity;
        
        String response;
        serializeJson(doc, response);
        server.send(200, "application/json", response);
        return;
    }
    
    if (server.method() == HTTP_POST) {
        // Toggle o establecer estado
        bool enable = true;
        
        if (server.hasArg("enable")) {
            enable = (server.arg("enable") == "true" || server.arg("enable") == "1");
        } else {
            // Toggle
            enable = !testModeEnabled;
        }
        
        testModeEnabled = enable;
        
        if (testModeEnabled) {
            testModeStartTime = millis();
            // Iniciar con valores aleatorios
            simulatedTemperature = 22.0 + random(0, 81) / 10.0; // 22.0 a 30.0
            simulatedHumidity = 50.0 + random(0, 251) / 10.0;   // 50.0 a 75.0
            Serial.println("[TEST MODE] ========================================");
            Serial.println("[TEST MODE] *** MODO TEST ACTIVADO ***");
            Serial.println("[TEST MODE] - Valores simulados cambian cada 10 segundos");
            Serial.println("[TEST MODE] - Las MEDICIONES se guardan según intervalo configurado (" + String(measurementInterval) + "h)");
            Serial.println("[TEST MODE] - El sistema funciona normalmente (riego, ventilador, etc.)");
            Serial.println("[TEST MODE] - Control automático respeta el intervalo de medición");
            Serial.println("[TEST MODE] ========================================");
        } else {
            Serial.println("[TEST MODE] *** MODO TEST DESACTIVADO ***");
            Serial.println("[TEST MODE] El sistema volverá a usar el sensor DHT11 real");
        }
        
        StaticJsonDocument<128> doc;
        doc["status"] = "success";
        doc["testModeEnabled"] = testModeEnabled;
        doc["message"] = testModeEnabled ? "Test mode activated" : "Test mode deactivated";
        
        String response;
        serializeJson(doc, response);
        server.send(200, "application/json", response);
        return;
    }
    
    server.send(405, "text/plain", "Method Not Allowed");
}

// ============================================
// DISCORD WEBHOOK ALERTS
// ============================================

// Cargar configuración de Discord desde archivo
void loadDiscordConfig() {
    if (!LittleFS.exists(DISCORD_CONFIG_FILE)) {
        Serial.println("[DISCORD] No config file found. Using defaults.");
        return;
    }
    
    File file = LittleFS.open(DISCORD_CONFIG_FILE, "r");
    if (!file) {
        Serial.println("[ERROR] Could not open Discord config file.");
        return;
    }
    
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    
    if (error) {
        Serial.print("[ERROR] Failed to parse Discord config: ");
        Serial.println(error.c_str());
        return;
    }
    
    discordWebhookUrl = doc["webhookUrl"] | "";
    discordAlertsEnabled = doc["enabled"] | false;
    
    if (doc.containsKey("alerts")) {
        JsonObject alerts = doc["alerts"];
        discordAlerts.tempHighAlert = alerts["tempHighAlert"] | true;
        discordAlerts.tempHighThreshold = alerts["tempHighThreshold"] | 35.0;
        discordAlerts.tempLowAlert = alerts["tempLowAlert"] | true;
        discordAlerts.tempLowThreshold = alerts["tempLowThreshold"] | 15.0;
        discordAlerts.humHighAlert = alerts["humHighAlert"] | true;
        discordAlerts.humHighThreshold = alerts["humHighThreshold"] | 85.0;
        discordAlerts.humLowAlert = alerts["humLowAlert"] | true;
        discordAlerts.humLowThreshold = alerts["humLowThreshold"] | 30.0;
        discordAlerts.sensorFailAlert = alerts["sensorFailAlert"] | true;
        discordAlerts.deviceActivationAlert = alerts["deviceActivationAlert"] | false;
    }
    
    Serial.println("[DISCORD] Configuration loaded successfully.");
    if (discordAlertsEnabled && discordWebhookUrl.length() > 0) {
        Serial.println("[DISCORD] Alerts are ENABLED.");
    }
}

// Guardar configuración de Discord en archivo
void saveDiscordConfig() {
    StaticJsonDocument<512> doc;
    
    doc["webhookUrl"] = discordWebhookUrl;
    doc["enabled"] = discordAlertsEnabled;
    
    JsonObject alerts = doc.createNestedObject("alerts");
    alerts["tempHighAlert"] = discordAlerts.tempHighAlert;
    alerts["tempHighThreshold"] = discordAlerts.tempHighThreshold;
    alerts["tempLowAlert"] = discordAlerts.tempLowAlert;
    alerts["tempLowThreshold"] = discordAlerts.tempLowThreshold;
    alerts["humHighAlert"] = discordAlerts.humHighAlert;
    alerts["humHighThreshold"] = discordAlerts.humHighThreshold;
    alerts["humLowAlert"] = discordAlerts.humLowAlert;
    alerts["humLowThreshold"] = discordAlerts.humLowThreshold;
    alerts["sensorFailAlert"] = discordAlerts.sensorFailAlert;
    alerts["deviceActivationAlert"] = discordAlerts.deviceActivationAlert;
    
    File file = LittleFS.open(DISCORD_CONFIG_FILE, "w");
    if (!file) {
        Serial.println("[ERROR] Could not open Discord config file for writing.");
        return;
    }
    
    serializeJson(doc, file);
    file.close();
    Serial.println("[DISCORD] Configuration saved.");
}

// Enviar alerta a Discord
void sendDiscordAlert(const String& title, const String& message, const String& color) {
    if (discordProcessing) {
        addDebugLog("[DISCORD] Busy, skipping");
        return;
    }
    
    if (!discordAlertsEnabled || discordWebhookUrl.length() == 0) {
        return;
    }
    
    if (WiFi.status() != WL_CONNECTED) {
        addDebugLog("[DISCORD] No WiFi!");
        return;
    }
    
    // Rate limiting
    unsigned long now = millis();
    if (now - lastDiscordAlert < discordAlertCooldown) {
        addDebugLog("[DISCORD] Cooldown active");
        return;
    }
    
    discordProcessing = true;
    addDebugLog("=== DISCORD START ===");
    addDebugLog("Title: " + title.substring(0, 25));
    
    // ===== VERIFICAR HEAP ANTES DE SSL =====
    uint32_t freeHeap = ESP.getFreeHeap();
    addDebugLog("Heap: " + String(freeHeap) + " bytes");
    
    // AUMENTADO: WiFiClientSecure necesita ~10-14KB overhead, reservar margen adicional
    if (freeHeap < 18000) {
        addDebugLog("[ERR] HEAP BAJO! <18KB");
        Serial.println("[DISCORD] ALERTA: Heap bajo (" + String(freeHeap) + " bytes). Saltando envío para evitar crash.");
        discordProcessing = false;
        return;
    }
    
    // Parse URL
    String host, path;
    int port = 443;
    
    if (!discordWebhookUrl.startsWith("https://")) {
        addDebugLog("[ERR] Not HTTPS");
        discordProcessing = false;
        return;
    }
    
    int hostStart = 8;
    int pathStart = discordWebhookUrl.indexOf('/', hostStart);
    if (pathStart > 0) {
        host = discordWebhookUrl.substring(hostStart, pathStart);
        path = discordWebhookUrl.substring(pathStart);
    } else {
        addDebugLog("[ERR] Bad URL format");
        discordProcessing = false;
        return;
    }
    
    addDebugLog("Host: " + host);
    
    // Connect
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(8000); // Reducido de 15s a 8s
    
    addDebugLog("Connecting...");
    if (!client.connect(host.c_str(), port)) {
        addDebugLog("[ERR] Connect failed");
        addDebugLog("WiFi: " + String(WiFi.RSSI()) + " dBm");
        discordProcessing = false;
        return;
    }
    
    addDebugLog("Connected!");
    ESP.wdtFeed(); // Feed watchdog después de conectar
    
    // Build JSON
    StaticJsonDocument<512> doc;
    JsonArray embeds = doc.createNestedArray("embeds");
    JsonObject embed = embeds.createNestedObject();
    embed["title"] = title;
    embed["description"] = message;
    embed["color"] = strtol(color.c_str(), NULL, 16);
    
    JsonObject footer = embed.createNestedObject("footer");
    footer["text"] = "GreenNanny";
    
    String payload;
    serializeJson(doc, payload);
    
    addDebugLog("Payload: " + String(payload.length()) + "B");
    
    // Send POST
    client.print(String("POST ") + path + " HTTP/1.1\r\n");
    client.print(String("Host: ") + host + "\r\n");
    client.print("User-Agent: ESP8266\r\n");
    client.print("Content-Type: application/json\r\n");
    client.print("Content-Length: " + String(payload.length()) + "\r\n");
    client.print("Connection: close\r\n\r\n");
    client.print(payload);
    client.flush();
    
    addDebugLog("Sent, waiting...");
    
    // Wait for response (timeout reducido a 8s)
    unsigned long timeout = millis();
    while (!client.available()) {
        if (millis() - timeout > 8000) {  // Reducido de 15s a 8s
            addDebugLog("[ERR] Timeout 8s");
            client.stop();
            discordProcessing = false;
            return;
        }
        delay(50);
        yield();
        ESP.wdtFeed(); // Feed watchdog durante espera
    }
    
    // Read status line only
    String statusLine = client.readStringUntil('\n');
    addDebugLog("Response: " + statusLine.substring(0, 20));
    
    int statusCode = 0;
    if (statusLine.indexOf("200") > 0) statusCode = 200;
    else if (statusLine.indexOf("204") > 0) statusCode = 204;
    else if (statusLine.indexOf("400") > 0) statusCode = 400;
    else if (statusLine.indexOf("401") > 0) statusCode = 401;
    else if (statusLine.indexOf("403") > 0) statusCode = 403;
    else if (statusLine.indexOf("404") > 0) statusCode = 404;
    else if (statusLine.indexOf("429") > 0) statusCode = 429;
    
    switch(statusCode) {
        case 200:
        case 204:
            addDebugLog("[SUCCESS] Sent!");
            lastDiscordAlert = now;
            break;
        case 400:
            addDebugLog("[ERR] Bad Request");
            break;
        case 401:
        case 403:
            addDebugLog("[ERR] Unauthorized");
            break;
        case 404:
            addDebugLog("[ERR] Not Found");
            break;
        case 429:
            addDebugLog("[ERR] Rate Limited");
            break;
        default:
            addDebugLog("[ERR] Code: " + String(statusCode));
    }
    
    client.stop();
    addDebugLog("=== DISCORD END ===");
    discordProcessing = false;
}

// Test alert - sin cooldown ni verificación de enabled
void sendDiscordAlertTest(const String& title, const String& message, const String& color) {
    if (discordProcessing) {
        addDebugLog("[TEST] Busy, skipping");
        return;
    }
    
    if (discordWebhookUrl.length() == 0) {
        addDebugLog("[TEST] No webhook URL");
        return;
    }
    
    if (WiFi.status() != WL_CONNECTED) {
        addDebugLog("[TEST] No WiFi!");
        return;
    }
    
    discordProcessing = true;
    addDebugLog("=== TEST ALERT START ===");
    addDebugLog("Title: " + title.substring(0, 25));
    
    // ===== VERIFICAR HEAP ANTES DE SSL =====
    uint32_t freeHeap = ESP.getFreeHeap();
    addDebugLog("Heap: " + String(freeHeap) + " bytes");
    
    if (freeHeap < 15000) {  // SSL necesita ~8-12KB mínimo
        addDebugLog("[ERR] HEAP BAJO! <15KB");
        Serial.println("[DISCORD TEST] ALERTA: Heap bajo (" + String(freeHeap) + " bytes). Saltando test para evitar crash.");
        discordProcessing = false;
        return;
    }
    
    // Parse URL
    String host, path;
    int port = 443;
    
    if (!discordWebhookUrl.startsWith("https://")) {
        addDebugLog("[ERR] Not HTTPS");
        discordProcessing = false;
        return;
    }
    
    int hostStart = 8;
    int pathStart = discordWebhookUrl.indexOf('/', hostStart);
    if (pathStart > 0) {
        host = discordWebhookUrl.substring(hostStart, pathStart);
        path = discordWebhookUrl.substring(pathStart);
    } else {
        addDebugLog("[ERR] Bad URL format");
        discordProcessing = false;
        return;
    }
    
    addDebugLog("Host: " + host);
    
    // Connect
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(8000); // Reducido de 15s a 8s
    
    addDebugLog("Connecting...");
    if (!client.connect(host.c_str(), port)) {
        addDebugLog("[ERR] Connect failed");
        addDebugLog("WiFi: " + String(WiFi.RSSI()) + " dBm");
        discordProcessing = false;
        return;
    }
    
    addDebugLog("Connected!");
    ESP.wdtFeed(); // Feed watchdog después de conectar
    
    // Build JSON
    StaticJsonDocument<512> doc;
    JsonArray embeds = doc.createNestedArray("embeds");
    JsonObject embed = embeds.createNestedObject();
    embed["title"] = title;
    embed["description"] = message;
    embed["color"] = strtol(color.c_str(), NULL, 16);
    
    JsonObject footer = embed.createNestedObject("footer");
    footer["text"] = "GreenNanny Test";
    
    String payload;
    serializeJson(doc, payload);
    
    addDebugLog("Payload: " + String(payload.length()) + "B");
    
    // Send POST
    client.print(String("POST ") + path + " HTTP/1.1\r\n");
    client.print(String("Host: ") + host + "\r\n");
    client.print("User-Agent: ESP8266\r\n");
    client.print("Content-Type: application/json\r\n");
    client.print("Content-Length: " + String(payload.length()) + "\r\n");
    client.print("Connection: close\r\n\r\n");
    client.print(payload);
    client.flush();
    
    addDebugLog("Sent, waiting...");
    
    // Wait for response (timeout reducido a 8s)
    unsigned long timeout = millis();
    while (!client.available()) {
        if (millis() - timeout > 8000) {  // Reducido de 15s a 8s
            addDebugLog("[ERR] Timeout 8s");
            client.stop();
            discordProcessing = false;
            return;
        }
        delay(50);
        yield();
        ESP.wdtFeed(); // Feed watchdog durante espera
    }
    
    // Read status line only
    String statusLine = client.readStringUntil('\n');
    addDebugLog("Response: " + statusLine.substring(0, 20));
    
    int statusCode = 0;
    if (statusLine.indexOf("200") > 0) statusCode = 200;
    else if (statusLine.indexOf("204") > 0) statusCode = 204;
    else if (statusLine.indexOf("400") > 0) statusCode = 400;
    else if (statusLine.indexOf("401") > 0) statusCode = 401;
    else if (statusLine.indexOf("403") > 0) statusCode = 403;
    else if (statusLine.indexOf("404") > 0) statusCode = 404;
    else if (statusLine.indexOf("429") > 0) statusCode = 429;
    
    switch(statusCode) {
        case 200:
        case 204:
            addDebugLog("[SUCCESS] Test sent!");
            break;
        case 400:
            addDebugLog("[ERR] Bad Request");
            break;
        case 401:
        case 403:
            addDebugLog("[ERR] Unauthorized");
            break;
        case 404:
            addDebugLog("[ERR] Not Found");
            break;
        case 429:
            addDebugLog("[ERR] Rate Limited");
            break;
        default:
            addDebugLog("[ERR] Code: " + String(statusCode));
    }
    
    client.stop();
    addDebugLog("=== TEST ALERT END ===");
    discordProcessing = false;
}

// Verificar condiciones y enviar alertas si es necesario
void checkAndSendAlerts(float temp, float hum, bool sensorValid) {
    if (!discordAlertsEnabled || discordWebhookUrl.length() == 0) {
        return;
    }
    
    // Alerta por fallo de sensor
    if (!sensorValid && discordAlerts.sensorFailAlert) {
        sendDiscordAlert(
            "⚠️ Sensor Failure Detected",
            "The DHT11 sensor is not responding or returning invalid data. Please check the connection.",
            "FF0000" // Rojo
        );
        return; // No verificar otros umbrales si el sensor falla
    }
    
    // Alerta por temperatura alta
    if (sensorValid && discordAlerts.tempHighAlert && temp >= discordAlerts.tempHighThreshold) {
        sendDiscordAlert(
            "🔥 High Temperature Alert",
            String("Temperature has reached ") + String(temp, 1) + "°C (threshold: " + String(discordAlerts.tempHighThreshold, 1) + "°C)",
            "FF6B00" // Naranja
        );
    }
    
    // Alerta por temperatura baja
    if (sensorValid && discordAlerts.tempLowAlert && temp <= discordAlerts.tempLowThreshold) {
        sendDiscordAlert(
            "❄️ Low Temperature Alert",
            String("Temperature has dropped to ") + String(temp, 1) + "°C (threshold: " + String(discordAlerts.tempLowThreshold, 1) + "°C)",
            "00BFFF" // Azul
        );
    }
    
    // Alerta por humedad alta
    if (sensorValid && discordAlerts.humHighAlert && hum >= discordAlerts.humHighThreshold) {
        sendDiscordAlert(
            "💧 High Humidity Alert",
            String("Humidity has reached ") + String(hum, 1) + "% (threshold: " + String(discordAlerts.humHighThreshold, 1) + "%)",
            "4169E1" // Azul royal
        );
    }
    
    // Alerta por humedad baja
    if (sensorValid && discordAlerts.humLowAlert && hum <= discordAlerts.humLowThreshold) {
        sendDiscordAlert(
            "🏜️ Low Humidity Alert",
            String("Humidity has dropped to ") + String(hum, 1) + "% (threshold: " + String(discordAlerts.humLowThreshold, 1) + "%)",
            "FFA500" // Naranja oscuro
        );
    }
}

// Handler para /getDiscordConfig - Obtener configuración de Discord
void handleGetDiscordConfig() {
    Serial.println("[HTTP] Solicitud /getDiscordConfig (GET).");
    server.sendHeader("Access-Control-Allow-Origin", "*");
    
    StaticJsonDocument<512> doc;
    doc["webhookUrl"] = discordWebhookUrl;
    doc["enabled"] = discordAlertsEnabled;
    
    JsonObject alerts = doc.createNestedObject("alerts");
    alerts["tempHighAlert"] = discordAlerts.tempHighAlert;
    alerts["tempHighThreshold"] = discordAlerts.tempHighThreshold;
    alerts["tempLowAlert"] = discordAlerts.tempLowAlert;
    alerts["tempLowThreshold"] = discordAlerts.tempLowThreshold;
    alerts["humHighAlert"] = discordAlerts.humHighAlert;
    alerts["humHighThreshold"] = discordAlerts.humHighThreshold;
    alerts["humLowAlert"] = discordAlerts.humLowAlert;
    alerts["humLowThreshold"] = discordAlerts.humLowThreshold;
    alerts["sensorFailAlert"] = discordAlerts.sensorFailAlert;
    alerts["deviceActivationAlert"] = discordAlerts.deviceActivationAlert;
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

// Handler para /setDiscordConfig - Configurar Discord webhook
void handleSetDiscordConfig() {
    Serial.println("[HTTP] Solicitud /setDiscordConfig (POST).");
    server.sendHeader("Access-Control-Allow-Origin", "*");
    
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"status\":\"error\", \"message\":\"Missing body\"}");
        return;
    }
    
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    
    if (error) {
        server.send(400, "application/json", "{\"status\":\"error\", \"message\":\"Invalid JSON\"}");
        return;
    }
    
    // Actualizar configuración
    if (doc.containsKey("webhookUrl")) {
        discordWebhookUrl = doc["webhookUrl"].as<String>();
    }
    if (doc.containsKey("enabled")) {
        discordAlertsEnabled = doc["enabled"];
    }
    
    if (doc.containsKey("alerts")) {
        JsonObject alerts = doc["alerts"];
        if (alerts.containsKey("tempHighAlert")) discordAlerts.tempHighAlert = alerts["tempHighAlert"];
        if (alerts.containsKey("tempHighThreshold")) discordAlerts.tempHighThreshold = alerts["tempHighThreshold"];
        if (alerts.containsKey("tempLowAlert")) discordAlerts.tempLowAlert = alerts["tempLowAlert"];
        if (alerts.containsKey("tempLowThreshold")) discordAlerts.tempLowThreshold = alerts["tempLowThreshold"];
        if (alerts.containsKey("humHighAlert")) discordAlerts.humHighAlert = alerts["humHighAlert"];
        if (alerts.containsKey("humHighThreshold")) discordAlerts.humHighThreshold = alerts["humHighThreshold"];
        if (alerts.containsKey("humLowAlert")) discordAlerts.humLowAlert = alerts["humLowAlert"];
        if (alerts.containsKey("humLowThreshold")) discordAlerts.humLowThreshold = alerts["humLowThreshold"];
        if (alerts.containsKey("sensorFailAlert")) discordAlerts.sensorFailAlert = alerts["sensorFailAlert"];
        if (alerts.containsKey("deviceActivationAlert")) discordAlerts.deviceActivationAlert = alerts["deviceActivationAlert"];
    }
    
    saveDiscordConfig();
    
    server.send(200, "application/json", "{\"status\":\"success\", \"message\":\"Discord configuration updated\"}");
}

// Handler para /testDiscordAlert - Enviar alerta de prueba
void handleTestDiscordAlert() {
    Serial.println("[HTTP] Solicitud /testDiscordAlert (POST).");
    server.sendHeader("Access-Control-Allow-Origin", "*");
    
    // Verificar solo que haya webhook URL configurada (no verificar si está enabled)
    // porque es una prueba y debe funcionar aunque esté deshabilitado
    if (discordWebhookUrl.length() == 0) {
        Serial.println("[DISCORD] Test alert failed: No webhook URL configured");
        server.send(400, "application/json", "{\"status\":\"error\", \"message\":\"Discord webhook URL not configured\"}");
        return;
    }
    
    Serial.println("[DISCORD] Sending test alert...");
    
    // Enviar alerta de prueba (sin cooldown)
    float temp = getTemperature();
    float hum = getHumidity();
    String message = String("This is a test alert from GreenNanny!\n\n") +
                    "Current conditions:\n" +
                    "🌡️ Temperature: " + String(temp, 1) + "°C\n" +
                    "💧 Humidity: " + String(hum, 1) + "%\n\n" +
                    "If you received this message, Discord alerts are working correctly!";
    
    sendDiscordAlertTest(
        "✅ Test Alert - GreenNanny",
        message,
        "00FF00" // Verde
    );
    
    Serial.println("[DISCORD] Test alert sent successfully");
    server.send(200, "application/json", "{\"status\":\"success\", \"message\":\"Test alert sent to Discord\"}");
}

// Handler para /getLogs - Obtener logs de debug
void handleGetLogs() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Cache-Control", "no-cache");
    
    // Crear JSON con los logs
    String response = "{\"logs\":[";
    
    if (debugLogCount > 0) {
        // Calcular índice de inicio (el log más antiguo en el buffer)
        int startIdx = (debugLogCount < DEBUG_LOG_SIZE) ? 0 : debugLogIndex;
        
        // Iterar por todos los logs en orden cronológico
        for (int i = 0; i < debugLogCount; i++) {
            int idx = (startIdx + i) % DEBUG_LOG_SIZE;
            if (i > 0) response += ",";
            
            // Escapar caracteres especiales en el log
            String logMsg = debugLogBuffer[idx];
            logMsg.replace("\\", "\\\\");
            logMsg.replace("\"", "\\\"");
            logMsg.replace("\n", "\\n");
            logMsg.replace("\r", "\\r");
            
            response += "\"" + logMsg + "\"";
        }
    }
    
    response += "]}";
    server.send(200, "application/json", response);
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

// ============================================
// OTA (Over-The-Air) UPDATE CONFIGURATION
// ============================================

#ifndef DISABLE_OTA
void setupOTA() {
    Serial.println("[SETUP] Configurando OTA updates...");
    
    // Generate hostname locally since actualHostname was removed
    String localHostname = String(HOSTNAME) + "-" + String(ESP.getChipId() & 0xFFF, HEX);
    
    // ArduinoOTA setup
    ArduinoOTA.setPort(8266);
    ArduinoOTA.setHostname(localHostname.c_str());
    ArduinoOTA.setPassword(OTA_PASSWORD);
    
    ArduinoOTA.onStart([]() {
        otaInProgress = true;
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH) {
            type = "sketch";
        } else { // U_FS
            type = "filesystem";
            LittleFS.end(); // Cerrar filesystem antes de actualizar
        }
        Serial.println("[OTA] Iniciando actualización: " + type);
    });
    
    ArduinoOTA.onEnd([]() {
        Serial.println("\n[OTA] Actualización completada!");
        otaInProgress = false;
    });
    
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        static unsigned long lastPrint = 0;
        unsigned long now = millis();
        if (now - lastPrint > 1000) { // Print every second
            Serial.printf("[OTA] Progreso: %u%%\r", (progress / (total / 100)));
            lastPrint = now;
        }
    });
    
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("[OTA ERROR] Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR) Serial.println("End Failed");
        otaInProgress = false;
    });
    
    ArduinoOTA.begin();
    Serial.println("[OTA] ArduinoOTA iniciado en puerto 8266");
    
    // HTTP Update Server setup (web-based OTA)
    httpUpdater.setup(&server, "/update", "admin", OTA_PASSWORD);
    Serial.println("[OTA] HTTP Update Server configurado en /update");
    Serial.println("[OTA] Usuario: admin | Contraseña: " + String(OTA_PASSWORD));
}
#else
void setupOTA() {
    Serial.println("[OTA] DESHABILITADO - Compilado sin soporte OTA (ahorra ~2-3KB RAM)");
    Serial.println("[OTA] Para actualizar firmware, usar USB solamente");
}
#endif

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
    server.on("/controlFan", HTTP_POST, handleFanControl);               // Encender/apagar ventilador manualmente
    server.on("/controlExtractor", HTTP_POST, handleExtractorControl);   // Encender/apagar turbina manualmente
    server.on("/setThresholds", HTTP_POST, handleSetThresholds);         // Configurar umbrales de temp/humedad
    server.on("/getThresholds", HTTP_GET, handleGetThresholds);          // Obtener umbrales actuales
    server.on("/testMode", HTTP_ANY, handleTestMode);                    // Activar/desactivar modo test (GET/POST)
    server.on("/getDiscordConfig", HTTP_GET, handleGetDiscordConfig);    // Obtener configuración de Discord
    server.on("/setDiscordConfig", HTTP_POST, handleSetDiscordConfig);   // Configurar Discord webhook
    server.on("/testDiscordAlert", HTTP_POST, handleTestDiscordAlert);   // Enviar alerta de prueba a Discord
    server.on("/getLogs", HTTP_GET, handleGetLogs);                      // Obtener logs de debug
    server.on("/health", HTTP_GET, handleHealth);                        // Health check endpoint
    server.on("/diskInfo", HTTP_GET, handleDiskInfo);                    // Información del filesystem
    server.on("/healthLog", HTTP_GET, handleHealthLog);                  // Tail health log (NEW)
    server.on("/downloadLogs", HTTP_GET, handleDownloadLogs);            // Download aggregated logs (NEW)
    server.on("/heapDiagnose", HTTP_GET, [](){                           // Diagnóstico manual de heap (NEW)
        Serial.println("[HTTP] Solicitud /heapDiagnose (GET).");
        server.sendHeader("Access-Control-Allow-Origin", "*");
        diagnoseHeapUsage();
        server.send(200, "application/json", "{\"status\":\"success\", \"message\":\"Heap diagnosis executed, check serial and health log.\"}");
    });
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
        delay(300); // Dar tiempo a enviar la respuesta
        scheduleRestart("MANUAL_RESTART");
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
