#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiClient.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>

// configuration structure store in EEPROM
struct EEPROMConfig {
  // magic byte to show if we've saved config before
  byte magic;

  // MQTT related config
  char mqttServer[32];
  char mqttMeasureTopic[32];
  char mqttStatusTopic[32];

  // mDNS name
  char mDNSName[16];
};
EEPROMConfig eepromData;

// HTTP server
ESP8266WebServer httpServer(80);

// MQTT client
WiFiClient espClient;
PubSubClient mqttClient(espClient);

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

// optional OLED display
#define HAS_DISPLAY

#ifdef HAS_DISPLAY
  #include <Wire.h>
  #include <Adafruit_GFX.h>
  #include <Adafruit_SSD1306.h>

  #define SCREEN_WIDTH 128 // OLED display width, in pixels
  #define SCREEN_HEIGHT 64 // OLED display height, in pixels
  #define OLED_RESET -1
  Adafruit_SSD1306 oledDisplay(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
#endif

// Store measurements globally, so we can display it on the web-gui
int iPower = 0;
float fVrms = 0;
int iRSSI = 0;

unsigned long lastMeasurement = millis();
#ifdef HAS_DISPLAY
  unsigned long lastDisplayUpdate = millis();
#endif

// Advance declarations
void mqttReconnect();

// handle request for web root
void handleRoot() {
  // create page
  String s = "<html>";
	s += "<head>";
  s += "<title>emonD1</title>";
	s += "</head>";
  s += "<body>";
  s += "<center>";
  s += "<h1 style=\"color: #82afcc\">";
  s += eepromData.mDNSName;
  s += ".local</h1><h3>RFM69Pi to MQTT bridge</h3>";
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

  s += "<p><a href=\"config\">Update config</a><p>";

  s += "</body>";
  s += "</html>";

  // sent html response
  httpServer.send(200, "text/html", s);
}

void handleConfig() {
  // create page
  String s = "<html>";
	s += "<head>";
  s += "<title>Configuration</title>";
	s += "</head>";
  s += "<body>";
  s += "<center>";
  s += "<h1 style=\"color: #82afcc\">Configuration</h1>";
  s += "</center>";

  s += "<p>Configuration parameters:</p>";

  s += "<form action=\"/config_save\">";
  s += "MQTT server: <input type=\"text\" name=\"mqttserver\" value=\"";
  s += eepromData.mqttServer;
  s += "\"><br>";

  s += "MQTT measure topic: <input type=\"text\" name=\"mqttmeasuretopic\" value=\"";
  s += eepromData.mqttMeasureTopic;
  s += "\"><br>";

  s += "MQTT status topic: <input type=\"text\" name=\"mqttstatustopic\" value=\"";
  s += eepromData.mqttStatusTopic;
  s += "\"><br>";

  s += "mDNS name: <input type=\"text\" name=\"mdnsname\" value=\"";
  s += eepromData.mDNSName;
  s += "\"><br>";

  s += "<input type=\"submit\" value=\"Save\">";
  s += "</form>";

  s += "</body>";
  s += "</html>";

  // sent html response
  httpServer.send(200, "text/html", s);
}

void handleConfigSave() {
  bool reconnect = false;
  bool reregister = false;

  // check parameters
  if (httpServer.hasArg("mqttserver")) {
    if (!httpServer.arg("mqttserver").equals(eepromData.mqttServer)) {
      // we need to reconnect to the new MQTT server
      reconnect = true;
      strcpy(eepromData.mqttServer, httpServer.arg("mqttserver").c_str());
    }
  }

  if (httpServer.hasArg("mqttmeasuretopic")) {
    strcpy(eepromData.mqttMeasureTopic, httpServer.arg("mqttmeasuretopic").c_str());
  }

  if (httpServer.hasArg("mqttstatustopic")) {
    strcpy(eepromData.mqttStatusTopic, httpServer.arg("mqttstatustopic").c_str());
  }

  if (httpServer.hasArg("mdnsname")) {
    if (!httpServer.arg("mdnsname").equals(eepromData.mDNSName)) {
      // we need to re-register with mDNS
      reregister = true;
      strcpy(eepromData.mDNSName, httpServer.arg("mdnsname").c_str());
    }
  }

  // save data to EEPROM
  eepromData.magic = 0xAC;
  EEPROM.put(0, eepromData);
  EEPROM.commit();
  Serial.println("[EEPR] Saved configration to EEPROM");

  // check if we need to reconnect
  if (reconnect) {
    mqttClient.disconnect();
    Serial.println("[MQTT] Disconnected from old server");
    mqttClient.setServer(eepromData.mqttServer, 1883);
    mqttReconnect();
  }

  // check if we need to re-register
  if (reregister) {
    // close MDNS to unannounce our services
    MDNS.close();
    // restart MDNS and announce with our new name
    MDNS.begin(eepromData.mDNSName);

    Serial.print("[MDNS] Responder updated - hostname ");
    Serial.print(eepromData.mDNSName);
    Serial.println(".local");
  }

  String s = "<html>";
	s += "<head>";
  s += "<title>emonD1 configuration</title>";
	s += "</head>";
  s += "<body>";
  s += "<center>";
  s += "<h1 style=\"color: #82afcc\">emonD1 config save success</h1>";
  s += "</center>";

  s += "<p><center><a href=\"/\">Return to main screen</a></center><p>";

  s += "</body>";
  s += "</html>";

  httpServer.send(200, "text/html", s);
}

void setup() {
  // init serial @ 115200
  Serial.begin(115200);
  // start with a clear line
  Serial.println();

  // start WiFi auto configuration
  WiFiManager wifiManager;
  wifiManager.autoConnect("emonD1_AutoConfig");

  // read configuration from EEPROM
  EEPROM.begin(128);
  Serial.println("[EEPR] Reading config from EEPROM");
  EEPROM.get(0, eepromData);
  // check magic byte - should be set to 0xAB if config has been saved to EEPROM before
  if (eepromData.magic == 0xAC) {
    Serial.println("[EEPR] Config checks out");
  } else {
    // set defaults
    Serial.println("[EEPR] Config not saved before - falling back to defaults");
    strcpy(eepromData.mqttServer, "192.168.0.254");
    strcpy(eepromData.mqttMeasureTopic, "home/pwrsens1/");
    strcpy(eepromData.mqttStatusTopic, "home/emonD1/");
    strcpy(eepromData.mDNSName, "emonD1");
  }

  #ifdef HAS_DISPLAY
    // initialize with the I2C addr 0x3C
    oledDisplay.begin(SSD1306_SWITCHCAPVCC, 0x3C);
    Serial.println("[OLED] Display initialised");
  #endif

  // dump some info to serial once we're connected
  Serial.print("[WIFI] Connected to ");
  Serial.println(WiFi.SSID());
  Serial.print("[WIFI] IP address: ");
  Serial.println(WiFi.localIP());

  #ifdef HAS_DISPLAY
    oledDisplay.clearDisplay();
  	oledDisplay.setTextSize(1);
	  oledDisplay.setTextColor(WHITE);
    oledDisplay.setCursor(0,0);
	  oledDisplay.print("SSID: ");
    oledDisplay.println(WiFi.SSID().substring(0, 15));
    oledDisplay.print("IP: ");
    oledDisplay.println(WiFi.localIP());
    oledDisplay.display();
  #endif

  // set up mDNS
  if (!MDNS.begin(eepromData.mDNSName)) {
    Serial.println("[MDNS] Error setting up mDNS responder!");
    while (1) {
      delay(1000);
    }
  }
  Serial.print("[MDNS] Responder started - hostname ");
  Serial.print(eepromData.mDNSName);
  Serial.println(".local");

  // Start HTTP server
  httpServer.begin();
  Serial.println("[HTTP] Server started");

  // add page(s) to HTTP server
  httpServer.on("/", handleRoot);
  httpServer.on("/config", handleConfig);
  httpServer.on("/config_save", handleConfigSave);

  // Add service to MDNS-SD
  MDNS.addService("http", "tcp", 80);

  // Set up MQTT connection
  mqttClient.setServer(eepromData.mqttServer, 1883);

  // init serial towards RFM69Pi
  rfm96Serial.begin(38400);

  // push the correct group to the FRM69Pi
  // (by default it comes up as group 0)
  rfm96Serial.print("210g");
}

void mqttReconnect() {
  Serial.print("[MQTT] Attempting connection...");
  // Create a random client ID
  String clientId = "emonD1-";
  clientId += String(random(0xffff), HEX);
  // Attempt to connect
  if (mqttClient.connect(clientId.c_str())) {
    Serial.println("[MQTT] Connected");
    String sStatus = String(eepromData.mqttStatusTopic);
    sStatus += "status";
    mqttClient.publish(sStatus.c_str(), "connected");
  } else {
    Serial.print("[MQTT] Connect failed, rc=");
    Serial.print(mqttClient.state());
    Serial.println("; try again in the next round");
  }
}

void processPacket(String packet) {
  // packet format
  // OK 6 167 2 82 92 (-38)
  // <OK> <node id> <power1 LSB> <power1 MSB> <Vrms LSB> <Vrms MSB> (<RSSI>)

  // dump received packet
  Serial.print("[RF69] processing received packet: '");
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

  String sSubject;

  // publish data on MQTT
  if (mqttClient.connected()) {
    sData = String(iPower);
    sSubject = String(eepromData.mqttMeasureTopic);
    sSubject += "power1";
    mqttClient.publish(sSubject.c_str(), sData.c_str());
    Serial.print("[MQTT] Publishing: ");
    Serial.print(sSubject);
    Serial.print(" ");
    Serial.println(sData);

    sData = String(fVrms);
    sSubject = String(eepromData.mqttMeasureTopic);
    sSubject += "vrms";
    mqttClient.publish(sSubject.c_str(), sData.c_str());
    Serial.print("[MQTT] Publishing: ");
    Serial.print(sSubject);
    Serial.print(" ");
    Serial.println(sData);

    sData = String(iRSSI);
    sSubject = String(eepromData.mqttMeasureTopic);
    sSubject += "rssi";
    mqttClient.publish(sSubject.c_str(), sData.c_str());
    Serial.print("[MQTT] Publishing: ");
    Serial.print(sSubject);
    Serial.print(" ");
    Serial.println(sData);

    // publish what we've received
    // home/emonD1/rx/6/values 679,236.34,-38
    sSubject = String(eepromData.mqttStatusTopic);
    sSubject += "rx/";
    sSubject += String(iNodeId);
    sSubject += "/values";
    sData = String(iPower);
    sData += ",";
    sData += String(fVrms);
    sData += ",";
    sData += String(iRSSI);

    mqttClient.publish(sSubject.c_str(), sData.c_str());
    Serial.print("[MQTT] Publishing: ");
    Serial.print(sSubject);
    Serial.print(" ");
    Serial.println(sData);
  } else {
    Serial.println("[MQTT] Not connected; skipping publish");
  }

  // update the timestamp
  lastMeasurement = millis();

  // broadcast raw packet if needed
  #ifdef RFM69PI_DEBUG
    if (mqttClient.connected()) {
      // home/emonD1/rx/6/raw OK 6 167 2 82 92 (-38)
      sSubject = String(eepromData.mqttStatusTopic);
      sSubject += "rx/";
      sSubject += String(iNodeId);
      sSubject += "/raw";
      mqttClient.publish(sSubject.c_str(), packet.c_str());
      Serial.print("[MQTT] Publishing: ");
      Serial.print(sSubject);
      Serial.print(" ");
      Serial.println(packet);
    } else {
      Serial.println("[MQTT] Not connected; skipping publish");
    }
  #endif
}

void loop() {
  // run mDNS update
  MDNS.update();

  // check if we're connected
  if (!mqttClient.connected()) {
    mqttReconnect();
  }
  // process MQTT stuff
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
        Serial.print("[RF69] command acknowledgement: ");
        Serial.println(packet);
      } else if (packet.startsWith("OK")) {
        // this is an actual packet
        processPacket(packet);
      } else {
        Serial.print("[RF69] Ignoring invalid packet: ");
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

  // update screen
  #ifdef HAS_DISPLAY
    // only update the screen once per second
    if (millis() - 1000 > lastDisplayUpdate) {
      oledDisplay.clearDisplay();
      oledDisplay.setCursor(0,0);

      oledDisplay.print("SSID: ");
      oledDisplay.println(WiFi.SSID().substring(0, 15));
      oledDisplay.print("IP: ");
      oledDisplay.println(WiFi.localIP());
      oledDisplay.println("---------------------");
      oledDisplay.println("Last measurements:");

      oledDisplay.print("Power: ");
      oledDisplay.print(iPower);
      oledDisplay.println("W");

      oledDisplay.print("Vrms: ");
      oledDisplay.print(fVrms);
      oledDisplay.println("V");

      oledDisplay.print("RSSI: ");
      oledDisplay.print(iRSSI);
      oledDisplay.println("dB");

      // display the elapsed time since last received measurement
      oledDisplay.print("                 ");
      oledDisplay.print((millis() - lastMeasurement)/1000);
      oledDisplay.print("s");
      oledDisplay.display();

      // update the last display update
      lastDisplayUpdate = millis();
    }
  #endif
}