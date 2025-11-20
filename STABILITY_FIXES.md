# Plan de Estabilidad para GreenNanny ESP8266

## Problemas Críticos Identificados (31 Oct 2025)

### 1. **Uso de RAM Crítico: 76.3%**
- `String measurements[500]` consume ~20-25KB
- `String debugLogBuffer[200]` consume ~8-10KB
- **TOTAL BUFFERS: ~30KB de 81KB disponibles**
- Fragmentación de heap por uso intensivo de `String`

### 2. **Memory Leaks**
- String concatenation en Discord (líneas 2511-2517)
- Múltiples objetos temporales sin cleanup
- WiFiClientSecure SSL consume 8-12KB sin verificación previa

### 3. **Loops Bloqueantes (Watchdog Kills)**
- Línea 417: `while (true) { delay(1000); }` - HARD LOCK FATAL
- Línea 2524: `while (!client.available())` - timeout implementado pero riesgoso
- Falta `yield()` en operaciones largas

### 4. **Sin Sistema de Recovery**
- No detecta bajo heap
- No guarda crash logs
- No hay auto-restart en condiciones críticas
- No persiste estado antes de crash

### 5. **WiFi Disconnection Handling**
- No reconecta automáticamente en loop
- Pierde conexión sin recovery
- Operaciones WiFi sin verificación de conexión

---

## Soluciones Prioritarias

### ✅ FASE 1: FIXES CRÍTICOS (IMPLEMENTAR YA)

#### 1.1 Reducir Uso de RAM
```cpp
// ANTES:
#define MAX_JSON_OBJECTS 500  // ~25KB
#define DEBUG_LOG_SIZE 200     // ~10KB

// DESPUÉS:
#define MAX_JSON_OBJECTS 200  // ~10KB (suficiente para 200 mediciones)
#define DEBUG_LOG_SIZE 100     // ~5KB (100 logs es suficiente)
// AHORRO: ~20KB de RAM
```

#### 1.2 Eliminar Hard Lock (CRÍTICO)
```cpp
// LÍNEA 417 - REMOVER INMEDIATAMENTE:
while (true) { delay(1000); }  // ❌ NUNCA HACER ESTO

// REEMPLAZAR POR:
Serial.println("[FATAL] LittleFS mount failed. Restarting...");
delay(1000);
ESP.restart();  // ✅ Restart limpio
```

#### 1.3 Watchdog Software
```cpp
// Agregar al inicio de loop():
static unsigned long lastLoopTime = 0;
unsigned long loopDuration = millis() - lastLoopTime;

if (loopDuration > 30000) {  // Loop tomó más de 30s
    Serial.println("[WATCHDOG] Loop bloqueado! Restarting...");
    ESP.restart();
}
lastLoopTime = millis();
```

#### 1.4 Verificar Heap Antes de Operaciones Pesadas
```cpp
bool sendDiscordAlert(...) {
    // Verificar heap antes de SSL
    uint32_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < 15000) {  // Menos de 15KB
        addDebugLog("[DISCORD] HEAP BAJO! Saltando alerta");
        return false;
    }
    
    WiFiClientSecure client;
    // ... resto del código
}
```

#### 1.5 Auto-Reconnect WiFi
```cpp
// En loop(), después de ArduinoOTA.handle():
static unsigned long lastWiFiCheck = 0;
if (millis() - lastWiFiCheck > 30000) {  // Cada 30s
    if (WiFi.status() != WL_CONNECTED && wifiState == IDLE) {
        Serial.println("[WIFI] Desconectado! Reconectando...");
        WiFi.reconnect();
    }
    lastWiFiCheck = millis();
}
```

---

### ✅ FASE 2: ENDPOINT DE SALUD

