#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiClient.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>

// HTTP server
WiFiServer server(80);

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
  Serial.println("mDNS responder started");

  // Start TCP (HTTP) server
  server.begin();
  Serial.println("TCP server started");

  // Add service to MDNS-SD
  MDNS.addService("http", "tcp", 80);
}

void loop() {
  MDNS.update();
}