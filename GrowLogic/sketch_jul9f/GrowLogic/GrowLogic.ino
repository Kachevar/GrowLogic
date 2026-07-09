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
  uint8_t fanHumidityThreshold;
  int pumpCooldown;
  uint8_t sensorInterval;
  bool systemEnabled;   // НОВОЕ: глобальное вкл/выкл системы
};

Settings cfg;

float airTemp = 0, airHum = 0;
int soil1 = 0;

bool pumpState = false;
bool fanState = false;

bool pumpRunning = false;
unsigned long pumpStart = 0;
unsigned long pumpEndTime = 0;

unsigned long pumpOnStart = 0;
unsigned long fanOnStart = 0;
unsigned long pumpTotalMillis = 0;
unsigned long fanTotalMillis = 0;
int pumpCycles = 0;

float minTemp = 100, maxTemp = -100, sumTemp = 0;
float minHum = 100, maxHum = -100, sumHum = 0;
int minSoil = 100, maxSoil = -100, sumSoil = 0;
int sampleCount = 0;

void saveConfig() {
  EEPROM.put(0, cfg);
  EEPROM.commit();
}

void loadConfig() {
  EEPROM.get(0, cfg);
  bool valid = true;
  if (cfg.soil1Min < 1 || cfg.soil1Min > 100) valid = false;
  if (cfg.fanTemp < 10 || cfg.fanTemp > 50) valid = false;
  if (cfg.pumpTime < 1 || cfg.pumpTime > 60) valid = false;
  if (cfg.fanHumidityThreshold > 100) valid = false;
  if (cfg.pumpCooldown < 0 || cfg.pumpCooldown > 3600) valid = false;
  if (cfg.sensorInterval < 1 || cfg.sensorInterval > 60) valid = false;
  
  if (!valid) {
    cfg.soil1Min = 40;
    cfg.fanTemp = 30;
    cfg.pumpTime = 10;
    cfg.autoMode = true;
    cfg.fanHumidityThreshold = 0;
    cfg.pumpCooldown = 300;
    cfg.sensorInterval = 5;
    cfg.systemEnabled = true;   // НОВОЕ: по умолчанию система включена
    saveConfig();
  }
}

void readSensors() {
  airTemp = dht.readTemperature();
  airHum = dht.readHumidity();
  int raw = analogRead(A0);
  soil1 = constrain(map(raw, 1023, 300, 0, 100), 0, 100);

  if (airTemp > -40 && airTemp < 80) {
    if (airTemp < minTemp) minTemp = airTemp;
    if (airTemp > maxTemp) maxTemp = airTemp;
    sumTemp += airTemp;
  }
  if (airHum >= 0 && airHum <= 100) {
    if (airHum < minHum) minHum = airHum;
    if (airHum > maxHum) maxHum = airHum;
    sumHum += airHum;
  }
  if (soil1 >= 0 && soil1 <= 100) {
    if (soil1 < minSoil) minSoil = soil1;
    if (soil1 > maxSoil) maxSoil = soil1;
    sumSoil += soil1;
  }
  sampleCount++;
}

void updateTimers() {
  if (pumpState && pumpOnStart == 0) {
    pumpOnStart = millis();
  }
  if (!pumpState && pumpOnStart > 0) {
    pumpTotalMillis += millis() - pumpOnStart;
    pumpOnStart = 0;
    pumpCycles++;
    pumpEndTime = millis();
  }
  if (fanState && fanOnStart == 0) {
    fanOnStart = millis();
  }
  if (!fanState && fanOnStart > 0) {
    fanTotalMillis += millis() - fanOnStart;
    fanOnStart = 0;
  }
}

// НОВОЕ: принудительное выключение всей периферии
void forceAllOff() {
  digitalWrite(RELAY_PUMP, HIGH);
  digitalWrite(RELAY_FAN, HIGH);
  pumpState = false;
  fanState = false;
  pumpRunning = false;
  pumpStart = 0;
}

const char* facts[] = {
  "Растения общаются через корни",
  "Капельный полив экономит 70% воды",
  "Оптимальная температура 22-25°C",
  "Корни привлекают полезные бактерии",
  "Растения чувствуют прикосновения",
  "Влажность 60-70% ускоряет фотосинтез",
  "Умные теплицы дают +30% урожая",
  "Растения поглощают CO2"
};

