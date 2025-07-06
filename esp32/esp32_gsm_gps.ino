#include <TinyGPSPlus.h>
#include <HardwareSerial.h>

// --- Konfigurasi Server ---
const char* serverIP = "82.29.165.106";
const int serverPort = 5000;
const char* device_id = "CHILD001";

// --- GPS Configuration ---
TinyGPSPlus gps;
HardwareSerial gpsSerial(2); // UART2: RX=16, TX=17

// --- GSM SIM800L Configuration ---
HardwareSerial gsmSerial(1); // UART1: RX=2, TX=4

// --- Status Variables ---
bool gpsSignalFound = false;
bool gsmConnected = false;
unsigned long lastGpsCheck = 0;
unsigned long lastDataSent = 0;
unsigned long lastGsmCheck = 0;

// --- Timing Configuration ---
const unsigned long GPS_CHECK_INTERVAL = 5000;    // 5 detik
const unsigned long DATA_SEND_INTERVAL = 30000;   // 30 detik
const unsigned long GSM_CHECK_INTERVAL = 60000;   // 60 detik

void setup() {
  Serial.begin(115200);
  delay(3000);
  
  Serial.println("\n=== SafeZoneX GPS Tracker dengan GSM ===");
  Serial.println("ESP32 berhasil diinisialisasi");
  
  // Inisialisasi GPS dengan HardwareSerial
  Serial.println("Menginisialisasi GPS NEO-6M...");
  gpsSerial.begin(9600, SERIAL_8N1, 16, 17); // UART2: RX=16, TX=17
  Serial.println("GPS Serial berhasil diinisialisasi");
  
  // Inisialisasi GSM dengan HardwareSerial
  Serial.println("Menginisialisasi GSM SIM800L...");
  gsmSerial.begin(9600, SERIAL_8N1, 2, 4); // UART1: RX=2, TX=4
  Serial.println("GSM Serial berhasil diinisialisasi");
  
  // Inisialisasi GSM
  initGSM();
  
  Serial.println("Sistem siap. Menunggu data GPS...");
  Serial.println("--------------------------------");
}

void initGSM() {
  Serial.println("Memulai inisialisasi GSM...");
  
  // Reset GSM module
  sendGSMCommand("AT+CFUN=1,1", 5000); // Reset module
  delay(5000);
  
  // Test AT command
  if (sendGSMCommand("AT", 1000)) {
    Serial.println("GSM module merespons");
  } else {
    Serial.println("GSM module tidak merespons!");
    return;
  }
  
  // Set SMS text mode
  sendGSMCommand("AT+CMGF=1", 1000);
  
  // Check SIM card
  if (sendGSMCommand("AT+CPIN?", 1000)) {
    Serial.println("SIM card terdeteksi");
  } else {
    Serial.println("SIM card tidak terdeteksi!");
    return;
  }
  
  // Check network registration
  if (sendGSMCommand("AT+CREG?", 1000)) {
    Serial.println("Network registration OK");
  } else {
    Serial.println("Network registration gagal!");
    return;
  }
  
  // Set APN (sesuaikan dengan provider Anda)
  sendGSMCommand("AT+CGDCONT=1,\"IP\",\"internet\"", 1000);
  
  // Attach to GPRS
  if (sendGSMCommand("AT+CGATT=1", 10000)) {
    Serial.println("GPRS attachment berhasil");
    gsmConnected = true;
  } else {
    Serial.println("GPRS attachment gagal!");
    gsmConnected = false;
  }
}

bool sendGSMCommand(String command, int timeout) {
  Serial.print("GSM Command: ");
  Serial.println(command);
  
  gsmSerial.println(command);
  delay(100);
  
  String response = "";
  unsigned long startTime = millis();
  
  while (millis() - startTime < timeout) {
    if (gsmSerial.available()) {
      char c = gsmSerial.read();
      response += c;
      
      if (response.indexOf("OK") != -1) {
        Serial.print("GSM Response: ");
        Serial.println(response);
        return true;
      } else if (response.indexOf("ERROR") != -1) {
        Serial.print("GSM Error: ");
        Serial.println(response);
        return false;
      }
    }
    delay(10);
  }
  
  Serial.println("GSM Timeout");
  return false;
}

