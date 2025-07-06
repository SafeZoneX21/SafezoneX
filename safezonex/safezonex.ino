// Sertakan library yang diperlukan
#include <TinyGPSPlus.h>    // Untuk parsing data GPS
#include <HardwareSerial.h> // Untuk Hardware Serial (UART)
#include <esp_sleep.h>      // Untuk fungsi tidur (tidak digunakan secara aktif di loop ini, tapi disertakan)
#include <time.h>           // Untuk mendapatkan waktu dari NTP

// --- Konfigurasi Server Flask ---
const char* serverName = "82.29.165.106"; // IP server tanpa http://
const int serverPort = 5000;              // Port server

// --- ID unik perangkat anak ---
String device_id = "CHILD001"; // ID unik untuk perangkat ini

// --- GPS: HardwareSerial (UART2) pada GPIO2 (RX) dan GPIO4 (TX) ---
// GPS_RX_PIN (ESP32 RX) terhubung ke GPS TX
// GPS_TX_PIN (ESP32 TX) terhubung ke GPS RX
static const int GPS_RX_PIN = 2; // Pin RX ESP32 untuk GPS (UART2_RX)
static const int GPS_TX_PIN = 4; // Pin TX ESP32 untuk GPS (UART2_TX)
TinyGPSPlus gps;
// Objek HardwareSerial untuk UART2 sudah ada sebagai 'Serial2'

// --- GSM: HardwareSerial (UART1) pada GPIO16 (RX) dan GPIO17 (TX) ---
// GSM_RX_PIN (ESP32 RX) terhubung ke GSM TX
// GSM_TX_PIN (ESP32 TX) terhubung ke GSM RX
static const int GSM_RX_PIN = 16; // Pin RX ESP32 untuk GSM (UART1_RX)
static const int GSM_TX_PIN = 17; // Pin TX ESP32 untuk GSM (UART1_TX)
HardwareSerial gsmSerial(1); // Gunakan UART1 untuk GSM

// --- NTP Time Config ---
const char* ntpServer = "pool.ntp.org"; // Server NTP
const long gmtOffset_sec = 25200;       // Offset waktu GMT (UTC+7 untuk WIB)
const int daylightOffset_sec = 0;       // Tidak ada daylight saving

// --- Status Flags ---
bool gpsSignalFound = false;
bool gsmReady = false;    // Modul GSM merespons perintah AT
bool simReady = false;    // Kartu SIM terdeteksi dan siap
bool gprsReady = false;   // GPRS siap untuk koneksi internet
bool isConnected = false; // Status koneksi ke server (dikonfirmasi oleh parent)
int gsmSignalStrength = -1; // Kekuatan sinyal GSM (0-31)
String lastConnectionStatus = "unknown"; // Status koneksi terakhir dari server

unsigned long lastGpsCheck = 0;
unsigned long lastDataSent = 0;
unsigned long lastGsmCheck = 0;
unsigned long lastStatusCheck = 0;

/**
 * @brief Mengatur waktu sistem secara manual.
 * @param year Tahun (misal: 2025)
 * @param month Bulan (1-12)
 * @param day Hari (1-31)
 * @param hour Jam (0-23)
 * @param minute Menit (0-59)
 * @param second Detik (0-59)
 */
void setManualTime(int year, int month, int day, int hour, int minute, int second) {
    struct tm t;
    t.tm_year = year - 1900; // Tahun sejak 1900
    t.tm_mon = month - 1;    // Bulan (0-11)
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min = minute;
    t.tm_sec = second;
    time_t timeSinceEpoch = mktime(&t); // Konversi ke detik sejak epoch
    struct timeval now = { .tv_sec = timeSinceEpoch };
    settimeofday(&now, NULL); // Atur waktu sistem
    Serial.println("Waktu manual diatur.");
}

/**
 * @brief Fungsi setup, dijalankan sekali saat boot.
 * Menginisialisasi serial, GPS, dan GSM.
 */
