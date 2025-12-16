#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <AsyncMqtt_Generic.h>
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

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

/* ===== Flow ===== */
volatile uint32_t pulseCount = 0;
float lastFlowLmin = 0.0;
float totalLiters = 0.0;

/* ===== Timing ===== */
unsigned long lastMeasure = 0;
const unsigned long measureInterval = 100;

/* ===== MQTT ===== */
bool connectedMQTT = false;

/* ===== WiFiManager parameters ===== */
char mqtt_host[40]  = DEFAULT_MQTT_HOST;
char mqtt_port[6]   = DEFAULT_MQTT_PORT;
char mqtt_topic[64] = DEFAULT_MQTT_TOPIC;

char wmFlowText[32];
char wmTotalText[32];

WiFiManagerParameter wm_header(
  "<h2>FLOW SENSOR</h2>"
  "<p>Local monitoring (no internet required)</p>"
);

WiFiManagerParameter wm_flow("flow", "Flow", wmFlowText, 32, "readonly");
WiFiManagerParameter wm_total("total", "Total", wmTotalText, 32, "readonly");

WiFiManagerParameter p_mqtt_host("mqtt_host", "MQTT Host", mqtt_host, 40);
WiFiManagerParameter p_mqtt_port("mqtt_port", "MQTT Port", mqtt_port, 6);
WiFiManagerParameter p_mqtt_topic("mqtt_topic", "MQTT Topic", mqtt_topic, 64);

/* ===================== ISR ===================== */
void IRAM_ATTR flowISR() {
  pulseCount++;
}

/* ===================== MQTT CALLBACKS ===================== */
void onMqttConnect(bool sessionPresent) {
  connectedMQTT = true;
  Debug.println("MQTT connected");

  mqttClient.publish(mqtt_topic, 0, true, "FLOW_SENSOR online");
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  connectedMQTT = false;
  Debug.println("MQTT disconnected");
}

/* ===================== SETUP ===================== */
void setup() {
  Serial.begin(115200);

  /* OLED */
  Wire.begin(I2C_SDA, I2C_SCL);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.println("Boot...");
  display.display();

  /* Flow sensor */
  pinMode(FLOW_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(FLOW_PIN), flowISR, FALLING);

  /* WiFiManager UI */
  wm.setConfigPortalBlocking(false);
  wm.setConfigPortalTimeout(120);
  wm.setShowInfoUpdate(true);

  wm.setCustomHeadElement(
    "<style>"
    "body{background:#111;color:#eee;font-family:monospace;}"
    "h2{color:#0f0;}"
    "label{color:#0ff;}"
    "</style>"
  );

  wm.addParameter(&wm_header);
  wm.addParameter(&wm_flow);
  wm.addParameter(&wm_total);

  wm.addParameter(&p_mqtt_host);
  wm.addParameter(&p_mqtt_port);
  wm.addParameter(&p_mqtt_topic);

  WiFi.mode(WIFI_STA);
  wm.autoConnect(DEVICE_NAME);

  /* RemoteDebug */
  Debug.begin(DEVICE_NAME);
  Debug.setSerialEnabled(true);
  Debug.setResetCmdEnabled(true);
  Debug.showProfiler(true);
  Debug.showColors(true);

  /* MQTT */
  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
}

/* ===================== LOOP ===================== */
void loop() {

  /* REQUIRED */
  wm.process();

  /* Flow measurement */
  unsigned long now = millis();
  if (now - lastMeasure >= measureInterval) {
    lastMeasure = now;

    noInterrupts();
    uint32_t pulses = pulseCount;
    pulseCount = 0;
    interrupts();

    float freq = pulses * (1000.0 / measureInterval);
    lastFlowLmin = freq / 7.5;

    /* pulse-based integration (recommended) */
    totalLiters += pulses / 7.5;

    /* Update WiFiManager text */
    snprintf(wmFlowText, sizeof(wmFlowText),
             "%.2f L/min", lastFlowLmin);

    snprintf(wmTotalText, sizeof(wmTotalText),
             "%.3f L", totalLiters);

    /* OLED */
    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(0, 0);
    display.print(lastFlowLmin, 2);
    display.println(" L/m");

    display.setTextSize(1);
    display.setCursor(0, 40);
    display.print("Total: ");
    display.print(totalLiters, 2);
    display.println(" L");

    display.display();

    Debug.printf("Flow: %.2f L/min | Total: %.3f L\n",
                 lastFlowLmin, totalLiters);
  }

  /* MQTT connect when WiFi is ready */
  if (WiFi.status() == WL_CONNECTED && !connectedMQTT) {
    mqttClient.setServer(mqtt_host, atoi(mqtt_port));
    mqttClient.connect();
  }

  /* MQTT publish */
  if (connectedMQTT) {
    static unsigned long lastPub = 0;
    if (millis() - lastPub > 1000) {
      lastPub = millis();
      char payload[64];
      snprintf(payload, sizeof(payload),
               "{\"flow\":%.2f,\"total\":%.3f}",
               lastFlowLmin, totalLiters);
      mqttClient.publish(mqtt_topic, 0, false, payload);
    }
  }
}
