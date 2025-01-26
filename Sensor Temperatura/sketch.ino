#include "DHTesp.h"
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>  // Librería para cliente seguro
#include <PubSubClient.h>
#include <ArduinoJson.h>

#define DHT_PIN 15
#define BUTTON_PIN 5
#define LED_ENCENDIDO_PIN 4
#define POT_PIN 34
#define POT_DIOXIDO_PIN 35
#define LED_CALDERA_PIN 16
#define LED_PURIFICADOR_PIN 17
#define LED_AIRE_ACONDICIONADO_PIN 2
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
DHTesp dhtSensor;
TempAndHumidity data;
int valorGrados;
int nivelDioxido;
String mensajeDisplay;

enum Estado { APAGADO, ENCENDIDO, CALENTANDO, ENFRIANDO };
volatile Estado estadoActual = APAGADO;
unsigned long ultimoClick = 0;
const unsigned long evitarRebote = 200;

QueueHandle_t xBinarySemaphore;
QueueHandle_t xMutex;

const char* ssid = "Wokwi-GUEST";
const char* password =  "";
// Configuración del servidor MQTT
const char* mqtt_server = "942075f3b1b14c5d96569228d55c3682.s1.eu.hivemq.cloud";
const int mqtt_port = 8883;  // Puerto para conexión segura
const char* mqtt_user = "hivemq.webclient.1737116538886";
const char* mqtt_password = "d:RThu0SC$sml#51B9.A";

#define TOPIC_TEMPERATURA_CALIDAD "/sensor/temperaturaCalidad"


WiFiClientSecure wifiClient;  // Cliente seguro para SSL
PubSubClient client(wifiClient);

void IRAM_ATTR cambiarEstado() { // Función de interrupción para cambiar el estado
  if (millis() - ultimoClick > evitarRebote) {
    static portBASE_TYPE xHigherPriorityTaskWoken;
    xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR( xBinarySemaphore, &xHigherPriorityTaskWoken );
    ultimoClick = millis();
  }
}

TempAndHumidity leerTemperatura() { // Función para leer la temperatura y humedad del sensor
  return dhtSensor.getTempAndHumidity();
}

int leerPotenciometro() { // Función para leer el valor del potenciómetro y mapearlo a la temperatura deseada
  int valorPotenciometro = analogRead(POT_PIN);
  return map(valorPotenciometro, 0, 4095, 15, 30);
}

int leerPotenciometroDioxido() { // Función para leer el valor del potenciómetro y mapearlo a la temperatura deseada
  int valorPotenciometro = analogRead(POT_DIOXIDO_PIN);
  return map(valorPotenciometro, 0, 4095, 500, 1500);
}

void mostrarValorDeseadoAmbiente(int valorGrados, TempAndHumidity data, int nivelDioxido) {
  display.clearDisplay();
  display.setCursor(0, 0);
  mensajeDisplay = "Temp. deseada: " + String(valorGrados) + " C\nTemp. actual: " + String(data.temperature, 2) + " C\nPPM CO2:"+String(nivelDioxido)+ " PPM";
  display.println(mensajeDisplay);
  display.display();
}

