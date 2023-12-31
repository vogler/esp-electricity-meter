// Wemos D1 mini + TCRT5000 to detect red triangle on rotating metal disk in old 'Ferrari' electricity meter

#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager WiFi Configuration Magic
// alternative: https://github.com/Hieromon/AutoConnect

// all for ArduinoOTA, see https://github.com/esp8266/Arduino/blob/master/libraries/ArduinoOTA/examples/BasicOTA/BasicOTA.ino
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h> // https://github.com/jandrassy/ArduinoOTA
// alternative (upload of .bin via webpage): https://github.com/ayushsharma82/ElegantOTA

// MQTT
#include <PubSubClient.h> // https://github.com/knolleary/pubsubclient
WiFiClient wifi;
PubSubClient mqtt(wifi);
// json
char buf[200];
#define json(s, ...) (sprintf(buf, "{ " s " }", __VA_ARGS__), buf)

#define MQTT_TOPIC "electricity"
#define MQTT_SERVER "rpi0"
#define MQTT_PORT   1883

#define NAME "esp-electricity-meter"
// #define DIFF 10 // sensor value difference in percent to trigger
#define THRESH 90 // sensor value threshold for red mark
// normal: 72-87; red: 95-150
// Stromzähler Iskra Dreiphasen-4-Leiter 3x220/380 V 75 U/kWh
#define RPU 75
// sanity check: minimum time between too pulses in seconds
// theoretical minimum: 63A * 360V = 22.68kW -> 1701 U/h -> 0.4725 U/s -> 2.116s s/U
// 4.799 s/U at 10kW
#define TIMEOUT 5

// setup ArduinoOTA: Over-The-Air updates with Arduino IDE
// add this in setup() after WiFi; add ArduinoOTA.handle() in loop()
void setup_OTA() {
  // ArduinoOTA.setPort(8266); // default port
  ArduinoOTA.setHostname(NAME);
  // ArduinoOTA.setPassword("admin");

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else {  // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
}

char clientId[32];

void mqtt_connect(){
  while (!mqtt.connected()) {
    snprintf(clientId, sizeof(clientId), "ElectricityMeter-%04x", random(0xffff)); // 4 chars for hex id
    Serial.printf("Connect MQTT to %s as %s ... ", MQTT_SERVER, clientId);
    if (mqtt.connect(clientId)) {
      Serial.printf("connected to mqtt://%s\n", MQTT_SERVER);
      mqtt.publish(MQTT_TOPIC "/status", json("\"millis\": %lu, \"event\": \"connect\", \"clientId\": \"%s\"", millis(), clientId)); // TODO millis() is really just the ms, not full unix timestamp!
      // mqtt.subscribe("");
    } else {
      Serial.printf("failed, rc=%d. retry in 5s.\n", mqtt.state());
      delay(5000);
    }
  }
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  Serial.printf("MQTT message on topic %s\n", topic);
}

void setup_mqtt() {
  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setCallback(mqtt_callback);
  randomSeed(micros());
  mqtt_connect();
}

int pre; // previous sensor value

void setup () {
  Serial.begin(115200);
  Serial.println("setup");

  pinMode(D5, INPUT_PULLUP);

  WiFiManager wm; // local in setup is enough, no need for global
  // wm.resetSettings(); // wipes stored credentials
  // automatically connects using saved credentials,
  // if connection fails, it starts an access point with the name below with a captive portal for WiFi setup (blinks)
  wm.setConfigPortalTimeout(60); // auto close configportal after 60s - in case of a power outage, the router will also be offline, so we want to retry the stored auth later
  if (!wm.autoConnect(NAME)) {
    Serial.println("WiFiManager failed to connect!");
    ESP.restart();
  } else {
    Serial.print("WiFiManager connected! IP: ");
    Serial.println(WiFi.localIP());
  }

  // ESP often lost wifi connection and didn't reconnect
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);

  setup_OTA();

  setup_mqtt();

  pre = analogRead(A0); // set initial value to not trigger a change on first loop()
}

double count = 138215.3;
unsigned long lastHigh;
unsigned long lastLow;
unsigned long lastPulse;

void loop () {
  ArduinoOTA.handle();

  // Serial.println(digitalRead(D5)); // TCRT5000 PCB has a poti for adjusting a digital output pin
  // we instead use the analog output pin to be able to do filtering in code
  int cur = analogRead(A0); // close to far: 56 to 960 (day) / 1023 (night)
  Serial.println(cur);
  // Serial.println((float) abs(cur-pre) / max(cur, pre) * 100);
  // if ((float) abs(cur-pre) / max(cur, pre) * 100 > DIFF) {
    // Serial.print("Changed ");
    // Serial.println((float) (cur-pre) / pre * 100);
  // }
  if (cur > THRESH) {
    if (pre <= THRESH && (millis()-lastPulse)/1000 >= TIMEOUT) {
      Serial.print("Pulse!\t");
      count += 1.0/RPU;
      Serial.println(count);
      lastPulse = millis();
      mqtt.publish(MQTT_TOPIC "/pulse", json("\"millis\": %lu, \"count\": %lf", lastPulse, count));
    }
    lastHigh = millis();
  } else {
    lastLow = millis();
  }
  pre = cur;
  delay(250);
}
