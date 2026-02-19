// ESP32 PlantSense
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

const char* ssid     = "YOUR-SSID";
const char* password = "YPUR-SSID-PASSWORD";
const int   port     = 80;

WebServer server(port);

const int led = 2;

// DHT11
#include "DHT.h"
#define DHTPIN  22
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// Sensor de humidade do solo (capacitivo)
float asoilmoist = 0.0f;

// ────────────────────────────────────────────────
// Configuração do histórico em LittleFS
// ────────────────────────────────────────────────
const char* HISTORY_FILE = "/history.json";
const size_t MAX_HISTORY_POINTS = 500;
const unsigned long SAVE_INTERVAL_MS = 300000;  // 5 minutos = 300000 ms

unsigned long lastSaveTime = 0;

// Estrutura simples para cada leitura
struct Reading {
  uint32_t timestamp;   // segundos desde boot (millis()/1000)
  uint8_t moisture;
  int8_t temp;          // -20 a 60
  uint8_t humidity;
};

std::vector<Reading> history;  // vector dinâmico (usa heap)

// Carrega histórico do LittleFS no boot
void loadHistory() {
  history.clear();

  if (!LittleFS.exists(HISTORY_FILE)) {
    Serial.println("Arquivo de histórico não existe ainda");
    return;
  }

  File file = LittleFS.open(HISTORY_FILE, "r");
  if (!file) {
    Serial.println("Falha ao abrir " + String(HISTORY_FILE) + " para leitura");
    return;
  }

  DynamicJsonDocument doc(8192);
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error) {
    Serial.print("deserializeJson() falhou: ");
    Serial.println(error.c_str());
    return;
  }

  JsonArray arr = doc.as<JsonArray>();
  for (JsonObject obj : arr) {
    Reading r;
    r.timestamp  = obj["ts"]   | 0;
    r.moisture   = obj["m"]    | 0;
    r.temp       = obj["t"]    | 0;
    r.humidity   = obj["h"]    | 0;
    history.push_back(r);
  }

  Serial.printf("Carregado %d pontos do histórico\n", history.size());
}

// Salva histórico no LittleFS (só quando necessário)
bool saveHistory() {
  if (history.empty()) return true;

  DynamicJsonDocument doc(8192);
  JsonArray arr = doc.to<JsonArray>();

  for (const auto& r : history) {
    JsonObject obj = arr.createNestedObject();
    obj["ts"] = r.timestamp;
    obj["m"]  = r.moisture;
    obj["t"]  = r.temp;
    obj["h"]  = r.humidity;
  }

  File file = LittleFS.open(HISTORY_FILE, "w");
  if (!file) {
    Serial.println("Falha ao abrir " + String(HISTORY_FILE) + " para escrita");
    return false;
  }

  serializeJson(doc, file);
  file.close();
  Serial.printf("Histórico salvo (%d pontos)\n", history.size());
  return true;
}

// Adiciona nova leitura (chamado periodicamente no loop)
void addReading(int moisture, float tempC, float hum) {
  Reading r;
  r.timestamp = millis() / 1000;
  r.moisture  = constrain(moisture, 0, 100);
  r.temp      = constrain((int)round(tempC), -20, 80);
  r.humidity  = constrain((int)round(hum), 0, 100);

  history.push_back(r);

  // Limita o tamanho máximo (remove os mais antigos)
  while (history.size() > MAX_HISTORY_POINTS) {
    history.erase(history.begin());
  }

  // Salva a cada SAVE_INTERVAL_MS
  unsigned long now = millis();
  if (now - lastSaveTime >= SAVE_INTERVAL_MS) {
    saveHistory();
    lastSaveTime = now;
  }
}

// ────────────────────────────────────────────────
// Handlers Web
// ────────────────────────────────────────────────

