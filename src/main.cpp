#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <DHT.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

// Definiciones de pines y tipos
#define DHTPIN D2
#define DHTTYPE DHT11
#define BOMBA_PIN D3

#define MAX_JSON_OBJECTS 500

// Configuración de DNS y Captive Portal
DNSServer dnsServer;
const byte DNS_PORT = 53;

// Servidor web en el puerto 80
ESP8266WebServer server(80);

// Documentos JSON
StaticJsonDocument<200> jsonDocument;

// Almacenamiento de mediciones
String measurements[MAX_JSON_OBJECTS];
int jsonIndex = 0;

// Configuración de red estática
IPAddress ip(192, 168, 0, 78);
IPAddress gateway(192, 168, 0, 1);
IPAddress subnet(255, 255, 255, 0);

// Variables de tiempo
unsigned long startTime = 0;
unsigned long lastDebugPrint = 0;

// Simulación de sensores
bool simulateSensors = false;
float simulatedHumidity = 55.0;
float simulatedTemperature = 25.0;

// Inicialización del sensor DHT
DHT dht(DHTPIN, DHTTYPE);

// Control de la bomba
int pumpActivationCount = 0;
unsigned long pumpOnTime = 0;
unsigned long pumpDurationMs = 30000; 
bool pumpAutoOff = false; 
bool pumpActivated = false; 
unsigned long lastSecondPrint = 0;
int pumpSecondsCount = 0;

// Intervalo de mediciones (en horas)
int measurementInterval = 3; // Por defecto 3 horas
int nextMeasure = 0;

// Definición de etapas fenológicas
struct Stage {
  const char* name;
  int duration_days;     // Días que dura la etapa
  int humidityThreshold; // Umbral mínimo de humedad (%)
  int wateringTimeSec;   // Tiempo de riego en segundos
};

Stage stages[] = {
  {"Germinacion", 7, 60, 10},
  {"Vegetativo", 14, 55, 20},
  {"Prefloracion", 7, 50, 30},
  {"Floracion", 30, 45, 30},
  {"Maduracion", 10, 40, 15}
};

// Variables para control manual de etapas
bool manualStageControl = false;
int manualStageIndex = 0;

// Prototipos de funciones
int getCurrentStage(int days);
void activatePump(unsigned long durationMs = 30000);
void deactivatePump();
void loadMeasurementInterval();
void saveMeasurementInterval(int interval);
void handleWifiListRequest();
void setupDHTSensor();
float getHumidity();
float getTemperature();
void setupBomba();
String loadMeasurements();
float calculateVPD(float temperature, float humidity);
void handleData();
void takeMeasurement();
void handleConnectWifi();
void handleSaveWifiCredentials();
int parseData(String input, String output[]);
String arrayToString(String array[], size_t arraySize);
void saveMeasurementFile(String Measurement);
void saveMeasurement(const String& jsonString);
void formatMeasurementsToString(String& formattedString);
void handleSaveMeasurement();
void handleLoadMeasurement();
void handleThresholdRequest();
void loadWifiCredentials();
void handleClearMeasurementHistory();
void handleMeasurementInterval();
void handlePumpControl();
void setupServer();
void startAPMode();
void loadManualStage();
void controlIndependiente();
void handleSetManualStage();
void handleGetCurrentStage();
void handleResetManualStage();
void handleListStages();
void handleSerialCommands();

void setup() {
    Serial.begin(115200);
    Serial.println("[SETUP] Iniciando sistema de riego con simulación de sensores.");

    // Montar el sistema de archivos
    if (!LittleFS.begin()) {
        Serial.println("[ERROR] No se pudo montar LittleFS");
        return;
    }

    // Configurar el pin de la bomba
    setupBomba();

    // Inicializar el sensor DHT
    setupDHTSensor();

    // Tiempo de inicio
    startTime = millis();

    // Cargar mediciones previas
    parseData(loadMeasurements(), measurements);

    // Cargar intervalo de medición
    loadMeasurementInterval();

    // Cargar credenciales WiFi y conectar
    loadWifiCredentials();

    // Cargar etapa manual si existe
    loadManualStage();

    // Iniciar modo AP si no está conectado a WiFi
    if (WiFi.status() != WL_CONNECTED) {
        startAPMode();
    }

    // Configurar el servidor web y endpoints
    setupServer();

    Serial.println("[INFO] Setup completo, el sistema está listo para funcionar.");
}

