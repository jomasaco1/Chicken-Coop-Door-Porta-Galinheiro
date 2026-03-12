#ifndef CONFIG_H
#define CONFIG_H

// ============================================
// HARDWARE PIN DEFINITIONS
// ============================================
#define RELAY_PIN_DOOR_OPEN      25
#define RELAY_PIN_DOOR_CLOSE     26
#define RELAY_PIN_FEEDER_UP      32
#define RELAY_PIN_FEEDER_DOWN    33
#define LED_PIN                  23

// GPS Serial Pins
#define GPS_RX_PIN               16
#define GPS_TX_PIN               17
#define GPS_BAUD_RATE            9600

// ============================================
// TIMING INTERVALS (in seconds)
// ============================================
#define WEATHER_UPDATE_INTERVAL  14400    // 4 hours
#define GPS_TIMEOUT_MS           2000     // 2 seconds
#define WIFI_CONNECT_TIMEOUT     30000    // 30 seconds
#define MAX_RELAY_ACTIVE_TIME    120000   // 2 minutes
#define MANUAL_MODE_TIMEOUT      3600000  // 1 hour
#define LED_BLINK_INTERVAL       500      // 500ms blink

// ============================================
// VALIDATION RANGES
// ============================================
#define TEMP_MIN                 -20.0f
#define TEMP_MAX                 45.0f
#define TIMING_MIN               5
#define TIMING_MAX               30

// ============================================
// DEFAULT VALUES
// ============================================
#define DEFAULT_LATITUDE         49.5665
#define DEFAULT_LONGITUDE        5.8049
#define DEFAULT_CITY_ID          "2802990"    // Aubange, Belgium
#define DEFAULT_TEMP_CONFIG      7.0f
#define DEFAULT_DOOR_OPEN_TIME   15
#define DEFAULT_DOOR_CLOSE_TIME  20
#define DEFAULT_FEEDER_UP_TIME   10
#define DEFAULT_FEEDER_DOWN_TIME 10

// ============================================
// API ENDPOINTS
// ============================================
#define WEATHER_API_OPEN_METEO   "http://api.open-meteo.com/v1/forecast"
#define WEATHER_API_OPENWEATHER  "http://api.openweathermap.org/data/2.5/weather"
#define PUBLIC_IP_SERVICE        "http://ifconfig.me/ip"
#define NTP_SERVER               "pool.ntp.org"

// ============================================
// STATE ENUMS
// ============================================
enum RelayState {
  RELAY_IDLE,
  RELAY_DOOR_OPENING,
  RELAY_DOOR_CLOSING,
  RELAY_FEEDER_UP,
  RELAY_FEEDER_DOWN,
  RELAY_ERROR
};

enum LogLevel {
  LOG_DEBUG = 0,
  LOG_INFO = 1,
  LOG_WARNING = 2,
  LOG_ERROR = 3
};

enum OperatingMode {
  MODE_MANUAL = 0,
  MODE_AUTOMATIC = 1
};

// ============================================
// SYSTEM CONFIGURATION
// ============================================
#define FIRMWARE_VERSION         "2.1"
#define FIRMWARE_AUTHOR          "jomasaco"
#define AP_PASSWORD              "12345678"
#define AP_CHANNEL               6
#define AP_MAX_CONNECTIONS       4

// ============================================
// NTP/TIME CONFIGURATION
// ============================================
#define GMT_OFFSET_CALCULATION(lon) (round((lon + 7.5) / 15.0) * 3600)
#define DAYLIGHT_OFFSET          3600

// ============================================
// RETRY CONFIGURATION
// ============================================
#define MAX_RETRIES              3
#define RETRY_DELAY_MS           1000
#define RETRY_BACKOFF_MULTIPLIER 1.5

#endif // CONFIG_H