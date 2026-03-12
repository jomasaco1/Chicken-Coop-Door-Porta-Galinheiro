# Code Improvements and Refactoring

## Overview
This document outlines the improvements made to the Chicken Coop Controller firmware (v2.1).

## New Header Files

### 1. **config.h** - Centralized Configuration
Consolidates all magic numbers and configuration values into a single location.

**Benefits:**
- Easy to modify settings without touching code logic
- Consistent naming conventions
- Better maintainability
- Clear documentation of all constants

**Contents:**
- Hardware pin definitions
- Timing intervals
- Temperature ranges
- API endpoints
- Validation constraints
- Enums for states and modes

### 2. **relay_control.h** - Improved Relay State Machine
Replaces scattered relay functions with a robust state machine class.

**Key Improvements:**
- **Race Condition Prevention**: Only one relay action at a time
- **Safety Timeout**: Prevents relay from getting stuck (2-minute limit)
- **State Tracking**: Clear tracking of relay status
- **Emergency Stop**: Immediately stops all relay operations
- **LED Feedback**: Blink pattern indicates active relay
- **Better Logging**: Detailed state change tracking

**Usage:**
```cpp
RelayController relayController;
relayController.init();
relayController.openDoor();
relayController.update(doorOpenTime, doorCloseTime, feederUpTime, feederDownTime);
relayController.updateLED();
```

### 3. **weather_api.h** - Enhanced Weather Functions
Improves weather data fetching with retry logic and validation.

**Features:**
- **Retry Mechanism**: 3 attempts with exponential backoff
- **Dual API Support**: Falls back between Open-Meteo and OpenWeatherMap
- **Better Error Handling**: Detailed error messages
- **JSON Validation**: Comprehensive response validation
- **URL Builder Functions**: Dynamic API URL generation

**Usage:**
```cpp
WeatherHandler weather;
if (weather.fetchWeatherData(url, temp, sunrise, sunset)) {
  if (weather.validateWeatherData(temp, sunrise, sunset)) {
    // Use data
  }
}
```

### 4. **logging.h** - Centralized Logging System
Replaces scattered logMessage() calls with structured logging.

**Features:**
- **Log Levels**: DEBUG, INFO, WARNING, ERROR
- **Timestamps**: Each log entry is timestamped
- **Dual Output**: Serial and WebSocket
- **Convenient Methods**: logTemperature(), logWiFiStatus(), etc.
- **Macro Support**: LOG_INFO(), LOG_ERROR(), etc.

**Usage:**
```cpp
LOG_INFO("System started");
LOG_ERROR("Critical error occurred");
globalLogger.logTemperature(25.5);
```

### 5. **websocket_commands.h** - Command Dispatcher Pattern
Refactors 40+ if-else statements into clean command handlers.

**Benefits:**
- **Scalability**: Easy to add new commands
- **Maintainability**: Each command is a separate function
- **Type Safety**: Command table provides structure
- **Error Handling**: Consistent error responses
- **Case Insensitive**: Commands are not case-sensitive

**Command Table:**
```cpp
{"info", cmdInfo},
{"toggleModo", cmdToggleMode},
{"openDoor", cmdOpenDoor},
{"closeDoor", cmdCloseDoor},
{"feedUp", cmdFeederUp},
{"feedDown", cmdFeederDown},
{"getEstado", cmdGetStatus},
// ... and more
```

## Migration Guide

### Step 1: Add Include Files
Add these lines at the top of your main sketch:
```cpp
#include "config.h"
#include "logging.h"
#include "relay_control.h"
#include "weather_api.h"
#include "websocket_commands.h"
```

### Step 2: Initialize Systems
In `setup()`:
```cpp
void setup() {
  relayController.init();
  globalLogger.init(&ws);
  LOG_INFO("System initialized");
}
```

### Step 3: Update Loop
```cpp
void loop() {
  if (!otaInProgress) {
    relayController.update(galinheiro.tpa, galinheiro.tpf, 
                           galinheiro.tcs, galinheiro.tcd);
    relayController.updateLED();
    // ... rest of loop
  }
}
```

### Step 4: Update WebSocket Handler
Replace the large if-else chain with:
```cpp
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len, 
                           AsyncWebSocketClient *client) {
  String message = "";
  for (size_t i = 0; i < len; i++) {
    message += (char)data[i];
  }
  
  if (!parseAndExecuteCommand(message, client)) {
    client->text("error: Command failed");
  }
}
```

## Performance Improvements

| Aspect | Before | After | Benefit |
|--------|--------|-------|---------|
| Relay Race Conditions | Possible | Eliminated | Safety |
| Code Duplication | High | Low | Maintainability |
| Error Handling | Basic | Comprehensive | Reliability |
| Logging Overhead | Variable | Optimized | Performance |
| Memory Usage | Scattered | Organized | Predictable |
| Add New Features | Difficult | Easy | Scalability |

## Safety Features Added

1. **Relay Timeout Protection**: Automatically stops relay after 2 minutes
2. **Emergency Stop Command**: Immediate shutdown via WebSocket
3. **State Validation**: Prevents invalid state transitions
4. **Error States**: System tracks and reports errors
5. **Watchdog Logging**: All operations are logged

## Future Enhancements

### Planned Features (v2.2+)
- [ ] SD Card logging for historical data
- [ ] MQTT support for remote monitoring
- [ ] Advanced web dashboard with charts
- [ ] OTA update verification
- [ ] Temperature alerts and notifications
- [ ] Scheduled maintenance reminders
- [ ] Activity log export to CSV

### Potential Improvements
- [ ] Add persistent configuration storage
- [ ] Implement settings backup/restore
- [ ] Add system health monitoring
- [ ] Web-based firmware updates
- [ ] Data retention policies
- [ ] Time zone auto-detection

## Compatibility Notes

- **Backward Compatible**: Existing code can coexist with new headers
- **Gradual Migration**: Update functions one by one
- **No Breaking Changes**: All original functionality preserved
- **Enhanced Features**: New functionality is optional

## Testing Recommendations

1. **Unit Tests**: Test individual handlers in isolation
2. **Integration Tests**: Test header combinations
3. **Load Tests**: Verify performance with multiple WebSocket clients
4. **Safety Tests**: Test emergency stop and timeout mechanisms
5. **Reliability Tests**: Run extended stress tests

## Support and Contributions

For issues, suggestions, or contributions:
1. Create an issue describing the problem
2. Submit a pull request with improvements
3. Include test cases for new features
4. Update documentation accordingly

---

**Version**: 2.1  
**Author**: jomasaco  
**Last Updated**: 2026-03-12 20:15:13