void loop() {
    // Manejar clientes del servidor web
    server.handleClient();

    // Procesar solicitudes DNS (captive portal)
    dnsServer.processNextRequest();

    // Manejar comandos seriales para control manual
    handleSerialCommands();

    unsigned long now = millis();

    // Apagar la bomba automáticamente si se cumplió el tiempo (autoOff)
    if (pumpAutoOff && pumpActivated) {
        if (now - pumpOnTime >= pumpDurationMs) {
            deactivatePump();
        }
    }

    // Contar segundos si la bomba está activada con autoOff
    if (pumpActivated && pumpAutoOff) {
        if (now - lastSecondPrint >= 1000) {
            pumpSecondsCount++;
            Serial.print("[INFO] Bomba activa (autoOff), segundo ");
            Serial.println(pumpSecondsCount);
            lastSecondPrint = now;
        }
    }

    // Calcular tiempo transcurrido
    unsigned long elapsedTime = (now - startTime) / 1000;
    unsigned long totalElapsedTimeHours = elapsedTime / 3600;

    // Verificar si es momento de tomar una medición
    if ((int)totalElapsedTimeHours >= nextMeasure) {
        Serial.println("[INFO] Se ha alcanzado la hora programada para la siguiente medición, preparándose para medir...");
        controlIndependiente();
        nextMeasure = totalElapsedTimeHours + measurementInterval;
        Serial.print("[INFO] Próxima medición programada en ");
        Serial.print(measurementInterval);
        Serial.println(" horas");
    }

    // Mensajes de debug cada 10 minutos
    if (now - lastDebugPrint >= 600000) { // 600000 ms = 10 minutos
        lastDebugPrint = now;
        int remain = nextMeasure - (int)totalElapsedTimeHours;
        Serial.print("[DEBUG] Estado actual: ");
        if (remain > 0) {
            Serial.print("Faltan ");
            Serial.print(remain);
            Serial.println(" horas para la próxima medición programada.");
        } else {
            Serial.println("La próxima medición se realizará muy pronto o ya se ha realizado.");
        }
    }
}

// Función para determinar la etapa actual
int getCurrentStage(int days) {
    if (manualStageControl) {
        Serial.print("[INFO] Control manual de etapa activado. Etapa actual: ");
        Serial.println(stages[manualStageIndex].name);
        return manualStageIndex;
    }

    int sum = 0;
    for (int i = 0; i < (int)(sizeof(stages)/sizeof(stages[0])); i++) {
        sum += stages[i].duration_days;
        if (days <= sum) {
            return i;
        }
    }
    return (sizeof(stages)/sizeof(stages[0])) - 1; 
}

// Función para activar la bomba
void activatePump(unsigned long durationMs) {
    Serial.println("[ACCION] Activando bomba de riego (autoOff)... Preparada para apagarse sola.");
    digitalWrite(BOMBA_PIN, HIGH);
    pumpActivated = true;
    pumpSecondsCount = 0;
    lastSecondPrint = millis();
    pumpOnTime = millis();
    pumpDurationMs = durationMs;
    pumpAutoOff = true;
    pumpActivationCount++;
}

// Función para desactivar la bomba
void deactivatePump() {
    Serial.println("[ACCION] Desactivando la bomba de riego...");
    digitalWrite(BOMBA_PIN, LOW);
    pumpActivated = false;
    pumpAutoOff = false;
    pumpSecondsCount = 0;
}

// Carga el intervalo de medición desde el archivo
void loadMeasurementInterval() {
    File configFile = LittleFS.open("/interval.txt", "r");
    if (!configFile) {
        Serial.println("[INFO] No se encontró interval.txt, usando valor por defecto 3 horas");
        measurementInterval = 3;
        return;
    }
    String val = configFile.readStringUntil('\n');
    val.trim();
    measurementInterval = val.toInt();
    if (measurementInterval <= 0) measurementInterval = 3;
    configFile.close();
    Serial.print("[INFO] Intervalo de medición cargado: ");
    Serial.print(measurementInterval);
    Serial.println(" horas");
}

// Guarda el intervalo de medición en el archivo
void saveMeasurementInterval(int interval) {
    File configFile = LittleFS.open("/interval.txt", "w");
    if (!configFile) {
        Serial.println("[ERROR] No se pudo guardar interval.txt");
        return;
    }
    configFile.println(interval);
    configFile.close();
    measurementInterval = interval;
    Serial.print("[INFO] Nuevo intervalo guardado: ");
    Serial.print(measurementInterval);
    Serial.println(" horas");
}

