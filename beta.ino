#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <Preferences.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>

// Konfigurasi
#define LED_PIN 2
#define SERIAL_BAUD 9600

const char* ssid = "jangganggu";
const char* password = "5qarjbnd";
const char* serverEndpoint = "http://dataalamdiy.com/update?key=phMonitoringEks";
const char* versionInfoUrl = "https://raw.githubusercontent.com/GlueBeee/esp32-ota/main/version.json";

// Global
Preferences prefs;
String currentVersion;
bool updatedThisBoot = false;

String serialBuffer = "";
int radonValue = 0;
int depthValue = 0;

unsigned long lastBlink = 0;
unsigned long lastSend = 0;
unsigned long lastOTA = 0;

const unsigned long blinkInterval = 1000;
const unsigned long sendInterval = 60000;
const unsigned long otaInterval = 3600000;
const int maxOTARetry = 3;
bool ledState = false;

void setup() {

  Serial1.begin(9600, SERIAL_8N1, 16, 17);  // RX=16, TX=17 (bisa disesuaikan)
  Serial.begin(SERIAL_BAUD);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.println("🚀 Memulai setup...");

  if (!SPIFFS.begin(true)) {
    Serial.println("❌ Gagal mount SPIFFS");
  }

  prefs.begin("ota", false);
  currentVersion = prefs.getString("version", "1.0.6");
  updatedThisBoot = prefs.getBool("updated", false);
  prefs.end();

  Serial.println("📦 Versi firmware: " + currentVersion);

  WiFi.begin(ssid, password);
  Serial.print("🔌 Menghubungkan WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✅ WiFi terhubung!");

  tampilkanLogUpdate();
  cekFirmwareTerbaru();
}

void loop() {
  if (millis() - lastBlink > blinkInterval) {
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState);
    lastBlink = millis();
    Serial.println("🔄 Loop aktif...");
  }

  if (millis() - lastSend > sendInterval) {
    lastSend = millis();
    bacaSerialDariESP1();
    kirimDataKeServer(radonValue, depthValue);
  }

  if (millis() - lastOTA > otaInterval) {
    lastOTA = millis();
    cekFirmwareTerbaru();
  }

  if (updatedThisBoot) {
    Serial.println("✅ Firmware telah diperbarui!");
    digitalWrite(LED_PIN, HIGH);
    delay(3000);
    digitalWrite(LED_PIN, LOW);
    prefs.begin("ota", false);
    prefs.putBool("updated", false);
    prefs.end();
    updatedThisBoot = false;
  }
}

void bacaSerialDariESP1() {
  while (Serial1.available()) {
    char c = Serial.read();
    if (c == '$') {
      serialBuffer += c;
      if (parseSensorFrame(serialBuffer, radonValue, depthValue)) {
        Serial.printf("✅ Radon: %.2f | Kedalaman: %.2f\n", radonValue, depthValue);
      } else {
        Serial.println("⚠️ Frame ditolak");
      }
      serialBuffer = "";
    } else {
      serialBuffer += c;
    }
  }
}

bool parseSensorFrame(const String& frame, int& radon, int& depth) {
  if (!frame.startsWith("#") || !frame.endsWith("$")) return false;

  int rIndex = frame.indexOf("R:");
  int dIndex = frame.indexOf("D:");
  int semiIndex = frame.indexOf(";", rIndex);

  if (rIndex == -1 || dIndex == -1 || semiIndex == -1 || semiIndex > dIndex) return false;

  String radonStr = frame.substring(rIndex + 2, semiIndex);
  String depthStr = frame.substring(dIndex + 2, frame.length() - 1);

  radon = radonStr.toInt();
  depth = depthStr.toInt();

  return (radon >= 0 && radon <= 1000 && depth >= 0 && depth <= 100);
}

void kirimDataKeServer(int radon, int depth) {
  String url = String(serverEndpoint) + "&radon6=" + String(radon, 2) + "&levelair=" + String(depth, 2);

  HTTPClient http;
  http.begin(url);
  int code = http.GET();
  Serial.println("🌐 Kirim: " + url);
  Serial.printf("📡 Response: %d\n", code);
  http.end();
}

