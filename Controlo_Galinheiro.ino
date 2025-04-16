// Create your won APIS KEYS AND REPLACE THE URLS
// Autor: jomasaco
// Projeto: Controlo Galinheiro
// Descrição: Sistema automatizado para controle de porta e comedouro de galinheiro
// Data: Janeiro de 2025

#include <WiFi.h>
#include <Preferences.h>
#include <time.h>
#include <ESPAsyncWebServer.h>
#include <FS.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>  ////V7
#include <HTTPClient.h>
#include <TinyGPSPlus.h>
#include <HardwareSerial.h>
#include <Update.h>

const char* Versao = "2.0";
const char* Autor = "jomasaco";
// Coordenadas padrão
const double defaultLatitude = 49.5665;
const double defaultLongitude = 5.8049;
double currentLatitude = defaultLatitude;
double currentLongitude = defaultLongitude;
// CityID padrão
const String defaultCityID = "2802990"; // Aubange, Belgium
String currentCityID = defaultCityID;
// Chave da API (configurável globalmente)
const char* apiKey = "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";  //openweathermap YOUR API KEY COME HERE

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
//long gmtOffset_sec = 3600;
long gmtOffset_sec = round((currentLongitude + 7.5) / 15.0) * 3600;
int daylightOffset_sec = 3600;
///////////GPS/////////
TinyGPSPlus gps;
HardwareSerial gpsSerial(2);
bool gpsTimeAvailable = false;
time_t gpsTimestamp;
/////////Fim GPS///////////

String horarioMudancaPorta = "";
String horarioMudancaComedouro = "";
String horarioMudancaModo = "";

bool portaAberta = false;           // Estado da porta
bool modoManual = true; // Modo padrão: Manual
bool comedouroSubido = false;    // Estado do comedouro
bool relayAtivoComedouro = false; // Indica se um relay do comedouro está ativo
unsigned long comedouroStartMillis = 0; // Tempo em que o relay foi acionado
bool relayAtivo = false;            // Indica se algum relay está ativo
unsigned long relayStartMillis = 0; // Tempo em que o relay foi acionado
unsigned long tempoInicialManual = 0; // Registro do tempo em que o sistema entrou em modo manual
unsigned long UpTime = 0; // Registro do tempo em que o sistema inicializou
const unsigned long limiteModoManual = 3600000; // 1 hora em milissegundos
unsigned long lastSecoundUpdate = 0;
const unsigned long UpdateInterval = 1000; // Atualiza a cada 1 segundo
int horaPor = 18;
int minutoPor = 00;
int horaNascer = 6;
int minutoNascer = 00;

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

void initGPS() {
  gpsSerial.begin(9600, SERIAL_8N1, 16, 17); // RX=GPI1,9,16 TX=GPIO3,10,17
  Serial.println("GPS inicializado.");
  updateCoordinates();
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

bool syncRTCWithGPS(AsyncWebSocketClient *client = nullptr) {
  time_t gpsTime;
  if (getGPSData(currentLatitude, currentLongitude, gpsTime)) {
    // Calcular fuso horário com base na longitude
    gmtOffset_sec = round((currentLongitude + 7.5) / 15.0) * 3600;
    // Converter gpsTime para struct tm
    struct tm timeinfo;
    gmtime_r(&gpsTime, &timeinfo); // Converte gpsTime para UTC em timeinfo
    // Calcular horário de verão
    daylightOffset_sec = isDaylightSavingTime(&timeinfo) ? 3600 : 0;
    // Ajustar gpsTime para o horário local
    time_t localTime = gpsTime + gmtOffset_sec + daylightOffset_sec;
    struct timeval tv = { localTime, 0 };
    settimeofday(&tv, nullptr);
    gpsTimeAvailable = true;
    if (client) {
      client->text("feedback: RTC sincronizado com GPS: " + String(localTime));
      client->text("feedback: Fuso horário ajustado para GMT+" + String(gmtOffset_sec / 3600));
      client->text("feedback: Coordenadas: Latitude " + String(currentLatitude, 6) + ", Longitude " + String(currentLongitude, 6));
    }
    return true;
  } else {
    if (client) client->text("feedback: Sinal GPS indisponível ou insuficiente.");
  }
  return false;
}

String getCityID(double latitude, double longitude) {
  if (WiFi.status() != WL_CONNECTED) {
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
    // Usando JsonDocument corretamente
    JsonDocument doc;
    doc.to<JsonVariant>();  // Converte para JSON válido
    DeserializationError error = deserializeJson(doc, payload);
    if (error) {
      return defaultCityID; // Fallback
    }
    JsonObject root = doc.as<JsonObject>();
    if (root["list"].is<JsonArray>() && root["list"].size() > 0) {
      JsonObject firstEntry = root["list"][0];
      if (firstEntry["id"].is<int>()) {
        return String(firstEntry["id"].as<int>());
      }
    }
  }

  http.end();
  return defaultCityID; // Fallback para ID fixo
}

String getPublicIP() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin("http://ifconfig.me/ip"); // Serviço externo
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
      return http.getString();
    }
    http.end();
  }
  return "Indisponível";
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

