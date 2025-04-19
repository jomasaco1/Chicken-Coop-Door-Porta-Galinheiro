# Chicken-Coop-Door-Porta-Galinheiro

An automated system to manage the door and feeder of a chicken coop using an ESP32 microcontroller. This system is designed to simplify the management of a chicken coop by operating in automatic or manual mode based on real-time meteorological data obtained via APIs. It also ensures reliability by using a GPS module to set the time in case of internet failure.

## Features
- **Automatic and Manual Modes**:
  - Automatic mode adjusts operations based on real-time weather data.
  - Manual mode allows users to control the system directly.
- **GPS Fallback**: Ensures accurate Real-Time Clock (RTC) synchronization in case of internet outages.
- **Hardware Integration**: Utilizes an ESP32 microcontroller, GPS module, and other components for seamless operation.

## Getting Started
### Prerequisites
- ESP32 microcontroller
- GPS module
- Relay modules
- Internet connection for API access
- Basic knowledge of C++ and hardware assembly

### Installation
1. Clone the repository:
   ```bash
   git clone https://github.com/jomasaco1/Chicken-Coop-Door-Porta-Galinheiro.git

![Index](images/Screenshot%202025-01-09%20at%2021-39-22%20Galinheiro%20Control.png)
![Placa ESP32](https://github.com/jomasaco1/Chicken-Coop-Door-Porta-Galinheiro/blob/main/images/ac-dc-esp32-relayx4-front.webp)