// Endpoint para listar redes WiFi disponibles
void handleWifiListRequest() {
    server.on("/wifiList", HTTP_GET, []() {
        int numNetworks = WiFi.scanNetworks();
        DynamicJsonDocument wifiJson(1024);
        JsonArray networks = wifiJson.to<JsonArray>();
        if (numNetworks > 0) {
            for (int i = 0; i < numNetworks; ++i) {
                JsonObject network = networks.createNestedObject();
                network["ssid"] = WiFi.SSID(i);
                network["rssi"] = WiFi.RSSI(i);
            }
        }
        String response;
        serializeJson(wifiJson, response);
        server.send(200, "application/json", response);
    });
}

// Inicializa el sensor DHT o usa valores simulados
void setupDHTSensor() {
    Serial.println("[SETUP] Inicializando sensor DHT o usando valores simulados...");
    dht.begin();
}

// Obtener humedad (real o simulada)
float getHumidity() {
    if (simulateSensors) {
        simulatedHumidity += (random(-10,10) / 10.0); 
        if (simulatedHumidity < 30) simulatedHumidity = 30;
        if (simulatedHumidity > 70) simulatedHumidity = 70;
        Serial.print("[SIMULACION] Humedad simulada: ");
        Serial.println(simulatedHumidity);
        return simulatedHumidity;
    } else {
        float h = dht.readHumidity();
        if (isnan(h)) h = 0;
        Serial.print("[SENSOR] Humedad leída: ");
        Serial.println(h);
        return h;
    }
}

// Obtener temperatura (real o simulada)
float getTemperature() {
    if (simulateSensors) {
        simulatedTemperature += (random(-5,5) / 10.0);
        if (simulatedTemperature < 18) simulatedTemperature = 18;
        if (simulatedTemperature > 30) simulatedTemperature = 30;
        Serial.print("[SIMULACION] Temperatura simulada: ");
        Serial.println(simulatedTemperature);
        return simulatedTemperature;
    } else {
        float t = dht.readTemperature();
        if (isnan(t)) t = 0;
        Serial.print("[SENSOR] Temperatura leída: ");
        Serial.println(t);
        return t;
    }
}

// Configura el pin de la bomba
void setupBomba() {
    Serial.println("[SETUP] Configurando el pin de la bomba...");
    pinMode(BOMBA_PIN, OUTPUT);
    digitalWrite(BOMBA_PIN, LOW);
}

// Carga las mediciones previas desde el archivo
String loadMeasurements() {
    File configFile = LittleFS.open("/Measurements.txt", "r");
    if (!configFile) {
        Serial.println("[INFO] No se encontraron mediciones previas.");
        return "";
    }
    String Measurements = configFile.readStringUntil('\n');
    Serial.println("[INFO] Mediciones cargadas desde el archivo:");
    Serial.println(Measurements);
    configFile.close();
    return Measurements;
}

// Calcula el VPD (Déficit de Presión de Vapor)
float calculateVPD(float temperature, float humidity) {
    float svp = 0.6108 * exp((17.27 * temperature) / (temperature + 237.3));
    float avp = (humidity / 100.0) * svp;
    float vpd = svp - avp;
    return vpd;
}
int loadHumidityThreshold() {
    File configFile = LittleFS.open("/humidityThreshold.txt", "r");
    if (!configFile) {
        Serial.println("Error al abrir humidityThreshold.txt, usando 50 por defecto");
        return 50;
    }
    String thresholdStr = configFile.readStringUntil('\n');
    configFile.close();
    return thresholdStr.toInt();
}
// Maneja la solicitud /data
void handleData() {
    Serial.println("[ACCION] Solicitud /data recibida, midiendo...");

    float humidity = getHumidity();
    float temperature = getTemperature();

    unsigned long currentTime = millis();
    unsigned long elapsedTime = (currentTime - startTime) / 1000;
    unsigned long totalElapsedTimeHours = elapsedTime / 3600;
    unsigned long totalElapsedTimeMinutes = (elapsedTime % 3600) / 60;
    String elapsedTimeString = String(totalElapsedTimeHours) + "." + String(totalElapsedTimeMinutes);

    float vpd = calculateVPD(temperature, humidity);

    String response = "{\"humidity\":" + String(humidity) +
                      ",\"temperature\":" + String(temperature) +
                      ",\"vpd\":" + String(vpd) +
                      ",\"pumpStatus\":" + String(pumpActivated) +
                      ",\"elapsedTime\":" + String(elapsedTime) +
                      ",\"humidityThreshold\":" + String(loadHumidityThreshold()) +
                      ",\"pumpActivationCount\":" + String(pumpActivationCount) +
                      ",\"totalElapsedTime\":\"" + elapsedTimeString + "\"" +
                      ",\"startTime\":" + String(startTime) +
                      "}";

    Serial.println("[INFO] Datos enviados al cliente:");
    Serial.println(response);

    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", response);
}

