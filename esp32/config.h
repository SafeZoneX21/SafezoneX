// File konfigurasi untuk ESP32 GPS Tracker dengan GSM

// --- Konfigurasi Server ---
#define SERVER_IP "82.29.165.106"
#define SERVER_PORT 5000
#define DEVICE_ID "CHILD001"

// --- Konfigurasi Pin ---
// GPS NEO-6M
#define GPS_RX_PIN 16
#define GPS_TX_PIN 17
#define GPS_BAUD_RATE 9600

// GSM SIM800L
#define GSM_RX_PIN 2
#define GSM_TX_PIN 4
#define GSM_BAUD_RATE 9600

// --- Konfigurasi APN (sesuaikan dengan provider) ---
#define APN_NAME "internet"
#define APN_USER ""
#define APN_PASS ""

// --- Timing Configuration ---
#define GPS_CHECK_INTERVAL 5000      // 5 detik
#define DATA_SEND_INTERVAL 30000     // 30 detik
#define GSM_CHECK_INTERVAL 60000     // 60 detik
#define CONFIRM_INTERVAL 300000      // 5 menit

// --- Debug Configuration ---
#define DEBUG_MODE true
#define SERIAL_BAUD_RATE 115200