void setup() {
    Serial.begin(115200); // Inisialisasi Serial monitor untuk debugging
    delay(3000); // Beri waktu untuk Serial monitor inisialisasi

    Serial.println("\n=== SafeZoneX GSM Mode ===");

    // Inisialisasi HardwareSerial2 untuk GPS pada pin 2 (RX) dan 4 (TX)
    Serial2.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
    
    // Inisialisasi HardwareSerial1 untuk GSM pada pin 16 (RX) dan 17 (TX)
    gsmSerial.begin(9600, SERIAL_8N1, GSM_RX_PIN, GSM_TX_PIN);

    // Set waktu manual (karena tidak ada WiFi untuk NTP)
    setManualTime(2025, 1, 1, 0, 0, 0); // Set waktu default

    // Inisialisasi GSM dan GPRS
    initGsmGprs();

    Serial.println("Sistem siap. Menunggu data GPS...");
    Serial.println("--------------------------------");
    printDebugInfo(); // Tampilkan informasi debug awal
}

/**
 * @brief Inisialisasi modul GSM dan koneksi GPRS.
 */
void initGsmGprs() {
    Serial.println("Inisialisasi GSM dan GPRS...");
    
    // Reset modul GSM
    gsmSerial.println("AT+CFUN=1,1");
    delay(3000);
    
    // Tunggu modul siap
    while(gsmSerial.available()) gsmSerial.read();
    delay(2000);
    
    // Cek status GSM
    checkGsmStatus();
    
    if (gsmReady && simReady) {
        Serial.println("GSM dan SIM siap, mengatur GPRS...");
        
        // Set APN (sesuaikan dengan provider Anda)
        gsmSerial.println("AT+CGDCONT=1,\"IP\",\"internet\"");
        delay(1000);
        while(gsmSerial.available()) gsmSerial.read();
        
        // Aktifkan GPRS
        gsmSerial.println("AT+CGACT=1,1");
        delay(3000);
        while(gsmSerial.available()) gsmSerial.read();
        
        // Cek status GPRS
        gsmSerial.println("AT+CGATT?");
        delay(1000);
        String response = "";
        while (gsmSerial.available()) {
            char c = gsmSerial.read();
            response += c;
        }
        
        if (response.indexOf("+CGATT: 1") != -1) {
            gprsReady = true;
            Serial.println("GPRS siap!");
            
            // Setelah GPRS siap, kirim konfirmasi koneksi
            confirmConnection();
            checkConnectionStatus();
        } else {
            Serial.println("GPRS tidak siap!");
        }
    } else {
        Serial.println("GSM atau SIM tidak siap!");
    }
}

/**
 * @brief Mengirim permintaan konfirmasi koneksi ke server Flask melalui GSM.
 */
void confirmConnection() {
    if (!gprsReady) {
        Serial.println("Tidak dapat mengirim konfirmasi: GPRS tidak siap.");
        return;
    }

    Serial.println("Mengirim konfirmasi koneksi ke server...");
    
    // Bersihkan buffer
    while(gsmSerial.available()) gsmSerial.read();
    
    // Set HTTP parameter
    gsmSerial.println("AT+HTTPINIT");
    delay(1000);
    while(gsmSerial.available()) gsmSerial.read();
    
    // Set HTTP parameter
    gsmSerial.println("AT+HTTPPARA=\"CID\",1");
    delay(1000);
    while(gsmSerial.available()) gsmSerial.read();
    
    // Set URL untuk konfirmasi
    String confirmUrl = "AT+HTTPPARA=\"URL\",\"http://" + String(serverName) + ":" + String(serverPort) + "/api/confirm_connection\"";
    gsmSerial.println(confirmUrl);
    delay(1000);
    while(gsmSerial.available()) gsmSerial.read();
    
    // Set content type
    gsmSerial.println("AT+HTTPPARA=\"CONTENT\",\"application/json\"");
    delay(1000);
    while(gsmSerial.available()) gsmSerial.read();
    
    // Siapkan data JSON untuk konfirmasi
    String jsonPayload = "{\"device_id\":\"" + device_id + "\"}";
    Serial.print("Payload Konfirmasi: "); Serial.println(jsonPayload);
    
    // Set data length
    gsmSerial.println("AT+HTTPDATA=" + String(jsonPayload.length()) + ",10000");
    delay(1000);
    while(gsmSerial.available()) gsmSerial.read();
    
    // Kirim data
    gsmSerial.println(jsonPayload);
    delay(2000);
    while(gsmSerial.available()) gsmSerial.read();
    
    // Kirim POST request
    gsmSerial.println("AT+HTTPACTION=1");
    delay(5000);
    
    String response = "";
    while (gsmSerial.available()) {
        char c = gsmSerial.read();
        response += c;
    }
    
    // Cek response
    if (response.indexOf("+HTTPACTION: 1,200") != -1) {
        Serial.println("Konfirmasi berhasil dikirim!");
        
        // Baca response body
        gsmSerial.println("AT+HTTPREAD");
        delay(2000);
        String payload = "";
        while (gsmSerial.available()) {
            char c = gsmSerial.read();
            payload += c;
        }
        
        Serial.print("Response Body: "); Serial.println(payload);
        
        // Cek apakah response mengandung "success"
        if (payload.indexOf("\"status\":\"success\"") != -1) {
            isConnected = true;
            Serial.println("*** KONEKSI DITERIMA OLEH SERVER ***");
        } else {
            Serial.println("Konfirmasi server tidak berhasil.");
            Serial.println("Response: " + payload);
        }
    } else {
        Serial.println("Gagal mengirim konfirmasi!");
        Serial.println("Response: " + response);
    }
    
    // Tutup HTTP
    gsmSerial.println("AT+HTTPTERM");
    delay(1000);
    while(gsmSerial.available()) gsmSerial.read();
}

