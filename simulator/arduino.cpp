#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>

// ----- WiFi Credentials -----
const char* ssid = "OPPO A57";
const char* password = "iyyappan@3283";

// ----- MQTT Broker Settings -----
const char* mqttServer = "test.mosquitto.org";
const int mqttPort = 1883;
const char* mqttClientId = "ESP32Client123"; // Unique Client ID

// Topics
const char* moistureTopic = "iot/field/moisture";
const char* humidityTopic = "iot/field/humidity";
const char* pumpStatusTopic = "iot/field/pump";
const char* pumpControlTopic = "iot/field/pump/control"; // new topic to listen from server

WiFiClient espClient;
PubSubClient client(espClient);

// ----- Pin Definitions -----
#define SOIL_MOISTURE_PIN 34
#define DHTPIN 4
#define RELAY_PIN 23

// ----- Sensor Settings -----
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// ----- Thresholds -----
const int moistureThresholdLow = 2000;
const int moistureThresholdHigh = 600;
const int moistureMinValue = 200;

// Manual control variables
bool manualOverride = false;
bool manualPumpState = false; // true = ON, false = OFF

void setup() {
  Serial.begin(115200);
  delay(2000);

  dht.begin();
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH); // Pump OFF initially

  setupWiFi();
  
  client.setServer(mqttServer, mqttPort);
  client.setCallback(mqttCallback); // set MQTT callback
}

void setupWiFi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi Connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
}

void reconnectMQTT() {
  while (!client.connected()) {
    Serial.print("Connecting to MQTT Broker...");
    if (client.connect(mqttClientId)) {
      Serial.println("Connected to MQTT!");
      client.subscribe(pumpControlTopic); // subscribe to pump control topic
      Serial.println("Subscribed to pump control topic!");
    } else {
      Serial.print("Failed (Error ");
      Serial.print(client.state());
      Serial.println("). Retrying in 5 seconds...");
      delay(5000);
    }
  }
}

// MQTT Callback
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  payload[length] = '\0'; // Make payload a proper string
  String message = String((char*)payload);

  Serial.print("Received message on topic ");
  Serial.print(topic);
  Serial.print(": ");
  Serial.println(message);

  if (String(topic) == pumpControlTopic) {
    if (message == "ON") {
      manualOverride = true;
      manualPumpState = true;
      digitalWrite(RELAY_PIN, LOW); // Pump ON
      Serial.println("Manual override: Pump ON");
    } else if (message == "OFF") {
      manualOverride = true;
      manualPumpState = false;
      digitalWrite(RELAY_PIN, HIGH); // Pump OFF
      Serial.println("Manual override: Pump OFF");
    } else if (message == "DEFAULT") {
      manualOverride = false;
      Serial.println("Manual override disabled. Back to automatic control.");
    }
  }
}

void publishData() {
  int soilMoisture = analogRead(SOIL_MOISTURE_PIN);
  float humidityValue = dht.readHumidity(); // keep it float
  float temperature = dht.readTemperature(); 

  if (isnan(humidityValue) || isnan(temperature)) {
    Serial.println("Failed to read from DHT sensor!");
    return;
  }

  Serial.print("Soil Moisture: ");
  Serial.print(soilMoisture);
  Serial.print(" | Humidity: ");
  Serial.print(humidityValue);
  Serial.print("% | Temperature: ");
  Serial.print(temperature);
  Serial.println("°C");

  // Pump Control Logic
  if (!manualOverride) { // Only auto-control if not overridden
    if (soilMoisture > moistureThresholdLow) {
      digitalWrite(RELAY_PIN, LOW); // Pump ON
      Serial.println("Automatic: Soil is Dry -> Pump ON");
    } else if (soilMoisture >= moistureMinValue && soilMoisture <= moistureThresholdHigh) {
      digitalWrite(RELAY_PIN, HIGH); // Pump OFF
      Serial.println("Automatic: Soil Moisture Good -> Pump OFF");
    }
  }

  // Publish moisture and humidity
  char moistureStr[10];
  char humidityStr[10];
  char pumpStatusStr[5];

  itoa(soilMoisture, moistureStr, 10);
  dtostrf(humidityValue, 6, 2, humidityStr);

  // Check pump status
  if (digitalRead(RELAY_PIN) == LOW) {
    strcpy(pumpStatusStr, "ON");
  } else {
    strcpy(pumpStatusStr, "OFF");
  }

  bool moistureSent = client.publish(moistureTopic, moistureStr);
  bool humiditySent = client.publish(humidityTopic, humidityStr);
  bool pumpSent = client.publish(pumpStatusTopic, pumpStatusStr);

  if (moistureSent && humiditySent && pumpSent) {
    Serial.println("Successfully published data to MQTT Broker!");
  } else {
    Serial.println("Failed to publish data to MQTT Broker!");
  }

  Serial.println("Data Published Successfully!\n");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    setupWiFi(); // Reconnect Wi-Fi if disconnected
  }

  if (!client.connected()) {
    reconnectMQTT(); // Reconnect MQTT if disconnected
  }

  client.loop(); // Handle incoming MQTT messages

  static unsigned long lastMsg = 0;
  unsigned long now = millis();

  if (now - lastMsg > 5000) { // Send every 5 seconds
    lastMsg = now;
    publishData();
  }
}