void extrairHoraMinuto(unsigned long timestamp, int &hora, int &minuto) {
  time_t t = timestamp;
  struct tm *timeinfo = localtime(&t);
  hora = timeinfo->tm_hour;
  minuto = timeinfo->tm_min;
}

String obterHorarioAtual() {
  time_t now = time(nullptr);
  char buffer[20];
  strftime(buffer, sizeof(buffer), "%H:%M:%S", localtime(&now));
  return String(buffer);
}

void logMessage(const String &message) {
    Serial.println(message);  // Enviar para Serial (caso esteja conectada)
    sendToWebSocket(message); // Enviar para WebSocket
}

// Sobrecarga para números inteiros
void logMessage(int value) {
    logMessage(String(value));  // Converte para String e chama a versão principal
}

// Sobrecarga para números de ponto flutuante
void logMessage(float value) {
    logMessage(String(value, 2));  // Converte para String com 2 casas decimais
}

// Função para converter __DATE__ e __TIME__ em timestamp Unix
unsigned long getBuildTimestamp() {
  struct tm buildTime;
  strptime(__DATE__ " " __TIME__, "%b %d %Y %H:%M:%S", &buildTime);
  return mktime(&buildTime);
}

bool isDaylightSavingTime(struct tm* timeinfo) {
  int month = timeinfo->tm_mon + 1;
  int day = timeinfo->tm_mday;
  // Horário de verão entre abril e setembro
  if (month > 3 && month < 10) {
    return true;
  }
  if (month == 3 || month == 10) {
    // Último dia do mês (31 para março e outubro)
    int lastDay = 31;
    // Criar uma cópia da struct tm para não modificar a original
    struct tm lastDayTm = *timeinfo;
    lastDayTm.tm_mday = lastDay;
    mktime(&lastDayTm); // Normaliza a data para obter o dia da semana correto
    int lastDayWday = lastDayTm.tm_wday; // 0 = domingo
    // Correção 1: Fórmula correta para o último domingo
    int lastSunday = lastDay - lastDayWday;
    // Correção 2: Condição ajustada para outubro (<=)
    if (month == 3) {
      return day >= lastSunday; // Março: a partir do último domingo
    } else { // month == 10
      return day <= lastSunday; // Outubro: até o último domingo (inclusive)
    }
  }
  return false;
}