void sendDataViaGSM(float latitude, float longitude) {
  if (!gsmConnected) {
    Serial.println("GSM tidak terhubung, skip pengiriman data");
    return;
  }
  
  Serial.println("\nMengirim data lokasi via GSM...");
  Serial.print("Latitude: "); Serial.println(latitude, 6);
  Serial.print("Longitude: "); Serial.println(longitude, 6);
  
  // Buat HTTP POST request
  String httpRequest = "POST /api/location HTTP/1.1\r\n";
  httpRequest += "Host: " + String(serverIP) + ":" + String(serverPort) + "\r\n";
  httpRequest += "Content-Type: application/json\r\n";
  httpRequest += "Content-Length: ";
  
  // Buat JSON payload
  String jsonPayload = "{";
  jsonPayload += "\"device_id\":\"" + String(device_id) + "\",";
  jsonPayload += "\"latitude\":" + String(latitude, 6) + ",";
  jsonPayload += "\"longitude\":" + String(longitude, 6);
  jsonPayload += "}";
  
  httpRequest += String(jsonPayload.length()) + "\r\n";
  httpRequest += "\r\n";
  httpRequest += jsonPayload;
  
  Serial.print("HTTP Request: ");
  Serial.println(httpRequest);
  
  // Buka koneksi TCP
  String tcpCommand = "AT+CIPSTART=\"TCP\",\"" + String(serverIP) + "\"," + String(serverPort);
  if (!sendGSMCommand(tcpCommand, 10000)) {
    Serial.println("Gagal membuka koneksi TCP");
    return;
  }
  
  // Kirim data
  String sendCommand = "AT+CIPSEND=" + String(httpRequest.length());
  if (!sendGSMCommand(sendCommand, 5000)) {
    Serial.println("Gagal mengirim data");
    return;
  }
  
  // Tunggu prompt '>'
  unsigned long startTime = millis();
  while (millis() - startTime < 5000) {
    if (gsmSerial.available()) {
      char c = gsmSerial.read();
      if (c == '>') {
        break;
      }
    }
    delay(10);
  }
  
  // Kirim HTTP request
  gsmSerial.print(httpRequest);
  
  // Tunggu response
  String response = "";
  startTime = millis();
  while (millis() - startTime < 10000) {
    if (gsmSerial.available()) {
      char c = gsmSerial.read();
      response += c;
      
      if (response.indexOf("SEND OK") != -1) {
        Serial.println("Data berhasil dikirim!");
        lastDataSent = millis();
        break;
      } else if (response.indexOf("ERROR") != -1) {
        Serial.println("Error mengirim data");
        break;
      }
    }
    delay(10);
  }
  
  // Tutup koneksi
  sendGSMCommand("AT+CIPCLOSE", 5000);
  
  Serial.print("Response: ");
  Serial.println(response);
}

void confirmConnectionViaGSM() {
  if (!gsmConnected) {
    Serial.println("GSM tidak terhubung, skip konfirmasi");
    return;
  }
  
  Serial.println("Mengkonfirmasi koneksi ke server via GSM...");
  
  // Buat HTTP POST request untuk konfirmasi
  String httpRequest = "POST /api/confirm_connection HTTP/1.1\r\n";
  httpRequest += "Host: " + String(serverIP) + ":" + String(serverPort) + "\r\n";
  httpRequest += "Content-Type: application/json\r\n";
  httpRequest += "Content-Length: ";
  
  String jsonPayload = "{\"device_id\":\"" + String(device_id) + "\"}";
  httpRequest += String(jsonPayload.length()) + "\r\n";
  httpRequest += "\r\n";
  httpRequest += jsonPayload;
  
  // Buka koneksi TCP
  String tcpCommand = "AT+CIPSTART=\"TCP\",\"" + String(serverIP) + "\"," + String(serverPort);
  if (!sendGSMCommand(tcpCommand, 10000)) {
    Serial.println("Gagal membuka koneksi TCP untuk konfirmasi");
    return;
  }
  
  // Kirim data
  String sendCommand = "AT+CIPSEND=" + String(httpRequest.length());
  if (!sendGSMCommand(sendCommand, 5000)) {
    Serial.println("Gagal mengirim data konfirmasi");
    return;
  }
  
  // Tunggu prompt '>'
  unsigned long startTime = millis();
  while (millis() - startTime < 5000) {
    if (gsmSerial.available()) {
      char c = gsmSerial.read();
      if (c == '>') {
        break;
      }
    }
    delay(10);
  }
  
  // Kirim HTTP request
  gsmSerial.print(httpRequest);
  
  // Tunggu response
  String response = "";
  startTime = millis();
  while (millis() - startTime < 10000) {
    if (gsmSerial.available()) {
      char c = gsmSerial.read();
      response += c;
      
      if (response.indexOf("SEND OK") != -1) {
        Serial.println("Konfirmasi berhasil dikirim!");
        break;
      } else if (response.indexOf("ERROR") != -1) {
        Serial.println("Error mengirim konfirmasi");
        break;
      }
    }
    delay(10);
  }
  
  // Tutup koneksi
  sendGSMCommand("AT+CIPCLOSE", 5000);
  
  Serial.print("Konfirmasi Response: ");
  Serial.println(response);
}

