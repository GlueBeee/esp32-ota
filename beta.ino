#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>

const char* ssid = "jangganggu";
const char* password = "5qarjbnd";
const char* firmwareUrl = "https://raw.githubusercontent.com/GlueBeee/esp32-ota/main/firmware/firmware_v1.bin";
void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) delay(500);

  HTTPClient http;
  http.begin(firmwareUrl);
  int httpCode = http.GET();

  if (httpCode == 200) {
    int contentLength = http.getSize();
    WiFiClient& client = http.getStream();

    if (Update.begin(contentLength)) {
      size_t written = Update.writeStream(client);
      if (written == contentLength && Update.end()) {
        Serial.println("Update sukses! Restarting...");
        ESP.restart();
      } else {
        Serial.println("Gagal update.");
      }
    }
  } else {
    Serial.printf("HTTP Error: %d\n", httpCode);
  }
  http.end();
}

void loop() {}