void handleSetRTC(String dataHora, AsyncWebSocketClient *client) {
  int spaceIndex = dataHora.indexOf(' ');
  String data = dataHora.substring(0, spaceIndex);
  String hora = dataHora.substring(spaceIndex + 1);

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
    client->text("feedback: RTC atualizado com sucesso." + dataHora);
  } else {
    client->text("feedback: Erro ao processar data e hora.");
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

void limparDadosWifi(AsyncWebSocketClient *client) {
  preferences.begin("galinheiro", false); // Abre a área de armazenamento "galinheiro" para escrita
  preferences.remove("wifi_ssid");        // Remove o SSID da rede Wi-Fi salva
  preferences.remove("wifi_pass");        // Remove a senha da rede Wi-Fi salva
  preferences.end();                      // Fecha o acesso aos dados no Preferences
  client->text("feedback: Dados Wifi Apagados.");
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
                 String(galinheiro.temp_config, 1) + "," +
                 String(millis()); // Adiciona o tempo de funcionamento em milissegundos
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

String prepararDadosGPS(AsyncWebSocketClient *client) {
  updateCoordinates();
  // Verifica a idade dos dados e se há fix
  unsigned long age = gps.location.age();
  bool isFixed = age != 0xFFFFFFFF && age < 2000; // Determina se está fixo

  // Constrói os dados do GPS
  String dados = "latitude:" + String(gps.location.lat(), 6) + ",";
  dados += "longitude:" + String(gps.location.lng(), 6) + ",";
  dados += "altitude:" + String(gps.altitude.meters(), 2) + ","; // Inclui altitude
  dados += "satellites:" + String(gps.satellites.value()) + ",";
  dados += "hdop:" + String(gps.hdop.value() / 100.0, 2) + ",";
  dados += "date:" + String(gps.date.month()) + "/" + String(gps.date.day()) + "/" + String(gps.date.year()) + ",";
  dados += "time:" + String(gps.time.hour()) + ":" + String(gps.time.minute()) + ":" + String(gps.time.second()) + ",";
  dados += "age:" + String(age) + ",";
  dados += "isFixed:" + String(isFixed ? "true" : "false");
  return dados;
}

// Função para configurar o GPS como fixo
void configurarModoFixo(AsyncWebSocketClient *client) {
  // Enviar comando para o módulo GPS
  gpsSerial.println("$PMTK225,4*2F"); // Modo fixo (stationary)
  // Responder ao cliente que a configuração foi aplicada
  client->text("GPS configurado como fixo (stationary).");
}

void restaurarValoresDeFabrica(AsyncWebSocketClient *client) {
  gpsSerial.println("$PMTK104*37"); // Restaurar valores de fábrica
  gpsSerial.println("$PMTK220,1000*1F"); // Configurar taxa de atualização para 1 Hz
  gpsSerial.println("$PMTK314,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0*28"); // Sentenças NMEA básicas
  client->text("GPS restaurado para os valores de fábrica.");
}

void enviarEstado(AsyncWebSocketClient *client = nullptr) {
  String estadoPorta = portaAberta ? "porta_aberta" : "porta_fechada";
  String estadoComedouro = comedouroSubido ? "comedouro_subido" : "comedouro_descido";
  String estadoModo = modoManual ? "Manual" : "Automático";
  String mensagem = "estado:" + estadoPorta + "|" + horarioMudancaPorta + "," +
                    estadoComedouro + "|" + horarioMudancaComedouro + "," +
                    estadoModo + "|" + horarioMudancaModo;
  if (client) {
    client->text(mensagem); // Envia para um cliente específico
  } else {
    ws.textAll(mensagem); // Envia para todos os clientes
  }
}

void sendToWebSocket(const String &message) {
    ws.textAll(message);  // Envia a mensagem para todos os clientes WebSocket conectados
}

void syncRTCWithNTP(AsyncWebSocketClient *client = nullptr) {
  if (WiFi.status() != WL_CONNECTED) {
    if (syncRTCWithGPS(client)) return; // Fallback para GPS
    if (client) client->text("feedback: Wi-Fi não conectado. Abortando sincronização com NTP.");
    return;
  }
  if (client) client->text("feedback: Tentando sincronizar RTC com NTP...");
  struct tm timeInfo;
  if (getLocalTime(&timeInfo)) {
    // Calcular o deslocamento do horário de verão antes de configurar o RTC
    daylightOffset_sec = isDaylightSavingTime(&timeInfo) ? 3600 : 0;
    gmtOffset_sec = round((currentLongitude + 7.5) / 15.0) * 3600;
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

    // Verificar os valores configurados
    if (client) {
      String feedback = "feedback: RTC sincronizado com NTP. ";
      feedback += "gmtOffset_sec: " + String(gmtOffset_sec);
      feedback += ", daylightOffset_sec: " + String(daylightOffset_sec);
      feedback += ", ntpServer: " + String(ntpServer);
      client->text(feedback);
    }

    // Enviar data e hora formatada
    time_t now = time(nullptr);
    printDateTime(now, client);
  } else {
    if (client) client->text("feedback: Falha ao sincronizar com NTP.");
  }
}

void setupWiFi() {
  // 1. Inicialização do AP com SSID único
  String apSSID = "Galinheiro_" + WiFi.macAddress().substring(12, 14) + WiFi.macAddress().substring(15, 17);
  apSSID.replace(":", "");

  // 2. Modo de operação dual (AP + STA)
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apSSID.c_str(), "12345678", 6, 0, 4);  // Canal 6, não oculto, máximo 4 conexões
  String ssid = galinheiro.wifi_ssid;
  String pass = galinheiro.wifi_pass;
  ssid.trim();
  pass.trim();

  if (!ssid.isEmpty() && !pass.isEmpty()) {
    WiFi.begin(ssid.c_str(), pass.c_str());
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < 30000) { // 30 segundos
      delay(500);
    }
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
}

bool validateAndSave(String key, String value) {
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

bool fetchData(const char* url, float &temp, unsigned long &nascer, unsigned long &por, AsyncWebSocketClient* client = nullptr) {
  HTTPClient http;
  http.begin(url);
  http.setTimeout(5000);
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();

    if (client) {
      client->text("Resposta da API:");
      client->text(payload);
    }

    // Usando JsonDocument corretamente
    JsonDocument doc;
    doc.to<JsonVariant>();  // Converte para JSON válido

    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
      if (client) client->text("Falha ao analisar JSON.");
      return false;
    }

    JsonObject root = doc.as<JsonObject>();

    if (strstr(url, "open-meteo.com") != NULL) {
      if (root["current"].is<JsonObject>() && root["daily"].is<JsonObject>() &&
          root["current"]["temperature_2m"].is<float>() &&
          root["daily"]["sunrise"][0].is<unsigned long>() &&
          root["daily"]["sunset"][0].is<unsigned long>()) {
        temp = root["current"]["temperature_2m"].as<float>();
        nascer = root["daily"]["sunrise"][0].as<unsigned long>();
        por = root["daily"]["sunset"][0].as<unsigned long>();
      } else {
        if (client) client->text("Campos ausentes ou inválidos na API Open-Meteo.");
        return false;
      }
    } else if (strstr(url, "openweathermap.org") != NULL) {
      if (root["main"].is<JsonObject>() && root["sys"].is<JsonObject>() &&
          root["main"]["temp"].is<float>() &&
          root["sys"]["sunrise"].is<unsigned long>() &&
          root["sys"]["sunset"].is<unsigned long>()) {
        temp = root["main"]["temp"].as<float>();
        nascer = root["sys"]["sunrise"].as<unsigned long>();
        por = root["sys"]["sunset"].as<unsigned long>();
      } else {
        if (client) client->text("Campos ausentes ou inválidos na API OpenWeatherMap.");
        return false;
      }
    } else {
      if (client) client->text("URL desconhecida.");
      return false;
    }

    return true;
  } else {
    if (client) {
      client->text("Erro na requisição HTTP. Código: " + String(httpCode));
    }
    return false;
  }
}

