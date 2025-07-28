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
const char* versionInfoUrl = "https://raw.githubusercontent.com/GlueBeee/esp32-ota/main/version.json";

Preferences prefs;
String currentVersion;
bool updatedThisBoot = false;

unsigned long lastBlink = 0;
bool ledState = false;
unsigned long lastOTA = 0;
const unsigned long otaInterval = 60000;  // 1 menit untuk debug
const int maxOTARetry = 3;

void setup() {
  Serial.begin(SERIAL_BAUD);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.println("🚀 Memulai setup...");

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
  // Heartbeat log
  if (millis() - lastBlink > 1000) {
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState);
    lastBlink = millis();
    Serial.println("🔄 Loop aktif...");
  }

  // Cek OTA
  if (millis() - lastOTA > otaInterval) {
    lastOTA = millis();
    cekFirmwareTerbaru();
  }

  // Reset flag update
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

bool isNewerVersion(const String& newVer, const String& currentVer) {
  int newParts[3] = {0}, currParts[3] = {0};
  sscanf(newVer.c_str(), "%d.%d.%d", &newParts[0], &newParts[1], &newParts[2]);
  sscanf(currentVer.c_str(), "%d.%d.%d", &currParts[0], &currParts[1], &currParts[2]);

  for (int i = 0; i < 3; i++) {
    if (newParts[i] > currParts[i]) return true;
    if (newParts[i] < currParts[i]) return false;
  }
  return false;
}

void logOTAGagal(const String& versi, const String& alasan) {
  File logFile = SPIFFS.open("/update_log.txt", FILE_APPEND);
  if (logFile) {
    logFile.printf("❌ OTA gagal versi: %s | Alasan: %s | Waktu(ms): %lu\n",
                   versi.c_str(), alasan.c_str(), millis());
    logFile.close();
  } else {
    Serial.println("⚠️ Gagal buka file log untuk OTA gagal.");
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
