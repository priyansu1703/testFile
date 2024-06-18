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
const char* mqtt_username = "nuez";  // Replace with your MQTT username
const char* mqtt_password = "emqx@nuez";  // Replace with your MQTT password
const char* mqtt_main_topic = "water-consumption-data";

const char* prefix_mqtt_maintenance_topic = "maintenance/";
const char* prefix_device_id = "SWM::";

WiFiClient espClient;
PubSubClient client(espClient);
Preferences preferences;
String maintenance = "MAINTENANCE";
String operational = "OPERATIONAL";
String firmware_upgrade = "FIRMWARE_UPGRADE";

String mqtt_maintenance_topic = "";
String device_id = "";

bool inMaintenanceMode = false;
String maintenance_reason = "";

// Variables to store water consumption data
unsigned long total_consumption = 0;
unsigned long lastPublishTime = 0;
const unsigned long publishInterval = 10000; // Interval between each publication (in milliseconds)

String uniqueId = "";

String getUniqueID() {
  if (uniqueId == "") generateUniqueId();
  return uniqueId;
}

void generateUniqueId() {
  // uint8_t mac[6];
  // esp_read_mac(mac, ESP_MAC_WIFI_STA);

  // char encodedId[23]; // 2 characters for each byte of MAC address, plus 5 colons and null terminator
  // snprintf(encodedId, sizeof(encodedId), "%02X:%02X:%02X:%02X:%02X:%02X",
  //          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  // String id;
  // for (int i = 0; i < 17; i += 3) {
  //   id += String(encodedId).substring(i, i + 2) + ":";
  // }
  // id.remove(id.length() - 1); // Remove the last colon
  // Generate a random number
  long randNumber = random(1000);  // generates a random number between 0 and 999
  
  // Convert the random number to a string
  String id = String(randNumber);
  uniqueId = id;
}

void connect_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  int attempts = 0, max_attempts = 20;
  while (WiFi.status() != WL_CONNECTED && attempts < max_attempts) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  Serial.println("");

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
    device_id = prefix_device_id + getUniqueID();
    Serial.print("Device ID = ");
    Serial.println(device_id);
    mqtt_maintenance_topic = prefix_mqtt_maintenance_topic + device_id;
    Serial.print("Maintenance Topic = ");
    Serial.println(mqtt_maintenance_topic);
  } else {
    Serial.println("Failed to connect to WiFi");
  }
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
  if (String(topic) == mqtt_maintenance_topic) {
    DynamicJsonDocument doc(256);
    deserializeJson(doc, payload, length);
    const char* mode = doc["maintenanceMode"];
    const char* reason = doc["reason"];

    if (strcmp(mode, "enter") == 0) {
      if (!inMaintenanceMode) enterMaintenanceMode(reason);
      else                    Serial.println("Already in maintenance mode.");
    } else if (strcmp(mode, "exit") == 0) {
      if (inMaintenanceMode)  exitMaintenanceMode(reason);
      else                    Serial.println("Not in maintenance mode yet.");
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
  if (WiFi.status() == WL_CONNECTED && !client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect(device_id.c_str(), mqtt_username, mqtt_password)) {
      Serial.println("MQTT connected");
      // Subscribe to maintenance topic upon successful connection
      client.subscribe(mqtt_maintenance_topic.c_str());
      //Serial.print(mqtt_maintenance_topic.c_str());
    } else {
      Serial.print("failed to connect MQTT, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
    }
  }
  routine();
}

bool isInMaintenanceMode() {
  return inMaintenanceMode;
}

String getMaintenanceReason() {
  return maintenance_reason;
}

void enterMaintenanceMode(String reason) {
  preferences.putString("mode", maintenance);
  preferences.putULong("consumption", total_consumption);
  preferences.putString("reason", reason);

  // if (reason == firmware_upgrade) {
  //   ArduinoOTA.begin();
  // }

  inMaintenanceMode = true;
  maintenance_reason = reason;

  // Perform maintenance mode entry actions
  Serial.println("Entering maintenance mode: " + String(reason));
  // Respond back with entered maintenance mode message
  String response = "{\"maintenanceMode\":\"entered\",\"reason\":\"" + String(reason) + "\"}";
  client.publish(mqtt_maintenance_topic.c_str(), response.c_str());
}
void exitMaintenanceMode(String reason) {
  preferences.putString("mode", operational);
  preferences.putULong("consumption", total_consumption);
  preferences.remove("reason");

  inMaintenanceMode = false;
  maintenance_reason = "";

  // Perform maintenance mode exit actions
  Serial.println("Exiting maintenance mode: " + String(reason));
  Serial.println("Switching mode to: OPERATIONAL");
  // Respond back with exited maintenance mode message
  String response = "{\"maintenanceMode\":\"exited\",\"reason\":\"" + String(reason) + "\"}";
  client.publish(mqtt_maintenance_topic.c_str(), response.c_str());
}
void publish() {
  unsigned long currentMillis = millis();
  // Check if it's time to publish
  if (WiFi.status() == WL_CONNECTED && client.connected() && currentMillis - lastPublishTime >= publishInterval) {
    if (isInMaintenanceMode())  sendMaintenanceMessage();
    else                        sendConsumptionMessage();
    lastPublishTime = currentMillis; // Update last publish time
  }
}

void sendConsumptionMessage() {
  // Generate random current consumption between 0 and 9
  int current_consumption = random(0, 10);
  // Update total consumption
  total_consumption += current_consumption;
  // Create JSON message for main topic
  String mainMessage = "{\"device_id\":\"" + device_id + "\",\"current_consumption\":" + String(current_consumption) + ",\"total_consumption\":" + String(total_consumption) + "}";
  Serial.println("Publishing message to main topic: " + mainMessage);
  client.publish(mqtt_main_topic, mainMessage.c_str());
}

void sendMaintenanceMessage() {
  // Create JSON message for maintenance mode
  String maintenanceMessage = "{\"maintenanceMode\":\"in-maintenance\",\"reason\":\"" + maintenance_reason + "\"}";
  Serial.println("Publishing maintenance message: " + maintenanceMessage);
  client.publish(mqtt_maintenance_topic.c_str(), maintenanceMessage.c_str());
}
void routine() {
  String mode = preferences.getString("mode", "");
  if (mode == "") {
    preferences.putString("mode", operational);
    preferences.putULong("consumption", 0);
    mode = operational;
  }

  if (mode == maintenance) {
    maintenance_reason = preferences.getString("reason", "");
    inMaintenanceMode = true;
  } else {
    maintenance_reason = "";
    inMaintenanceMode = false;
  }
  total_consumption = preferences.getULong("consumption", 0);
}

void setup() {
  Serial.begin(115200);
  connect_wifi();
    preferences.begin("settings", false); // Initialize Preferences
    preferences.begin("firmware", true);
    String curr_version = preferences.getString("version", "0.0");  // Default to "1.0" if not set
    preferences.end();
    Serial.println("current_version: ");
    Serial.println(curr_version);
    Serial.println("\nhello user");
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
}

void loop() {
   if (WiFi.status() != WL_CONNECTED)                        connect_wifi(); // Reconnect to WiFi if not connected
  if (WiFi.status() == WL_CONNECTED && !client.connected()) reconnect(); // Reconnect to MQTT broker if not connected
  client.loop();
  publish();
  delay(100); // Small delay to avoid high CPU consumption 
}