unsigned long getRTCTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Erro ao obter hora do RTC");
    return 0;
  }
  return mktime(&timeinfo);
}

void checkAndUpdateData(bool forceUpdate = false, AsyncWebSocketClient* client = nullptr) {
  // Obter o timestamp atual do RTC interno
  unsigned long currentTime = getRTCTime();
  if (!isValidTimestamp(currentTime)) {
    if (client) {
      client->text("Erro - Timestamp inválido obtido do RTC.");
    }
    return;
  }

  // Verificar se já se passaram 4 horas desde a última atualização, exceto em caso de atualização forçada
  if (!forceUpdate && currentTime - galinheiro.last_update < 14400) { // 14400 segundos = 4 horas
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    if (client) {
      client->text("Sem conexão com a internet. Tentando atualizar usando GPS...");
    }

    if (getGPSData(currentLatitude, currentLongitude, gpsTimestamp)) {
      if (gpsTimestamp > getBuildTimestamp()) {
        struct timeval tv = { gpsTimestamp, 0 };
        settimeofday(&tv, nullptr);
        if (client) {
          client->text("RTC atualizado com GPS.");
          client->text("Coordenadas GPS: Latitude " + String(currentLatitude)
                       + ", Longitude " + String(currentLongitude)
                       + ", Cidade " + currentCityID);
        }
      } else {
        if (client) {
          client->text("Dados do GPS inválidos. RTC mantido.");
        }
      }
    } else {
      if (client) {
        client->text("Sinal GPS indisponível ou insuficiente.");
      }
    }

    galinheiro.last_update = currentTime;
    validateAndSave("last_update", String(currentTime));
    return;
  }

  syncRTCWithNTP(nullptr); // Sincronizar RTC com NTP sem cliente WebSocket
  updateCoordinates(); // Atualiza coordenadas e cityID

  // Gerar URLs dinamicamente
  String weatherURL1 = buildWeatherAPIURL("http://api.open-meteo.com/v1/forecast", false);
  String weatherURL2 = buildWeatherAPIURL("http://api.openweathermap.org/data/2.5/weather", true);

  if (client) {
    client->text("URLs geradas para APIs:");
    client->text("Open-Meteo: " + weatherURL1);
    client->text("OpenWeatherMap: " + weatherURL2);
  }

  // Tentar obter dados de ambas as APIs
  float temp;
  unsigned long nascer, por;
  bool dataValida = false;

  if (client) {
    if (fetchData(weatherURL1.c_str(), temp, nascer, por, client)) {
      dataValida = validateData(temp, nascer, por);
    }

    if (!dataValida && fetchData(weatherURL2.c_str(), temp, nascer, por, client)) {
      dataValida = validateData(temp, nascer, por);
    }
  } else {
    if (fetchData(weatherURL1.c_str(), temp, nascer, por)) {
      dataValida = validateData(temp, nascer, por);
    }

    if (!dataValida && fetchData(weatherURL2.c_str(), temp, nascer, por)) {
      dataValida = validateData(temp, nascer, por);
    }
  }

  if (dataValida) {
    galinheiro.temp_api = temp;
    galinheiro.nascer = nascer;
    galinheiro.por = por;
    galinheiro.last_update = currentTime;

    validateAndSave("temp_api", String(temp));
    validateAndSave("nascer", String(nascer));
    validateAndSave("por", String(por));
    validateAndSave("last_update", String(currentTime));

   extrairHoraMinuto(por, horaPor, minutoPor);
     extrairHoraMinuto(nascer, horaNascer, minutoNascer);
    if (client) {
      client->text("Dados atualizados e salvos com sucesso.");
    }
  } else {
    if (client) {
      client->text("Falha ao obter dados válidos de ambas as APIs. Apenas o timestamp foi atualizado.");
    }
    galinheiro.last_update = currentTime;
    validateAndSave("last_update", String(currentTime));
  }
}