void vTaskActualizarSensores(void* pvParam) {
  for (;;) {
    if (xSemaphoreTake(xMutex, portMAX_DELAY)) {
      data = leerTemperatura();
      valorGrados = leerPotenciometro();
      nivelDioxido = leerPotenciometroDioxido();
      xSemaphoreGive(xMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(250 / portTICK_RATE_MS));
  }
}

void vTaskActualizarLCD(void* pvParam) {
  for (;;) {
    if (estadoActual != APAGADO) {
      mostrarValorDeseadoAmbiente(valorGrados, data, nivelDioxido);
    } else {
      display.clearDisplay();
      display.display(); // Apagar pantalla
    }
    vTaskDelay(pdMS_TO_TICKS(250 / portTICK_RATE_MS ));
  }
}

void vTaskComprobarDioxido(void* pvParam) {
  static bool ledState = false; // Variable para almacenar el estado previo del LED
  
  for (;;) {
    if (estadoActual != APAGADO) {
      if (nivelDioxido > 900) {
        if (!ledState) { // Solo imprime si el LED estaba apagado
          digitalWrite(LED_PURIFICADOR_PIN, HIGH);
          Serial.println("Purificador de aire encendido al superar el umbral recomendado");
          ledState = true; // Actualiza el estado del LED
        }
      } else {
        if (ledState) { // Solo imprime si el LED estaba encendido
          digitalWrite(LED_PURIFICADOR_PIN, LOW);
          Serial.println("Purificador de aire apagado al recuperar umbrales recomendados");
          ledState = false; // Actualiza el estado del LED
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(500 / portTICK_RATE_MS));
  }
}

void vTaskCambiarEstado(void* pvParam) {
  // Ignorar interrupciones que ocurran en medio segundo tras iniciar
  const unsigned long tiempoIgnorarInicio = millis() + 500;

  for (;;) {
    xSemaphoreTake(xBinarySemaphore, portMAX_DELAY);

    // Verificación adicional para ignorar el primer evento si ocurre demasiado rápido
    if (millis() < tiempoIgnorarInicio) {
      continue;
    }

    if (xSemaphoreTake(xMutex, portMAX_DELAY)) {
      if (estadoActual == APAGADO) {
        estadoActual = ENCENDIDO;
        Serial.println("Termostato encendido");
      } else {
        estadoActual = APAGADO;
        Serial.println("Termostato apagado");
      }
      xSemaphoreGive(xMutex);
    }
  }
}

void vTaskControlEstado(void* pvParam) {
  for (;;) {
    if (xSemaphoreTake(xMutex, portMAX_DELAY)) {
      switch (estadoActual) {
        case APAGADO:
          digitalWrite(LED_ENCENDIDO_PIN, LOW);
          digitalWrite(LED_CALDERA_PIN, LOW);
          digitalWrite(LED_AIRE_ACONDICIONADO_PIN, LOW);
          digitalWrite(LED_PURIFICADOR_PIN, LOW);
          display.clearDisplay();
          display.display();
          break;

        case ENCENDIDO:
          digitalWrite(LED_ENCENDIDO_PIN, HIGH);

          if (valorGrados > data.temperature) {
            estadoActual = CALENTANDO;
            Serial.println("Caldera encendida");
          }
          else if (valorGrados < data.temperature) {
            estadoActual = ENFRIANDO;
            Serial.println("Aire acondicionado activado");
          }
          break;

        case CALENTANDO:
          digitalWrite(LED_CALDERA_PIN, HIGH);

          if (valorGrados <= data.temperature) {
            estadoActual = ENCENDIDO;
            digitalWrite(LED_CALDERA_PIN, LOW);
            Serial.println("Caldera apagada");
          }
          break;

        case ENFRIANDO:
          digitalWrite(LED_AIRE_ACONDICIONADO_PIN, HIGH);

          if (valorGrados >= data.temperature) {
            estadoActual = ENCENDIDO;
            digitalWrite(LED_AIRE_ACONDICIONADO_PIN, LOW);
            Serial.println("Aire acondicionado apagado");
          }
          break;
      }
      xSemaphoreGive(xMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(200 / portTICK_RATE_MS ));
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
        client.subscribe(TOPIC_TEMPERATURA_CALIDAD);  // Suscribirse a un tema
      } else {
        Serial.print("Error de conexión. Estado = ");
        Serial.println(client.state());
        delay(2000);
      }
  }
}
void vTaskPublishMQTT(void *pvParameters) {
  for (;;) {
    if (estadoActual != APAGADO) {
      if (client.connected()) {
        // Crear objetos JSON
        DynamicJsonDocument jsonPayload(256);
  
        // Asignar valores
        jsonPayload["temperatura"] = data.temperature;
        jsonPayload["calidadAire"] = nivelDioxido;
  
        // Convertir JSON a cadena
        char bufferPayload[256];
        serializeJson(jsonPayload, bufferPayload);
  
        // Publicar en los topics correspondientes
        client.publish(TOPIC_TEMPERATURA_CALIDAD, bufferPayload);
  
        // Imprimir para depuración
        Serial.println("Datos publicados en formato JSON en un único topic:");
        Serial.println(bufferPayload);
  
      } else {
        Serial.println("Cliente MQTT desconectado. Intentando reconectar...");
        mqttConnect(); // Reintentar conexión si es necesario
      }
  
      vTaskDelay(pdMS_TO_TICKS(5000)); // Publicar cada 5 segundos
    }
  }
}

void setup() {
  Serial.begin(115200);
  dhtSensor.setup(DHT_PIN, DHTesp::DHT22);
  pinMode(LED_ENCENDIDO_PIN, OUTPUT);
  pinMode(LED_CALDERA_PIN, OUTPUT);
  pinMode(LED_AIRE_ACONDICIONADO_PIN, OUTPUT);
  pinMode(LED_PURIFICADOR_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLDOWN);

  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), cambiarEstado, RISING);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;);
  }

  display.clearDisplay();
  display.setTextSize(1.3);
  display.setTextColor(WHITE);
  display.setCursor(1, 0);
  display.clearDisplay();

  xMutex = xSemaphoreCreateMutex();
  vSemaphoreCreateBinary( xBinarySemaphore );

  wifiConnect();
  mqttConnect();

  if ( xBinarySemaphore != NULL && xMutex != NULL) {
    // Crear tareas
    xTaskCreate(vTaskActualizarLCD, "ActualizarLCD", 2048, NULL, 1, NULL);
    xTaskCreate(vTaskControlEstado, "ControlEstado", 2048, NULL, 1, NULL);
    xTaskCreate(vTaskCambiarEstado, "CambiarEstado", 2048, NULL, 1, NULL);
    xTaskCreate(vTaskActualizarSensores, "ActualizarSensores", 2048, NULL, 1, NULL);
    xTaskCreate(vTaskComprobarDioxido, "Comprobar CO2",2048, NULL, 1, NULL);
    xTaskCreate(vTaskPublishMQTT, "PublishMQTT", 4096, NULL, 1, NULL); // Tarea para publicar MQTT
  }
}

void loop() {

}