String getRandomFact() {
  return String(facts[random(0, sizeof(facts) / sizeof(facts[0]))]);
}

String page() {
  int tempOffset = 440 - (constrain(airTemp, 0, 50) / 50.0 * 440);
  int humOffset = 440 - (constrain(airHum, 0, 100) / 100.0 * 440);
  int soilOffset = 440 - (constrain(soil1, 0, 100) / 100.0 * 440);
  String fact = getRandomFact();

  String html = "";
  html += "<!DOCTYPE html><html lang='ru'><head><meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>GrowLogic</title><style>";
  html += "body{margin:0;background:#0b1120;color:#e2e8f0;font-family:sans-serif;padding:20px}";
  html += ".app{max-width:1280px;margin:0 auto}";
  html += ".header{display:flex;justify-content:space-between;align-items:center;margin-bottom:24px;flex-wrap:wrap}";
  html += ".logo{font-size:32px;background:#22c55e;width:48px;height:48px;border-radius:14px;display:flex;align-items:center;justify-content:center}";
  html += ".title{font-size:24px;font-weight:800;color:#e2e8f0}";
  html += ".sub{font-size:13px;color:#64748b}";
  html += ".badge{background:#1e293b;padding:8px 16px;border-radius:100px;font-size:13px;border:1px solid #334155}";
  html += ".nav{display:flex;gap:8px;margin-bottom:24px;background:#111d2f;padding:6px;border-radius:16px}";
  html += ".nav button{flex:1;padding:10px;border:none;border-radius:12px;background:transparent;color:#94a3b8;font-weight:600;font-size:14px;cursor:pointer}";
  html += ".nav button.active{background:#22c55e;color:#0b1120}";
  html += ".tab{display:block}.tab.hidden{display:none}";
  html += ".grid{display:grid;grid-template-columns:2fr 1fr;gap:24px}";
  html += "@media(max-width:900px){.grid{grid-template-columns:1fr}}";
  html += ".gauges{display:grid;grid-template-columns:repeat(3,1fr);gap:16px;background:#111d2f;border-radius:24px;padding:24px;border:1px solid #1e2d45}";
  html += ".gauge{display:flex;flex-direction:column;align-items:center;padding:16px 8px}";
  html += ".gwrap{position:relative;width:160px;height:160px}";
  html += ".gwrap svg{width:100%;height:100%;transform:rotate(-90deg)}";
  html += ".gbg{fill:none;stroke:#1e2d45;stroke-width:10}";
  html += ".gfill{fill:none;stroke-width:10;stroke-linecap:round;transition:stroke-dashoffset 0.8s}";
  html += ".gcenter{position:absolute;top:50%;left:50%;transform:translate(-50%,-50%);text-align:center}";
  html += ".gval{font-size:32px;font-weight:700}";
  html += ".glabel{font-size:12px;color:#94a3b8;text-transform:uppercase}";
  html += ".temp{color:#fb923c}.hum{color:#38bdf8}.soil{color:#4ade80}";
  html += ".fact{grid-column:1/-1;background:rgba(255,255,255,0.04);border-radius:14px;padding:14px 20px;border:1px solid #1e2d45;font-size:14px;color:#cbd5e1}";
  html += ".card{background:#111d2f;border-radius:24px;padding:24px;border:1px solid #1e2d45;margin-bottom:16px}";
  html += ".card-title{font-size:13px;font-weight:600;text-transform:uppercase;color:#94a3b8;margin-bottom:16px}";
  html += ".form-group{margin-bottom:14px}";
  html += ".form-label{display:block;font-size:13px;color:#cbd5e1;margin-bottom:4px}";
  html += ".form-input{width:100%;padding:10px 14px;background:#1a2a3f;border:1px solid #2a3f5a;border-radius:12px;color:#e2e8f0;font-size:15px;outline:none}";
  html += ".btn{width:100%;padding:10px;border:none;border-radius:12px;font-weight:600;font-size:14px;cursor:pointer;color:white;text-align:center}";
  html += ".btn-primary{background:#22c55e}";
  html += ".btn-auto{background:#8b5cf6}";
  html += ".btn-auto.off{background:#ef4444}";
  html += ".btn-system{background:#f59e0b}";
  html += ".btn-system.off{background:#6b7280}";
  html += ".ctrl-grid{display:grid;grid-template-columns:1fr 1fr;gap:12px}";
  html += ".ctrl-card{background:#1a2a3f;border-radius:16px;padding:16px;text-align:center;border:1px solid #2a3f5a}";
  html += ".ctrl-label{font-size:12px;text-transform:uppercase;color:#94a3b8;margin-bottom:8px}";
  html += ".btn-small{padding:6px 18px;border:none;border-radius:8px;font-weight:600;font-size:13px;cursor:pointer;text-decoration:none;color:white}";
  html += ".btn-on{background:#3b82f6}.btn-off{background:#ef4444}";
  html += ".status-row{display:grid;grid-template-columns:1fr 1fr;gap:12px}";
  html += ".status-item{background:#1a2a3f;border-radius:12px;padding:12px 16px;display:flex;justify-content:space-between;border:1px solid #2a3f5a}";
  html += ".status-on{color:#4ade80}.status-off{color:#ef4444}";
  html += ".stats-table{width:100%;border-collapse:collapse;margin-top:8px}";
  html += ".stats-table th{text-align:left;padding:10px 12px;font-size:12px;color:#94a3b8;border-bottom:1px solid #1e2d45}";
  html += ".stats-table td{padding:10px 12px;font-size:15px;border-bottom:1px solid #1a2a3f}";
  html += ".val-temp{color:#fb923c}.val-hum{color:#38bdf8}.val-soil{color:#4ade80}";
  html += ".disabled-overlay{pointer-events:none;opacity:0.5}";
  html += "@media(max-width:480px){.gwrap{width:120px;height:120px}.gval{font-size:26px}}";
  html += "</style></head><body><div class='app'>";

  // Header
  html += "<div class='header'><div style='display:flex;align-items:center;gap:12px'>";
  html += "<div class='logo'>🌱</div><div><div class='title'>GrowLogic</div><div class='sub'>Умная теплица</div></div></div>";
  // НОВОЕ: индикатор состояния системы
  if (cfg.systemEnabled) {
    html += "<div class='badge'><span style='display:inline-block;width:8px;height:8px;border-radius:50%;background:#22c55e;animation:pulse 2s infinite'></span> Система активна</div>";
  } else {
    html += "<div class='badge'><span style='display:inline-block;width:8px;height:8px;border-radius:50%;background:#ef4444'></span> Система отключена</div>";
  }
  html += "</div>";

  // Navigation
  html += "<div class='nav'><button class='active' data-tab='main'>🏠 Главная</button><button data-tab='settings'>⚙️ Настройки</button><button data-tab='stats'>📊 Статистика</button></div>";

  // Tab Main
  html += "<div id='tab-main' class='tab'>";
  html += "<div class='gauges'>";

  // Temp
  html += "<div class='gauge'><div class='gwrap'><svg viewBox='0 0 144 144'>";
  html += "<circle class='gbg' cx='72' cy='72' r='66'/>";
  html += "<circle class='gfill' stroke='#fb923c' cx='72' cy='72' r='66' stroke-dasharray='415' stroke-dashoffset='" + String(tempOffset) + "' id='tempCircle'/>";
  html += "</svg><div class='gcenter'><div class='gval temp' id='tempValue'>" + String(airTemp,1) + "°</div><div class='glabel'>Температура</div></div></div></div>";

  // Hum
  html += "<div class='gauge'><div class='gwrap'><svg viewBox='0 0 144 144'>";
  html += "<circle class='gbg' cx='72' cy='72' r='66'/>";
  html += "<circle class='gfill' stroke='#38bdf8' cx='72' cy='72' r='66' stroke-dasharray='415' stroke-dashoffset='" + String(humOffset) + "' id='humCircle'/>";
  html += "</svg><div class='gcenter'><div class='gval hum' id='humValue'>" + String(airHum,0) + "%</div><div class='glabel'>Влажность</div></div></div></div>";

  // Soil
  html += "<div class='gauge'><div class='gwrap'><svg viewBox='0 0 144 144'>";
  html += "<circle class='gbg' cx='72' cy='72' r='66'/>";
  html += "<circle class='gfill' stroke='#4ade80' cx='72' cy='72' r='66' stroke-dasharray='415' stroke-dashoffset='" + String(soilOffset) + "' id='soilCircle'/>";
  html += "</svg><div class='gcenter'><div class='gval soil' id='soilValue'>" + String(soil1) + "%</div><div class='glabel'>Почва</div></div></div></div>";

  // Fact
  html += "<div class='fact'><span>💡 </span><span id='factText'>" + fact + "</span></div>";
  html += "</div>";

  // Status & Manual controls (с учётом systemEnabled)
  String disabledClass = cfg.systemEnabled ? "" : " disabled-overlay";
  html += "<div style='margin-top:24px;'>";
  html += "<div class='card'><div class='card-title'>📊 Состояние</div><div class='status-row'>";
  html += "<div class='status-item'><span>💧 Насос</span><span class='status-item-value " + String(pumpState ? "status-on" : "status-off") + "' id='pumpStatus'>" + String(pumpState ? "ON" : "OFF") + "</span></div>";
  html += "<div class='status-item'><span>🌪 Вентилятор</span><span class='status-item-value " + String(fanState ? "status-on" : "status-off") + "' id='fanStatus'>" + String(fanState ? "ON" : "OFF") + "</span></div>";
  html += "</div></div>";
  html += "<div class='card " + disabledClass + "'><div class='card-title'>🎮 Ручное управление</div><div class='ctrl-grid'>";
  html += "<div class='ctrl-card'><div class='ctrl-label'>💧 Насос</div><div><a href='/pumpon?tab=main' class='btn-small btn-on'>Вкл</a> <a href='/pumpoff?tab=main' class='btn-small btn-off'>Выкл</a></div></div>";
  html += "<div class='ctrl-card'><div class='ctrl-label'>🌪 Вентилятор</div><div><a href='/fanon?tab=main' class='btn-small btn-on'>Вкл</a> <a href='/fanoff?tab=main' class='btn-small btn-off'>Выкл</a></div></div>";
  html += "</div></div></div>";
  html += "</div>";

  // Tab Settings
  html += "<div id='tab-settings' class='tab hidden'>";
  html += "<div class='card'><div class='card-title'>⚙️ Настройки</div>";
  html += "<form action='/save' method='get'>";
  html += "<div class='form-group'><label class='form-label'>Почва мин. %</label><input class='form-input' type='number' name='soil' value='" + String(cfg.soil1Min) + "' min='1' max='100'></div>";
  html += "<div class='form-group'><label class='form-label'>Температура вентилятора °C</label><input class='form-input' type='number' name='fan' value='" + String(cfg.fanTemp) + "' min='10' max='50'></div>";
  html += "<div class='form-group'><label class='form-label'>Время полива (сек)</label><input class='form-input' type='number' name='pump' value='" + String(cfg.pumpTime) + "' min='1' max='60'></div>";
  html += "<div class='form-group'><label class='form-label'>Влажность для вентилятора % (0-выкл)</label><input class='form-input' type='number' name='fhum' value='" + String(cfg.fanHumidityThreshold) + "' min='0' max='100'></div>";
  html += "<div class='form-group'><label class='form-label'>Задержка между поливами (сек)</label><input class='form-input' type='number' name='cooldown' value='" + String(cfg.pumpCooldown) + "' min='0' max='3600'></div>";
  html += "<div class='form-group'><label class='form-label'>Интервал датчиков (сек)</label><input class='form-input' type='number' name='sinterval' value='" + String(cfg.sensorInterval) + "' min='1' max='60'></div>";
  html += "<button class='btn btn-primary' type='submit'>💾 Сохранить</button>";
  html += "</form></div>";
  html += "<div class='card'><div class='card-title'>🔄 Режим управления</div>";
  html += "<a href='/toggleauto?tab=settings' style='text-decoration:none;display:block;'><button class='btn btn-auto " + String(cfg.autoMode ? "" : "off") + "'>" + String(cfg.autoMode ? "🤖 Автоматический (ON)" : "👤 Ручной (OFF)") + "</button></a>";
  html += "</div>";
  // НОВОЕ: кнопка вкл/выкл системы
  html += "<div class='card'><div class='card-title'>🔌 Система</div>";
  html += "<a href='/togglesystem?tab=settings' style='text-decoration:none;display:block;'><button class='btn btn-system " + String(cfg.systemEnabled ? "" : "off") + "'>" + String(cfg.systemEnabled ? "🟢 Система ВКЛ" : "🔴 Система ВЫКЛ") + "</button></a>";
  html += "</div></div>";

  // Tab Stats (без изменений, только добавим отображение systemEnabled в данных)
  html += "<div id='tab-stats' class='tab hidden'>";
  html += "<div class='card'><div class='card-title'>📈 Статистика за сессию</div>";
  html += "<table class='stats-table'><thead><tr><th>Параметр</th><th>Текущее</th><th>Мин</th><th>Макс</th><th>Среднее</th></tr></thead><tbody>";
  html += "<tr><td>🌡️ Температура</td><td class='val-temp' id='statTempCur'>--</td><td class='val-temp' id='statTempMin'>--</td><td class='val-temp' id='statTempMax'>--</td><td class='val-temp' id='statTempAvg'>--</td></tr>";
  html += "<tr><td>💧 Влажность</td><td class='val-hum' id='statHumCur'>--</td><td class='val-hum' id='statHumMin'>--</td><td class='val-hum' id='statHumMax'>--</td><td class='val-hum' id='statHumAvg'>--</td></tr>";
  html += "<tr><td>🌱 Почва</td><td class='val-soil' id='statSoilCur'>--</td><td class='val-soil' id='statSoilMin'>--</td><td class='val-soil' id='statSoilMax'>--</td><td class='val-soil' id='statSoilAvg'>--</td></tr>";
  html += "</tbody></table>";
  html += "<div style='margin-top:16px;font-size:13px;color:#94a3b8;'>Измерений: <span id='sampleCount'>0</span></div>";
  html += "</div>";
  html += "<div class='card'><div class='card-title'>⏱️ Дополнительно</div>";
  html += "<div class='status-row' style='margin-bottom:12px;'>";
  html += "<div class='status-item'><span>Время работы</span><span id='uptime'>--</span></div>";
  html += "<div class='status-item'><span>Поливов</span><span id='pumpCycles'>0</span></div>";
  html += "</div>";
  html += "<div class='status-row' style='margin-bottom:12px;'>";
  html += "<div class='status-item'><span>Насос (всего)</span><span id='pumpTotal'>0 сек</span></div>";
  html += "<div class='status-item'><span>Вентилятор (всего)</span><span id='fanTotal'>0 сек</span></div>";
  html += "</div>";
  html += "<div class='status-row' style='margin-bottom:12px;'>";
  html += "<div class='status-item'><span>Осталось полива</span><span id='pumpRemaining'>--</span></div>";
  html += "<div class='status-item'><span>Последний полив</span><span id='lastPumpAgo'>--</span></div>";
  html += "</div>";
  html += "<div style='margin-top:12px;'><span style='color:#94a3b8;'>Режим: </span><span id='statAutoMode' style='font-weight:600;'>--</span></div>";
  html += "</div></div>";

  // JavaScript
  html += "<script>";
  html += "function setTab(id){document.querySelectorAll('.tab').forEach(t=>t.classList.toggle('hidden',t.id!=='tab-'+id));document.querySelectorAll('.nav button').forEach(b=>b.classList.toggle('active',b.dataset.tab===id));}";
  html += "document.querySelectorAll('.nav button').forEach(b=>b.onclick=function(){setTab(this.dataset.tab);});";
  html += "var p=window.location.search;var m=p.match(/tab=([^&]+)/);if(m)setTab(m[1]);";
  html += "var f=[";
  for (int i = 0; i < sizeof(facts)/sizeof(facts[0]); i++) {
    if (i > 0) html += ",";
    html += "\"" + String(facts[i]) + "\"";
  }
  html += "];";
  html += "var fi=Math.floor(Math.random()*f.length);";
  html += "function updateFact(){fi=(fi+1)%f.length;document.getElementById('factText').textContent=f[fi];}";
  html += "function updGauge(id,val,max,unit){var c=document.getElementById(id+'Circle');var v=document.getElementById(id+'Value');if(!c||!v)return;c.setAttribute('stroke-dashoffset',415-(Math.min(val,max)/max)*415);v.textContent=(unit=='C'?val.toFixed(1)+'°':Math.round(val)+'%');}";
  html += "function updateData(){fetch('/data').then(r=>r.json()).then(d=>{";
  html += "updGauge('temp',d.temp,50,'C');updGauge('hum',d.hum,100,'%');updGauge('soil',d.soil,100,'%');";
  html += "var p=document.getElementById('pumpStatus');var f=document.getElementById('fanStatus');p.textContent=d.pump?'ON':'OFF';p.className='status-item-value '+(d.pump?'status-on':'status-off');f.textContent=d.fan?'ON':'OFF';f.className='status-item-value '+(d.fan?'status-on':'status-off');";
  html += "document.getElementById('statTempCur').textContent=d.temp.toFixed(1)+'°';document.getElementById('statHumCur').textContent=Math.round(d.hum)+'%';document.getElementById('statSoilCur').textContent=Math.round(d.soil)+'%';";
  html += "document.getElementById('statTempMin').textContent=d.tempMin.toFixed(1)+'°';document.getElementById('statHumMin').textContent=Math.round(d.humMin)+'%';document.getElementById('statSoilMin').textContent=Math.round(d.soilMin)+'%';";
  html += "document.getElementById('statTempMax').textContent=d.tempMax.toFixed(1)+'°';document.getElementById('statHumMax').textContent=Math.round(d.humMax)+'%';document.getElementById('statSoilMax').textContent=Math.round(d.soilMax)+'%';";
  html += "document.getElementById('statTempAvg').textContent=d.tempAvg.toFixed(1)+'°';document.getElementById('statHumAvg').textContent=Math.round(d.humAvg)+'%';document.getElementById('statSoilAvg').textContent=Math.round(d.soilAvg)+'%';";
  html += "document.getElementById('sampleCount').textContent=d.samples;";
  html += "var sp=document.getElementById('statPump');var sf=document.getElementById('statFan');sp.textContent=d.pump?'ON':'OFF';sp.className='status-item-value '+(d.pump?'status-on':'status-off');sf.textContent=d.fan?'ON':'OFF';sf.className='status-item-value '+(d.fan?'status-on':'status-off');";
  html += "var auto=document.getElementById('statAutoMode');auto.textContent=d.auto?'Автоматический':'Ручной';auto.style.color=d.auto?'#a78bfa':'#f87171';";
  html += "document.getElementById('uptime').textContent=d.uptime;";
  html += "document.getElementById('pumpCycles').textContent=d.pumpCycles;";
  html += "document.getElementById('pumpTotal').textContent=d.pumpTotalSec+' сек';";
  html += "document.getElementById('fanTotal').textContent=d.fanTotalSec+' сек';";
  html += "document.getElementById('pumpRemaining').textContent=d.pumpRemainingSec>0?d.pumpRemainingSec+' сек':'--';";
  html += "document.getElementById('lastPumpAgo').textContent=d.lastPumpSecAgo>=0?d.lastPumpSecAgo+' сек назад':'--';";
  // НОВОЕ: обновление визуального статуса системы на основе JSON (поле systemOn)
  html += "if(d.systemOn !== undefined){var badge=document.querySelector('.badge');if(badge){if(d.systemOn){badge.innerHTML='<span style=\"display:inline-block;width:8px;height:8px;border-radius:50%;background:#22c55e;animation:pulse 2s infinite\"></span> Система активна';}else{badge.innerHTML='<span style=\"display:inline-block;width:8px;height:8px;border-radius:50%;background:#ef4444\"></span> Система отключена';}}";
  html += "var manualCard=document.querySelector('.card.disabled-overlay, .card:not(.disabled-overlay)'); if(manualCard) manualCard.classList.toggle('disabled-overlay',!d.systemOn);";
  html += "}";
  html += "}).catch(e=>console.error(e));}";
  html += "setInterval(updateFact,8000);setInterval(updateData,2000);updateData();";
  html += "</script>";
  html += "</body></html>";

  return html;
}

