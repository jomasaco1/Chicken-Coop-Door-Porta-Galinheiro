// Create your won APIS KEYS AND REPLACE THE URLS
// Autor: jomasaco
// Projeto: Controlo Galinheiro
// Descrição: Sistema automatizado para controle de porta e comedouro de galinheiro
// Data: Janeiro de 2025

#include <WiFi.h>
#include <Preferences.h>
#include <time.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>  ////V5
#include <HTTPClient.h>
#include <TinyGPSPlus.h>
#include <HardwareSerial.h>
#include <Update.h>

const char* Versao = "0.1";
const char* Autor = "jomasaco";
const int relayPinFeedUp = 32;  // Pino do relay para subir o comedouro
const int relayPinFeedDown = 33; // Pino do relay para descer o comedouro
const int relayPinOpen = 25;        // Pino do relay para abrir a porta
const int relayPinClose = 26;       // Pino do relay para fechar a porta
const int ledPin = 23; // Define o pino do LED

// Struct do Galinheiro
struct Galinheiro {   // 11 elementos
  float temp_api;        // Temperatura atual recebida da API (intervalo: -25 a 45 graus Celsius)
  float temp_config;     // Temperatura mínima configurada para o funcionamento do sistema (-25 a 45 graus Celsius)
  unsigned long nascer;  // Horário do nascer do sol (timestamp em segundos desde Epoch)
  unsigned long por;     // Horário do pôr do sol (timestamp em segundos desde Epoch)
  unsigned long last_update; // Timestamp da última atualização dos dados meteorológicos
  unsigned int tpa;      // Tempo para abrir a porta (em segundos, intervalo típico: 10 a 30)
  unsigned int tpf;      // Tempo para fechar a porta (em segundos, intervalo típico: 10 a 30)
  unsigned int tcs;      // Tempo para subir o comedouro (em segundos, intervalo típico: 5 a 20)
  unsigned int tcd;      // Tempo para descer o comedouro (em segundos, intervalo típico: 5 a 20)
  String wifi_ssid;      // Nome da rede Wi-Fi configurada (SSID)
  String wifi_pass;      // Senha da rede Wi-Fi configurada
};
Galinheiro galinheiro;
Preferences preferences;

// Variáveis do RTC interno
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
bool otaInProgress = false;

char* ntpServer = "pool.ntp.org";
long gmtOffset_sec = 3600;
int daylightOffset_sec = 3600;
 ///////////GPS/////////
TinyGPSPlus gps;
HardwareSerial gpsSerial(0);
bool gpsTimeAvailable = false;
time_t gpsTimestamp;
/////////Fim GPS///////////

bool portaAberta = false;           // Estado da porta
bool modoManual = true; // Modo padrão: Manual
bool comedouroSubido = false;    // Estado do comedouro
bool relayAtivoComedouro = false; // Indica se um relay do comedouro está ativo
unsigned long comedouroStartMillis = 0; // Tempo em que o relay foi acionado
bool relayAtivo = false;            // Indica se algum relay está ativo
unsigned long relayStartMillis = 0; // Tempo em que o relay foi acionado
unsigned long tempoInicialManual = 0; // Registro do tempo em que o sistema entrou em modo manual
const unsigned long limiteModoManual = 3600000; // 1 hora em milissegundos
int horaPor = 18;
int horaNascer = 6;

// Coordenadas padrão
const double defaultLatitude = 49.5677;  // Default coordenates usedas fallback
const double defaultLongitude = 5.8331;
double currentLatitude = defaultLatitude;
double currentLongitude = defaultLongitude;
// CityID padrão
const String defaultCityID = "2803073"; // Ettelbruck, Luxemburgo
String currentCityID = defaultCityID;
// Chave da API (configurável globalmente)
const char* apiKey = "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";  //openweathermap YOUR API KEY COME HERE


void initGPS() {
    gpsSerial.begin(9600, SERIAL_8N1, 3, 1); // RX=GPIO3, TX=GPIO1
    Serial.println("GPS inicializado.");
}

bool getGPSData(double &latitude, double &longitude, time_t &timestamp) {
    while (gpsSerial.available() > 0) {
        gps.encode(gpsSerial.read());
    }

    if (gps.location.isValid() && gps.date.isValid() && gps.time.isValid()) {
        currentLatitude = gps.location.lat();
        currentLongitude = gps.location.lng();

        struct tm gpsTime;
        gpsTime.tm_year = gps.date.year() - 1900;
        gpsTime.tm_mon = gps.date.month() - 1;
        gpsTime.tm_mday = gps.date.day();
        gpsTime.tm_hour = gps.time.hour();
        gpsTime.tm_min = gps.time.minute();
        gpsTime.tm_sec = gps.time.second();
        timestamp = mktime(&gpsTime);

        return true;
    }
    return false;
}

void syncRTCWithGPS() {
    time_t gpsTime;

    if (getGPSData(currentLatitude, currentLongitude, gpsTime)) {
        if (gpsTime > getBuildTimestamp()) {
            struct timeval tv = { gpsTime, 0 };
            settimeofday(&tv, nullptr);
            gpsTimeAvailable = true;
            Serial.printf("RTC sincronizado com GPS: %lld\n", gpsTime);
            Serial.printf("Coordenadas: Latitude %.6f, Longitude %.6f\n", currentLatitude, currentLongitude);
        } else {
            Serial.println("Dados do GPS inválidos para sincronização.");
        }
    } else {
        Serial.println("Sinal GPS indisponível ou insuficiente.");
    }
}

