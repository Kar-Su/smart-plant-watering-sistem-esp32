#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "config.hpp"

#define ENABLE_SOIL
#define ENABLE_PUMP
#define ENABLE_LIGHT

const char* SSID = _SSID;
const char* PASSWORD = _PASSWORD;

const char* SENSOR_URL = _SENSOR_URL;
const char* COMMAND_URL = _COMMAND_URL;

const int32_t SOIL_TRESHOLD = 1000;
const int32_t LIGHT_TRESHOLD = 1500;

const uint32_t WATER_TIME = 5000;
const uint32_t COOLDOWN_WATER = 60000;
uint32_t last_water = 0;

const uint32_t DATA_COOLDOWN = 5000;
const uint32_t COMMAND_COOLDOWN = 500;

uint32_t last_data = 0;
uint32_t last_command = 0;

bool auto_enable = false;

void setupAccessPoint() {
  Serial.print("Trying to create Access Point ");
  Serial.println(SSID);
  WiFi.mode(WIFI_AP);

  while (!WiFi.softAP(SSID, PASSWORD)) {
    Serial.print(".");
    delay(1000);
  }
  Serial.print("\nESP32 IP (AP): ");
  Serial.println(WiFi.softAPIP());
}

void waterNow() {
  #ifdef ENABLE_PUMP
    digitalWrite(_RELAY_PIN, LOW);
    Serial.println("WATER PUMP ON");
    delay(WATER_TIME);
    Serial.println("WATER PUMP OFF");
    digitalWrite(_RELAY_PIN, HIGH);
  #else
    Serial.println("WATER PUMP ON");
    delay(WATER_TIME);
    Serial.println("WATER PUMP OFF");
  #endif

  last_water = millis();
}

void sendData(int32_t soil, int32_t light, bool is_watering) {
  String payload = "{";
  payload += "\"soil\":" + String(soil) + ",";
  payload += "\"light\":" + String(light) + ",";
  payload += "\"is_watering\":" + String(is_watering ? "true" : "false");
  payload += "}";

  Serial.print("Send payload: ");
  Serial.println(payload);

  HTTPClient http;
  http.begin(SENSOR_URL);
  http.addHeader("Content-Type", "application/json");

  int http_code = http.POST(payload);

  if (http_code > 0) {
    String res = http.getString();
    Serial.printf("Get response code: %d, body: %s\n", http_code, res.c_str());
  } else {
    Serial.printf("POST failed, error: %s\n", http.errorToString(http_code).c_str());
  }

  http.end();
}

bool checkCommandWater() {
  HTTPClient http;
  http.begin(COMMAND_URL);
  int http_code = http.GET();

  if (http_code == 200) {
    String body = http.getString();
    Serial.print("Command body: ");
    Serial.println(body);

    StaticJsonDocument<128> doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
      Serial.print("JSON error: ");
      Serial.println(err.c_str());
      http.end();
      return false;
    }

    bool water_now = doc["water_now"] | false;

    if (doc.containsKey("auto_enabled")) {
      auto_enable = doc["auto_enabled"].as<bool>();
    }

    http.end();
    return water_now;
  } else {
    Serial.printf("GET /api/command failed: %s\n",
                  http.errorToString(http_code).c_str());
    http.end();
    return false;
  }
}

void setup() {
  Serial.begin(115200);
  #ifdef ENABLE_SOIL
    pinMode(_SOIL_PIN, INPUT);
  #endif
  #ifdef ENABLE_PUMP
    pinMode(_RELAY_PIN, OUTPUT);
    digitalWrite(_RELAY_PIN, HIGH);
  #endif
  #ifdef ENABLE_LIGHT
  #endif
  
  setupAccessPoint();
  delay(2000);
}

void loop() {
  Serial.println("---");
  #ifdef ENABLE_SOIL
    int32_t soil  = analogRead(_SOIL_PIN);
  #else
    int32_t soil = random(990, 1010);
  #endif
  

  #ifdef ENABLE_LIGHT
    int32_t light  = 0;
  #else
    int32_t light = random(1490, 1510);
  #endif
  

  bool water = false;
  uint32_t now = millis();

  if (auto_enable  &&
      soil < SOIL_TRESHOLD &&
      (now - last_water >= COOLDOWN_WATER)) {

    water = true;
    sendData(soil, light, water);
    Serial.println("AUTO:");
    waterNow();
  }

  if (now - last_command >= COMMAND_COOLDOWN) {
    last_command = now;
    if (checkCommandWater()) {
      water = true;
      sendData(soil, light, water);
      Serial.println("MANUAL:");
      waterNow();
    }
  }

  if (now - last_data >= DATA_COOLDOWN) {
    last_data = now;
    sendData(soil, light, water);
  }

  delay(1000);
}