void printDebugInfo() {
  Serial.println("=== Status Sistem ===");
  
  // GSM Status
  Serial.print("GSM Status: ");
  Serial.println(gsmConnected ? "Terhubung" : "Tidak Terhubung");
  
  // GPS Status
  Serial.print("GPS Signal: ");
  Serial.println(gpsSignalFound ? "Ditemukan" : "Tidak Ditemukan");
  
  // GPS Satellites
  if (gps.satellites.isValid()) {
    Serial.print("GPS Satellites: ");
    Serial.println(gps.satellites.value());
  }
  
  // GPS Location
  if (gps.location.isValid()) {
    Serial.print("GPS Location: ");
    Serial.print(gps.location.lat(), 6);
    Serial.print(", ");
    Serial.println(gps.location.lng(), 6);
  }
  
  // Last Data Sent
  Serial.print("Data Terakhir Dikirim: ");
  if (lastDataSent > 0) {
    Serial.print((millis() - lastDataSent) / 1000);
    Serial.println(" detik yang lalu");
  } else {
    Serial.println("Belum ada data dikirim");
  }
  
  // Memory Info
  Serial.print("Free Heap: ");
  Serial.print(ESP.getFreeHeap());
  Serial.println(" bytes");
  
  Serial.println("===================");
}

void loop() {
  // Baca data GPS
  while (gpsSerial.available()) {
    char c = gpsSerial.read();
    if (gps.encode(c)) {
      // Data GPS berhasil di-decode
    }
  }
  
  // Update status GPS setiap 5 detik
  if (millis() - lastGpsCheck > GPS_CHECK_INTERVAL) {
    gpsSignalFound = gps.location.isValid();
    lastGpsCheck = millis();
    
    // Print debug info setiap 5 detik
    printDebugInfo();
  }
  
  // Cek koneksi GSM setiap 60 detik
  if (millis() - lastGsmCheck > GSM_CHECK_INTERVAL) {
    Serial.println("\n--- Pengecekan Koneksi GSM ---");
    
    // Test AT command untuk cek koneksi
    if (!sendGSMCommand("AT", 1000)) {
      gsmConnected = false;
      Serial.println("GSM terputus, mencoba reconnect...");
      initGSM();
    } else {
      gsmConnected = true;
    }
    
    lastGsmCheck = millis();
  }
  
  // Cek jika ada update lokasi GPS
  if (gps.location.isUpdated()) {
    float latitude = gps.location.lat();
    float longitude = gps.location.lng();
    
    Serial.println("\n*** GPS Location Updated ***");
    Serial.print("Lat: "); Serial.println(latitude, 6);
    Serial.print("Lng: "); Serial.println(longitude, 6);
    
    // Kirim data jika GSM terhubung dan sudah waktunya
    if (gsmConnected && (millis() - lastDataSent > DATA_SEND_INTERVAL)) {
      sendDataViaGSM(latitude, longitude);
    }
  }
  
  // Konfirmasi koneksi setiap 5 menit jika belum pernah dikirim
  static unsigned long lastConfirmTime = 0;
  if (millis() - lastConfirmTime > 300000) { // 5 menit
    if (gsmConnected) {
      confirmConnectionViaGSM();
    }
    lastConfirmTime = millis();
  }
  
  // Tambahan yield untuk mencegah watchdog timeout
  yield();
  delay(100);
} 