void updateCoordinates() {
    time_t gpsTimestamp;
    // Atualizar coordenadas usando GPS ou fallback para valores padrão
    if (getGPSData(currentLatitude, currentLongitude, gpsTimestamp)) {
        Serial.printf("Coordenadas atualizadas com GPS: Latitude %.6f, Longitude %.6f\n", currentLatitude, currentLongitude);
    } else {
        currentLatitude = defaultLatitude;
        currentLongitude = defaultLongitude;
        Serial.println("Falha ao obter coordenadas do GPS. Usando valores padrão.");
    }
    // Atualizar o CityID dinamicamente ou usar fallback
    if (WiFi.status() == WL_CONNECTED) {
        String cityID = getCityID(currentLatitude, currentLongitude);
        if (!cityID.isEmpty()) {
            currentCityID = cityID;
            Serial.printf("CityID atualizado dinamicamente: %s\n", currentCityID.c_str());
            return;
        }
    }
    // Fallback para CityID padrão
    currentCityID = defaultCityID;
    Serial.println("Falha ao atualizar CityID. Usando valor padrão.");
}


String getCityID(double latitude, double longitude) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Sem conexão com Wi-Fi. Não é possível obter cityID.");
        return defaultCityID; // Fallback para CityID padrão
    }

    HTTPClient http;
    char url[256];
    snprintf(url, sizeof(url),
             "http://api.openweathermap.org/data/2.5/find?lat=%.6f&lon=%.6f&cnt=1&appid=%s",
             latitude, longitude, apiKey);

    http.begin(url);
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        StaticJsonBuffer<1024> jsonBuffer; // Versão 5
        JsonObject& root = jsonBuffer.parseObject(payload);

        if (!root.success()) {
            Serial.println("Falha ao analisar resposta da API para cityID.");
            http.end();
            return defaultCityID; // Fallback
        }

        if (root["list"][0]["id"]) {
            int cityID = root["list"][0]["id"];
            Serial.printf("CityID encontrado: %d\n", cityID);
            http.end();
            return String(cityID);
        } else {
            Serial.println("CityID não encontrado no JSON.");
        }
    } else {
        Serial.printf("Erro na requisição para cityID. Código HTTP: %d\n", httpCode);
    }

    http.end();
    return defaultCityID; // Fallback para ID fixo
}

String buildWeatherAPIURL(const char* baseURL, bool useCityID = false) {
    char url[256];

    if (useCityID) {
        snprintf(url, sizeof(url),
                 "%s?id=%s&appid=%s&units=metric",
                 baseURL, currentCityID.c_str(), apiKey);
    } else {
        snprintf(url, sizeof(url),
                 "%s?latitude=%.6f&longitude=%.6f&current=temperature_2m,is_day&daily=sunrise,sunset&timeformat=unixtime&timezone=auto&forecast_days=1",
                 baseURL, currentLatitude, currentLongitude);
    }

    return String(url);
}



int extrairHora(unsigned long timestamp) {
  time_t t = timestamp;
  struct tm *timeinfo = localtime(&t);
  return timeinfo->tm_hour;
}

void logMessage(String message) {
  Serial.println(message);
}

void logMessage(int value) {
  Serial.println(String(value));
}

void logMessage(float value) {
  Serial.println(String(value, 2)); // 2 casas decimais para floats
}

// Função para converter __DATE__ e __TIME__ em timestamp Unix
unsigned long getBuildTimestamp() {
  struct tm buildTime;
  strptime(__DATE__ " " __TIME__, "%b %d %Y %H:%M:%S", &buildTime);
  return mktime(&buildTime);
}

bool isDaylightSavingTime(struct tm* timeinfo) {
  // Exemplo: Regra para Europa (último domingo de março e outubro)
  int month = timeinfo->tm_mon + 1; // tm_mon é de 0 a 11
  int day = timeinfo->tm_mday;
  int wday = timeinfo->tm_wday; // 0 = domingo, 1 = segunda, etc.

  if (month > 3 && month < 10) {
    return true; // Entre abril e setembro é horário de verão
  }

  if (month == 3) {
    // Último domingo de março
    int lastSunday = day - wday;
    return lastSunday >= 25; // Última semana de março
  }

  if (month == 10) {
    // Último domingo de outubro
    int lastSunday = day - wday;
    return lastSunday < 25; // Última semana de outubro
  }

  return false; // Fora do horário de verão
}

void adjustTimeZone() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    gmtOffset_sec = 3600; // Fuso horário padrão (UTC+1)
    daylightOffset_sec = isDaylightSavingTime(&timeinfo) ? 3600 : 0; // +1h se for horário de verão

    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

    Serial.print("Fuso horário ajustado para GMT+");
    Serial.println((gmtOffset_sec + daylightOffset_sec) / 3600);
  } else {
    Serial.println("Falha ao obter o tempo local para ajuste.");
  }
}

