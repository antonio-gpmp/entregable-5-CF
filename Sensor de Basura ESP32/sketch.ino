#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

#define TRIG_PIN 5
#define ECHO_PIN 18
#define LED 2
#define TOPIC_CONTAINER "/contenedores/estado"

float duration_us, distance_cm;
boolean message;
float usedCapacity;
SemaphoreHandle_t xMutex;

const char* ssid = "Wokwi-GUEST";
const char* password = "";

const char* mqtt_server = "942075f3b1b14c5d96569228d55c3682.s1.eu.hivemq.cloud";
const int mqtt_port = 8883;
const char* mqtt_user = "hivemq.webclient.1737116538886";
const char* mqtt_password = "d:RThu0SC$sml#51B9.A";

WiFiClientSecure wifiClient;
PubSubClient client(wifiClient);

void vBinControlTask(void* pvParam) {
  for (;;) {
    if (xSemaphoreTake(xMutex, portMAX_DELAY)) {
      digitalWrite(TRIG_PIN, HIGH);
      delayMicroseconds(10);
      digitalWrite(TRIG_PIN, LOW);

      duration_us = pulseIn(ECHO_PIN, HIGH);
      distance_cm = 0.017 * duration_us;

      if (distance_cm > 100) {
        usedCapacity = 0.0; // Contenedor vacío
      } else {
        usedCapacity = (100 - distance_cm) / 100;
        if (usedCapacity > 0.85) {
          digitalWrite(LED, HIGH);
          if (!message) {
            Serial.println("Bin at least at 85% of its total capacity");
            message = true;
          }
        } else {
          digitalWrite(LED, LOW);
          message = false;
        }
      }
       usedCapacity = 100 *  usedCapacity;

      if (client.connected()) {
        DynamicJsonDocument jsonPayload(256);
        jsonPayload["capacidad_usada"] = usedCapacity;
        char bufferPayload[256];
        serializeJson(jsonPayload, bufferPayload);
        client.publish(TOPIC_CONTAINER, bufferPayload);
      } else {
        Serial.println("MQTT disconnected. Reconnecting...");
        mqttConnect();
      }

      xSemaphoreGive(xMutex);
    }
    vTaskDelay(10000 / portTICK_PERIOD_MS); // 10 segundos
  }
}

void wifiConnect() {
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
}

void mqttConnect() {
  client.setServer(mqtt_server, mqtt_port);
  wifiClient.setInsecure();

  while (!client.connected()) {
    Serial.print("Connecting to MQTT...");
    if (client.connect("ESP32_Client", mqtt_user, mqtt_password)) {
      Serial.println("Connected to MQTT!");
      client.subscribe(TOPIC_CONTAINER);
    } else {
      Serial.print("MQTT connection failed. State: ");
      Serial.println(client.state());
      delay(2000);
    }
  }
}

void setup() {
  Serial.begin(9600);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(LED, OUTPUT);

  wifiConnect();
  mqttConnect();

  xMutex = xSemaphoreCreateMutex();
  if (xMutex != NULL) {
    xTaskCreate(vBinControlTask, "BinControl", 4096, NULL, 1, NULL);
  }
}

void loop() {
  // El bucle principal no hace nada
}