void setupFileUpload() {
  server.on("/upload", HTTP_POST, [](AsyncWebServerRequest * request) {
    request->send(200, "text/plain", "Upload concluído.");
  }, [](AsyncWebServerRequest * request, const String & filename, size_t index, uint8_t *data, size_t len, bool final) {
    static File file;
    static bool isFirmware = false;
    static bool validFirmware = false;

    // Resetar valores no início do upload
    if (index == 0) {
      isFirmware = false;
      validFirmware = false;
      sendToWebSocket("Recebendo arquivo: " + filename); // Enviar para o WebSocket
      // Verificar se o arquivo é firmware OTA
      if (filename.endsWith(".bin")) {
        isFirmware = true;
        otaInProgress = true; // Ativar a flag para evitar outros processos

        // Verificar se o nome começa com "Controlo_Galinheiro.ino."
        if (!filename.startsWith("Controlo_Galinheiro")) {
          sendToWebSocket("Erro: Nome inválido. Deve começar com 'Controlo_Galinheiro'");  // Enviar para o WebSocket
          otaInProgress = false; // Desativar a flag em caso de erro
          return;
        }

        // Verificar espaço disponível
        size_t freeSpace = ESP.getFreeSketchSpace();
        if (len > freeSpace) {
          sendToWebSocket("Erro: Firmware muito grande para o ESP32.");  // Enviar para o WebSocket
          otaInProgress = false; // Desativar a flag em caso de erro
          return;
        }

        // Iniciar atualização OTA
        if (!Update.begin(freeSpace)) {
          sendToWebSocket("Erro: ao inicializar a atualização OTA.");  // Enviar para o WebSocket
          otaInProgress = false; // Desativar a flag em caso de erro
          return;
        }

        validFirmware = true;
        sendToWebSocket("Firmware válido. Iniciando OTA...");  // Enviar para o WebSocket
		}
      // Se for um arquivo SPIFFS (.html, .css, .js)
      else if (filename.endsWith(".html") || filename.endsWith(".css") || filename.endsWith(".js")) {
        isFirmware = false;
        file = SPIFFS.open("/" + filename, "w");
        if (!file) {
          sendToWebSocket("Erro: ao abrir arquivo para escrita.");  // Enviar para o WebSocket
          return;
        }
      }
      // Tipo de arquivo não suportado
      else {
        sendToWebSocket("Erro: Tipo de arquivo não suportado.");  // Enviar para o WebSocket
        return;
      }
    }

    // Processamento do arquivo durante o upload
    if (isFirmware && validFirmware) {
      if (Update.write(data, len) != len) {
        sendToWebSocket("Erro: ao gravar o firmware.");  // Enviar para o WebSocket
        otaInProgress = false; // Desativar a flag em caso de erro
        return;
      }
    } else if (!isFirmware) {
      if (file) {
        file.write(data, len);
      } else {
        sendToWebSocket("Erro: ao escrever no SPIFFS.");  // Enviar para o WebSocket
        return;
      }
    }

    // Finalização do upload
    if (final) {
      if (isFirmware && validFirmware) {
        if (!Update.end(true)) {
          sendToWebSocket("Erro: ao finalizar a atualização OTA.");  // Enviar para o WebSocket
          otaInProgress = false; // Desativar a flag em caso de erro
          return;
        }
        sendToWebSocket("Atualização OTA concluída com sucesso! Reiniciando...");  // Enviar para o WebSocket
        delay(2000);
        otaInProgress = false; // Desativar a flag antes de reiniciar
        ESP.restart();
      } else if (!isFirmware) {
        file.close();
        sendToWebSocket("Arquivo salvo com sucesso no SPIFFS.");  // Enviar para o WebSocket
		}
      Serial.printf("Upload concluído: %s\n", filename.c_str());
    }
  });
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len, AsyncWebSocketClient *client) {
  AwsFrameInfo *info = (AwsFrameInfo *)arg;

  // Verifica se é uma mensagem de texto
  if (info->opcode == WS_TEXT) {
    String message = "";
    for (size_t i = 0; i < len; i++) {
      message += (char)data[i];
    }

    // Lida com mensagens específicas do cliente
    if (message.startsWith("info:")) {
      String response = String("autversion:") + Autor + " | " + Versao;
      client->text(response);
    } else if (message.startsWith("querohoras:")) {
      time_t now = time(nullptr);
      printDateTime(now, client);
    } else if (message == "toggleModo") {
      modoManual = !modoManual;
      horarioMudancaModo = obterHorarioAtual(); // Atualiza o horário da mudança
      if (modoManual) {
        tempoInicialManual = millis();
      }
      enviarEstado(client);
    } else if (message == "openDoor") {
      abrirPorta();
      enviarEstado(client);
    } else if (message == "closeDoor") {
      fecharPorta();
      enviarEstado(client);
    } else if (message == "feedUp") {
      subirComedouro();
      enviarEstado(client);
    } else if (message == "feedDown") {
      descerComedouro();
      enviarEstado(client);
    } else if (message == "getEstado") {
      enviarEstado(client);
    } else if (message.startsWith("ajuste:")) {
      String ajuste = message.substring(7);
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
      enviarAjustes(client);
    } else if (message.startsWith("setRTC:")) {
      String dataHora = message.substring(7);
      handleSetRTC(dataHora, client);
    } else if (message == "syncNTP") {
      syncRTCWithNTP(client);
    } else if (message == "syncGPS") {
      syncRTCWithGPS(client);
    } else if (message == "scanWifi") {
      scanAndSendNetworks(client);
    } else if (message.startsWith("setWifi:")) {
      String credentials = message.substring(8);
      int commaIndex = credentials.indexOf(',');
      String ssid = credentials.substring(0, commaIndex);
      String password = credentials.substring(commaIndex + 1);

      if (validateAndSave("wifi_ssid", ssid) && validateAndSave("wifi_pass", password)) {
        client->text("feedback:Credenciais Wi-Fi salvas com sucesso.");
        setupWiFi();
      } else {
        client->text("feedback:Erro ao salvar credenciais Wi-Fi.");
      }
    }
    else if (message == "clearWiFiData:") {
      limparDadosWifi(client); // Limpar dados de Wi-Fi
    }
    else if (message.startsWith("getMeteorData:")) {
      enviarDadosMeteorologicos(client);
    }
	else if (message.startsWith("getLocalIP")) {
  String statusMessage;
  String publicIP = getPublicIP();

  if (WiFi.status() == WL_CONNECTED) {
    statusMessage =
      "localIP: " + WiFi.localIP().toString() +
      " | PublicIP: " + publicIP +
      " | SSID: " + galinheiro.wifi_ssid;
  } else {
    statusMessage =
      "localIP: Sem Conexão | PublicIP: " + publicIP +
      " | SSID: " + galinheiro.wifi_ssid;
  }

  client->text(statusMessage);
}

    else if (message.startsWith("updateWeatherData")) {
      checkAndUpdateData(true, client);
      client->text("Atualização meteorológica manual iniciada.");
    } else if (message == "obterDadosGPS") {
      String dados = prepararDadosGPS(client);
      client->text(dados);
    } else if (message == "configurarFixo") {
      configurarModoFixo(client);
    } else if (message == "restaurarGPS") {
      restaurarValoresDeFabrica(client);
    }
    else if (message == "reset:") {
      ESP.restart();
    }
    else if (message.startsWith("comandoGPS:")) {
      String comando = message.substring(11);  // Remove o prefixo "comandoGPS:"
      if (comando.length() > 0) {
        gpsSerial.println(comando);
        client->text("Comando GPS enviado com sucesso");
      } else {
        client->text("Comando inválido");
      }
    }
  }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      logMessage("Cliente " + String(client->id()) + " conectado.");
      // Registra a conexão inicial
      break;
    case WS_EVT_DISCONNECT:
      logMessage("Cliente " + String(client->id()) + " desconectado.");
      // Remove o cliente do mapa ao desconectar
      break;
    case WS_EVT_DATA:
      handleWebSocketMessage(arg, data, len, client); // Processa a mensagem
      // Atualiza a última atividade
      break;
    case WS_EVT_PONG:
      logMessage("PONG recebido do cliente " + String(client->id()));
      // Atualiza a última atividade ao receber PONG
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
  Serial.begin(115200);
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
  // Tentar sincronizar com o GPS
  if (!syncRTCWithGPS(nullptr)) { // Se GPS não sincronizar
    if (WiFi.status() == WL_CONNECTED) { // Verificar se há conexão Wi-Fi
      syncRTCWithNTP(nullptr); // Sincronizar via NTP
    } else {
      logMessage("Sem sinal GPS e sem conexão Wi-Fi. RTC continuará com o timestamp inicial.");
    }
  }
  // Configurar WebSocket
  initWebSocket();
  server.begin();
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
  server.serveStatic("/gps", SPIFFS, "/gps.html");
  server.serveStatic("/debug", SPIFFS, "/debug.html")
  .setDefaultFile("debug.html");  

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
  UpTime = millis(); // Registo tempo activo
}