void handleSetRTC(String dataHora, AsyncWebSocketClient *client) {
  Serial.println("Data e Hora recebidas: " + dataHora);
  int spaceIndex = dataHora.indexOf(' ');
  String data = dataHora.substring(0, spaceIndex);
  String hora = dataHora.substring(spaceIndex + 1);

  Serial.println("Data separada: " + data);
  Serial.println("Hora separada: " + hora);


  int day = data.substring(8, 10).toInt();
  int month = data.substring(5, 7).toInt();
  int year = data.substring(0, 4).toInt();

  int hour = hora.substring(0, 2).toInt();
  int minute = hora.substring(3, 5).toInt();
  int second = hora.substring(6, 8).toInt();

  struct tm timeinfo;
  timeinfo.tm_year = year - 1900;
  timeinfo.tm_mon = month - 1;
  timeinfo.tm_mday = day;
  timeinfo.tm_hour = hour;
  timeinfo.tm_min = minute;
  timeinfo.tm_sec = second;

  time_t t = mktime(&timeinfo);
  if (t != -1) {
    struct timeval now = { .tv_sec = t };
    settimeofday(&now, nullptr);
    client->text("feedback:RTC atualizado com sucesso.");
  } else {
    client->text("feedback:Erro ao processar data e hora.");
  }
}

void printDateTime(unsigned long timestamp, AsyncWebSocketClient *client = nullptr) {
  time_t rawTime = timestamp;
  struct tm *timeInfo = localtime(&rawTime);
  if (timeInfo == nullptr) {
    return;
  }
  char buffer[50];
  strftime(buffer, sizeof(buffer), "enviodatahoras: %d/%m/%Y %H:%M:%S", timeInfo);
  if (client != nullptr) {
    client->text(buffer); // Envia data e hora ao cliente, se fornecido
  }
}

void scanAndSendNetworks(AsyncWebSocketClient *client) {
  int n = WiFi.scanNetworks(); // Escanear redes Wi-Fi disponíveis
  if (n == 0) {
    client->text("wifiList:"); // Nenhuma rede encontrada
    return;
  }
  String networks = "";
  for (int i = 0; i < n; ++i) {
    if (i > 0) {
      networks += ",";
    }
    networks += WiFi.SSID(i); // Adicionar o SSID da rede
  }
  client->text("wifiList:" + networks); // Enviar lista de redes ao cliente
}

// Função que envia os dados meteorológicos armazenados
void enviarDadosMeteorologicos(AsyncWebSocketClient *client) {
  String dados = "meteorData:" + String(galinheiro.temp_api, 1) + "," +
                 String(galinheiro.nascer) + "," +
                 String(galinheiro.por) + "," +
                 String(galinheiro.last_update) + "," +
				 String(galinheiro.temp_config, 1); // Adiciona a temperatura de trabalho
  client->text(dados);
}

void enviarAjustes(AsyncWebSocketClient *client) {
  String ajustes = "ajustes:";
  ajustes += "temp_config=" + String(galinheiro.temp_config) + ",";
  ajustes += "tpa=" + String(galinheiro.tpa) + ",";
  ajustes += "tpf=" + String(galinheiro.tpf) + ",";
  ajustes += "tcs=" + String(galinheiro.tcs) + ",";
  ajustes += "tcd=" + String(galinheiro.tcd);
  client->text(ajustes);
}

void enviarEstado(AsyncWebSocketClient *client = nullptr) {
  String estadoPorta = portaAberta ? "porta_aberta" : "porta_fechada";
  String estadoComedouro = comedouroSubido ? "comedouro_subido" : "comedouro_descido";
  String estadoModo = modoManual ? "Manual" : "Automático";

  String mensagem = "estado:" + estadoPorta + "," + estadoComedouro + "," + estadoModo;

  if (client) {
    client->text(mensagem); // Envia para um cliente específico
  } else {
    ws.textAll(mensagem); // Envia para todos os clientes
  }
}

void syncRTCWithNTP(AsyncWebSocketClient *client) {
  if (WiFi.status() != WL_CONNECTED) {
    syncRTCWithGPS(); // Fallback para GPS
    if (client) client->text("feedback:Wi-Fi não conectado. Abortando sincronização com NTP.");
    else logMessage("Wi-Fi não conectado. Abortando sincronização com NTP.");
    return;
  }

  if (client) client->text("feedback:Tentando sincronizar RTC com NTP...");
  else logMessage("Tentando sincronizar RTC com NTP...");

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  struct tm timeInfo;
  if (getLocalTime(&timeInfo)) {
    time_t now = time(nullptr);
    if (client) {
      client->text("feedback:RTC sincronizado com NTP.");
      printDateTime(now, client);
    } else {
      logMessage("RTC sincronizado com NTP.");
      printDateTime(now);
    }
  } else {
    if (client) client->text("feedback:Falha ao sincronizar com NTP.");
    else logMessage("Falha ao sincronizar com NTP.");
  }
}

void setupWiFi() {
  // Configurar modo Wi-Fi STA + AP
  WiFi.mode(WIFI_AP_STA);

  // Configurar AP
  WiFi.softAP("Galinheiro_AP", "12345678");
  logMessage("Modo AP ativo. Nome da rede: Galinheiro_AP");
  logMessage("IP do AP: " + WiFi.softAPIP().toString());

  // Configurar STA
  String ssid = galinheiro.wifi_ssid;
  String pass = galinheiro.wifi_pass;

  if (ssid.length() > 0 && pass.length() > 0) {
    WiFi.begin(ssid.c_str(), pass.c_str());
    logMessage("Tentando conectar a Wi-Fi: " + ssid);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 5) {
      delay(1000); // Espera 1 segundo entre as tentativas
      logMessage("Tentativa " + String(attempts + 1) + " falhou...");
      attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      logMessage("Conectado à Wi-Fi com sucesso!");
      logMessage("Endereço IP STA: " + WiFi.localIP().toString());
      syncRTCWithNTP(nullptr); // Sincronizar RTC com NTP sem cliente WebSocket
    } else {
      logMessage("Falha ao conectar à Wi-Fi após 5 tentativas. Mantendo apenas AP ativo.");
    }
  } else {
    logMessage("Credenciais Wi-Fi não encontradas. Mantendo apenas AP ativo.");
  }
}

