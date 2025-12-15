#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiManager.h>   // tzapu WiFiManager
#include <AsyncMqtt_Generic.h>
#include <WebSerial.h>
#include <ArduinoJson.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

RemoteDebug Debug;
WiFiManager wm;
const char *DEVICE_NAME = "BMN";
int timeout = 120;  // seconds for wifimanager to run for

// -------------------- Display Config --------------------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET   -1
#define I2C_SDA 10
#define I2C_SCL 11

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// -------------------- Flow Sensor --------------------
#define FLOW_PIN 4
volatile uint32_t pulseCount = 0;

void IRAM_ATTR flowISR() {
    pulseCount++;
}
// global flow value
float lastFlowLmin = 0.0;

// -------------------- Timing --------------------
unsigned long lastMeasure = 0;
const unsigned long measureInterval = 100;  // ms

// -------------------- Web Server --------------------
WebServer server(80);



// -------------------- Web UI HTML --------------------
const char* INDEX_HTML PROGMEM = R"HTML(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<title>Flow Dashboard</title>
<style>
body { font-family: Arial; padding: 20px; }
card { padding: 20px; margin-top: 20px; border-radius: 10px; background: #f0f0f0; }
</style>
<script>
async function refresh() {
  let r = await fetch("/flow");
  let v = await r.text();
  document.getElementById("flow").innerHTML = v + " L/min";
}
setInterval(refresh, 100);
window.onload = refresh;
</script>
</head>
<body>
<h2>Flow Sensor Dashboard</h2>
<card>
<h3>Current Flow:</h3>
<div id="flow" style="font-size:2em;color:#007700;">...</div>
</card>
</body>
</html>
)HTML";

// -------------------- Web Handlers --------------------
void handleRoot() {
    server.send(200, "text/html", INDEX_HTML);
}

void handleFlow() {
    server.send(200, "text/plain", String(lastFlowLmin, 2));
}

// -------------------- Setup --------------------
void setup() {
    Serial.begin(115200);

    // Display
    Wire.begin(I2C_SDA, I2C_SCL);
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println("SSD1306 failed");
        while (1) delay(1000);
    }
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0,0);
    display.println("Boot...");
    display.display();

    // Flow sensor
    pinMode(FLOW_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(FLOW_PIN), flowISR, FALLING);

    display.clearDisplay();
    display.setCursor(0,0);
    display.println("AP Mode");
    display.setTextSize(1);
    display.setCursor(0,20);
    display.println("Connect:");
    display.println("SSID: AutoConfigAP");
    display.display();

    bool res;
    WiFi.mode(WIFI_STA);
    wm.setConfigPortalTimeout(timeout);
    wm.setConfigPortalBlocking(true);
    wm.setShowInfoUpdate(true);
    res = wm.autoConnect(DEVICE_NAME); 

    if (!res) {
        Serial.println("Failed to connect");
        ESP.restart();
    }

    Serial.println("WiFi connected!");
    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(0,0);
    display.println("Online");
    display.setTextSize(1);
    display.setCursor(0,30);
    display.print("IP: ");
    display.println(WiFi.localIP());
    display.display();

    // ------------- Start Web Server -------------
    server.on("/", handleRoot);
    server.on("/flow", handleFlow);
    server.begin();
}

// -------------------- Loop --------------------
void loop() {
    server.handleClient();

    unsigned long now = millis();

    if (now - lastMeasure >= measureInterval) {
        lastMeasure = now;

        // Copy pulse count safely
        noInterrupts();
        uint32_t pulses = pulseCount;
        pulseCount = 0;
        interrupts();

        // Convert pulses → flow
        float freq = pulses * (1000.0 / measureInterval);
        float flowL_min = freq / 7.5; 
        lastFlowLmin = flowL_min;

        // Debug
        Serial.printf("Flow: %.2f L/min (pulses=%u)\n", flowL_min, pulses);

        // Update OLED
        display.clearDisplay();
        display.setTextSize(2);
        display.setCursor(0,0);
        display.print(flowL_min, 2);
        display.println(" L/m");
        display.setTextSize(1);
        display.setCursor(0,40);
        display.print("IP: ");
        display.println(WiFi.localIP());
        display.display();
    }
}