void cekFirmwareTerbaru() {
  HTTPClient http;
  Serial.println("🌐 Mengakses metadata firmware...");
  http.begin(versionInfoUrl);
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    Serial.println("📦 Payload mentah:\n" + payload);

    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
      Serial.println("❌ Parsing JSON gagal:");
      Serial.println(error.c_str());
      return;
    }

    String versiBaru = doc["firmware"];
    String firmwareUrl = doc["url"];

    if (versiBaru.length() > 0 && firmwareUrl.length() > 0) {
      Serial.println("🔍 Versi terbaru: " + versiBaru);
      Serial.println("🌐 URL firmware: " + firmwareUrl);

      if (isNewerVersion(versiBaru, currentVersion)) {
        Serial.println("📢 Versi baru tersedia! Memulai OTA...");
        bool sukses = false;
        for (int i = 0; i < maxOTARetry; i++) {
          Serial.printf("🔁 OTA percobaan ke-%d...\n", i + 1);
          if (performOTA(firmwareUrl, versiBaru)) {
            sukses = true;
            break;
          }
          delay(3000);
        }

        if (!sukses) {
          Serial.println("❌ Semua percobaan OTA gagal.");
          logOTAGagal(versiBaru, "Semua percobaan gagal");
        }
      } else {
        Serial.println("✅ Firmware sudah versi terbaru. Tidak perlu OTA.");
      }

    } else {
      Serial.println("⚠️ Versi atau URL kosong di JSON.");
    }

  } else {
    Serial.println("❌ Gagal akses metadata. HTTP code: " + String(httpCode));
  }

  http.end();
}

bool performOTA(String url, String newVersion) {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setTimeout(15000);
  http.begin(client, url);
  int httpCode = http.GET();

  if (httpCode != 200) {
    Serial.println("❌ Gagal akses firmware");
    logOTAGagal(newVersion, "HTTP code " + String(httpCode));
    return false;
  }

  int contentLength = http.getSize();
  if (!Update.begin(contentLength)) {
    Serial.println("❌ Gagal memulai update");
    logOTAGagal(newVersion, "Update.begin gagal");
    return false;
  }

  WiFiClient& stream = http.getStream();
  size_t written = Update.writeStream(stream);

  if (written == contentLength && Update.end()) {
    Serial.println("✅ OTA sukses!");
    prefs.begin("ota", false);
    prefs.putString("version", newVersion);
    prefs.putBool("updated", true);
    prefs.end();

    File logFile = SPIFFS.open("/update_log.txt", FILE_APPEND);
    if (logFile) {
      logFile.printf("Versi: %s | Waktu(ms): %lu\n", newVersion.c_str(), millis());
      logFile.close();
    }

    delay(2000);
    ESP.restart();
    return true;
  } else {
    Serial.println("❌ Gagal flash firmware");
    logOTAGagal(newVersion, "Update.writeStream gagal");
    return false;
  }
}

void tampilkanLogUpdate() {
  File logFile = SPIFFS.open("/update_log.txt", FILE_READ);
  if (!logFile) {
    Serial.println("📁 Tidak ada log update.");
    return;
  }

  Serial.println("📜 Riwayat update:");
  while (logFile.available()) {
    Serial.write(logFile.read());
  }
  logFile.close();
}

void logOTAGagal(const String& versi, const String& alasan) {
  File logFile = SPIFFS.open("/update_log.txt", FILE_APPEND);
  if (logFile) {
    logFile.printf("GAGAL Versi: %s | Alasan: %s | Waktu(ms): %lu\n", versi.c_str(), alasan.c_str(), millis());
    logFile.close();
  } else {
    Serial.println("❌ Gagal membuka file log untuk OTA gagal.");
  }
}

bool isNewerVersion(const String& newVer, const String& currentVer) {
  int newParts[3] = { 0 }, currParts[3] = { 0 };
  sscanf(newVer.c_str(), "%d.%d.%d", &newParts[0], &newParts[1], &newParts[2]);
  sscanf(currentVer.c_str(), "%d.%d.%d", &currParts[0], &currParts[1], &currParts[2]);

  for (int i = 0; i < 3; i++) {
    if (newParts[i] > currParts[i]) return true;
    if (newParts[i] < currParts[i]) return false;
  }
  return false;
}