void loadGalinheiroData(Galinheiro &data) {
  preferences.begin("galinheiro", true);
  data.temp_api = preferences.getFloat("temp_api", 7.0);
  data.temp_config = preferences.getFloat("temp_config", 7.0);
  data.nascer = preferences.getULong("nascer", 1733378400);
  data.por = preferences.getULong("por", 1733421600);
  data.last_update = preferences.getULong("last_update", 0);
  data.tpa = preferences.getUInt("tpa", 15);
  data.tpf = preferences.getUInt("tpf", 20);
  data.tcs = preferences.getUInt("tcs", 10);
  data.tcd = preferences.getUInt("tcd", 10);
  data.wifi_ssid = preferences.getString("wifi_ssid", "");
  data.wifi_pass = preferences.getString("wifi_pass", "");

  // Validações
  if (data.temp_api < -20.0 || data.temp_api > 45.0) data.temp_api = 7.0;
  if (data.temp_config < -20.0 || data.temp_config > 45.0) data.temp_config = 7.0;
  if (data.tpa < 10 || data.tpa > 20) data.tpa = 15;
  if (data.tpf < 10 || data.tpf > 25) data.tpf = 20;
  if (data.tcs < 5 || data.tcs > 20) data.tcs = 10;
  if (data.tcd < 5 || data.tcd > 20) data.tcd = 10;

  if (data.nascer >= data.por || (data.por - data.nascer) > 64800) { // 18h
    data.nascer = 1733378400;
    data.por = 1733421600;
  }

  preferences.end();
  logMessage("Dados do Galinheiro carregados com sucesso.");
  logMessage("Dados carregados na estrutura Galinheiro:");
  logMessage("Temp API: " + String(galinheiro.temp_api));
  logMessage("Temp Config: " + String(galinheiro.temp_config));
  logMessage("Nascer: " + String(galinheiro.nascer));
  logMessage("Por: " + String(galinheiro.por));
  logMessage("Last Update: " + String(galinheiro.last_update));
  logMessage("TPA: " + String(galinheiro.tpa));
  logMessage("TPF: " + String(galinheiro.tpf));
  logMessage("TCS: " + String(galinheiro.tcs));
  logMessage("TCD: " + String(galinheiro.tcd));
  logMessage("Wi-Fi SSID: " + galinheiro.wifi_ssid);
  logMessage("Wi-Fi Pass: " + galinheiro.wifi_pass);
}

bool validateAndSave(String key, String value) {
	logMessage("Chave: " + key + ", Valor recebido: " + value + " em validateAndSave");
    preferences.begin("galinheiro", false); // Abrir para escrita
    bool isSaved = false;

    if (key == "temp_api" || key == "temp_config") {
        float temp = value.toFloat();
        if (temp >= -20 && temp <= 45) {
            preferences.putFloat(key.c_str(), temp);
            isSaved = true;
        }
    } else if (key == "nascer" || key == "por" || key == "last_update") {
        unsigned long unixTime = strtoul(value.c_str(), NULL, 10);
        if (unixTime > 0) {
            preferences.putULong(key.c_str(), unixTime);
            isSaved = true;
            printDateTime(unixTime);
        }
    } else if (key == "tpa" || key == "tpf" || key == "tcs" || key == "tcd") {
        unsigned int val = value.toInt();
        if (val >= 5 && val <= 30) {
            preferences.putUInt(key.c_str(), val);
            isSaved = true;
        }
    } else if (key == "wifi_ssid" || key == "wifi_pass") {
        if (value.length() > 0) {
            preferences.putString(key.c_str(), value);
            isSaved = true;
        }
    }
    preferences.end(); // Fechar as preferências
    if (isSaved) {
        loadGalinheiroData(galinheiro); // Recarregar toda a estrutura
    }
    return isSaved;
}

void checkAndUpdateData(bool forceUpdate = false) {
    // Obter o timestamp atual do RTC interno
    unsigned long currentTime = getRTCTime();
    if (!isValidTimestamp(currentTime)) {
        Serial.println("Erro: Timestamp inválido obtido do RTC.");
        return;
    }

  // Verificar se já se passaram 4 horas desde a última atualização, exceto em caso de atualização forçada
    if (!forceUpdate && currentTime - galinheiro.last_update < 14400) { // 14400 segundos = 4 horas
        return;
    }

  // Verificar conexão com a internet
   if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Sem conexão com a internet. Usando GPS para atualizar dados...");

        if (getGPSData(currentLatitude, currentLongitude, gpsTimestamp)) {
            // Atualizar RTC com dados do GPS
            if (gpsTimestamp > getBuildTimestamp()) {
                struct timeval tv = { gpsTimestamp, 0 };
                settimeofday(&tv, nullptr);
                Serial.printf("RTC atualizado com GPS: %lld\n", gpsTimestamp);
                Serial.printf("Coordenadas GPS: Latitude %.6f, Longitude %.6f\n", currentLatitude, currentLongitude);
            } else {
                Serial.println("Dados do GPS inválidos. Mantendo RTC atual.");
            }
        } else {
            Serial.println("Sinal GPS indisponível ou insuficiente.");
        }

        galinheiro.last_update = currentTime;
        validateAndSave("last_update", String(currentTime));
        return;
    }
  adjustTimeZone();
  syncRTCWithNTP(nullptr); // Sincronizar RTC com NTP sem cliente WebSocket
    updateCoordinates(); // Atualiza coordenadas e cityID
	// Gerar URLs dinamicamente
    String weatherURL1 = buildWeatherAPIURL("http://api.open-meteo.com/v1/forecast", false);
    String weatherURL2 = buildWeatherAPIURL("http://api.openweathermap.org/data/2.5/weather", true);

