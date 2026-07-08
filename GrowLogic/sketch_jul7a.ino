
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <DHT.h>

#define DHTPIN 16
#define DHTTYPE DHT11

#define RELAY_PUMP 5
#define RELAY_FAN 4

#define SOIL1_PIN 2

DHT dht(DHTPIN, DHTTYPE);
ESP8266WebServer server(80);

struct Settings {
  int soil1Min;
  int fanTemp;
  int pumpTime;
  bool autoMode;
};

Settings cfg;

float airTemp = 0;
float airHum = 0;
int soil1 = 0;

void saveConfig() {
  EEPROM.put(0, cfg);
  EEPROM.commit();
}

void loadConfig() {
  EEPROM.get(0, cfg);

  if (cfg.soil1Min < 1 || cfg.soil1Min > 100) {
    cfg.soil1Min = 40;
    cfg.fanTemp = 30;
    cfg.pumpTime = 10;
    cfg.autoMode = true;
    saveConfig();
  }
}

String page() {

  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>Smart Greenhouse</title>

<style>
body{
font-family:Arial;
max-width:700px;
margin:auto;
padding:20px;
background:#f2f2f2;
}

.card{
background:white;
padding:15px;
margin:10px;
border-radius:10px;
}

input{
width:100%;
padding:8px;
}

button{
padding:10px;
width:100%;
margin-top:10px;
}
</style>
</head>
<body>

<h2>Умная теплица</h2>

<div class='card'>
Температура: %TEMP% °C<br>
Влажность: %HUM% %<br>
Почва: %SOIL% %
</div>

<div class='card'>
<form action='/save'>

Порог влажности почвы:
<input name='soil' value='%SOILMIN%'>

Температура вентилятора:
<input name='fan' value='%FAN%'>

Время полива (сек):
<input name='pump' value='%PUMP%'>

<button>Сохранить</button>

</form>
</div>

<div class='card'>
<a href='/pumpon'><button>Насос ON</button></a>
<a href='/pumpoff'><button>Насос OFF</button></a>
<a href='/fanon'><button>Вентилятор ON</button></a>
<a href='/fanoff'><button>Вентилятор OFF</button></a>
</div>

</body>
</html>
)rawliteral";

  html.replace("%TEMP%", String(airTemp));
  html.replace("%HUM%", String(airHum));
  html.replace("%SOIL%", String(soil1));

  html.replace("%SOILMIN%", String(cfg.soil1Min));
  html.replace("%FAN%", String(cfg.fanTemp));
  html.replace("%PUMP%", String(cfg.pumpTime));

  return html;
}

void readSensors() {

  airTemp = dht.readTemperature();
  airHum = dht.readHumidity();

  int raw = analogRead(SOIL1_PIN);

  soil1 = map(raw, 1023, 300, 0, 100);
  soil1 = constrain(soil1, 0, 100);
}

void autoControl() {

  if (!cfg.autoMode)
    return;

  if (soil1 < cfg.soil1Min) {

    digitalWrite(RELAY_PUMP, LOW);

    delay(cfg.pumpTime * 1000);

    digitalWrite(RELAY_PUMP, HIGH);
  }

  if (airTemp > cfg.fanTemp)
    digitalWrite(RELAY_FAN, LOW);
  else
    digitalWrite(RELAY_FAN, HIGH);
}

void setup() {

  Serial.begin(115200);

  pinMode(RELAY_PUMP, OUTPUT);
  pinMode(RELAY_FAN, OUTPUT);

  digitalWrite(RELAY_PUMP, HIGH);
  digitalWrite(RELAY_FAN, HIGH);

  EEPROM.begin(512);

  loadConfig();

  dht.begin();

  WiFi.begin("VII-218", "38607240");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println(WiFi.localIP());

  server.on("/", []() {
    server.send(200, "text/html", page());
  });

  server.on("/save", []() {

    if (server.hasArg("soil"))
      cfg.soil1Min = server.arg("soil").toInt();

    if (server.hasArg("fan"))
      cfg.fanTemp = server.arg("fan").toInt();

    if (server.hasArg("pump"))
      cfg.pumpTime = server.arg("pump").toInt();

    saveConfig();

    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
  });

  server.on("/pumpon", []() {
    digitalWrite(RELAY_PUMP, LOW);
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
  });

  server.on("/pumpoff", []() {
    digitalWrite(RELAY_PUMP, HIGH);
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
  });

  server.on("/fanon", []() {
    digitalWrite(RELAY_FAN, LOW);
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
  });

  server.on("/fanoff", []() {
    digitalWrite(RELAY_FAN, HIGH);
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
  });

  server.begin();
}

unsigned long timer = 0;

void loop() {

  server.handleClient();

  if (millis() - timer > 5000) {

    timer = millis();

    readSensors();

    autoControl();

    Serial.println("-----");
    Serial.print("Temp: ");
    Serial.println(airTemp);

    Serial.print("Hum: ");
    Serial.println(airHum);

    Serial.print("Soil: ");
    Serial.println(soil1);
  }
}