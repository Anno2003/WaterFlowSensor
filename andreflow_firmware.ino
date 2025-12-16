#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <AsyncMqtt_Generic.h>
#include <Preferences.h>
#include <ESPAsyncWebServer.h>
#include "RemoteDebug.h"

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

/* ===================== CONFIG ===================== */
#define DEVICE_NAME "FLOW_SENSOR"
#define FLOW_PIN 4

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define I2C_SDA 10
#define I2C_SCL 11

#define DEFAULT_MQTT_HOST "broker.emqx.io"
#define DEFAULT_MQTT_PORT "1883"
#define DEFAULT_MQTT_TOPIC "flow_sensor/value"

/* ===================== GLOBALS ===================== */
RemoteDebug Debug;
WiFiManager wm;
AsyncMqttClient mqttClient;
AsyncWebServer server(80);
Preferences prefs;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

/* ===== Flow ===== */
volatile uint32_t pulseCount = 0;
float lastFlowLmin = 0.0;
float totalLiters = 0.0;

/* ===== Calibration ===== */
float calibrationFactor = 1.0f;  // ★

/* ===== Timing ===== */
unsigned long lastMeasure = 0;
const unsigned long measureInterval = 100;

/* ===== MQTT ===== */
bool connectedMQTT = false;

/* ===== MQTT config ===== */
char mqtt_host[40];
char mqtt_port[6];
char mqtt_topic[64];

/* ===== Calibration config ===== */
char cal_factor_str[12];  // ★

/* ===== WiFiManager display ===== */
char wmFlowText[32];
char wmTotalText[32];

WiFiManagerParameter wm_header(
  "<h2>FLOW SENSOR</h2>"
  "<p>Local monitoring (no internet required)</p>");

WiFiManagerParameter wm_flow("flow", "Flow", wmFlowText, 32, "readonly");
WiFiManagerParameter wm_total("total", "Total", wmTotalText, 32, "readonly");

WiFiManagerParameter p_mqtt_host("mqtt_host", "MQTT Host", mqtt_host, 40);
WiFiManagerParameter p_mqtt_port("mqtt_port", "MQTT Port", mqtt_port, 6);
WiFiManagerParameter p_mqtt_topic("mqtt_topic", "MQTT Topic", mqtt_topic, 64);
WiFiManagerParameter p_cal_factor("cal", "Calibration Factor", cal_factor_str, 12);  // ★

/* ===================== ISR ===================== */
void IRAM_ATTR flowISR() {
  pulseCount++;
}

/* ===================== STORAGE ===================== */
void loadConfig() {
  prefs.begin("flow", true);

  strlcpy(mqtt_host,
          prefs.getString("host", DEFAULT_MQTT_HOST).c_str(),
          sizeof(mqtt_host));
  strlcpy(mqtt_port,
          prefs.getString("port", DEFAULT_MQTT_PORT).c_str(),
          sizeof(mqtt_port));
  strlcpy(mqtt_topic,
          prefs.getString("topic", DEFAULT_MQTT_TOPIC).c_str(),
          sizeof(mqtt_topic));

  totalLiters = prefs.getFloat("total", 0.0);
  calibrationFactor = prefs.getFloat("cal", 1.0f);   // ★
  dtostrf(calibrationFactor, 1, 6, cal_factor_str);  // ★

  prefs.end();
}

void saveConfig() {
  prefs.begin("flow", false);
  prefs.putString("host", mqtt_host);
  prefs.putString("port", mqtt_port);
  prefs.putString("topic", mqtt_topic);
  prefs.putFloat("total", totalLiters);
  prefs.putFloat("cal", calibrationFactor);  // ★
  prefs.end();
}

/* ===================== MQTT ===================== */
void onMqttConnect(bool) {
  connectedMQTT = true;
  mqttClient.publish(mqtt_topic, 0, true, "FLOW_SENSOR online");
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason) {
  connectedMQTT = false;
}

