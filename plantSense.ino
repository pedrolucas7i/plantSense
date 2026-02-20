/*
 * ------------------------------------------------------------
 *  ESP32 PlantSense
 *  Low-Power WiFi Soil & Environment Monitoring Node
 *
 *  Features:
 *   - Capacitive soil moisture sensor
 *   - DHT11 temperature & humidity sensor
 *   - Embedded HTTP server
 *   - mDNS support (plant.local)
 *   - CPU frequency reduced to 80 MHz
 *
 *  Power Optimization Strategy:
 *   - Sensors are read only on client request
 *   - WiFi MAX_MODEM power save enabled
 *   - No background polling
 * ------------------------------------------------------------
 */

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <DHT.h>
#include <esp_wifi.h>

// ─────────────────────────────────────────────
// Network Configuration
// ─────────────────────────────────────────────

const char* ssid     = "YOUR_SSID_HERE";
const char* password = "YOUR_PASSWORD_HERE";
const int   HTTP_PORT = 80;

WebServer server(HTTP_PORT);

// ─────────────────────────────────────────────
// Hardware Configuration
// ─────────────────────────────────────────────

#define LED_PIN   2
#define SOIL_PIN  32
#define DHT_PIN   22
#define DHT_TYPE  DHT11

DHT dht(DHT_PIN, DHT_TYPE);

// Smoothed soil ADC value
float soilFiltered = 0.0f;

// Soil calibration constants (adjust if needed)
constexpr float SOIL_MIN = 2000.0f;  // Sensor fully in water
constexpr float SOIL_MAX = 3344.0f;  // Sensor in dry air

// ─────────────────────────────────────────────
// Sensor Reading Routine
// Reads sensors only when explicitly requested
// ─────────────────────────────────────────────

void readSensors(int &soilPercent,
                 float &temperature,
                 float &humidity,
                 bool  &validReading)
{
    // Apply light exponential smoothing to soil ADC
    float raw = analogRead(SOIL_PIN);
    soilFiltered = 0.9f * soilFiltered + 0.1f * raw;

    float moisture =
        100.0f - ((soilFiltered - SOIL_MIN) /
                 (SOIL_MAX - SOIL_MIN) * 100.0f);

    moisture = constrain(moisture, 0.0f, 100.0f);
    soilPercent = static_cast<int>(round(moisture));

    humidity    = dht.readHumidity();
    temperature = dht.readTemperature();

    validReading = !isnan(humidity) && !isnan(temperature);
}

// ─────────────────────────────────────────────
// HTTP Root Handler (HTML UI)
// ─────────────────────────────────────────────

void handleRoot()
{
    digitalWrite(LED_PIN, HIGH);

    int soil;
    float temp, hum;
    bool ok;

    readSensors(soil, temp, hum, ok);

    String statusClass =
        (soil < 30) ? "danger" :
        (soil < 52) ? "warn"   : "good";

    String statusText =
        (soil < 30) ? "Needs Water" :
        (soil < 52) ? "Moderate"    : "Healthy";

    String ringColor =
        (soil < 30) ? "var(--ring-bad)" :
        (soil < 52) ? "var(--ring-warn)" :
                      "var(--ring-good)";

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
  html += "      <div class=\"gauge-value\" id=\"moistVal\">" + String(soil) + "%</div>\n    </div>\n\n";
  html += "    <div class=\"readings\">\n";
  html += "      <div class=\"reading\"><div class=\"reading-label\">Temperatura</div><div class=\"reading-value\" id=\"tempVal\">" + (ok ? String(temp, 1) : "–") + " °C</div></div>\n";
  html += "      <div class=\"reading\"><div class=\"reading-label\">Humidade</div><div class=\"reading-value\" id=\"humVal\">" + (ok ? String(hum, 0) : "–") + " %</div></div>\n";
  html += "    </div>\n\n";
  html += "    <div class=\"status " + statusClass + "\" id=\"status\">" + statusText + "</div>\n  </div>\n\n";
  html += "  <footer id=\"footer\">ESP32 • by Pedro Lucas</footer>\n\n";

  // JavaScript for dynamic updates and animations
  html += "  <script>\n";
  html += "    const radius = 78;\n";
  html += "    const circ = 2 * Math.PI * radius;\n\n";

  html += "    let current = {\n";
  html += "      moisture: " + String(soil) + ",\n";
  html += "      temp: \"" + (ok ? String(temp,1) : "–") + "\",\n";
  html += "      humidity: \"" + (ok ? String(hum,0) : "–") + "\",\n";
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

    digitalWrite(LED_PIN, LOW);
}

// ─────────────────────────────────────────────
// JSON Data Endpoint
// ─────────────────────────────────────────────

void handleCurrentData()
{
    int soil;
    float temp, hum;
    bool ok;

    readSensors(soil, temp, hum, ok);

    String statusClass =
        (soil < 30) ? "danger" :
        (soil < 52) ? "warn"   : "good";

    String statusText =
        (soil < 30) ? "Needs Water" :
        (soil < 52) ? "Moderate"    : "Healthy";

    String ringColor =
        (soil < 30) ? "var(--ring-bad)" :
        (soil < 52) ? "var(--ring-warn)" :
                      "var(--ring-good)";

    String json = "{";
    json += "\"moisture\":" + String(soil) + ",";
    json += "\"temp\":\"" + (ok ? String(temp,1) : "–") + "\",";
    json += "\"humidity\":\"" + (ok ? String(hum,0) : "–") + "\",";
    json += "\"statusClass\":\"" + statusClass + "\",";
    json += "\"statusText\":\"" + statusText + "\",";
    json += "\"ringColor\":\"" + ringColor + "\"";
    json += "}";

    server.send(200, "application/json", json);
}

void handleNotFound()
{
    server.send(404, "text/plain", "Not Found");
}

// ─────────────────────────────────────────────
// System Initialization
// ─────────────────────────────────────────────

void setup()
{
    Serial.begin(115200);
    delay(100);

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    pinMode(SOIL_PIN, INPUT);

    dht.begin();

    // Reduce CPU frequency to lower power consumption
    setCpuFrequencyMhz(80);

    // Connect to WiFi
    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi");

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(300);
        Serial.print(".");
    }

    Serial.println("\nConnected");
    Serial.println("IP Address: " + WiFi.localIP().toString());

    // Enable WiFi power save mode (MAX_MODEM)
    WiFi.setSleep(true);
    esp_wifi_set_ps(WIFI_PS_MAX_MODEM);

    // Start mDNS responder
    if (MDNS.begin("plant"))
    {
        Serial.println("mDNS responder started (http://plant.local)");
    }

    // Register HTTP routes
    server.on("/", HTTP_GET, handleRoot);
    server.on("/data", HTTP_GET, handleCurrentData);
    server.onNotFound(handleNotFound);

    server.begin();
}

// ─────────────────────────────────────────────
// Main Loop
// ─────────────────────────────────────────────

void loop()
{
    server.handleClient();
}