// Realiza una medición manual
void takeMeasurement() {
    Serial.println("[ACCION] /takeMeasurement: Se realizará una medición manual ahora...");
    handleData();
}

// Conecta a WiFi con credenciales proporcionadas
void handleConnectWifi() {
    server.on("/connectWifi", HTTP_POST, []() {
        Serial.println("[ACCION] Intentando conectar a la WiFi con las credenciales proporcionadas...");
        String ssid = server.arg("ssid");
        String password = server.arg("password");
        WiFi.begin(ssid.c_str(), password.c_str());
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 10) {
            delay(1000);
            attempts++;
            Serial.print(".");
        }
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("\n[INFO] Conexión WiFi exitosa.");
            server.send(200, "text/plain", "Conexión exitosa. IP: " + WiFi.localIP().toString());
        } else {
            Serial.println("\n[ERROR] No se pudo conectar a la red WiFi.");
            server.send(500, "text/plain", "Error al conectar a la red WiFi.");
        }
    });
}

// Guarda las credenciales WiFi y reinicia el sistema
void handleSaveWifiCredentials() {
    server.on("/saveWifiCredentials", HTTP_POST, []() {
        Serial.println("[ACCION] Guardando credenciales WiFi y validando...");
        String ssid = server.arg("ssid");
        String password = server.arg("password");

        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid.c_str(), password.c_str());
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 10) {
            delay(1000);
            attempts++;
            Serial.print(".");
        }

        if (WiFi.status() == WL_CONNECTED) {
            File configFile = LittleFS.open("/WifiConfig.txt", "w");
            if (!configFile) {
                Serial.println("\n[ERROR] No se pudo guardar WifiConfig.txt.");
                server.send(500, "text/plain", "Error al guardar las credenciales WiFi.");
                return;
            }
            configFile.println(ssid);
            configFile.println(password);
            configFile.close();
            Serial.println("\n[INFO] Credenciales WiFi guardadas. Reiniciando...");
            server.send(200, "text/plain", "Credenciales WiFi guardadas. Reiniciando...");
            delay(2000);
            ESP.restart();
        } else {
            Serial.println("\n[ERROR] No se pudo conectar con las credenciales proporcionadas.");
            server.send(500, "text/plain", "No se pudo conectar a la red con las credenciales proporcionadas.");
        }
    });
}

// Analiza los datos JSON
int parseData(String input, String output[]) {
    int count = 0;
    int startIndex = 0;
    int endIndex = 0;
    while ((startIndex = input.indexOf('{', endIndex)) != -1) {
        endIndex = input.indexOf('}', startIndex);
        if (endIndex == -1) {
            break;
        }
        String entry = input.substring(startIndex, endIndex + 1);
        output[count++] = entry;
    }
    return count;
}

// Convierte el array de Strings a un solo String
String arrayToString(String array[], size_t arraySize) {
    String result = "";
    bool first = true;
    for (size_t i = 0; i < arraySize; i++) {
        if (array[i].length() > 0) {
            if (!first) {
                result += ", ";
            }
            result += array[i];
            first = false;
        }
    }
    return result;
}

// Guarda las mediciones en un archivo
void saveMeasurementFile(String Measurement) {
    File configFile = LittleFS.open("/Measurements.txt", "w");
    if (!configFile) {
        Serial.println("[ERROR] No se pudo abrir Measurements.txt para escritura.");
        return;
    }
    configFile.println(Measurement);
    configFile.close();
}

// Guarda una medición individual
void saveMeasurement(const String& jsonString) {
    Serial.println("[ACCION] Guardando medición...");
    Serial.println(jsonString);
    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, jsonString);
    if (error) {
        Serial.println("[ERROR] al deserializar JSON de la medición.");
        return;
    }
    String measurementString;
    serializeJson(doc, measurementString);
    if (jsonIndex < MAX_JSON_OBJECTS) {
        measurements[jsonIndex++] = measurementString;
        saveMeasurementFile(arrayToString(measurements, MAX_JSON_OBJECTS));
    } else {
        Serial.println("[ERROR] Array de JSON lleno. No se pudo agregar la medición.");
    }
}