// Debug: Imprimir URLs no console
Serial.println("URL gerada para Open-Meteo:");
Serial.println(weatherURL1);

Serial.println("URL gerada para OpenWeatherMap:");
Serial.println(weatherURL2);
  // Tentar obter dados de ambas as APIs
  float temp;
  unsigned long nascer, por;
  bool dataValida = false;

  if (fetchData(weatherURL1.c_str(), temp, nascer, por)) {
        dataValida = validateData(temp, nascer, por);
    }

    if (!dataValida && fetchData(weatherURL2.c_str(), temp, nascer, por)) {
        dataValida = validateData(temp, nascer, por);
    }

  // Se os dados forem válidos, atualizar a estrutura e salvar em memória
  if (dataValida) {
    galinheiro.temp_api = temp;
    galinheiro.nascer = nascer;
    galinheiro.por = por;
    galinheiro.last_update = currentTime;

    validateAndSave("temp_api", String(temp));
    validateAndSave("nascer", String(nascer));
    validateAndSave("por", String(por));
    validateAndSave("last_update", String(currentTime));
    horaPor = extrairHora(por);
    horaNascer = extrairHora(nascer);
    Serial.println("Dados atualizados e salvos com sucesso.");
  } else {
    // Atualizar apenas o timestamp da última verificação se os dados não forem válidos
    Serial.println("Falha ao obter dados válidos de ambas as APIs. Apenas o timestamp foi atualizado.");
    galinheiro.last_update = currentTime;
    validateAndSave("last_update", String(currentTime));
  }
}

bool fetchData(const char* url, float &temp, unsigned long &nascer, unsigned long &por) {
    HTTPClient http;
    http.begin(url);
    http.setTimeout(5000); // 5 segundos
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();

        if (strstr(url, "open-meteo.com") != NULL) {
            StaticJsonBuffer<1024> jsonBuffer;
            JsonObject& root = jsonBuffer.parseObject(payload);

            if (!root.success()) {
                Serial.println("Falha ao analisar JSON da API 1.");
                return false;
            }

            if (!root["current"]["temperature_2m"].is<float>() || 
                !root["daily"]["sunrise"][0].is<unsigned long>() || 
                !root["daily"]["sunset"][0].is<unsigned long>()) {
                Serial.println("Campos ausentes ou inválidos na API 1.");
                return false;
            }

            temp = root["current"]["temperature_2m"];
            nascer = root["daily"]["sunrise"][0];
            por = root["daily"]["sunset"][0];
        } else if (strstr(url, "openweathermap.org") != NULL) {
            StaticJsonBuffer<1024> jsonBuffer;
            JsonObject& root = jsonBuffer.parseObject(payload);

            if (!root.success()) {
                Serial.println("Falha ao analisar JSON da API 2.");
                return false;
            }
            if (!root["main"]["temp"].is<float>() || 
                !root["sys"]["sunrise"].is<unsigned long>() || 
                !root["sys"]["sunset"].is<unsigned long>()) {
                Serial.println("Campos ausentes ou inválidos na API 2.");
                return false;
            }
            temp = root["main"]["temp"];
            nascer = root["sys"]["sunrise"];
            por = root["sys"]["sunset"];
        } else {
            Serial.println("URL desconhecida.");
            return false;
        }
        return validateData(temp, nascer, por);
    }
    Serial.printf("Erro na requisição HTTP. Código: %d\n", httpCode);
    return false;
}

void setupFileUpload() {
    server.on("/upload", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", "Upload concluído.");
    }, [](AsyncWebServerRequest *request, const String &filename, size_t index, uint8_t *data, size_t len, bool final) {
        if (index == 0) {
            Serial.printf("Recebendo arquivo: %s\n", filename.c_str());
            // Abre o arquivo no SPIFFS para escrita
            if (!SPIFFS.open("/" + filename, "w")) {
                Serial.println("Erro ao abrir arquivo para escrita.");
                return;
            }
        }

        // Escreve os dados do arquivo
        File file = SPIFFS.open("/" + filename, "a");
        if (file) {
            file.write(data, len);
            file.close();
        } else {
            Serial.println("Erro ao escrever no arquivo.");
        }

        if (final) {
            Serial.printf("Upload concluído: %s\n", filename.c_str());
        }
    });
}

void listFiles() {
    Serial.println("Arquivos disponíveis no SPIFFS:");
    File root = SPIFFS.open("/");
    File file = root.openNextFile();
    while (file) {
        Serial.printf("Arquivo: %s, Tamanho: %d bytes\n", file.name(), file.size());
        file = root.openNextFile();
    }
}