/**
 * @brief Mengecek status koneksi perangkat dari server Flask melalui GSM.
 */
void checkConnectionStatus() {
    if (!gprsReady) {
        Serial.println("Tidak dapat mengecek status: GPRS tidak siap.");
        return;
    }

    Serial.println("Mengecek status koneksi dari server...");
    
    // Bersihkan buffer
    while(gsmSerial.available()) gsmSerial.read();
    
    // Set HTTP parameter
    gsmSerial.println("AT+HTTPINIT");
    delay(1000);
    while(gsmSerial.available()) gsmSerial.read();
    
    // Set HTTP parameter
    gsmSerial.println("AT+HTTPPARA=\"CID\",1");
    delay(1000);
    while(gsmSerial.available()) gsmSerial.read();
    
    // Set URL untuk cek status
    String statusUrl = "AT+HTTPPARA=\"URL\",\"http://" + String(serverName) + ":" + String(serverPort) + "/api/check_connection_status/" + device_id + "\"";
    gsmSerial.println(statusUrl);
    delay(1000);
    while(gsmSerial.available()) gsmSerial.read();
    
    // Kirim GET request
    gsmSerial.println("AT+HTTPACTION=0");
    delay(5000);
    
    String response = "";
    while (gsmSerial.available()) {
        char c = gsmSerial.read();
        response += c;
    }
    
    // Cek response
    if (response.indexOf("+HTTPACTION: 0,200") != -1) {
        // Baca response body
        gsmSerial.println("AT+HTTPREAD");
        delay(2000);
        String payload = "";
        while (gsmSerial.available()) {
            char c = gsmSerial.read();
            payload += c;
        }
        
        Serial.print("Status Response: "); Serial.println(payload);
        
        // Parse JSON response untuk mendapatkan status koneksi
        String connectionStatus = "unknown";
        if (payload.indexOf("\"connection_status\":\"connected\"") != -1) {
            connectionStatus = "connected";
        } else if (payload.indexOf("\"connection_status\":\"disconnected\"") != -1) {
            connectionStatus = "disconnected";
        } else if (payload.indexOf("\"connection_status\":\"pending\"") != -1) {
            connectionStatus = "pending";
        } else if (payload.indexOf("\"connection_status\":\"not_found\"") != -1) {
            connectionStatus = "not_found";
        }

        // Cek apakah status berubah
        if (connectionStatus != lastConnectionStatus) {
            Serial.println("*** STATUS KONEKSI BERUBAH ***");
            if (connectionStatus == "connected") {
                Serial.println(">>> TERHUBUNG DENGAN PARENT <<<");
                isConnected = true;
            } else if (connectionStatus == "disconnected") {
                Serial.println(">>> KONEKSI DIPUTUSKAN OLEH PARENT <<<");
                isConnected = false;
            } else if (connectionStatus == "pending") {
                Serial.println(">>> MENUNGGU KONFIRMASI PARENT <<<");
                isConnected = false;
            } else if (connectionStatus == "not_found") {
                Serial.println(">>> PERANGKAT TIDAK TERDAFTAR <<<");
                isConnected = false;
            }
            lastConnectionStatus = connectionStatus; // Perbarui status terakhir
        }
    } else {
        Serial.println("Gagal mengecek status koneksi!");
        Serial.println("Response: " + response);
    }
    
    // Tutup HTTP
    gsmSerial.println("AT+HTTPTERM");
    delay(1000);
    while(gsmSerial.available()) gsmSerial.read();
}