// Formatea las mediciones para enviarlas como un array JSON
void formatMeasurementsToString(String& formattedString) {
    formattedString = "[";
    bool first = true;
    for (int i = 0; i < MAX_JSON_OBJECTS; i++) {
        if (measurements[i] != "") {
            if (!first) {
                formattedString += ", ";
            }
            formattedString += measurements[i];
            first = false;
        }
    }
    formattedString += "]";
}

// Endpoint para guardar una medición
void handleSaveMeasurement() {
    server.on("/saveMeasurement", HTTP_POST, []() {
        String jsonString = server.arg("plain");
        saveMeasurement(jsonString);
        server.send(200, "text/plain", "Medición guardada correctamente");
    });
}

// Endpoint para cargar las mediciones
void handleLoadMeasurement() {
    server.on("/loadMeasurement", HTTP_GET, []() {
        String jsonString = arrayToString(measurements, MAX_JSON_OBJECTS);
        formatMeasurementsToString(jsonString);
        Serial.println("[ACCION] Cargando mediciones para enviar al cliente:");
        Serial.println(jsonString);
        server.send(200, "application/json", jsonString);
    });
}

// Endpoint para establecer el umbral global (aunque se usa por etapa)
void handleThresholdRequest() {
    server.on("/threshold", HTTP_POST, [](){
      if (!server.hasArg("plain")) {
        server.send(400, "text/plain", "Invalid request");
        return;
      }

      String jsonString = server.arg("plain");
      StaticJsonDocument<200> doc;
      DeserializationError error = deserializeJson(doc, jsonString);

      if (error) {
        server.send(400, "text/plain", "Invalid JSON");
        return;
      }

      int newThreshold = doc["umbral"];
      File configFile = LittleFS.open("/humidityThreshold.txt", "w");
      if (!configFile) {
          Serial.println("[ERROR] al abrir humidityThreshold.txt");
          server.send(500, "text/plain", "Error interno");
          return;
      }
      configFile.println(newThreshold);
      configFile.close();

      Serial.print("[INFO] Nuevo umbral global guardado: ");
      Serial.println(newThreshold);

      server.send(200, "application/json", "{\"status\":\"success\"}");
    });
}

// Carga las credenciales WiFi desde el archivo
void loadWifiCredentials() {
    File configFile = LittleFS.open("/WifiConfig.txt", "r");
    if (!configFile) {
        Serial.println("[INFO] No hay credenciales WiFi guardadas.");
        return;
    }
    delay(500);
    if (configFile.available()) {
        String ssidc = configFile.readStringUntil('\n');
        String passwordc = configFile.readStringUntil('\n');
        configFile.close();
        ssidc.trim();
        passwordc.trim();
        Serial.println("[ACCION] Intentando conectar con las credenciales guardadas...");
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssidc.c_str(), passwordc.c_str());
        WiFi.config(ip, gateway, subnet);
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 10) {
            delay(1000);
            attempts++;
            Serial.print(".");
        }
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("\n[INFO] Conectado a WiFi con credenciales guardadas.");
            Serial.print("Dirección IP: ");
            Serial.println(WiFi.localIP());
        } else {
            Serial.println("\n[ERROR] No se pudo conectar con credenciales guardadas.");
        }
    } else {
        Serial.println("[INFO] WifiConfig.txt vacío o ilegible.");
        configFile.close();
    }
}

// Endpoint para limpiar el historial de mediciones
void handleClearMeasurementHistory() {
    server.on("/clearHistory", HTTP_POST, [](){
        Serial.println("[ACCION] Borrando historial de mediciones...");
        for (int i = 0; i < MAX_JSON_OBJECTS; i++) {
            measurements[i] = "";
        }
        jsonIndex = 0;
        saveMeasurementFile("");
        server.send(200, "application/json", "{\"status\":\"success\"}");
    });
}

