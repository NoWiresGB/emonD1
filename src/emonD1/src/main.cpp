#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiClient.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <PubSubClient.h>

// hardcode it for now
const char* mqttServer = "192.168.0.254";

// HTTP server
ESP8266WebServer httpServer(80);

// MQTT client
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// handle request for web root
void handleRoot() {

  // create page
  String s = "<html>";
	s += "<head>";
  s += "<title>emonD1</title>";
	s += "</head>";
  s += "<body>";
  s += "<center>";
  s += "<h1 style=\"color: #82afcc\">emonESP</h1>";
  s += "<h3>RFM69Pi to MQTT bridge</h3>";
  s += "</center>";
  s += "</body>";
  s += "</html>";

  // sent html response
  httpServer.send(200, "text/html", s);
}

void setup() {
  // init serial @ 115200
  Serial.begin(115200);

  // start WiFi auto configuration
  WiFiManager wifiManager;
  wifiManager.autoConnect("emonD1_AutoConfig");

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

  // Start HTTP server
  httpServer.begin();
  Serial.println("HTTP server started");

  // add page(s) to HTTP server
  httpServer.on("/", handleRoot);

  // Add service to MDNS-SD
  MDNS.addService("http", "tcp", 80);

  // Set up MQTT connection
  mqttClient.setServer(mqttServer, 1883);
}

void mqttReconnect() {
  // Loop until we're reconnected
  while (!mqttClient.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "emonD1-";
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

  // process web stuff
  httpServer.handleClient();
}