/**
 * @brief Mendapatkan timestamp saat ini dalam format YYYY-MM-DD HH:MM:SS.
 * @return String timestamp.
 */
String getCurrentTimestamp() {
    struct tm timeinfo;
    // Coba dapatkan waktu lokal, jika gagal, kembalikan string error
    if (!getLocalTime(&timeinfo)) {
        Serial.println("Gagal mendapatkan waktu sistem.");
        return "Waktu tidak tersedia";
    }
    char timeStringBuff[50];
    // Format waktu ke string
    strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%d %H:%M:%S", &timeinfo);
    return String(timeStringBuff);
}

/**
 * @brief Mengecek status modul GSM (AT, SIM, Sinyal).
 */
void checkGsmStatus() {
    gsmReady = false;
    simReady = false;
    gsmSignalStrength = -1;

    // Pastikan buffer serial GSM bersih
    while(gsmSerial.available()) gsmSerial.read();
    delay(100);

    // Kirim AT dan cek respons OK
    gsmSerial.println("AT");
    delay(500);
    if (gsmSerial.find("OK")) {
        gsmReady = true;
    }
    while(gsmSerial.available()) gsmSerial.read(); // Bersihkan buffer

    // Cek status SIM (CPIN)
    gsmSerial.println("AT+CPIN?");
    delay(500);
    if (gsmSerial.find("READY")) {
        simReady = true;
    }
    while(gsmSerial.available()) gsmSerial.read(); // Bersihkan buffer

    // Cek kekuatan sinyal (CSQ)
    gsmSerial.println("AT+CSQ");
    delay(500);
    String response = "";
    while (gsmSerial.available()) {
        char c = gsmSerial.read();
        response += c;
    }

    int index = response.indexOf("+CSQ:");
    if (index != -1) {
        int commaIndex = response.indexOf(",", index);
        if (commaIndex != -1) {
            String rssiStr = response.substring(index + 6, commaIndex);
            gsmSignalStrength = rssiStr.toInt();
        }
    }
}

/**
 * @brief Mencetak informasi debug ke Serial Monitor.
 */
void printDebugInfo() {
    Serial.println("=== Status Sistem ===");
    Serial.print("Waktu: "); Serial.println(getCurrentTimestamp());
    Serial.print("GPS: "); Serial.println(gpsSignalFound ? "Sinyal Ditemukan" : "Sinyal Tidak Ditemukan");
    if (gps.satellites.isValid()) {
        Serial.print("Satelit: "); Serial.println(gps.satellites.value());
    }

    // Status GSM
    Serial.print("GSM Modul: "); Serial.println(gsmReady ? "Aktif" : "Tidak Aktif");
    Serial.print("SIM Card: "); Serial.println(simReady ? "Terdeteksi" : "Tidak Terdeteksi");
    Serial.print("GPRS: "); Serial.println(gprsReady ? "Siap" : "Tidak Siap");
    Serial.print("Server: "); Serial.println(isConnected ? "Terhubung" : "Tidak Terhubung");
    Serial.print("Status Koneksi: "); Serial.println(lastConnectionStatus);
    Serial.print("Sinyal GSM: ");
    if (gsmSignalStrength >= 0) {
        Serial.print(gsmSignalStrength);
        Serial.println(" (0-31)");
    } else {
        Serial.println("Tidak Diketahui");
    }

    Serial.print("Free Heap: "); Serial.println(ESP.getFreeHeap());
    Serial.println("=======================");
}

/**
 * @brief Fungsi loop, dijalankan berulang kali.
 * Membaca data GPS, mengecek status, dan mengirim data.
 */