void handleRoot() {
  digitalWrite(led, HIGH);

  // Leitura inicial (apenas para primeira carga HTML)
  float hum  = dht.readHumidity();
  float temp = dht.readTemperature();
  bool sensorOk = !isnan(hum) && !isnan(temp);

  if (!sensorOk) {
    hum  = -999;
    temp = -999;
  }

  // Umidade do solo
  float moistMin = 2000.0f;     // probes in water
  float moistMax = 3344.0f;     // probes in air
  float moistPercent = 100.0f - ((asoilmoist - moistMin) / (moistMax - moistMin) * 100.0f);
  moistPercent = constrain(moistPercent, 0.0f, 100.0f);
  int moistureInt = (int)round(moistPercent);

  // Status
  String statusClass, statusText, ringColor;
  if (moistureInt < 30) {
    statusClass = "danger";
    statusText  = "Needs Water";
    ringColor   = "var(--ring-bad)";
  } else if (moistureInt < 52) {
    statusClass = "warn";
    statusText  = "Moderate";
    ringColor   = "var(--ring-warn)";
  } else {
    statusClass = "good";
    statusText  = "Healthy";
    ringColor   = "var(--ring-good)";
  }

  // ──────────────── HTML ────────────────
  String html = "<!DOCTYPE html>\n";
  html += "<html lang=\"pt-br\">\n<head>\n";
  html += "  <meta charset=\"UTF-8\">\n";
  html += "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n";
  html += "  <title>PlantSense</title>\n";
  html += "  <link rel=\"stylesheet\" href=\"https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&display=swap\">\n";
  html += "  <style>\n";
  html += "    :root {\n";
  html += "      --bg: #0a0f1a; --surface: #121826; --surface2: #1a2232;\n";
  html += "      --accent: #22c55e; --accent-dim: #16a34a;\n";
  html += "      --warning: #f59e0b; --danger: #ef4444;\n";
  html += "      --text: #e2e8f0; --text-soft: #94a3b8; --text-ghost: #64748b;\n";
  html += "      --ring-good: #22c55e; --ring-warn: #f59e0b; --ring-bad: #ef4444;\n";
  html += "      --radius: 16px;\n    }\n";
  html += "    * { margin:0; padding:0; box-sizing:border-box; }\n";
  html += "    body { font-family: 'Inter', system-ui, sans-serif; background: linear-gradient(135deg, var(--bg), #0f172a);\n";
  html += "           color: var(--text); min-height: 100vh; padding: 2rem 1.5rem; line-height: 1.5; }\n";
  html += "    header { text-align: center; margin-bottom: 2.5rem; }\n";
  html += "    h1 { font-size: 2.1rem; font-weight: 700; letter-spacing: -0.02em; margin-bottom: 0.4rem; }\n";
  html += "    .subtitle { color: var(--text-soft); font-size: 0.95rem; }\n";
  html += "    .card { background: var(--surface); border-radius: var(--radius); padding: 1.8rem;\n";
  html += "            box-shadow: 0 10px 30px -12px rgba(0,0,0,0.5); border: 1px solid rgba(255,255,255,0.04);\n";
  html += "            max-width: 420px; margin: 0 auto; }\n";
  html += "    .plant-header { display: flex; align-items: center; gap: 1rem; margin-bottom: 1.6rem; }\n";
  html += "    .plant-icon { width: 54px; height: 54px; border-radius: 14px; background: linear-gradient(135deg, #166534, #22c55e22);\n";
  html += "                  display: grid; place-items: center; font-size: 1.9rem; box-shadow: inset 0 2px 6px rgba(0,0,0,0.35); }\n";
  html += "    .plant-name { font-size: 1.38rem; font-weight: 600; }\n";
  html += "    .gauge-container { position: relative; height: 200px; display: flex; justify-content: center; align-items: center; margin: 1rem 0 1.5rem; }\n";
  html += "    .gauge-svg { width: 170px; height: 170px; }\n";
  html += "    .gauge-bg, .gauge-progress { fill: none; stroke-width: 14; stroke-linecap: round; cx: 85; cy: 85; r: 78; }\n";
  html += "    .gauge-bg { stroke: rgba(60,80,110,0.65); }\n";
  html += "    .gauge-progress { stroke: var(--accent); transition: stroke-dashoffset 0.8s ease, stroke 0.5s ease; }\n";
  html += "    .gauge-value { position: absolute; font-size: 2.8rem; font-weight: 700;\n";
  html += "                   background: linear-gradient(90deg, #a7f3d0, #bbf7fe); -webkit-background-clip: text; -webkit-text-fill-color: transparent; }\n";
  html += "    .readings { display: grid; grid-template-columns: 1fr 1fr; gap: 1rem; margin: 1.2rem 0 1.6rem; }\n";
  html += "    .reading { background: var(--surface2); padding: 1rem; border-radius: 10px; text-align: center;\n";
  html += "               border: 1px solid rgba(255,255,255,0.035); }\n";
  html += "    .reading-label { font-size: 0.82rem; color: var(--text-soft); margin-bottom: 0.4rem; }\n";
  html += "    .reading-value { font-size: 1.42rem; font-weight: 600; }\n";
  html += "    .status { padding: 0.9rem; border-radius: 12px; text-align: center; font-weight: 600; font-size: 1.05rem; margin-top: 0.5rem; transition: all 0.4s ease; }\n";
  html += "    .status.good   { background: rgba(34,197,94,0.18); color: #86efac; }\n";
  html += "    .status.warn   { background: rgba(245,158,11,0.18); color: #fcd34d; }\n";
  html += "    .status.danger { background: rgba(239,68,68,0.18);  color: #fca5a5; }\n";
  html += "    footer { text-align: center; margin-top: 3rem; color: var(--text-ghost); font-size: 0.82rem; }\n";
  html += "    @media (max-width: 500px) { .gauge-value { font-size: 2.4rem; } .gauge-svg { width: 150px; height: 150px; } }\n";
  html += "  </style>\n</head>\n<body>\n\n";

  html += "  <header>\n    <h1>🌿 PlantSense</h1>\n    <div class=\"subtitle\">Monstera – Sala de Estar — Live</div>\n  </header>\n\n";
  html += "  <div class=\"card\">\n";
  html += "    <div class=\"plant-header\">\n      <div class=\"plant-icon\">🪴</div>\n      <div class=\"plant-name\">Monstera</div>\n    </div>\n\n";
  html += "    <div class=\"gauge-container\">\n";
  html += "      <svg class=\"gauge-svg\" viewBox=\"0 0 170 170\">\n";
  html += "        <circle class=\"gauge-bg\"/>\n";
  html += "        <circle class=\"gauge-progress\" id=\"progress\" stroke=\"" + ringColor + "\" stroke-dasharray=\"490\" stroke-dashoffset=\"490\"></circle>\n";
  html += "      </svg>\n";
  html += "      <div class=\"gauge-value\" id=\"moistVal\">" + String(moistureInt) + "%</div>\n    </div>\n\n";
  html += "    <div class=\"readings\">\n";
  html += "      <div class=\"reading\"><div class=\"reading-label\">Temperatura</div><div class=\"reading-value\" id=\"tempVal\">" + (sensorOk ? String(temp, 1) : "–") + " °C</div></div>\n";
  html += "      <div class=\"reading\"><div class=\"reading-label\">Humidade</div><div class=\"reading-value\" id=\"humVal\">" + (sensorOk ? String(hum, 0) : "–") + " %</div></div>\n";
  html += "    </div>\n\n";
  html += "    <div class=\"status " + statusClass + "\" id=\"status\">" + statusText + "</div>\n  </div>\n\n";
  html += "  <footer id=\"footer\">ESP32 • by Pedro Lucas</footer>\n\n";

  // JavaScript – atualização dinâmica
  html += "  <script>\n";
  html += "    const radius = 78;\n";
  html += "    const circ = 2 * Math.PI * radius;\n\n";

  html += "    let current = {\n";
  html += "      moisture: " + String(moistureInt) + ",\n";
  html += "      temp: \"" + (sensorOk ? String(temp,1) : "–") + "\",\n";
  html += "      humidity: \"" + (sensorOk ? String(hum,0) : "–") + "\",\n";
  html += "      statusClass: \"" + statusClass + "\",\n";
  html += "      statusText: \"" + statusText + "\",\n";
  html += "      ringColor: \"" + ringColor + "\"\n";
  html += "    };\n\n";

  html += "    const elMoistVal   = document.getElementById('moistVal');\n";
  html += "    const elProgress   = document.getElementById('progress');\n";
  html += "    const elTemp       = document.getElementById('tempVal');\n";
  html += "    const elHum        = document.getElementById('humVal');\n";
  html += "    const elStatus     = document.getElementById('status');\n";
  html += "    const elFooter     = document.getElementById('footer');\n\n";

  html += "    function updateGauge(percent) {\n";
  html += "      const offset = circ - (percent / 100) * circ;\n";
  html += "      elProgress.style.strokeDashoffset = offset;\n";
  html += "      elProgress.style.stroke = current.ringColor;\n";
  html += "      elMoistVal.textContent = percent + '%';\n";
  html += "    }\n\n";

  html += "    function updateUI(data) {\n";
  html += "      let changed = false;\n\n";

  html += "      if (data.moisture !== current.moisture) {\n";
  html += "        current.moisture = data.moisture;\n";
  html += "        updateGauge(data.moisture);\n";
  html += "        changed = true;\n";
  html += "      }\n\n";

  html += "      if (data.temp !== current.temp) {\n";
  html += "        current.temp = data.temp;\n";
  html += "        elTemp.textContent = data.temp + ' °C';\n";
  html += "        changed = true;\n";
  html += "      }\n\n";

  html += "      if (data.humidity !== current.humidity) {\n";
  html += "        current.humidity = data.humidity;\n";
  html += "        elHum.textContent = data.humidity + ' %';\n";
  html += "        changed = true;\n";
  html += "      }\n\n";

  html += "      if (data.statusClass !== current.statusClass || data.statusText !== current.statusText) {\n";
  html += "        current.statusClass = data.statusClass;\n";
  html += "        current.statusText  = data.statusText;\n";
  html += "        current.ringColor   = data.ringColor;\n\n";
  html += "        elStatus.className = 'status ' + data.statusClass;\n";
  html += "        elStatus.textContent = data.statusText;\n";
  html += "        changed = true;\n";
  html += "      }\n\n";

  html += "      elFooter.textContent = `ESP32 • by Pedro Lucas`;\n\n";

  html += "      if (changed) {\n";
  html += "        elMoistVal.style.transition = 'transform 0.35s';\n";
  html += "        elMoistVal.style.transform = 'scale(1.06)';\n";
  html += "        setTimeout(() => elMoistVal.style.transform = 'scale(1)', 450);\n";
  html += "      }\n";
  html += "    }\n\n";

  html += "    function fetchData() {\n";
  html += "      fetch('/data')\n";
  html += "        .then(r => r.json())\n";
  html += "        .then(data => updateUI(data))\n";
  html += "        .catch(err => console.log('Erro ao buscar dados:', err));\n";
  html += "    }\n\n";

  html += "    // Inicializa o gauge com valor da primeira carga\n";
  html += "    updateGauge(current.moisture);\n\n";

  html += "    // Atualiza a cada 5 segundos\n";
  html += "    setInterval(fetchData, 5000);\n";
  html += "    // Primeira atualização rápida após carregar\n";
  html += "    setTimeout(fetchData, 1200);\n";
  html += "  </script>\n";
  html += "</body></html>";

  server.send(200, "text/html", html);
  digitalWrite(led, LOW);
}
void handleCurrentData() {
  float hum  = dht.readHumidity();
  float temp = dht.readTemperature();
  bool ok = !isnan(hum) && !isnan(temp);

  float moistMin = 2000.0f;
  float moistMax = 3344.0f;
  float moistPercent = 100.0f - ((asoilmoist - moistMin) / (moistMax - moistMin) * 100.0f);
  moistPercent = constrain(moistPercent, 0.0f, 100.0f);
  int moistureInt = (int)round(moistPercent);

  String statusClass = (moistureInt < 30) ? "danger" : (moistureInt < 52) ? "warn" : "good";
  String statusText  = (moistureInt < 30) ? "Needs Water" : (moistureInt < 52) ? "Moderate" : "Healthy";
  String ringColor   = (moistureInt < 30) ? "var(--ring-bad)" : (moistureInt < 52) ? "var(--ring-warn)" : "var(--ring-good)";

  DynamicJsonDocument doc(512);
  doc["moisture"]     = moistureInt;
  doc["temp"]         = ok ? String(temp,1) : "–";
  doc["humidity"]     = ok ? String(hum,0)  : "–";
  doc["statusClass"]  = statusClass;
  doc["statusText"]   = statusText;
  doc["ringColor"]    = ringColor;
  doc["uptime"]       = millis() / 1000;

  // Últimos 20 do histórico (para atualização rápida)
  JsonArray recent = doc.createNestedArray("recent");
  size_t start = (history.size() > 20) ? history.size() - 20 : 0;
  for (size_t i = start; i < history.size(); i++) {
    JsonObject obj = recent.createNestedObject();
    obj["ts"] = history[i].timestamp;
    obj["m"]  = history[i].moisture;
    obj["t"]  = history[i].temp;
    obj["h"]  = history[i].humidity;
  }

  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleFullHistory() {
  DynamicJsonDocument doc(8192);
  JsonArray arr = doc.to<JsonArray>();

  for (const auto& r : history) {
    JsonObject obj = arr.createNestedObject();
    obj["ts"] = r.timestamp;
    obj["m"]  = r.moisture;
    obj["t"]  = r.temp;
    obj["h"]  = r.humidity;
  }

  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleHistoryCSV() {
  String csv = "timestamp,moisture,temp,humidity\n";
  for (const auto& r : history) {
    csv += String(r.timestamp) + "," + 
           String(r.moisture) + "," + 
           String(r.temp) + "," + 
           String(r.humidity) + "\n";
  }
  server.send(200, "text/csv", csv);
}

void handleClearHistory() {
  history.clear();
  LittleFS.remove(HISTORY_FILE);
  server.send(200, "application/json", "{\"status\":\"cleared\"}");
}

void handleNotFound() {
  server.send(404, "text/plain", "Not Found");
}

// ────────────────────────────────────────────────
// setup & loop
// ────────────────────────────────────────────────

void setup() {
  pinMode(led, OUTPUT);
  digitalWrite(led, LOW);
  Serial.begin(115200);
  delay(100);

  if (!LittleFS.begin()) {
    Serial.println("Falha ao montar LittleFS");
    while (true) delay(100);
  }
  Serial.println("LittleFS montado com sucesso");

  pinMode(32, INPUT);

  // Estabilização sensor solo
  asoilmoist = analogRead(32);
  for (int i = 0; i < 80; i++) {
    asoilmoist = 0.85 * asoilmoist + 0.15 * analogRead(32);
    delay(22);
  }

  loadHistory();   // ← carrega histórico persistente

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(480);
    Serial.print(".");
  }
  Serial.println("\nIP: " + WiFi.localIP().toString());

  if (MDNS.begin("plant")) {
    Serial.println("Acesse via http://plant.local");
  }

  server.on("/",            HTTP_GET, handleRoot);
  server.on("/data",        HTTP_GET, handleCurrentData);
  server.on("/history",     HTTP_GET, handleFullHistory);
  server.on("/history.csv", HTTP_GET, handleHistoryCSV);
  server.on("/clearhistory",HTTP_GET, handleClearHistory);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("Servidor HTTP iniciado");

  dht.begin();
}

void loop() {
  server.handleClient();

  // Atualização suave do sensor
  float raw = analogRead(32);
  asoilmoist = 0.92 * asoilmoist + 0.08 * raw;

  // Leitura sensores a cada ~5 segundos (exemplo)
  static unsigned long lastRead = 0;
  if (millis() - lastRead > 5000) {
    lastRead = millis();

    float t = dht.readTemperature();
    float h = dht.readHumidity();

    float moistP = 100.0f - ((asoilmoist - 2000.0f) / (3344.0f - 2000.0f) * 100.0f);
    int mInt = constrain((int)round(moistP), 0, 100);

    addReading(mInt, t, h);   // ← adiciona ao histórico

    Serial.printf("Leitura: Solo %d%% | Temp %.1f°C | Umid %.0f%%\n", mInt, t, h);
  }
}