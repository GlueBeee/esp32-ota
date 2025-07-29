ESP32 OTA + Sensor Monitoring
=============================

Deskripsi:
----------
Proyek ini adalah sistem monitoring berbasis ESP32 yang mendukung Over-the-Air (OTA) firmware update, pengambilan data sensor, dan pengiriman data ke server. Dirancang untuk keandalan dan efisiensi memori, proyek ini tidak menggunakan fitur rollback OTA.

Fitur Utama:
------------
- OTA Update via HTTPS dari GitHub
- Versi firmware tersimpan di Preferences
- Log update dan kegagalan OTA di SPIFFS (/update_log.txt)
- Pengambilan data sensor dari ESP1 via UART
- Parsing frame sensor dengan format: #R:<radon>;D:<depth>$
- Kirim data ke server HTTP setiap 60 detik
- LED heartbeat berkedip setiap detik
- Retry OTA hingga 3 kali jika gagal

Konfigurasi Default:
--------------------
- WiFi SSID: jangganggu
- Server Endpoint: http://dataalamdiy.com/update?key=phMonitoringEks
- Firmware Metadata URL: https://raw.githubusercontent.com/GlueBeee/esp32-ota/main/version.json
- UART ESP1: RX = GPIO16, TX = GPIO17
- LED Status: GPIO2

Format Frame Sensor:
--------------------
Contoh frame:
#R:123;D:45$

Logika OTA:
-----------
1. ESP32 mengakses version.json dari GitHub.
2. Jika versi baru tersedia, firmware diunduh via HTTPS.
3. Firmware diflash menggunakan Update.writeStream().
4. Jika sukses, versi disimpan dan perangkat restart.
5. Semua aktivitas OTA dicatat di SPIFFS.

Tanpa Rollback:
---------------
Fitur rollback OTA tidak digunakan untuk menghemat memori dan menyederhanakan sistem. Firmware dianggap valid setelah update sukses dan restart.

Board yang Digunakan
====================

Jenis Board:
------------
- ESP32 DevKit v1 (DOIT ESP32 DEVKIT V1 atau kompatibel)
- Chipset: ESP32-WROOM-32
- Flash Memory: 4MB
- Interface: Micro-USB
- Power: 5V via USB atau regulator eksternal

GPIO yang Digunakan:
--------------------
- GPIO2  : LED indikator (heartbeat dan status update)
- GPIO16 : RX UART (menerima data dari ESP1/sensor)
- GPIO17 : TX UART (opsional, tergantung komunikasi dua arah)

Koneksi UART ke ESP1:
----------------------
- ESP32 GPIO16 (RX) <--- TX ESP1
- ESP32 GPIO17 (TX) ---> RX ESP1 (jika diperlukan)

Skema Partisi Flash:
---------------------
- Partition Scheme: Default 4MB with spiffs
  - 1.2MB untuk aplikasi
  - 1.5MB untuk SPIFFS (log update)

Pengaturan Arduino IDE:
------------------------
- Board: ESP32 Dev Module
- Flash Size: 4MB
- Partition Scheme: Default 4MB with spiffs
- Upload Speed: 921600 (atau 115200 jika error)
- Core Debug Level: None

Library yang Digunakan:
------------------------
- WiFi.h
- HTTPClient.h
- WiFiClientSecure.h
- Update.h
- Preferences.h
- SPIFFS.h
- ArduinoJson.h
