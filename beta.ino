#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <Preferences.h>
#include <SPIFFS.h>

// Konfigurasi
#define LED_PIN 2
#define SERIAL_BAUD 9600

const char* ssid = "jangganggu";
const char* password = "5qarjbnd";
const char* serverEndpoint = "http://dataalamdiy.com/update?key=phMonitoringEks";  //https://dataalamdiy.com/update?key=phMonitoringEks&levelair=xx&radon6=xx
const char* versionInfoUrl = "https://raw.githubusercontent.com/GlueBeee/esp32-ota/main/version.json";

// Global
Preferences prefs;
String currentVersion;
bool updatedThisBoot = false;

String serialBuffer = "";
float radonValue = 0.0;
float depthValue = 0.0;

unsigned long lastSend = 0;
unsigned long lastOTA = 0;
const unsigned long sendInterval = 60000;   // 1 menit
const unsigned long otaInterval = 3600000;  // 1 jam
const int maxOTARetry = 3;

void setup() {
  Serial.begin(SERIAL_BAUD);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  if (!SPIFFS.begin(true)) {
    Serial.println("❌ Gagal mount SPIFFS");
  }

  prefs.begin("ota", false);
  currentVersion = prefs.getString("version", "1.0.1");
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
  //bacaSerialDariESP1();

  if (millis() - lastSend > sendInterval) {
    lastSend = millis();
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
  }
}

void bacaSerialDariESP1() {
  while (Serial.available()) {
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

bool parseSensorFrame(const String& frame, float& radon, float& depth) {
  if (!frame.startsWith("#") || !frame.endsWith("$")) {
    Serial.println("❌ Format frame tidak valid");
    return false;
  }

  int rIndex = frame.indexOf("R:");
  int dIndex = frame.indexOf("D:");

  if (rIndex == -1 || dIndex == -1) {
    Serial.println("❌ Field R: atau D: tidak ditemukan");
    return false;
  }

  int semiIndex = frame.indexOf(";", rIndex);
  if (semiIndex == -1 || semiIndex > dIndex) {
    Serial.println("❌ Format pemisah tidak sesuai");
    return false;
  }

  String radonStr = frame.substring(rIndex + 2, semiIndex);
  String depthStr = frame.substring(dIndex + 2, frame.length() - 1);  // exclude $

  radon = radonStr.toFloat();
  depth = depthStr.toFloat();

  if (radon < 0 || radon > 1000 || depth < 0 || depth > 100) {
    Serial.println("❌ Nilai tidak logis");
    return false;
  }

  return true;
}

void kirimDataKeServer(float radon, float depth) {
  float radon6 = radon;
  float levelair = depth;
  String url = String(serverEndpoint) + "&levelair=" + String(radon6, 2) + "&depth=" + String(depth, 2);

  HTTPClient http;
  http.begin(url);
  int code = http.GET();
  Serial.println("🌐 Kirim: " + url);
  Serial.printf("📡 Response: %d\n", code);
  http.end();
}

void cekFirmwareTerbaru() {
  HTTPClient http;
  http.begin(versionInfoUrl);
  int httpCode = http.GET();

  if (httpCode == 200) {
    String payload = http.getString();

    int idxFirmware = payload.indexOf("\"firmware\":\"");
    int idxUrl = payload.indexOf("\"url\":\"");

    if (idxFirmware != -1 && idxUrl != -1) {
      int startVer = idxFirmware + 11;
      int endVer = payload.indexOf("\"", startVer);
      String versiBaru = payload.substring(startVer, endVer);

      int startUrl = idxUrl + 7;
      int endUrl = payload.indexOf("\"", startUrl);
      String firmwareUrl = payload.substring(startUrl, endUrl);

      Serial.println("🔍 Versi terbaru: " + versiBaru);
      Serial.println("🌐 URL firmware: " + firmwareUrl);

      if (versiBaru != currentVersion) {
        Serial.println("🚀 Memulai OTA...");
        for (int i = 1; i <= maxOTARetry; i++) {
          Serial.printf("🔄 OTA percobaan ke-%d...\n", i);
          if (performOTA(firmwareUrl, versiBaru)) {
            Serial.println("✅ OTA berhasil pada percobaan ke-" + String(i));
            break;
          } else {
            Serial.println("⚠️ OTA gagal pada percobaan ke-" + String(i));
            delay(5000);  // jeda sebelum retry
          }

          if (i == maxOTARetry) {
            Serial.println("❌ OTA gagal setelah " + String(maxOTARetry) + " percobaan.");
          }
        }
      } else {
        Serial.println("✅ Firmware sudah versi terbaru.");
      }
    } else {
      Serial.println("⚠️ Format JSON tidak sesuai.");
    }
  } else {
    Serial.println("❌ Gagal cek versi firmware. HTTP code: " + String(httpCode));
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
    return false;
  }

  int contentLength = http.getSize();
  if (!Update.begin(contentLength)) {
    Serial.println("❌ Gagal memulai update");
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
    return false;
  }
}

void tampilkanLogUpdate() {
  File logFile = SPIFFS.open("/update_log.txt", FILE_READ);
  if (!logFile) {
    Serial.println("❌ Tidak ada log update.");
    return;
  }

  Serial.println("📜 Riwayat update:");
  while (logFile.available()) {
    Serial.write(logFile.read());
  }
  logFile.close();
}
