#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h> 


// Configuración de OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Pines de los sensores hc-sr04
#define TRIG_PIN 5
#define ECHO_PIN 18
#define TRIG_PIN_2 12
#define ECHO_PIN_2 13

// Pines de LEDs
#define LED_SENSOR_GREEN 17
#define LED_SENSOR_RED 16
#define LED_SENSOR_GREEN_2 26
#define LED_SENSOR_RED_2 25
#define LED_GENERAL_GREEN 23
#define LED_GENERAL_RED 19

// Variables globales
volatile long duration_1 = 0, duration_2 = 0;
volatile bool isSpotOccupied_1 = false, isSpotOccupied_2 = false;

// Variables para medir tiempo
volatile unsigned long startTime_1 = 0, startTime_2 = 0;

const char* ssid = "Wokwi-GUEST";
const char* password =  "";

#define TOPIC_PARKING "/sensor/parking"

// Configuración del servidor MQTT
const char* mqtt_server = "942075f3b1b14c5d96569228d55c3682.s1.eu.hivemq.cloud";
const int mqtt_port = 8883;  // Puerto para conexión segura
const char* mqtt_user = "hivemq.webclient.1737116538886";
const char* mqtt_password = "d:RThu0SC$sml#51B9.A";

WiFiClientSecure wifiClient;
PubSubClient client(wifiClient);

// ISR para el primer sensor
void IRAM_ATTR handleEcho1() {
  if (digitalRead(ECHO_PIN) == HIGH) {
    startTime_1 = micros();
  } else {
    duration_1 = micros() - startTime_1;
  }
}

// ISR para el segundo sensor
void IRAM_ATTR handleEcho2() {
  if (digitalRead(ECHO_PIN_2) == HIGH) {
    startTime_2 = micros();
  } else {
    duration_2 = micros() - startTime_2;
  }
}

// Función para enviar pulso de disparo
void triggerSensor(int trigPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
}

// Actualiza el estado de los LEDs del primer sensor
void updateSensor1LEDs() {
  if (isSpotOccupied_1) {
    digitalWrite(LED_SENSOR_GREEN, LOW);
    digitalWrite(LED_SENSOR_RED, HIGH);
  } else {
    digitalWrite(LED_SENSOR_GREEN, HIGH);
    digitalWrite(LED_SENSOR_RED, LOW);
  }
}

// Actualiza el estado de los LEDs del segundo sensor
void updateSensor2LEDs() {
  if (isSpotOccupied_2) {
    digitalWrite(LED_SENSOR_GREEN_2, LOW);
    digitalWrite(LED_SENSOR_RED_2, HIGH);
  } else {
    digitalWrite(LED_SENSOR_GREEN_2, HIGH);
    digitalWrite(LED_SENSOR_RED_2, LOW);
  }
}

// Actualiza el OLED con el estado del parking
void updateOLED() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("Parking Status:");
  display.setCursor(0, 10);
  display.print("Total: 2");
  display.setCursor(0, 20);
  display.print("Available: ");
  display.print((!isSpotOccupied_1 + !isSpotOccupied_2));
  display.setCursor(0, 30);
  display.print("Occupied: ");
  display.print(isSpotOccupied_1 + isSpotOccupied_2);
  display.display();
}

// Actualiza los LEDs generales
void updateGeneralLEDs() {
  if (isSpotOccupied_1 && isSpotOccupied_2) {
    digitalWrite(LED_GENERAL_GREEN, LOW);
    digitalWrite(LED_GENERAL_RED, HIGH);
  } else {
    digitalWrite(LED_GENERAL_GREEN, HIGH);
    digitalWrite(LED_GENERAL_RED, LOW);
  }
}

// Tarea para gestionar el primer sensor
void taskSensor1(void *pvParameters) {
  for (;;) {
    triggerSensor(TRIG_PIN);
    vTaskDelay(pdMS_TO_TICKS(100));
    long distance_1 = duration_1 * 0.034 / 2; // Convertir a cm
    isSpotOccupied_1 = (distance_1 < 100);
    updateSensor1LEDs();
  }
}