void loop() {
  if (!otaInProgress) {
    verificarPorta();     // Verificar temporização da porta
    verificarComedouro(); // Verificar temporização do comedouro
    piscarLed(); // Apenas chama a função, que usa as variáveis globais
    if (modoManual && millis() - tempoInicialManual > limiteModoManual) {
      modoManual = false;

      horarioMudancaModo = obterHorarioAtual(); // Atualiza o horário da mudança
    }

    if (millis() - lastSecoundUpdate >= UpdateInterval) {
      updateCoordinates();
      checkAndUpdateData();
      lastSecoundUpdate = millis();
    }
    static unsigned long lastCheck = 0;
    if (millis() - lastCheck > 60000) { // A cada 1 minuto
      if (!modoManual) {
        controlarPortaAutomatica();
        if (WiFi.status() != WL_CONNECTED) {
          WiFi.reconnect();
        }
      }
      lastCheck = millis();
    }
  } ///End Ota
  ws.cleanupClients();
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
}

void verificarPorta() {
  if (relayAtivo) {
    unsigned long tempoDecorrido = millis() - relayStartMillis;
    if (portaAberta && tempoDecorrido >= galinheiro.tpf * 1000) { // Tempo para fechar
      interromperRelay();
      relayAtivo = false;
      portaAberta = false;
      horarioMudancaPorta = obterHorarioAtual(); // Atualiza o horário da mudança
      enviarEstado();
    } else if (!portaAberta && tempoDecorrido >= galinheiro.tpa * 1000) { // Tempo para abrir
      interromperRelay();
      relayAtivo = false;
      portaAberta = true;
      horarioMudancaPorta = obterHorarioAtual(); // Atualiza o horário da mudança
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
      horarioMudancaComedouro = obterHorarioAtual(); // Atualiza o horário da mudança
      enviarEstado();
    } else if (!comedouroSubido && tempoDecorrido >= galinheiro.tcs * 1000) { // Tempo para subir
      digitalWrite(relayPinFeedUp, LOW);
      relayAtivoComedouro = false;
      comedouroSubido = true;
      horarioMudancaComedouro = obterHorarioAtual(); // Atualiza o horário da mudança
      enviarEstado();
    }
  }
}