void setupOTA() {
    server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request) {
        // Finaliza o upload e verifica se houve erro
        if (Update.hasError()) {
            request->send(500, "text/plain", "Atualização falhou.");
        } else {
            request->send(200, "text/plain", "Atualização bem-sucedida. Reiniciando...");
            ESP.restart();
        }
    }, [](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
        if (index == 0) {
            Serial.printf("Iniciando atualização: %s\n", filename.c_str());
            if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { // Inicia o OTA
                Update.printError(Serial);
            }
        }

        // Escreve os dados do arquivo
        if (Update.write(data, len) != len) {
            Update.printError(Serial);
        }

        if (final) { // Finaliza o upload
            if (Update.end(true)) {
                Serial.printf("Atualização concluída com sucesso: %s\n", filename.c_str());
            } else {
                Update.printError(Serial);
            }
        }
    });

    Serial.println("OTA configurado em /update");
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len, AsyncWebSocketClient *client) {
  String message = String((char *)data).substring(0, len);

if (message.startsWith("info:")) {
        String response = String("autversion:") + Autor + " | " + Versao;
        client->text(response);
    }
 else if (message.startsWith("querohoras:")) {
    time_t now = time(nullptr);
    printDateTime(now, client);
  } else if (message == "toggleModo") {
    modoManual = !modoManual; // Alternar entre Manual e Automático
    if (modoManual) {
      tempoInicialManual = millis(); // Registrar o momento em que entrou no modo manual
    } enviarEstado(client); // Enviar estado atual da porta e do comedouro
  } else if (message == "openDoor") {
    abrirPorta(); // Chamar abrir porta
    enviarEstado(client); // Enviar estado atual da porta e do comedouro
  } else if (message == "closeDoor") {
    fecharPorta(); // Chamar fechar porta
    enviarEstado(client); // Enviar estado atual da porta e do comedouro
  } else if (message == "feedUp") {
    subirComedouro();
    enviarEstado(client); // Enviar estado atual da porta e do comedouro
  } else if (message == "feedDown") {
    descerComedouro();
    enviarEstado(client); // Enviar estado atual da porta e do comedouro
  } else if (message == "getEstado") {
    enviarEstado(client); // Enviar estado atual da porta e do comedouro
  } else if (message.startsWith("ajuste:")) {
    String ajuste = message.substring(7); // Remover o prefixo "ajuste:"
    int separatorIndex = ajuste.indexOf('=');
    if (separatorIndex > 0) {
      String key = ajuste.substring(0, separatorIndex);
      String value = ajuste.substring(separatorIndex + 1);

      if (validateAndSave(key, value)) {
        client->text("Ajuste salvo: " + key + " = " + value);
      } else {
        client->text("Erro ao salvar ajuste: " + key);
      }
    } else {
      client->text("Formato inválido para ajuste: " + ajuste);
    }
  } else if (message == "getAjustes") {
    enviarAjustes(client); // Envia os ajustes atuais para o cliente
  } else if (message.startsWith("setRTC:")) {
    String dataHora = message.substring(7);
    handleSetRTC(dataHora, client);
  } else if (message == "syncNTP") {
    syncRTCWithNTP(client);
  }
  if (message == "scanWifi") {
    scanAndSendNetworks(client);
  } else if (message.startsWith("setWifi:")) {
    String credentials = message.substring(8);
    int commaIndex = credentials.indexOf(',');
    String ssid = credentials.substring(0, commaIndex);
    String password = credentials.substring(commaIndex + 1);

    // Validar e salvar credenciais Wi-Fi
    if (validateAndSave("wifi_ssid", ssid) && validateAndSave("wifi_pass", password)) {
      client->text("feedback:Credenciais Wi-Fi salvas com sucesso.");
	  setupWiFi();
    } else {
      client->text("feedback:Erro ao salvar credenciais Wi-Fi.");
    }
  } else if (message.startsWith("getMeteorData:")) {
    enviarDadosMeteorologicos(client);
  }
  else if (message.startsWith("getLocalIP")) {
    if (WiFi.status() == WL_CONNECTED) {
      client->text("localIP: " + WiFi.localIP().toString());
    } else {
      client->text("localIP: Sem Conecção á Internet.");
    }
  }
 else if (message.startsWith("updateWeatherData")) {
	checkAndUpdateData(true);
    client->text("Atualização meteorológica manual iniciada.");
	}
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      logMessage("Cliente " + String(client->id()) + " conectado.");
      break;
    case WS_EVT_DISCONNECT:
      logMessage("Cliente " + String(client->id()) + " desconectado.");
      break;
    case WS_EVT_DATA:
      handleWebSocketMessage(arg, data, len, client);  // Passa o cliente
      break;
    case WS_EVT_PONG:
      logMessage("PONG recebido do cliente " + String(client->id()));
      break;
    case WS_EVT_PING:
      logMessage("PING recebido do cliente " + String(client->id()));
      break;
    case WS_EVT_ERROR:
      logMessage("Erro no WebSocket com cliente " + String(client->id()));
      break;
  }
}

void initWebSocket() {
  ws.onEvent(onEvent);
  server.addHandler(&ws);
}

// Funções auxiliares
void configurarRelays() {
  pinMode(relayPinOpen, OUTPUT);
  pinMode(relayPinClose, OUTPUT);
  pinMode(relayPinFeedUp, OUTPUT);
  pinMode(relayPinFeedDown, OUTPUT);
  pinMode(ledPin, OUTPUT);
  digitalWrite(relayPinOpen, LOW);
  digitalWrite(relayPinClose, LOW);
  digitalWrite(relayPinFeedUp, LOW);
  digitalWrite(relayPinFeedDown, LOW);
  digitalWrite(ledPin, LOW);
}

