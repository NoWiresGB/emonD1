#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiClient.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <SoftwareSerial.h>

// HTTP server
ESP8266WebServer httpServer(80);

// MQTT client
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// MQTT related config
// hardcode it for now
const char* mqttServer = "192.168.0.254";
const char* mqttMeasureTopic = "home/pwrsens1/";
const char* mqttStatusTopic = "home/emonD1/";

// RFM69Pi GPIO_1 pinout (top view, antenna top left corner)
//
//            +----+
//  D1/TX --9-|    |-10- GND
//  D0/RX --7-|    |-8-- RESET
//  GND   --5-|    |-6--
//        --3-|    |-4--
//        --1-|    |-2-- +3V
//            +----+

// Serial to communicate with RFM69Pi
#define D1RX_PIN D5
#define D1TX_PIN D6
SoftwareSerial rfm96Serial(D1RX_PIN, D1TX_PIN);
String rxBuffer = "";

// define this flag if the serial messages received from the RFM69Pi
// should be broadcast over MQTT
// #define RFM69PI_DEBUG

// Store measurements globally, so we can display it on the web-gui
int iPower = 0;
float fVrms = 0;
int iRSSI = 0;

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

  s += "<p>Last measurements:</p>";
  s += "<ul>";

  s += "<li>Power: ";
  s += String(iPower);
  s += " W</li>";

  s += "<li>Vrms: ";
  s += String(fVrms);
  s += " V</li>";

  s += "<li>RSSI: ";
  s += String(iRSSI);
  s += " dB</li>";

  s += "</ul>";

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

  // init serial towards RFM69Pi
  rfm96Serial.begin(38400);

  // push the correct group to the FRM69Pi
  // (by default it comes up as group 0)
  rfm96Serial.print("210g");
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
      String sStatus = String(mqttStatusTopic);
      sStatus += "status";
      mqttClient.publish(sStatus.c_str(), "connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void processPacket(String packet) {
  // packet format
  // OK 6 167 2 82 92 (-38)
  // <OK> <node id> <power1 LSB> <power1 MSB> <Vrms LSB> <Vrms MSB> (<RSSI>)

  // dump received packet
  Serial.print("processing received packet: '");
  Serial.print(packet);
  Serial.println("'");

  // find node ID
  int iPos1 = packet.indexOf(" ");
  int iPos2 = packet.indexOf(" ", iPos1 + 1);
  String sData = packet.substring(iPos1 + 1, iPos2);
  int iNodeId = sData.toInt();

  // power1 LSB
  iPos1 = iPos2;
  iPos2 = packet.indexOf(" ", iPos1 + 1);
  String sLSB = packet.substring(iPos1 + 1, iPos2);

  // power1 MSB
  iPos1 = iPos2;
  iPos2 = packet.indexOf(" ", iPos1 + 1);
  String sMSB = packet.substring(iPos1 + 1, iPos2);

  // actual power
  iPower = sMSB.toInt() * 256 + sLSB.toInt();

  // Vrms LSB
  iPos1 = iPos2;
  iPos2 = packet.indexOf(" ", iPos1 + 1);
  sLSB = packet.substring(iPos1 + 1, iPos2);

  // Vrms MSB
  iPos1 = iPos2;
  iPos2 = packet.indexOf(" ", iPos1 + 1);
  sMSB = packet.substring(iPos1 + 1, iPos2);

  // calculate Vrms
  fVrms = (sMSB.toInt() * 256 + sLSB.toInt()) / 100.0;

  // get RSSI
  iPos1 = packet.indexOf("(");
  iPos2 = packet.indexOf(")", iPos1 + 1);
  sData = packet.substring(iPos1 + 1, iPos2);
  iRSSI = sData.toInt();

  // publish data on MQTT
  sData = String(iPower);
  String sSubject = String(mqttMeasureTopic);
  sSubject += "power1";
  mqttClient.publish(sSubject.c_str(), sData.c_str());
  Serial.print("Publishing: ");
  Serial.print(sSubject);
  Serial.print(" ");
  Serial.println(sData);

  sData = String(fVrms);
  sSubject = String(mqttMeasureTopic);
  sSubject += "vrms";
  mqttClient.publish(sSubject.c_str(), sData.c_str());
  Serial.print("Publishing: ");
  Serial.print(sSubject);
  Serial.print(" ");
  Serial.println(sData);

  sData = String(iRSSI);
  sSubject = String(mqttMeasureTopic);
  sSubject += "rssi";
  mqttClient.publish(sSubject.c_str(), sData.c_str());
  Serial.print("Publishing: ");
  Serial.print(sSubject);
  Serial.print(" ");
  Serial.println(sData);

  // publish what we've received
  // home/emonD1/rx/6/values 679,236.34,-38
  sSubject = String(mqttStatusTopic);
  sSubject += "rx/";
  sSubject += String(iNodeId);
  sSubject += "/values";
  sData = String(iPower);
  sData += ",";
  sData += String(fVrms);
  sData += ",";
  sData += String(iRSSI);
  mqttClient.publish(sSubject.c_str(), sData.c_str());
  Serial.print("Publishing: ");
  Serial.print(sSubject);
  Serial.print(" ");
  Serial.println(sData);

  // broadcast raw packet if needed
  #ifdef RFM69PI_DEBUG
    // home/emonD1/rx/6/raw OK 6 167 2 82 92 (-38)
    sSubject = String(mqttStatusTopic);
    sSubject += "rx/";
    sSubject += String(iNodeId);
    sSubject += "/raw";
    mqttClient.publish(sSubject.c_str(), packet.c_str());
    Serial.print("Publishing: ");
    Serial.print(sSubject);
    Serial.print(" ");
    Serial.println(packet);
  #endif
}

void loop() {
  // run mDNS update
  MDNS.update();

  // process MQTT stuff
    if (!mqttClient.connected()) {
    mqttReconnect();
  }
  mqttClient.loop();

  // Process serial stuff from RFM69Pi
  if (rfm96Serial.available() || rxBuffer.length() > 0) {
    rxBuffer += rfm96Serial.readString();

    // check if we've read a full line
    int nlPos = rxBuffer.indexOf("\r\n");
    if (nlPos != -1) {
      // got a newline in our buffer
      String packet = rxBuffer.substring(0, nlPos);

      // check what we need to do
      if (packet.startsWith(">") || packet.startsWith("->")) {
        // this is an acknowledgement - no need to process it
        Serial.print("command acknowledgement: ");
        Serial.println(packet);
      } else if (packet.startsWith("OK")) {
        // this is an actual packet
        processPacket(packet);
      } else {
        Serial.print("Ignoring invalid packet: ");
        Serial.println(packet);
      }

      // remove packet from buffer (but check if it's the only thing in there)
      if ((int)(rxBuffer.length()) == nlPos + 2) {
        rxBuffer = "";
      } else {
        rxBuffer = rxBuffer.substring(nlPos + 2);
      }
    }
  }

  // process web stuff
  httpServer.handleClient();
}