// Endpoint para establecer y obtener el intervalo de medición
void handleMeasurementInterval() {
    // Establecer el intervalo
    server.on("/setMeasurementInterval", HTTP_POST, [](){
       if (!server.hasArg("plain")) {
           server.send(400, "text/plain", "Missing JSON");
           return;
       }
       String body = server.arg("plain");
       StaticJsonDocument<200> doc;
       DeserializationError error = deserializeJson(doc, body);
       if (error) {
           server.send(400, "text/plain", "Invalid JSON");
           return;
       }
       int newInterval = doc["interval"] | -1;
       if (newInterval <= 0) {
           server.send(400, "text/plain", "Interval must be > 0");
           return;
       }
       Serial.print("[ACCION] Ajustando el intervalo de medición a: ");
       Serial.print(newInterval);
       Serial.println(" horas");
       saveMeasurementInterval(newInterval);
       server.send(200, "application/json", "{\"status\":\"success\"}");
    });

    // Obtener el intervalo
    server.on("/getMeasurementInterval", HTTP_GET, [](){
       DynamicJsonDocument doc(200);
       doc["interval"] = measurementInterval;
       String response;
       serializeJson(doc, response);
       Serial.print("[INFO] Devolviendo intervalo de medición actual: ");
       Serial.println(response);
       server.send(200, "application/json", response);
    });
}

// Endpoint para controlar la bomba manualmente
void handlePumpControl() {
    server.on("/controlPump", HTTP_POST, [](){
        if (!server.hasArg("plain")) {
            server.send(400, "text/plain", "Missing JSON");
            return;
        }
        String body = server.arg("plain");
        StaticJsonDocument<200> doc;
        DeserializationError error = deserializeJson(doc, body);
        if (error) {
            server.send(400, "text/plain", "Invalid JSON");
            return;
        }   
        String action = doc["action"] | "";
        if (action == "on") {
            int duration = doc["duration"] | 30;
            Serial.print("[ACCION] Encendiendo la bomba manualmente por ");
            Serial.print(duration);
            Serial.println(" segundos (autoOff).");
            activatePump(duration * 1000UL);
            server.send(200, "application/json", "{\"status\":\"pump_on\"}");
        } else if (action == "off") {
            Serial.println("[ACCION] Apagando la bomba manualmente.");
            deactivatePump();
            server.send(200, "application/json", "{\"status\":\"pump_off\"}");
        } else {
            Serial.println("[ERROR] Acción de bomba inválida recibida.");
            server.send(400, "application/json", "{\"status\":\"invalid_action\"}");
        }
    });
}

