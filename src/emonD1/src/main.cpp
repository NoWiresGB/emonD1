#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiClient.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <PubSubClient.h>

// hardcode it for now
const char* mqtt_server = "192.168.0.254";

// HTTP server
WiFiServer server(80);

// MQTT client
WiFiClient espClient;
PubSubClient mqttClient(espClient);

void setup() {
  // init serial @ 115200
  Serial.begin(115200);

  // start WiFi auto configuration
  WiFiManager wifiManager;
  wifiManager.autoConnect("ESP8266_AutoConfig");

  // dump some info to serial once we're connected
  Serial.print("Connected to ");
  Serial.println(WiFi.SSID());
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // set up mDNS
    if (!MDNS.begin("emonD1")) {
    Serial.println("Error setting up mDNS responder!");
    while (1) {
      delay(1000);
    }
  }
  Serial.println("mDNS responder started - hostname emonD1.local");

  // Start TCP (HTTP) server
  server.begin();
  Serial.println("TCP server started");

  // Add service to MDNS-SD
  MDNS.addService("http", "tcp", 80);

  // Set up MQTT connection
  mqttClient.setServer(mqtt_server, 1883);
}

void mqttReconnect() {
  // Loop until we're reconnected
  while (!mqttClient.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESP8266Client-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (mqttClient.connect(clientId.c_str())) {
      Serial.println("MQTT connected");
      mqttClient.publish("/home/emonD1/status", "connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void loop() {
  // run mDNS update
  MDNS.update();

  // process MQTT stuff
    if (!mqttClient.connected()) {
    mqttReconnect();
  }
  mqttClient.loop();

  // TODO: process serial stuff from RFM69Pi

  // TODO: process WEB stuff
}