// Tarea para gestionar el segundo sensor
void taskSensor2(void *pvParameters) {
  for (;;) {
    triggerSensor(TRIG_PIN_2);
    vTaskDelay(pdMS_TO_TICKS(100));
    long distance_2 = duration_2 * 0.034 / 2; // Convertir a cm
    isSpotOccupied_2 = (distance_2 < 100);
    updateSensor2LEDs();
  }
}

// Tarea para actualizar el OLED
void taskOLED(void *pvParameters) {
  for (;;) {
    updateOLED();
    updateGeneralLEDs();
    vTaskDelay(pdMS_TO_TICKS(500));
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
  // Configurar la conexión al servidor MQTT
  client.setServer(mqtt_server, mqtt_port);

  // Configurar cliente seguro para evitar verificación de certificado
  wifiClient.setInsecure();

  while (!client.connected()) {
      Serial.print("Conectando a MQTT...");
      if (client.connect("ESP32_Client", mqtt_user, mqtt_password)) {
        Serial.println("Conectado!");
        client.subscribe(TOPIC_PARKING);
      } else {
        Serial.print("Error de conexión. Estado = ");
        Serial.println(client.state());
        delay(2000);
      }
  }
}

// Añadimos una tarea para publicar datos MQTT
void taskPublishMQTT(void *pvParameters) {
  for (;;) {
    if (client.connected()) {
      // Crear objetos JSON
      DynamicJsonDocument jsonPayload(256);

      // Asignar valores
      jsonPayload["occupied"] = isSpotOccupied_1 + isSpotOccupied_2;
      jsonPayload["available"] = 2 - (isSpotOccupied_1 + isSpotOccupied_2);

      // Convertir JSON a cadena
      char bufferPayload[256];
      serializeJson(jsonPayload, bufferPayload);

      client.publish(TOPIC_PARKING, bufferPayload);
      Serial.println("Datos publicados en formato JSON en un único topic:");
      Serial.println(bufferPayload);

    } else {
      Serial.println("Cliente MQTT desconectado. Intentando reconectar...");
      mqttConnect();
    }

    vTaskDelay(pdMS_TO_TICKS(5000)); // Publicar cada 5 segundos
  }
}

void setup() {
  Serial.begin(115200);

  // Configurar pines
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(TRIG_PIN_2, OUTPUT);
  pinMode(ECHO_PIN_2, INPUT);

  pinMode(LED_SENSOR_GREEN, OUTPUT);
  pinMode(LED_SENSOR_RED, OUTPUT);
  pinMode(LED_SENSOR_GREEN_2, OUTPUT);
  pinMode(LED_SENSOR_RED_2, OUTPUT);
  pinMode(LED_GENERAL_GREEN, OUTPUT);
  pinMode(LED_GENERAL_RED, OUTPUT);

  digitalWrite(LED_SENSOR_GREEN, LOW);
  digitalWrite(LED_SENSOR_RED, LOW);
  digitalWrite(LED_SENSOR_GREEN_2, LOW);
  digitalWrite(LED_SENSOR_RED_2, LOW);
  digitalWrite(LED_GENERAL_GREEN, LOW);
  digitalWrite(LED_GENERAL_RED, LOW);

  // Configurar interrupciones
  attachInterrupt(digitalPinToInterrupt(ECHO_PIN), handleEcho1, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ECHO_PIN_2), handleEcho2, CHANGE);

  wifiConnect();
  mqttConnect();

  // Inicializar OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 allocation failed. Check wiring!");
    while (true);
  }
  display.clearDisplay();
  display.display();

  // Crear tareas
  xTaskCreatePinnedToCore(taskSensor1, "Sensor1", 4096, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(taskSensor2, "Sensor2", 4096, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(taskOLED, "OLED", 4096, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(taskPublishMQTT, "PublishMQTT", 4096, NULL, 1, NULL, 1);
}

void loop() {
  
}