void autoControl() {
  // НОВОЕ: если система выключена, ничего не делаем (и дополнительно гарантируем OFF)
  if (!cfg.systemEnabled) {
    // на всякий случай убедимся, что реле выключены
    if (pumpState || fanState) forceAllOff();
    return;
  }

  if (!cfg.autoMode) return;

  // Полив с учётом задержки
  if (soil1 < cfg.soil1Min && !pumpRunning) {
    unsigned long cooldownMs = cfg.pumpCooldown * 1000UL;
    if (pumpEndTime == 0 || (millis() - pumpEndTime) >= cooldownMs) {
      pumpRunning = true;
      pumpState = true;
      digitalWrite(RELAY_PUMP, LOW);
      pumpStart = millis();
    }
  }

  if (pumpRunning && (millis() - pumpStart >= (unsigned long)cfg.pumpTime * 1000)) {
    digitalWrite(RELAY_PUMP, HIGH);
    pumpRunning = false;
    pumpState = false;
    pumpEndTime = millis();
  }

  bool fanByTemp = (airTemp > cfg.fanTemp);
  bool fanByHum = (cfg.fanHumidityThreshold > 0 && airHum > cfg.fanHumidityThreshold);
  if (fanByTemp || fanByHum) {
    digitalWrite(RELAY_FAN, LOW);
    fanState = true;
  } else {
    digitalWrite(RELAY_FAN, HIGH);
    fanState = false;
  }
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
  randomSeed(analogRead(A0));

  WiFi.begin("VII-218", "38607240");
  while (WiFi.status() != WL_CONNECTED) { delay(500); }

  server.on("/", []() { server.send(200, "text/html", page()); });

  server.on("/save", []() {
    if (server.hasArg("soil")) cfg.soil1Min = server.arg("soil").toInt();
    if (server.hasArg("fan")) cfg.fanTemp = server.arg("fan").toInt();
    if (server.hasArg("pump")) cfg.pumpTime = server.arg("pump").toInt();
    if (server.hasArg("fhum")) cfg.fanHumidityThreshold = server.arg("fhum").toInt();
    if (server.hasArg("cooldown")) cfg.pumpCooldown = server.arg("cooldown").toInt();
    if (server.hasArg("sinterval")) cfg.sensorInterval = server.arg("sinterval").toInt();
    saveConfig();
    server.sendHeader("Location", "/?tab=settings");
    server.send(302, "text/plain", "");
  });

  server.on("/pumpon", []() {
    if (!cfg.systemEnabled) { // НОВОЕ: блокировка при выключенной системе
      String tab = server.hasArg("tab") ? server.arg("tab") : "main";
      server.sendHeader("Location", "/?tab=" + tab);
      server.send(302, "text/plain", "");
      return;
    }
    digitalWrite(RELAY_PUMP, LOW);
    pumpState = true;
    pumpRunning = false;
    String tab = server.hasArg("tab") ? server.arg("tab") : "main";
    server.sendHeader("Location", "/?tab=" + tab);
    server.send(302, "text/plain", "");
  });
  server.on("/pumpoff", []() {
    digitalWrite(RELAY_PUMP, HIGH);
    pumpState = false;
    pumpRunning = false;
    String tab = server.hasArg("tab") ? server.arg("tab") : "main";
    server.sendHeader("Location", "/?tab=" + tab);
    server.send(302, "text/plain", "");
  });
  server.on("/fanon", []() {
    if (!cfg.systemEnabled) {
      String tab = server.hasArg("tab") ? server.arg("tab") : "main";
      server.sendHeader("Location", "/?tab=" + tab);
      server.send(302, "text/plain", "");
      return;
    }
    digitalWrite(RELAY_FAN, LOW);
    fanState = true;
    String tab = server.hasArg("tab") ? server.arg("tab") : "main";
    server.sendHeader("Location", "/?tab=" + tab);
    server.send(302, "text/plain", "");
  });
  server.on("/fanoff", []() {
    digitalWrite(RELAY_FAN, HIGH);
    fanState = false;
    String tab = server.hasArg("tab") ? server.arg("tab") : "main";
    server.sendHeader("Location", "/?tab=" + tab);
    server.send(302, "text/plain", "");
  });

  server.on("/toggleauto", []() {
    cfg.autoMode = !cfg.autoMode;
    saveConfig();
    if (!cfg.autoMode) pumpRunning = false;
    String tab = server.hasArg("tab") ? server.arg("tab") : "settings";
    server.sendHeader("Location", "/?tab=" + tab);
    server.send(302, "text/plain", "");
  });

  // НОВОЕ: переключение системы вкл/выкл
  server.on("/togglesystem", []() {
    cfg.systemEnabled = !cfg.systemEnabled;
    saveConfig();
    if (!cfg.systemEnabled) {
      forceAllOff();  // немедленно всё выключить
    }
    String tab = server.hasArg("tab") ? server.arg("tab") : "settings";
    server.sendHeader("Location", "/?tab=" + tab);
    server.send(302, "text/plain", "");
  });

  server.on("/data", []() {
    float avgTemp = (sampleCount > 0) ? sumTemp / sampleCount : 0;
    float avgHum = (sampleCount > 0) ? sumHum / sampleCount : 0;
    float avgSoil = (sampleCount > 0) ? (float)sumSoil / sampleCount : 0;

    unsigned long uptimeSec = millis() / 1000;
    unsigned long pumpTotalSec = pumpTotalMillis / 1000;
    unsigned long fanTotalSec = fanTotalMillis / 1000;
    long pumpRemainingSec = 0;
    if (pumpRunning) {
      unsigned long elapsed = millis() - pumpStart;
      pumpRemainingSec = ((unsigned long)cfg.pumpTime * 1000 - elapsed) / 1000;
      if (pumpRemainingSec < 0) pumpRemainingSec = 0;
    }
    long lastPumpSecAgo = (pumpEndTime > 0) ? (millis() - pumpEndTime) / 1000 : -1;

    String json = "{";
    json += "\"temp\":" + String(airTemp, 1) + ",";
    json += "\"hum\":" + String(airHum, 0) + ",";
    json += "\"soil\":" + String(soil1) + ",";
    json += "\"pump\":" + String(pumpState ? 1 : 0) + ",";
    json += "\"fan\":" + String(fanState ? 1 : 0) + ",";
    json += "\"auto\":" + String(cfg.autoMode ? 1 : 0) + ",";
    json += "\"systemOn\":" + String(cfg.systemEnabled ? 1 : 0) + ",";  // НОВОЕ
    json += "\"tempMin\":" + String(minTemp, 1) + ",";
    json += "\"tempMax\":" + String(maxTemp, 1) + ",";
    json += "\"tempAvg\":" + String(avgTemp, 1) + ",";
    json += "\"humMin\":" + String(minHum, 0) + ",";
    json += "\"humMax\":" + String(maxHum, 0) + ",";
    json += "\"humAvg\":" + String(avgHum, 0) + ",";
    json += "\"soilMin\":" + String(minSoil) + ",";
    json += "\"soilMax\":" + String(maxSoil) + ",";
    json += "\"soilAvg\":" + String(avgSoil, 0) + ",";
    json += "\"samples\":" + String(sampleCount) + ",";
    json += "\"uptime\":\"" + String(uptimeSec / 3600) + "ч " + String((uptimeSec % 3600) / 60) + "м " + String(uptimeSec % 60) + "с\",";
    json += "\"pumpCycles\":" + String(pumpCycles) + ",";
    json += "\"pumpTotalSec\":" + String(pumpTotalSec) + ",";
    json += "\"fanTotalSec\":" + String(fanTotalSec) + ",";
    json += "\"pumpRemainingSec\":" + String(pumpRemainingSec) + ",";
    json += "\"lastPumpSecAgo\":" + String(lastPumpSecAgo);
    json += "}";
    server.send(200, "application/json", json);
  });

  server.begin();
}

unsigned long timer = 0;
void loop() {
  server.handleClient();
  unsigned long interval = cfg.sensorInterval * 1000UL;
  if (millis() - timer > interval) {
    timer = millis();
    readSensors();
    autoControl();
  }
  updateTimers();
}