```cpp
// Nuevo endpoint: /health
void handleHealth() {
    StaticJsonDocument<512> doc;
    
    doc["uptime_ms"] = millis();
    doc["free_heap"] = ESP.getFreeHeap();
    doc["heap_fragmentation"] = ESP.getHeapFragmentation();
    doc["max_free_block"] = ESP.getMaxFreeBlockSize();
    doc["wifi_rssi"] = WiFi.RSSI();
    doc["wifi_status"] = (WiFi.status() == WL_CONNECTED) ? "connected" : "disconnected";
    doc["reset_reason"] = ESP.getResetReason();
    doc["measurements_count"] = jsonIndex;
    doc["logs_count"] = debugLogCount;
    doc["discord_processing"] = discordProcessing;
    doc["ntp_synced"] = ntpTimeSynchronized;
    
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
    if (WiFi.status() != WL_CONNECTED) {
        healthy = false;
        issues += "WIFI_DOWN,";
    }
    
    doc["healthy"] = healthy;
    doc["issues"] = issues;
    
    String response;
    serializeJson(doc, response);
    server.send(healthy ? 200 : 503, "application/json", response);
}
```

---

### ✅ FASE 3: CRASH LOGS PERSISTENTES

```cpp
// Guardar crash info en LittleFS antes de restart
void saveCrashLog(String reason) {
    File file = LittleFS.open("/crash_log.txt", "a");
    if (file) {
        time_t now = time(nullptr);
        file.printf("[%lu] %s | Heap: %u | Reason: %s\n", 
                    now, 
                    getTimeString().c_str(),
                    ESP.getFreeHeap(), 
                    reason.c_str());
        file.close();
    }
}

// Endpoint para ver crashes
void handleCrashLog() {
    if (!LittleFS.exists("/crash_log.txt")) {
        server.send(200, "text/plain", "No crash logs found.");
        return;
    }
    
    File file = LittleFS.open("/crash_log.txt", "r");
    String content = file.readString();
    file.close();
    
    server.send(200, "text/plain", content);
}
```

---

### ✅ FASE 4: OPTIMIZACIÓN DE STRING

```cpp
// Usar String reservado para evitar reallocaciones
void sendDiscordAlert(...) {
    String payload;
    payload.reserve(512);  // ⚡ Pre-allocate
    
    // Construir JSON manualmente en lugar de ArduinoJson si es posible
    payload = "{\"embeds\":[{";
    payload += "\"title\":\"";
    payload += title;
    payload += "\",\"description\":\"";
    payload += message;
    payload += "\",\"color\":";
    payload += String(color);
    payload += "}]}";
}
```

---

## Checklist de Implementación

- [ ] **CRÍTICO**: Reducir MAX_JSON_OBJECTS a 200
- [ ] **CRÍTICO**: Reducir DEBUG_LOG_SIZE a 100
- [ ] **CRÍTICO**: Eliminar `while(true)` en línea 417
- [ ] **CRÍTICO**: Agregar watchdog software en loop
- [ ] **CRÍTICO**: Verificar heap antes de WiFiClientSecure
- [ ] **ALTO**: Auto-reconnect WiFi en loop
- [ ] **ALTO**: Endpoint /health con métricas
- [ ] **MEDIO**: Crash logs persistentes
- [ ] **MEDIO**: String.reserve() en Discord
- [ ] **BAJO**: Optimizar concatenación de strings

---

## Testing de Estabilidad

1. **Heap Monitor**: Revisar /health cada 5 min
2. **Stress Test**: Enviar 100 requests HTTP consecutivos
3. **Long Run**: Dejar corriendo 48h monitoreando crashes
4. **WiFi Stress**: Desconectar/reconectar router varias veces
5. **Discord Stress**: Enviar 20 alertas consecutivas

---

## Métricas de Éxito

- ✅ Uptime > 7 días sin restart
- ✅ Free heap siempre > 15KB
- ✅ Heap fragmentation < 30%
- ✅ WiFi auto-reconnect funcional
- ✅ Cero hard locks / watchdog resets
- ✅ Discord alerts 100% reliability