void setup() {
  Serial.begin(9600);
  initGPS();
  unsigned long buildTimestamp = getBuildTimestamp();
  struct timeval tv = { .tv_sec = buildTimestamp, .tv_usec = 0 };
  settimeofday(&tv, nullptr);
  logMessage("RTC inicializado com o timestamp do build.");
  // Inicializar SPIFFS
  if (!SPIFFS.begin(true)) {
    logMessage("Erro ao montar o sistema de arquivos SPIFFS.");
    return;
  }

  // Carregar dados na estrutura Galinheiro
  loadGalinheiroData(galinheiro); // Passando a referência correta

  // Configurar Wi-Fi
  setupWiFi();
  if (WiFi.status() == WL_CONNECTED) {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    syncRTCWithNTP(nullptr); // Sincronizar RTC com NTP sem cliente WebSocket
  }
  // Configurar WebSocket
    initWebSocket();
    server.begin();
    setupOTA();
	setupFileUpload();
  // Configurar servidor para servir arquivos do SPIFFS
  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");
  server.serveStatic("/ajustes", SPIFFS, "/ajustes.html")
  .setDefaultFile("ajustes.html")
  .setCacheControl("no-cache, no-store, must-revalidate");
  server.serveStatic("/config", SPIFFS, "/config.html")
  .setDefaultFile("config.html")
  .setCacheControl("no-cache, no-store, must-revalidate");
server.serveStatic("/upgrade", SPIFFS, "/upgrade.html")
  .setDefaultFile("upgrade.html")
  .setCacheControl("no-cache, no-store, must-revalidate");
  // Exibir a hora atual
  logMessage("Hora atual: ");
  printDateTime(time(nullptr));
  struct tm timeInfo;
  if (getLocalTime(&timeInfo)) {
    Serial.printf("Hora local: %02d:%02d:%02d %02d/%02d/%04d\n",
                  timeInfo.tm_hour, timeInfo.tm_min, timeInfo.tm_sec,
                  timeInfo.tm_mday, timeInfo.tm_mon + 1, timeInfo.tm_year + 1900);
  } else {
    Serial.println("Falha ao obter a hora local.");
  }

  configurarRelays();

  modoManual = true;
  tempoInicialManual = millis(); // Registro do momento do reset
}

void loop() {
  ws.cleanupClients();
      if (!otaInProgress) {
  verificarPorta();     // Verificar temporização da porta
  verificarComedouro(); // Verificar temporização do comedouro
  checkAndUpdateData();
  piscarLed(); // Apenas chama a função, que usa as variáveis globais
  if (modoManual && millis() - tempoInicialManual > limiteModoManual) {
    modoManual = false;
    logMessage("Tempo de modo manual excedido. Alterando para modo automático.");
  }
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 60000) { // A cada 1 minuto
    if (!modoManual) {
      controlarPortaAutomatica();
    }
    lastCheck = millis();
  }
	} ///End Ota
}

void piscarLed() {
  static bool ledState = false;           // Estado atual do LED
  static unsigned long previousMillis = 0; // Tempo da última mudança do LED
  const unsigned long interval = 500;     // Intervalo fixo de 500ms

  if (relayAtivo || relayAtivoComedouro) { // Verifica o modo global
    unsigned long currentMillis = millis();
    if (currentMillis - previousMillis >= interval) {
      previousMillis = currentMillis;   // Atualiza o tempo da última mudança
      ledState = !ledState;             // Alterna o estado do LED
      digitalWrite(ledPin, ledState);   // Atualiza o pino do LED
    }
  } else {
    digitalWrite(ledPin, LOW); // Mantém o LED apagado no modo manual
  }
}

void interromperRelay() {
  digitalWrite(relayPinOpen, LOW);
  digitalWrite(relayPinClose, LOW);
  relayAtivo = false;
  logMessage("Ação interrompida.");
}

void verificarPorta() {
  if (relayAtivo) {
    unsigned long tempoDecorrido = millis() - relayStartMillis;
    if (portaAberta && tempoDecorrido >= galinheiro.tpf * 1000) { // Tempo para fechar
      interromperRelay();
      relayAtivo = false;
      portaAberta = false;
      logMessage("Porta fechada.");
      enviarEstado();
    } else if (!portaAberta && tempoDecorrido >= galinheiro.tpa * 1000) { // Tempo para abrir
      interromperRelay();
      relayAtivo = false;
      portaAberta = true;
      logMessage("Porta aberta.");
      enviarEstado();
    }
  }
}

void verificarComedouro() {
  if (relayAtivoComedouro) {
    unsigned long tempoDecorrido = millis() - comedouroStartMillis;
    if (comedouroSubido && tempoDecorrido >= galinheiro.tcd * 1000) { // Tempo para descer
      digitalWrite(relayPinFeedDown, LOW);
      relayAtivoComedouro = false;
      comedouroSubido = false;
      enviarEstado();
    } else if (!comedouroSubido && tempoDecorrido >= galinheiro.tcs * 1000) { // Tempo para subir
      digitalWrite(relayPinFeedUp, LOW);
      relayAtivoComedouro = false;
      comedouroSubido = true;
      enviarEstado();
    }
  }
}

