#include <WiFi.h>
#include <PubSubClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <Preferences.h>

// Replace with your network credentials
const char* ssid = "NUEZ-SERVER-2";
const char* password = "smart@nuez";

// MQTT broker details
const char* mqtt_server = "192.168.123.250";  // Replace with your EMQX broker IP
const int mqtt_port = 1883;  // Default MQTT port
const char* mqtt_user = "nuez";  // Replace with your MQTT username
const char* mqtt_pass = "smart@nuez";  // Replace with your MQTT password

WiFiClient espClient;
PubSubClient client(espClient);
Preferences preferences;

void connect_wifi() {
  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  
  // Check if the topic is "intimate"
  if (String(topic) == "intimate") {
    DynamicJsonDocument doc(256);
    deserializeJson(doc, payload, length);
    String available_version = doc["version"].as<String>();

    Serial.print("Available version: ");
    Serial.println(available_version);

    // Fetch the current version from preferences
    preferences.begin("firmware", true);
    String curr_version = preferences.getString("version", "0.0");  // Default to "1.0" if not set
    preferences.end();

    Serial.print("Current firmware version: ");
    Serial.println(curr_version);

    // Compare versions
    if (available_version != curr_version) {
      Serial.println("New firmware available. Starting update...");
      if (downloadAndUpdateFirmware("http://192.168.123.250:4001/request")) {
        preferences.begin("firmware", false);
        preferences.putString("version", available_version);
        preferences.end();
        Serial.println("Firmware update successful. Rebooting...");
        ESP.restart();
      } else {
        Serial.println("Firmware update failed.");
      }
    }
  }
}

bool downloadAndUpdateFirmware(const char* url) {
  HTTPClient http;
  http.begin(url);

  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    int contentLength = http.getSize();
    bool canBegin = Update.begin(contentLength);

    if (canBegin) {
      WiFiClient* stream = http.getStreamPtr();
      size_t written = Update.writeStream(*stream);
      
      if (written == contentLength) {
        if (Update.end()) {
          if (Update.isFinished()) {
            Serial.println("Update successfully completed.");
            return true;
          } else {
            Serial.println("Update not finished? Something went wrong!");
          }
        } else {
          Serial.printf("Update failed. Error #: %d\n", Update.getError());
        }
      } else {
        Serial.printf("Written only %d/%d bytes. Retry?\n", written, contentLength);
      }
    } else {
      Serial.println("Not enough space to begin update");
    }
  } else {
    Serial.printf("HTTP GET failed. Error code: %d\n", httpCode);
  }
  
  http.end();
  return false;
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect("ESP32Client", mqtt_user, mqtt_pass)) {
      Serial.println("connected");
      // Subscribe to a topic
      client.subscribe("intimate");
      Serial.println("subscribed successfully");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  connect_wifi();
  
    preferences.begin("firmware", true);
    String curr_version = preferences.getString("version", "0.0");  // Default to "1.0" if not set
    preferences.end();
    Serial.println("current_version: ");
    Serial.println(curr_version);
    Serial.println("\nhello Priyansu");
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
}
