#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <BH1750.h>
#include "config.hpp"

bool enable_soil = true;
bool enable_light = true;
bool enable_pump = true;

BH1750 lightSensor;
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
  Serial.print("SSID: ");
  Serial.println(SSID);
}

void readSerialInput() {
  Serial.println("--- CONFIG SETUP ---");
  Serial.println("Type Feature You Want To Enable And Separate It With Comma (s,p,l,no)");
  Serial.print("> ");

  uint32_t start = millis();
  while (!Serial.available() && (millis() - start < 20000)) {
    delay(20);
  }
  
  if (!Serial.available()) {
    Serial.println("\nNo input. Using defaults.");
    return;
  }

  String line = Serial.readStringUntil('\n');
  line.trim();
  line.toUpperCase();

  enable_soil = false;
  enable_light = false;
  enable_pump = false;
  
  if (line.indexOf('S') >= 0) enable_soil = true;
  if (line.indexOf('P') >= 0) enable_pump = true;
  if (line.indexOf('L') >= 0) enable_light = true;

  Serial.println();
  Serial.println("Enabled:");
  Serial.printf("- Soil  : %s\n", enable_soil  ? "ON" : "OFF");
  Serial.printf("- Pump  : %s\n", enable_pump  ? "ON" : "OFF");
  Serial.printf("- Light : %s\n", enable_light ? "ON" : "OFF");
  Serial.println("--- END CONFIG ---");
}

void waterNow() {
  if (!enable_pump) {
    Serial.println("WATER PUMP (disabled) - simulate only");
    delay(WATER_TIME);
    last_water = millis();
    return;
  }

  digitalWrite(_RELAY_PIN, LOW);
  Serial.println("WATER PUMP ON");
  delay(WATER_TIME);
  Serial.println("WATER PUMP OFF");
  digitalWrite(_RELAY_PIN, HIGH);

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
  delay(5000);
  readSerialInput();

  if(enable_soil) {
    pinMode(_SOIL_PIN, INPUT);
  }

  if(enable_pump) {
    pinMode(_RELAY_PIN, OUTPUT);
    digitalWrite(_RELAY_PIN, HIGH);
  }
  
  if(enable_light) {
    Wire.begin(_SDA_PIN,_SCL_PIN);
    lightSensor.begin();
  }
  
  setupAccessPoint();
  delay(2000);
}

void loop() {
  Serial.println("---");

  int32_t soil;
  if (enable_soil) soil = analogRead(_SOIL_PIN);
  else soil = random(990, 1010);

  int32_t light;
  if (enable_light) light = (int32_t)lightSensor.readLightLevel();
  else light = random(1490, 1510);

  bool water = false;
  uint32_t now = millis();

  if (auto_enable &&
      enable_soil &&
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