void subirComedouro() {
  if (relayAtivoComedouro && modoManual) {
    logMessage("Invertendo sentido para subir comedouro...");
    digitalWrite(relayPinFeedDown, LOW); // Desliga o relé de descida
  } else if (comedouroSubido) {
    logMessage("Comedouro já está no topo.");
    return;
  } else if (relayAtivoComedouro) {
    logMessage("Ação em andamento, aguardando conclusão.");
    return;
  }
  digitalWrite(relayPinFeedDown, LOW); // Desliga o relé de descida
  digitalWrite(relayPinFeedUp, HIGH); // Liga o relé de subida
  relayAtivoComedouro = true;
  comedouroStartMillis = millis(); // Reinicia o temporizador
  logMessage("Subindo comedouro...");
}

void descerComedouro() {
  if (relayAtivoComedouro && modoManual) {
    logMessage("Invertendo sentido para descer comedouro...");
    digitalWrite(relayPinFeedUp, LOW); // Desliga o relé de subida
  } else if (!comedouroSubido) {
    logMessage("Comedouro já está embaixo.");
    return;
  } else if (relayAtivoComedouro) {
    logMessage("Ação em andamento, aguardando conclusão.");
    return;
  }
  digitalWrite(relayPinFeedUp, LOW); // Desliga o relé de subida
  digitalWrite(relayPinFeedDown, HIGH); // Liga o relé de descida
  relayAtivoComedouro = true;
  comedouroStartMillis = millis(); // Reinicia o temporizador
  logMessage("Descendo comedouro...");
}

void abrirPorta() {
  if (portaAberta) {
    logMessage("Porta já está aberta.");
    return;
  }
  if (relayAtivo) {
    interromperRelay(); // Interrompe qualquer ação em andamento
  }
  digitalWrite(relayPinClose, LOW); // Garante que o relay inverso está desligado
  digitalWrite(relayPinOpen, HIGH);
  relayStartMillis = millis();
  relayAtivo = true;
  logMessage("Abrindo porta...");
}

void fecharPorta() {
  if (!portaAberta) {
    logMessage("Porta já está fechada.");
    return;
  }
  if (relayAtivo) {
    interromperRelay(); // Interrompe qualquer ação em andamento
  }
  digitalWrite(relayPinOpen, LOW); // Garante que o relay inverso está desligado
  digitalWrite(relayPinClose, HIGH);
  relayStartMillis = millis();
  relayAtivo = true;
  logMessage("Fechando porta...");
}

void controlarPortaAutomatica() {
    time_t now = time(nullptr);
    struct tm *timeinfo = localtime(&now);
    int horaAtual = timeinfo->tm_hour;

			logMessage("Hora Atual: " + String(horaAtual));
logMessage("Hora Nascer: " + String(horaNascer));
logMessage("Hora Por: " + String(horaPor));
logMessage("Temperatura Guardada: " + String(galinheiro.temp_api));
logMessage("Temperatura Configurada: " + String(galinheiro.temp_config));

    if (horaAtual >= horaNascer && horaAtual < horaPor) {
        if (!portaAberta && galinheiro.temp_api >= galinheiro.temp_config) {
            abrirPorta();
        }
		if (comedouroSubido) {
            descerComedouro(); // Desce o comedouro
			logMessage("Enviado comando Descer Comedouro.");
        }
    } 
    else {
        if (portaAberta) {
            fecharPorta();
        }
		 if (!comedouroSubido) {
            subirComedouro(); // Sobe o comedouro
		logMessage("Enviado comando Subir Comedouro.");
        }
    }
}

bool isValidTimestamp(time_t timestamp) {
    // 1. Verificar se o timestamp é maior ao build timestamp
    if (timestamp < getBuildTimestamp()) {
        return false;
    }
    // 2. Verificar a quantidade de dígitos do timestamp
    if (timestamp < 1000000000 || timestamp > 9999999999) {
        return false;
    }
    // 3. Validar que o timestamp representa um horário plausível
    struct tm *timeinfo = gmtime(&timestamp);
    if (timeinfo == nullptr) {
        return false;
    }
    if (timeinfo->tm_mon < 0 || timeinfo->tm_mon > 11) return false;      // Mês
    if (timeinfo->tm_mday < 1 || timeinfo->tm_mday > 31) return false;    // Dia
    if (timeinfo->tm_hour < 0 || timeinfo->tm_hour > 23) return false;    // Hora
    if (timeinfo->tm_min < 0 || timeinfo->tm_min > 59) return false;      // Minuto
    if (timeinfo->tm_sec < 0 || timeinfo->tm_sec > 59) return false;      // Segundo
    return true; // Timestamp válido
}

bool validateData(float temp, unsigned long nascer, unsigned long por) {
    // Validar temperatura
    if (temp < -20 || temp > 45) return false;
    // Validar timestamps de nascer e pôr do sol
    if (!isValidTimestamp(nascer) || !isValidTimestamp(por)) {
        Serial.println("Timestamps de nascer ou pôr do sol inválidos.");
        return false;
    }
    // Verificar se nascer é anterior a pôr do sol e se a diferença não excede 18 horas
    if (nascer >= por || (por - nascer) > 64800) {
        Serial.println("Intervalo inválido entre nascer e pôr do sol.");
        return false;
    }
    return true;
}

unsigned long getRTCTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Erro ao obter hora do RTC");
    return 0;
  }
  return mktime(&timeinfo);
}