// Configura el servidor web y los endpoints
void setupServer() {
    // Página principal
    server.on("/", HTTP_GET, []() {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[INFO] No conectado a WiFi, mostrando página de configuración.");
            File file = LittleFS.open("/config.html", "r");
            if (!file) {
                server.send(500, "text/plain", "No se encontró config.html");
                return;
            }
            server.streamFile(file, "text/html");
            file.close();
        } else {
            Serial.println("[INFO] Conectado a WiFi, mostrando página principal.");
            File file = LittleFS.open("/index.html", "r");
            if (!file) {
                server.send(500, "text/plain", "No se encontró index.html");
                return;
            }
            server.streamFile(file, "text/html");
            file.close();
        }
    });

    // Página de configuración
    server.on("/config.html", HTTP_GET, []() {
        Serial.println("[ACCION] Mostrando página de configuración WiFi.");
        File file = LittleFS.open("/config.html", "r");
        if (!file) {
            server.send(500, "text/plain", "No se encontró config.html");
            return;
        }
        server.streamFile(file, "text/html");
        file.close();
    });

    // Endpoints existentes
    server.on("/data", HTTP_GET, handleData);
    server.on("/takeMeasurement", HTTP_POST, takeMeasurement);
    server.on("/restartSystem", HTTP_POST, []() {
        Serial.println("[ACCION] Reiniciando el sistema...");
        server.send(200, "text/plain", "Reiniciando...");
        ESP.restart();
    });
    

    handleThresholdRequest();
    handleSaveWifiCredentials();
    handleConnectWifi();
    handleSaveMeasurement();
    handleLoadMeasurement();
    handleClearMeasurementHistory();
    handleWifiListRequest();
    handleMeasurementInterval();
    handlePumpControl();

    // Endpoints para control manual de etapas
    // Establecer etapa manual
    server.on("/setManualStage", HTTP_POST, []() {
        if (!server.hasArg("plain")) {
            server.send(400, "text/plain", "Missing JSON");
            return;
        }

        String body = server.arg("plain");
        StaticJsonDocument<200> doc;
        DeserializationError error = deserializeJson(doc, body);
        if (error) {
            server.send(400, "text/plain", "Invalid JSON");
            return;
        }

        int stage = doc["stage"];
        if (stage < 0 || stage >= (sizeof(stages)/sizeof(stages[0]))) {
            server.send(400, "text/plain", "Invalid stage index");
            return;
        }

        manualStageIndex = stage;
        manualStageControl = true;

        // Guardar en el sistema de archivos para persistencia
        File configFile = LittleFS.open("/ManualStage.txt", "w");
        if (!configFile) {
            server.send(500, "text/plain", "Error saving manual stage");
            return;
        }
        configFile.println(manualStageIndex);
        configFile.close();

        Serial.print("[INFO] Etapa manual establecida a: ");
        Serial.println(stages[manualStageIndex].name);

        server.send(200, "application/json", "{\"status\":\"manual stage set\"}");
    });

    // Obtener la etapa actual
    server.on("/getCurrentStage", HTTP_GET, []() {
        int days = (millis() - startTime) / 86400000; // Convertir ms a días
        int stageIndex = getCurrentStage(days);
        DynamicJsonDocument doc(200);
        doc["currentStage"] = stages[stageIndex].name;
        doc["stageIndex"] = stageIndex;
        doc["manualControl"] = manualStageControl;
        String response;
        serializeJson(doc, response);
        server.send(200, "application/json", response);
    });

    // Resetear el control manual de etapas
    server.on("/resetManualStage", HTTP_POST, []() {
        manualStageControl = false;
        File configFile = LittleFS.open("/ManualStage.txt", "w");
        if (!configFile) {
            Serial.println("[ERROR] No se pudo borrar ManualStage.txt.");
            server.send(500, "text/plain", "Error resetting manual stage");
            return;
        }
        configFile.close();
        Serial.println("[INFO] Control manual de etapa desactivado.");
        server.send(200, "application/json", "{\"status\":\"manual stage reset\"}");
    });

    // Listar todas las etapas
    server.on("/listStages", HTTP_GET, []() {
        DynamicJsonDocument doc(1024);
        JsonArray stagesArray = doc.to<JsonArray>();
        for (int i = 0; i < (sizeof(stages)/sizeof(stages[0])); i++) {
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
    });

    // Iniciar el servidor
    server.serveStatic("/", LittleFS, "/");
    server.begin();
    Serial.println("[INFO] Servidor HTTP iniciado");
}

// Inicia el modo AP para configuración
void startAPMode() {
    Serial.println("[ACCION] Iniciando modo AP para configuración...");
    WiFi.mode(WIFI_AP);
    WiFi.softAP("Sistema-Riego", "password123");
    IPAddress apIP = WiFi.softAPIP();
    Serial.print("[INFO] Punto de acceso iniciado. IP: ");
    Serial.println(apIP);
    dnsServer.start(DNS_PORT, "*", apIP);
}

// Carga la etapa manual desde el archivo al iniciar
void loadManualStage() {
    File configFile = LittleFS.open("/ManualStage.txt", "r");
    if (!configFile) {
        Serial.println("[INFO] No se encontró ManualStage.txt, el control de etapa es automático.");
        manualStageControl = false;
        return;
    }

    String stageStr = configFile.readStringUntil('\n');
    configFile.close();

    if (stageStr.length() > 0) {
        int stage = stageStr.toInt();
        if (stage >= 0 && stage < (sizeof(stages)/sizeof(stages[0]))) {
            manualStageIndex = stage;
            manualStageControl = true;
            Serial.print("[INFO] Control manual de etapa cargado: ");
            Serial.println(stages[manualStageIndex].name);
        } else {
            Serial.println("[WARN] Etapa manual guardada inválida, desactivando control manual.");
            manualStageControl = false;
        }
    } else {
        Serial.println("[INFO] ManualStage.txt está vacío, control de etapa es automático.");
        manualStageControl = false;
    }
}

