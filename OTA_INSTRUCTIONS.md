# 📡 OTA Update Instructions for GreenNanny

## Overview
GreenNanny supports **Over-The-Air (OTA)** firmware updates, eliminating the need to connect via USB cable. You can update both the **firmware** and the **filesystem** wirelessly.

---

## 🔧 Prerequisites

1. **PlatformIO IDE** installed (VS Code extension or CLI)
2. Device connected to WiFi network
3. Device accessible at its hostname or IP address
4. OTA password: `greennanny2024`

---

## 📦 Method 1: Web-Based Upload (Easiest)

### Step 1: Build Firmware Binary
```bash
# Navigate to project directory
cd c:\Users\Juan\Documents\GitHub\GreenNanny

# Build firmware (generates .bin file)
pio run
```

The firmware binary will be located at:
```
.pio/build/nodemcuv2/firmware.bin
```

### Step 2: Build Filesystem Binary
```bash
# Build filesystem image
pio run --target buildfs
```

The filesystem binary will be located at:
```
.pio/build/nodemcuv2/littlefs.bin
```

### Step 3: Upload via Web Interface

1. Open browser and go to:
   ```
   http://greennanny.local/update.html
   ```
   Or use IP address:
   ```
   http://192.168.x.x/update.html
   ```

2. Select update type:
   - **Firmware** → Upload `firmware.bin`
   - **Filesystem** → Upload `littlefs.bin`

3. Drag & drop or browse to select the .bin file

4. Click **"Start Upload"**

5. Wait for upload to complete (device will restart automatically)

6. Verify update by checking dashboard

---

## 🖥️ Method 2: Arduino IDE OTA (Advanced)

### Prerequisites
- Arduino IDE with ESP8266 board support
- Device already running OTA-enabled firmware

### Upload Process

1. In Arduino IDE, go to **Tools > Port**

2. Select the network port:
   ```
   greennanny at 192.168.x.x (ESP8266)
   ```
   *(Device must be on same network)*

3. Click **Upload** as usual

4. Enter OTA password when prompted:
   ```
   greennanny2024
   ```

5. Wait for upload to complete

---

## 🚀 Method 3: PlatformIO OTA (Command Line)

### Setup platformio.ini for OTA

Add to your `platformio.ini`:

```ini
[env:nodemcuv2_ota]
platform = espressif8266
board = nodemcuv2
framework = arduino
upload_protocol = espota
upload_port = greennanny.local  ; or IP address
upload_flags =
    --auth=greennanny2024
    --port=8266
```

### Upload Firmware
```bash
pio run -e nodemcuv2_ota --target upload
```

### Upload Filesystem
```bash
pio run -e nodemcuv2_ota --target uploadfs
```

---

## ⚠️ Important Notes

### Before Updating
- ✅ Ensure device has stable power supply
- ✅ Do NOT disconnect power during update
- ✅ Backup current configuration if needed
- ✅ Test firmware on one device before mass deployment

### During Update
- 🔴 LED will blink rapidly during upload
- 🔴 Device may become temporarily unresponsive
- 🔴 Do NOT reset or power off device

### After Update
- ✅ Device restarts automatically (10-15 seconds)
- ✅ Configuration files are preserved
- ✅ Verify functionality on dashboard
- ✅ Check serial output for errors if issues occur

---

## 🐛 Troubleshooting

### "Update Failed" or "Connection Timeout"
- Check device is powered on and connected to WiFi
- Verify you're on the same network as the device
- Try using IP address instead of hostname
- Ping device to verify connectivity: `ping greennanny.local`

### "Authentication Failed"
- Verify OTA password is: `greennanny2024`
- Check firewall isn't blocking port 8266

### "Insufficient Space"
- Current firmware may be too large
- Free up flash space by removing unused libraries
- Check flash layout in platformio.ini

### Device Won't Boot After Update
1. Connect via USB serial (115200 baud)
2. Check error messages in serial monitor
3. Reflash firmware via USB if necessary
4. Erase flash and re-upload if corrupted: `pio run --target erase`

---

## 📊 File Size Limits

**Flash Layout:** `4M3M` (4MB total, 3MB filesystem)
- **Max Firmware Size:** ~1MB
- **Max Filesystem Size:** ~3MB

Current firmware size: ~500KB
Current filesystem size: ~200KB

---

## 🔒 Security Considerations

1. **Change Default Password:**
   Edit `OTA_PASSWORD` in `src/main.cpp`:
   ```cpp
   const char* OTA_PASSWORD = "your_secure_password";
   ```

2. **Network Security:**
   - Use OTA only on trusted networks
   - Consider VPN for remote updates
   - Monitor device logs for unauthorized attempts

3. **Disable OTA in Production:**
   If not needed, comment out `setupOTA()` and recompile

---

## 🎯 Quick Reference Commands

```bash
# Build firmware
pio run

# Build filesystem
pio run --target buildfs

# Upload firmware (USB)
pio run --target upload

# Upload filesystem (USB)
pio run --target uploadfs

# Clean build
pio run --target clean

# Monitor serial output
pio device monitor

# Erase flash completely
pio run --target erase
```

---

## 📝 Version Control Best Practices

Before creating .bin files for OTA:

1. **Tag releases:**
   ```bash
   git tag -a v1.0.0 -m "OTA update with Discord alerts"
   git push origin v1.0.0
   ```

2. **Keep binaries organized:**
   ```
   releases/
   ├── v1.0.0/
   │   ├── firmware.bin
   │   └── littlefs.bin
   ├── v1.0.1/
   │   ├── firmware.bin
   │   └── littlefs.bin
   ```

3. **Document changes:**
   - Create CHANGELOG.md
   - Note breaking changes
   - List new features and fixes

---

## 🆘 Emergency Recovery

If device becomes unresponsive after OTA:

1. **Connect via USB cable**

2. **Erase flash:**
   ```bash
   pio run --target erase
   ```

3. **Upload firmware via USB:**
   ```bash
   pio run --target upload
   ```

4. **Upload filesystem via USB:**
   ```bash
   pio run --target uploadfs
   ```

5. **Reconfigure via captive portal**

---

## 📞 Support

For issues or questions:
- Check serial monitor output: `pio device monitor`
- Review PlatformIO documentation: https://docs.platformio.org
- ESP8266 Arduino Core docs: https://arduino-esp8266.readthedocs.io

---

**Last Updated:** 2024
**Firmware Version:** 3.0 (OTA-enabled)
**Compatible Boards:** ESP8266 NodeMCU V2