void subirComedouro() {
  if (relayAtivoComedouro && modoManual) {
    digitalWrite(relayPinFeedDown, LOW); // Desliga o relé de descida
  } else if (comedouroSubido) {
    return;
  } else if (relayAtivoComedouro) {
    return;
  }
  digitalWrite(relayPinFeedDown, LOW); // Desliga o relé de descida
  digitalWrite(relayPinFeedUp, HIGH); // Liga o relé de subida
  relayAtivoComedouro = true;
  comedouroStartMillis = millis(); // Reinicia o temporizador
}

void descerComedouro() {
  if (relayAtivoComedouro && modoManual) {
    digitalWrite(relayPinFeedUp, LOW); // Desliga o relé de subida
  } else if (!comedouroSubido) {
    return;
  } else if (relayAtivoComedouro) {
    return;
  }
  digitalWrite(relayPinFeedUp, LOW); // Desliga o relé de subida
  digitalWrite(relayPinFeedDown, HIGH); // Liga o relé de descida
  relayAtivoComedouro = true;
  comedouroStartMillis = millis(); // Reinicia o temporizador
}

void abrirPorta() {
  if (portaAberta) {
    return;
  }
  if (relayAtivo) {
    interromperRelay(); // Interrompe qualquer ação em andamento
  }
  digitalWrite(relayPinClose, LOW); // Garante que o relay inverso está desligado
  digitalWrite(relayPinOpen, HIGH);
  relayStartMillis = millis();
  relayAtivo = true;
}

