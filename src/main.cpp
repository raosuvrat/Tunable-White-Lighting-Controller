#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <PubSubClient.h>

#if __has_include("secrets.h")
#include "secrets.h"
#else
#define WIFI_SSID "ssid"
#define WIFI_PSK "password"
#define MQTT_SERVER "10.0.0.10"
#define MQTT_PORT 1883
#define MQTT_USER "username"
#define MQTT_PASS "password"
#endif

#define COOL_LIGHT_PIN D7
#define WARM_LIGHT_PIN D8

#define HOSTNAME "office_lights"

#define MQTT_CONFIG_TOPIC "homeassistant/light/" HOSTNAME "/config"
#define MQTT_STATE_TOPIC "homeassistant/light/" HOSTNAME "/state"
#define MQTT_COMMAND_TOPIC "homeassistant/light/" HOSTNAME "/set"
#define MQTT_BUFFER_SIZE JSON_OBJECT_SIZE(20)

#define MIN_TEMP_K 2000
#define MAX_TEMP_K 6535
#define CONVERT_TEMP(t) (1000000 / t)

void ota_setup();
void wifi_setup();
void mqtt_setup();
void mqtt_reconnect();
void publish_state();
void update_light();
void mqtt_callback(char *topic, byte *payload, unsigned int length);

WiFiClient wifi_client;
PubSubClient mqtt_client(MQTT_SERVER, MQTT_PORT, mqtt_callback, wifi_client);

bool state_on = true;
int brightness = 255, temperature = (MIN_TEMP_K + MAX_TEMP_K) / 2;
char buffer[MQTT_BUFFER_SIZE];
StaticJsonDocument<MQTT_BUFFER_SIZE> doc;

void setup() {
  Serial.begin(115200);
  pinMode(D7, OUTPUT);
  pinMode(D8, OUTPUT);

  wifi_setup();
  ota_setup();
  mqtt_setup();

  update_light();
  publish_state();
}

void loop() {
  ArduinoOTA.handle();

  if (!mqtt_client.connected()) {
    mqtt_reconnect();
  }
  mqtt_client.loop();
}

void ota_setup() {
  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.onStart([]() { Serial.println("OTA Start"); });
  ArduinoOTA.onEnd([]() { Serial.println("\nOTA End"); });
  ArduinoOTA.begin();
  Serial.println("OTA ready");
}

void wifi_setup() {
  Serial.printf("\nConnecting to %s\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PSK);

  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
    Serial.println(".");
  }
  Serial.printf("\nWiFi connected. IP address: %s\n",
                WiFi.localIP().toString().c_str());

  if (!MDNS.begin(HOSTNAME)) {
    Serial.println("MDNS responder failed to init");
  } else {
    Serial.printf("mDNS responder OK. Name: %s\n", HOSTNAME);
  }
}

void mqtt_reconnect() {
  while (!mqtt_client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (mqtt_client.connect(HOSTNAME, MQTT_USER, MQTT_PASS)) {
      Serial.println("connected");
      mqtt_client.subscribe(MQTT_COMMAND_TOPIC);
    } else {
      Serial.print("failed, rc=");
      Serial.println(mqtt_client.state());
      delay(5000);
    }
  }
}

void mqtt_setup() {
  mqtt_client.setBufferSize(MQTT_BUFFER_SIZE);
  mqtt_reconnect();

  doc.clear();

  doc["name"] = HOSTNAME;
  doc["unique_id"] = HOSTNAME;
  doc["state_topic"] = MQTT_STATE_TOPIC;
  doc["command_topic"] = MQTT_COMMAND_TOPIC;
  doc["schema"] = "json";
  doc["brightness"] = true;
  doc["color_temp"] = true;

  serializeJson(doc, buffer);
  Serial.printf("SEND [%s]: %s\n", MQTT_CONFIG_TOPIC, buffer);
  mqtt_client.publish(MQTT_CONFIG_TOPIC, buffer, true);
}

void publish_state() {
  doc.clear();

  doc["state"] = (state_on) ? "ON" : "OFF";
  doc["brightness"] = brightness;
  doc["color_temp"] = CONVERT_TEMP(temperature);

  serializeJson(doc, buffer);
  Serial.printf("SEND [%s]: %s\n", MQTT_STATE_TOPIC, buffer);
  mqtt_client.publish(MQTT_STATE_TOPIC, buffer, true);
}

void update_light() {
  if (state_on) {
    int coolness, warmness;
    if (temperature > (MIN_TEMP_K + MAX_TEMP_K) / 2) {
      coolness = 255;
      warmness =
          map(temperature, (MIN_TEMP_K + MAX_TEMP_K) / 2, MAX_TEMP_K, 255, 0);
    } else {
      warmness = 255;
      coolness =
          map(temperature, MIN_TEMP_K, (MIN_TEMP_K + MAX_TEMP_K) / 2, 0, 255);
    }
    analogWrite(COOL_LIGHT_PIN, 255 - coolness);
    analogWrite(WARM_LIGHT_PIN, 255 - warmness);
  } else {
    analogWrite(COOL_LIGHT_PIN, 255);
    analogWrite(WARM_LIGHT_PIN, 255);
  }
}

void mqtt_callback(char *topic, byte *payload, unsigned int length) {
  Serial.printf("RECV [%s]: ", topic);
  char message[length + 1];
  for (int i = 0; i < (int)length; ++i) {
    message[i] = (char)payload[i];
  }
  message[length] = '\0';
  Serial.println(message);

  if (!strcmp(topic, MQTT_COMMAND_TOPIC)) {
    DeserializationError error = deserializeJson(doc, message);
    if (error) {
      Serial.print("deserializeJson() failed: ");
      Serial.println(error.c_str());
      return;
    }

    if (doc.containsKey("state")) {
      state_on = !strcmp(doc["state"], "ON");
    }
    if (doc.containsKey("brightness")) {
      brightness = doc["brightness"];
    }
    if (doc.containsKey("color_temp")) {
      temperature = CONVERT_TEMP(doc["color_temp"].as<int>());
    }
  }

  update_light();
  publish_state();
}