void loop() {
    // Baca data dari HardwareSerial2 (GPS)
    while (Serial2.available()) {
        char c = Serial2.read();
        gps.encode(c); // Encode karakter NMEA
    }

    // Cek status GPS dan perbarui info debug setiap 5 detik
    if (millis() - lastGpsCheck > 5000) {
        gpsSignalFound = gps.location.isValid();
        lastGpsCheck = millis();
        printDebugInfo();
    }

    // Cek status GSM setiap 30 detik
    if (millis() - lastGsmCheck > 30000) {
        checkGsmStatus();
        lastGsmCheck = millis();
        
        // Jika GPRS tidak siap, coba inisialisasi ulang
        if (!gprsReady && gsmReady && simReady) {
            initGsmGprs();
        }
    }

    // Cek status koneksi ke server setiap 60 detik
    if (millis() - lastStatusCheck > 60000) {
        if (gprsReady) {
            checkConnectionStatus();
        }
        lastStatusCheck = millis();
    }

    // Jika lokasi GPS baru tersedia
    if (gps.location.isUpdated()) {
        float lat = gps.location.lat();
        float lng = gps.location.lng();
        Serial.println("*** Lokasi Diperbarui ***");
        Serial.print("Lat: "); Serial.println(lat, 6);
        Serial.print("Lng: "); Serial.println(lng, 6);

        // Kirim data hanya jika GPRS siap dan koneksi ke server dikonfirmasi
        if (gprsReady && isConnected) {
            sendDataViaGsm(lat, lng);
        } else {
            Serial.println("Tidak dapat mengirim data: GPRS tidak siap atau server tidak dikonfirmasi.");
        }
    }

    yield(); // Izinkan tugas background ESP32 berjalan
    delay(100); // Jeda singkat untuk stabilitas
}

/**
 * @brief Mengirim data lokasi ke server melalui GSM (HTTP POST).
 * @param latitude Latitude dari GPS.
 * @param longitude Longitude dari GPS.
 */
void sendDataViaGsm(float latitude, float longitude) {
    if (!gprsReady) {
        Serial.println("Tidak dapat mengirim data: GPRS tidak siap.");
        return;
    }

    Serial.println("\nMengirim data ke server melalui GSM...");
    
    // Bersihkan buffer
    while(gsmSerial.available()) gsmSerial.read();
    
    // Set HTTP parameter
    gsmSerial.println("AT+HTTPINIT");
    delay(1000);
    while(gsmSerial.available()) gsmSerial.read();
    
    // Set HTTP parameter
    gsmSerial.println("AT+HTTPPARA=\"CID\",1");
    delay(1000);
    while(gsmSerial.available()) gsmSerial.read();
    
    // Set URL
    String url = "AT+HTTPPARA=\"URL\",\"http://" + String(serverName) + ":" + String(serverPort) + "/api/location\"";
    gsmSerial.println(url);
    delay(1000);
    while(gsmSerial.available()) gsmSerial.read();
    
    // Set content type
    gsmSerial.println("AT+HTTPPARA=\"CONTENT\",\"application/json\"");
    delay(1000);
    while(gsmSerial.available()) gsmSerial.read();
    
    // Siapkan data JSON
    String timestamp = getCurrentTimestamp();
    String jsonPayload = "{";
    jsonPayload += "\"device_id\":\"" + device_id + "\",";
    jsonPayload += "\"latitude\":" + String(latitude, 6) + ",";
    jsonPayload += "\"longitude\":" + String(longitude, 6) + ",";
    jsonPayload += "\"timestamp\":\"" + timestamp + "\"}";
    
    Serial.print("Payload JSON: "); Serial.println(jsonPayload);
    
    // Set data length
    gsmSerial.println("AT+HTTPDATA=" + String(jsonPayload.length()) + ",10000");
    delay(1000);
    while(gsmSerial.available()) gsmSerial.read();
    
    // Kirim data
    gsmSerial.println(jsonPayload);
    delay(2000);
    while(gsmSerial.available()) gsmSerial.read();
    
    // Kirim POST request
    gsmSerial.println("AT+HTTPACTION=1");
    delay(5000);
    
    String response = "";
    while (gsmSerial.available()) {
        char c = gsmSerial.read();
        response += c;
    }
    
    // Cek response
    if (response.indexOf("+HTTPACTION: 1,200") != -1) {
        Serial.println("Data berhasil dikirim!");
        lastDataSent = millis();
        
        // Baca response body
        gsmSerial.println("AT+HTTPREAD");
        delay(2000);
        while(gsmSerial.available()) {
            Serial.write(gsmSerial.read());
        }
    } else {
        Serial.println("Gagal mengirim data!");
        Serial.println("Response: " + response);
    }
    
    // Tutup HTTP
    gsmSerial.println("AT+HTTPTERM");
    delay(1000);
    while(gsmSerial.available()) gsmSerial.read();
}