void fecharPorta() {
  if (!portaAberta) {
    return;
  }
  if (relayAtivo) {
    interromperRelay(); // Interrompe qualquer ação em andamento
  }
  digitalWrite(relayPinOpen, LOW); // Garante que o relay inverso está desligado
  digitalWrite(relayPinClose, HIGH);
  relayStartMillis = millis();
  relayAtivo = true;
}

void controlarPortaAutomatica() {
  // Obtemos o timestamp atual
  time_t now = time(nullptr);
  struct tm *timeinfo = localtime(&now);
  int horaAtual = timeinfo->tm_hour;
  int minutoAtual = timeinfo->tm_min;

  // Calcula a hora e minuto do fechamento (1 hora após o pôr do sol)
  int horaFechar = (horaPor + 1) % 24;
  int minutoFechar = minutoPor;

  // Comparação entre hora:minuto atuais e hora:minuto de fechamento
  bool horarioDentroIntervalo = (horaAtual > horaNascer || (horaAtual == horaNascer && minutoAtual >= minutoNascer)) &&
                                (horaAtual < horaFechar || (horaAtual == horaFechar && minutoAtual < minutoFechar));

  if (horarioDentroIntervalo) {
    if (!portaAberta && galinheiro.temp_api >= galinheiro.temp_config) {
      abrirPorta();
    }
    if (comedouroSubido) {
      descerComedouro();
    }
  } else {
    if (portaAberta) {
      fecharPorta();
    }
    if (!comedouroSubido) {
      subirComedouro();
    }
  }
}

bool isValidTimestamp(time_t timestamp) {
  if (timestamp < 1000000000 || timestamp > 9999999999) {
    return false;
  }
  struct tm *timeinfo = gmtime(&timestamp);
  if (timeinfo == nullptr) {
    return false;
  }
  if (timeinfo->tm_year + 1900 < 2020 || timeinfo->tm_year + 1900 > 2100) {
    return false; // Ano
  }
  if (timeinfo->tm_mon < 0 || timeinfo->tm_mon > 11) {
    return false; // Mês
  }
  if (timeinfo->tm_mday < 1 || timeinfo->tm_mday > 31) {
    return false; // Dia
  }
  if (timeinfo->tm_hour < 0 || timeinfo->tm_hour > 23) {
    return false; // Hora
  }
  if (timeinfo->tm_min < 0 || timeinfo->tm_min > 59) {
    return false; // Minuto
  }
  if (timeinfo->tm_sec < 0 || timeinfo->tm_sec > 59) {
    return false; // Segundo
  }
  return true;
}

bool validateData(float temp, unsigned long nascer, unsigned long por) {
  if (temp < -20 || temp > 45) return false;
  if (!isValidTimestamp(nascer) || !isValidTimestamp(por)) {
    return false;
  }
  if (nascer >= por || (por - nascer) > 64800) {
    return false;
  }
  return true;
}
