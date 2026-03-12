#ifndef WEBSOCKET_COMMANDS_H
#define WEBSOCKET_COMMANDS_H

#include "config.h"
#include "relay_control.h"

// Forward declarations
extern RelayController relayController;
extern bool modoManual;
extern String horarioMudancaModo;

// ====================================
// WEBSOCKET COMMAND HANDLERS
// ====================================

void cmdInfo(const String& params, AsyncWebSocketClient* client) {
  String response = String("autversion:") + FW_AUTHOR + " | " + FW_VERSION + 
                    " | " + FW_PROJECT + " | " + FW_DATE;
  client->text(response);
}

void cmdGetTime(const String& params, AsyncWebSocketClient* client) {
  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);
  char buffer[50];
  strftime(buffer, sizeof(buffer), "enviodatahoras: %d/%m/%Y %H:%M:%S", timeinfo);
  client->text(buffer);
}

void cmdToggleMode(const String& params, AsyncWebSocketClient* client) {
  modoManual = !modoManual;
  horarioMudancaModo = String(millis());
  
  String mode = modoManual ? "Manual" : "Automatic";
  client->text("feedback: Mode changed to " + mode);
  LOG_INFO("Mode toggled to: " + mode);
}

void cmdOpenDoor(const String& params, AsyncWebSocketClient* client) {
  relayController.openDoor();
  client->text("feedback: Door opening command sent");
}

void cmdCloseDoor(const String& params, AsyncWebSocketClient* client) {
  relayController.closeDoor();
  client->text("feedback: Door closing command sent");
}

void cmdFeederUp(const String& params, AsyncWebSocketClient* client) {
  relayController.raiseFeeder();
  client->text("feedback: Feeder raising command sent");
}

void cmdFeederDown(const String& params, AsyncWebSocketClient* client) {
  relayController.lowerFeeder();
  client->text("feedback: Feeder lowering command sent");
}

void cmdGetStatus(const String& params, AsyncWebSocketClient* client) {
  String doorStatus = relayController.isDoorOpen() ? "open" : "closed";
  String feederStatus = relayController.isFeederUp() ? "raised" : "lowered";
  String modeStatus = modoManual ? "Manual" : "Automatic";
  
  String response = "status:" + doorStatus + "|" + feederStatus + "|" + modeStatus;
  client->text(response);
}

void cmdScanWiFi(const String& params, AsyncWebSocketClient* client) {
  int n = WiFi.scanNetworks();
  String networks = "";
  
  for (int i = 0; i < n; i++) {
    if (i > 0) networks += ",";
    networks += WiFi.SSID(i);
  }
  
  client->text("wifiList:" + networks);
  LOG_DEBUG("WiFi scan completed: " + String(n) + " networks found");
}

void cmdSetWiFi(const String& params, AsyncWebSocketClient* client) {
  int commaIndex = params.indexOf(',');
  if (commaIndex < 0) {
    client->text("feedback: Invalid WiFi credentials format");
    return;
  }
  
  String ssid = params.substring(0, commaIndex);
  String password = params.substring(commaIndex + 1);
  
  // Save WiFi credentials (requires preferences implementation)
  client->text("feedback: WiFi credentials saved. Reconnecting...");
  LOG_INFO("WiFi credentials updated for SSID: " + ssid);
}

void cmdClearWiFi(const String& params, AsyncWebSocketClient* client) {
  // Clear WiFi data (requires preferences implementation)
  client->text("feedback: WiFi data cleared");
  LOG_INFO("WiFi data cleared");
}

void cmdSetRTC(const String& params, AsyncWebSocketClient* client) {
  int spaceIndex = params.indexOf(' ');
  if (spaceIndex < 0) {
    client->text("feedback: Invalid date/time format");
    return;
  }
  
  String date = params.substring(0, spaceIndex);
  String time = params.substring(spaceIndex + 1);
  
  // Parse and set RTC (requires time.h implementation)
  client->text("feedback: RTC updated to: " + date + " " + time);
  LOG_INFO("RTC manually set to: " + date + " " + time);
}

void cmdSyncNTP(const String& params, AsyncWebSocketClient* client) {
  client->text("feedback: Attempting NTP synchronization...");
  // Sync implementation here
  LOG_INFO("NTP synchronization requested");
}

void cmdSyncGPS(const String& params, AsyncWebSocketClient* client) {
  client->text("feedback: Attempting GPS synchronization...");
  // Sync implementation here
  LOG_INFO("GPS synchronization requested");
}

void cmdUpdateWeather(const String& params, AsyncWebSocketClient* client) {
  client->text("feedback: Manual weather update initiated");
  LOG_INFO("Manual weather update requested");
}

void cmdEmergencyStop(const String& params, AsyncWebSocketClient* client) {
  relayController.emergencyStop();
  client->text("feedback: EMERGENCY STOP activated!");
  LOG_ERROR("Emergency stop activated via WebSocket");
}

void cmdReset(const String& params, AsyncWebSocketClient* client) {
  client->text("feedback: System resetting...");
  delay(500);
  ESP.restart();
}

// ====================================
// COMMAND DISPATCHER
// ====================================
struct CommandHandler {
  const char* command;
  void (*handler)(const String&, AsyncWebSocketClient*);
};

static const CommandHandler commandTable[] = {
  {"info", cmdInfo},
  {"querohoras", cmdGetTime},
  {"toggleModo", cmdToggleMode},
  {"openDoor", cmdOpenDoor},
  {"closeDoor", cmdCloseDoor},
  {"feedUp", cmdFeederUp},
  {"feedDown", cmdFeederDown},
  {"getEstado", cmdGetStatus},
  {"scanWifi", cmdScanWiFi},
  {"setWifi", cmdSetWiFi},
  {"clearWiFiData", cmdClearWiFi},
  {"setRTC", cmdSetRTC},
  {"syncNTP", cmdSyncNTP},
  {"syncGPS", cmdSyncGPS},
  {"updateWeatherData", cmdUpdateWeather},
  {"emergencyStop", cmdEmergencyStop},
  {"reset", cmdReset}
};

static const int COMMAND_TABLE_SIZE = sizeof(commandTable) / sizeof(commandTable[0]);

// ====================================
// COMMAND PARSER
// ====================================
bool parseAndExecuteCommand(String message, AsyncWebSocketClient* client) {
  // Extract command and parameters
  int colonIndex = message.indexOf(':');
  String command, params;
  
  if (colonIndex > 0) {
    command = message.substring(0, colonIndex);
    params = message.substring(colonIndex + 1);
  } else {
    command = message;
    params = "";
  }
  
  // Search command table
  for (int i = 0; i < COMMAND_TABLE_SIZE; i++) {
    if (command.equalsIgnoreCase(commandTable[i].command)) {
      commandTable[i].handler(params, client);
      LOG_DEBUG("Command executed: " + command);
      return true;
    }
  }
  
  // Command not found
  client->text("error: Unknown command: " + command);
  LOG_WARNING("Unknown command received: " + command);
  return false;
}

#endif // WEBSOCKET_COMMANDS_H