// Función principal de control independiente basada en etapas
void controlIndependiente() {
    unsigned long elapsedTime = (millis() - startTime) / 1000;
    unsigned long totalElapsedTimeHours = elapsedTime / 3600;
    unsigned long totalElapsedTimeDays = elapsedTime / 86400;

    Serial.println("[ACCION] Tomando medición programada según intervalo.");
    Serial.print("[INFO] Horas transcurridas: ");
    Serial.println(totalElapsedTimeHours);

    float humidity = getHumidity();
    float temperature = getTemperature();

    int stageIndex = getCurrentStage(totalElapsedTimeDays);
    int currentThreshold = stages[stageIndex].humidityThreshold;
    int wateringTimeSec = stages[stageIndex].wateringTimeSec;

    Serial.println("[INFO] Parámetros según la etapa actual:");
    Serial.print("Etapa: ");
    Serial.println(stages[stageIndex].name);
    Serial.print("Umbral de humedad: ");
    Serial.println(currentThreshold);
    Serial.print("Tiempo de riego: ");
    Serial.print(wateringTimeSec);
    Serial.println("s");

    bool localPumpActivated = false;
    // Usamos una variable estática para simular la última activación
    static int lastActivation = 0; 

    if (!pumpAutoOff) {
        if (humidity < currentThreshold) {
            Serial.println("[ACCION] La humedad está por debajo del umbral, se regará ahora.");
            activatePump(wateringTimeSec * 1000UL);
            localPumpActivated = true;
        } else {
            int diferencia = (int)totalElapsedTimeHours - lastActivation;
            if (diferencia > 23) {
                Serial.println("[ACCION] Han pasado más de 23h sin riego, se regará ahora.");
                activatePump(wateringTimeSec * 1000UL);
                localPumpActivated = true;
            } else {
                Serial.println("[INFO] No se riega en este momento, la humedad es suficiente y no han pasado 23h desde el último riego.");
                deactivatePump();
            }
        }
        if (localPumpActivated) {
            lastActivation = totalElapsedTimeHours;
        }
    } else {
        Serial.println("[INFO] Modo autoOff activo, no se ejecuta el control independiente.");
    }

    String elapsedTimeString = String(totalElapsedTimeHours) + "h";
    String measurementString = "{ \"humidity\": " + String(humidity) +
                               ", \"temperature\": " + String(temperature) +
                               ", \"timestamp\": \"" + elapsedTimeString +
                               "\", \"pumpActivated\": " + String(localPumpActivated ? "true" : "false") +
                               ", \"stage\": \"" + stages[stageIndex].name + "\" }";

    Serial.println("[ACCION] Guardando medición tomada en controlIndependiente:");
    Serial.println(measurementString);

    if (jsonIndex < MAX_JSON_OBJECTS) {
        measurements[jsonIndex++] = measurementString;
        saveMeasurementFile(arrayToString(measurements, MAX_JSON_OBJECTS));
    } else {
        Serial.println("[WARN] Array de JSON lleno. Eliminando registros antiguos.");
        for (int i = 0; i < MAX_JSON_OBJECTS - 100; i++) {
            measurements[i] = measurements[i + 100];
        }
        for (int i = MAX_JSON_OBJECTS - 100; i < MAX_JSON_OBJECTS; i++) {
            measurements[i] = "";
        }
        measurements[MAX_JSON_OBJECTS - 100] = measurementString;
        jsonIndex = MAX_JSON_OBJECTS - 99;
    }
}

// Función para manejar comandos seriales (opcional, si se desea)
void handleSerialCommands() {
    if (Serial.available() > 0) {
        String command = Serial.readStringUntil('\n');
        command.trim();

        if (command.startsWith("SET_STAGE")) {
            int stage = command.substring(9).toInt(); // Asumiendo el formato "SET_STAGE X"
            if (stage >= 0 && stage < (sizeof(stages)/sizeof(stages[0]))) {
                manualStageIndex = stage;
                manualStageControl = true;

                // Guardar en el sistema de archivos
                File configFile = LittleFS.open("/ManualStage.txt", "w");
                if (!configFile) {
                    Serial.println("[ERROR] No se pudo guardar la etapa manual.");
                } else {
                    configFile.println(manualStageIndex);
                    configFile.close();
                    Serial.print("[INFO] Etapa manual establecida a: ");
                    Serial.println(stages[manualStageIndex].name);
                }
            } else {
                Serial.println("[ERROR] Índice de etapa inválido.");
            }
        } else if (command == "RESET_STAGE") {
            manualStageControl = false;
            File configFile = LittleFS.open("/ManualStage.txt", "w");
            if (!configFile) {
                Serial.println("[ERROR] No se pudo borrar ManualStage.txt.");
            } else {
                configFile.close();
                Serial.println("[INFO] Control manual de etapa desactivado.");
            }
        } else {
            Serial.println("[WARN] Comando desconocido.");
        }
    }
}