/* ===================== WEB ===================== */
void setupWeb() {

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
    String html =
      "<html><head>"
      "<meta http-equiv='refresh' content='1'>"
      "<style>"
      "body{background:#111;color:#eee;font-family:monospace;}"
      "h1{color:#0f0;}"
      "button{padding:10px;font-size:16px;}"
      "</style></head><body>"
      "<h1>FLOW SENSOR</h1>"
      "<p>Flow: "
      + String(lastFlowLmin, 2) + " L/min</p>"
                                  "<p>Total: "
      + String(totalLiters, 3) + " L</p>"
                                 "<p>Cal: "
      + String(calibrationFactor, 6) + "</p>"  // ★
                                       "<form action='/reset' method='post'>"
                                       "<button>Reset Total</button>"
                                       "</form>"
                                       "<p><a href='/settings'>Settings</a></p>"
                                       "</body></html>";

    req->send(200, "text/html", html);
  });

  server.on("/settings", HTTP_GET, [](AsyncWebServerRequest *req) {
    String html =
      "<html><head><style>"
      "body{background:#111;color:#eee;font-family:monospace;}"
      "input{width:100%;padding:8px;font-size:14px;}"
      "button{padding:10px;margin-top:10px;font-size:16px;}"
      "</style></head><body>"
      "<h1>Settings</h1>"

      "<form method='POST' action='/settings'>"
      "MQTT Host:<br>"
      "<input name='host' value='"
      + String(mqtt_host) + "'><br>"
                            "MQTT Port:<br>"
                            "<input name='port' value='"
      + String(mqtt_port) + "'><br>"
                            "MQTT Topic:<br>"
                            "<input name='topic' value='"
      + String(mqtt_topic) + "'><br>"
                             "<button type='submit'>Save</button>"
                             "</form>"

                             "<p><a href='/'>Back</a></p>"
                             "</body></html>";

    req->send(200, "text/html", html);
  });

  server.on("/settings", HTTP_POST, [](AsyncWebServerRequest *req) {
    if (req->hasParam("host", true))
      strlcpy(mqtt_host, req->getParam("host", true)->value().c_str(), sizeof(mqtt_host));

    if (req->hasParam("port", true))
      strlcpy(mqtt_port, req->getParam("port", true)->value().c_str(), sizeof(mqtt_port));

    if (req->hasParam("topic", true))
      strlcpy(mqtt_topic, req->getParam("topic", true)->value().c_str(), sizeof(mqtt_topic));

    saveConfig();

    connectedMQTT = false;
    mqttClient.disconnect();

    req->redirect("/");
  });


  server.on("/reset", HTTP_POST, [](AsyncWebServerRequest *req) {
    totalLiters = 0.0;
    saveConfig();
    req->redirect("/");
  });

  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *req) {
    String json = "{";
    json += "\"flow\":" + String(lastFlowLmin, 2) + ",";
    json += "\"total\":" + String(totalLiters, 3) + ",";
    json += "\"cal\":" + String(calibrationFactor, 6);
    json += "}";
    req->send(200, "application/json", json);
  });

  server.begin();
}

/* ===================== SETUP ===================== */
void setup() {
  Serial.begin(115200);
  loadConfig();

  Wire.begin(I2C_SDA, I2C_SCL);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3c);

  pinMode(FLOW_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(FLOW_PIN), flowISR, FALLING);

  wm.setConfigPortalBlocking(false);
  wm.setShowInfoUpdate(true);

  wm.addParameter(&wm_header);
  wm.addParameter(&wm_flow);
  wm.addParameter(&wm_total);
  wm.addParameter(&p_mqtt_host);
  wm.addParameter(&p_mqtt_port);
  wm.addParameter(&p_mqtt_topic);
  wm.addParameter(&p_cal_factor);  // ★

  WiFi.mode(WIFI_STA);
  wm.autoConnect(DEVICE_NAME);

  calibrationFactor = atof(cal_factor_str);  // ★

  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);

  Debug.begin(DEVICE_NAME);
  Debug.setResetCmdEnabled(true);
  Debug.showProfiler(true);
  Debug.showColors(true);
  Debug.setSerialEnabled(true);

  setupWeb();
}

/* ===================== LOOP ===================== */
void loop() {
  Debug.handle();
  wm.process();

  unsigned long now = millis();
  if (now - lastMeasure >= measureInterval) {
    lastMeasure = now;

    noInterrupts();
    uint32_t pulses = pulseCount;
    pulseCount = 0;
    interrupts();

    float pulsesCal = pulses * calibrationFactor;  // ★

    lastFlowLmin = (pulsesCal * (1000.0 / measureInterval)) / 7.5;
    totalLiters += pulsesCal / 7.5;

    snprintf(wmFlowText, sizeof(wmFlowText), "%.2f L/min", lastFlowLmin);
    snprintf(wmTotalText, sizeof(wmTotalText), "%.3f L", totalLiters);

    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(0, 0);
    display.print(lastFlowLmin, 2);
    display.println(" L/m");
    display.setTextSize(1);
    display.setCursor(0, 40);
    display.print("Total: ");
    display.print(totalLiters, 2);
    display.display();
    char buf[128];
    sprintf(buf, "{\"rate\":%.3f,\"total\":%.3f}", lastFlowLmin, totalLiters);
    Debug.printf("%s\n", buf);

    if (connectedMQTT) {
      mqttClient.publish(mqtt_topic, 0, true, buf);
    }
  }

  if (WiFi.status() == WL_CONNECTED && !connectedMQTT) {
    mqttClient.setServer(mqtt_host, atoi(mqtt_port));
    mqttClient.connect();
  }

  static unsigned long lastSave = 0;
  if (millis() - lastSave > 5000) {
    lastSave = millis();
    saveConfig();
  }

  yield();
}
