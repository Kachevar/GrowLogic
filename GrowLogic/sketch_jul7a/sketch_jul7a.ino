
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <DHT.h>

#define DHTPIN 12
#define DHTTYPE DHT11

#define RELAY_PUMP 5
#define RELAY_FAN 4

#define SOIL1_PIN A0


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

String page()
{
float tempPercent = constrain((airTemp / 50.0) * 100.0, 0, 100);
float humPercent = constrain(airHum, 0, 100);
float soilPercent = constrain(soil1, 0, 100);

int tempOffset = 440 - (tempPercent * 4.4);
int humOffset  = 440 - (humPercent * 4.4);
int soilOffset = 440 - (soilPercent * 4.4);

String html = R"=====(

<!DOCTYPE html>
<html lang="ru">
<head>

<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">

<title>SMART GREENHOUSE</title>

<style>

body{
background:#0f172a;
color:white;
font-family:Segoe UI;
padding:20px;
margin:0;
}

h1{
text-align:center;
color:#22c55e;
margin-bottom:25px;
}

.gauges{
display:flex;
flex-wrap:wrap;
justify-content:center;
gap:30px;
}

.gauge{
width:180px;
height:180px;
position:relative;
}

.gauge svg{
width:180px;
height:180px;
transform:rotate(-90deg);
}

.gauge-bg{
fill:none;
stroke:#334155;
stroke-width:12;
}

.gauge-fill{
fill:none;
stroke-width:12;
stroke-linecap:round;
transition:0.8s;
}

.center{
position:absolute;
top:50%;
left:50%;
transform:translate(-50%,-50%);
text-align:center;
}

.num{
font-size:30px;
font-weight:bold;
}

.label{
font-size:14px;
color:#94a3b8;
margin-top:5px;
}

.panel{
background:#1e293b;
padding:20px;
border-radius:20px;
margin-top:20px;
}

input{
width:100%;
padding:12px;
border:none;
border-radius:10px;
margin-top:5px;
margin-bottom:15px;
}

button{
width:100%;
padding:12px;
border:none;
border-radius:10px;
cursor:pointer;
margin-top:8px;
font-size:16px;
}

.green{
background:#22c55e;
color:white;
}

.red{
background:#ef4444;
color:white;
}

.blue{
background:#3b82f6;
color:white;
}

</style>

</head>

<body>

<h1>🌱 GrowLogic</h1>

<div class="gauges">

<div class="gauge">
<svg>
<circle class="gauge-bg" cx="90" cy="90" r="70"/>
<circle class="gauge-fill"
stroke="#f97316"
cx="90"
cy="90"
r="70"
stroke-dasharray="440"
stroke-dashoffset="%TEMPOFFSET%"/>
</svg>

<div class="center">
<div class="num">%TEMP%</div>
<div class="label">Температура</div>
</div>
</div>

<div class="gauge">
<svg>
<circle class="gauge-bg" cx="90" cy="90" r="70"/>
<circle class="gauge-fill"
stroke="#06b6d4"
cx="90"
cy="90"
r="70"
stroke-dasharray="440"
stroke-dashoffset="%HUMOFFSET%"/>
</svg>

<div class="center">
<div class="num">%HUM%</div>
<div class="label">Влажность</div>
</div>
</div>

<div class="gauge">
<svg>
<circle class="gauge-bg" cx="90" cy="90" r="70"/>
<circle class="gauge-fill"
stroke="#22c55e"
cx="90"
cy="90"
r="70"
stroke-dasharray="440"
stroke-dashoffset="%SOILOFFSET%"/>
</svg>

<div class="center">
<div class="num">%SOIL%</div>
<div class="label">Почва</div>
</div>
</div>

</div>

<div class="panel">

<h2>⚙ Настройки</h2>

<form action="/save">

Порог влажности почвы

<input name="soil" value="%SOILMIN%">

Температура вентилятора

<input name="fan" value="%FAN%">

Время полива (сек)

<input name="pump" value="%PUMP%">

<button class="green">
Сохранить настройки
</button>

</form>

</div>

<div class="panel">

<h2>🎛 Управление</h2>

<a href="/pumpon">
<button class="blue">Насос ВКЛ</button>
</a>

<a href="/pumpoff">
<button class="red">Насос ВЫКЛ</button>
</a>

<a href="/fanon">
<button class="blue">Вентилятор ВКЛ</button>
</a>

<a href="/fanoff">
<button class="red">Вентилятор ВЫКЛ</button>
</a>

</div>

</body>
</html>

)=====";

html.replace("%TEMP%", String(airTemp,1) + "°C");
html.replace("%HUM%", String(airHum,0) + "%");
html.replace("%SOIL%", String(soil1) + "%");

html.replace("%TEMPOFFSET%", String(tempOffset));
html.replace("%HUMOFFSET%", String(humOffset));
html.replace("%SOILOFFSET%", String(soilOffset));

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