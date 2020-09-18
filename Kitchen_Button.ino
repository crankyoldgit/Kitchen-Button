// Copyright 2020 David Conran

#include "SSD1306Wire.h"
#include "KY040rotary.h"
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiClient.h>
#include <DNSServer.h>
#include <WiFiManager.h>
#include <ESP8266HTTPClient.h>
#include <PubSubClient.h>

typedef struct {
  bool on;
  const char *url;
  const char *topic;
} Switch_t;

typedef struct {
  const char *name;
  const uint8_t count;
  Switch_t switches[8];
} Zone_t;

// Initialize the OLED display using Arduino Wire:
SSD1306Wire display(0x3c, D2, D1, GEOMETRY_128_64);
// init the button
KY040 Rotary(14, 12, 13);

bool updateDisplay = true;
uint16_t zone_nr = 0;
Zone_t Zones[] = {
    {"Kitchen", 2, {{false,
                     "http://10.20.0.18/cm?cmnd=Power%20",
                     "stat/kitchen_lights_1/POWER"},
                    {false,
                     "http://10.20.0.19/cm?cmnd=Power%20",
                     "stat/kitchen_lights_2/POWER"}}},
    {"Kitchen L", 1, {{false,
                       "http://10.20.0.18/cm?cmnd=Power%20",
                       "stat/kitchen_lights_1/POWER"}}},
    {"Kitchen R", 1, {{false,
                       "http://10.20.0.19/cm?cmnd=Power%20",
                       "stat/kitchen_lights_2/POWER"}}},
    {"Living Room", 8, {{false,
                         "http://10.20.0.10/cm?cmnd=Power%20",
                         "stat/livingroom_downlight_warm_1/POWER"},
                        {false,
                         "http://10.20.0.11/cm?cmnd=Power%20",
                         "stat/livingroom_downlight_warm_2/POWER"},
                        {false,
                         "http://10.20.0.12/cm?cmnd=Power%20",
                         "stat/livingroom_downlight_cold_1_1/POWER"},
                        {false,
                         "http://10.20.0.13/cm?cmnd=Power%20",
                         "stat/livingroom_downlight_cold_1_2/POWER"},
                        {false,
                         "http://10.20.0.14/cm?cmnd=Power%20",
                         "stat/livingroom_downlight_cold_2_1/POWER"},
                        {false,
                         "http://10.20.0.15/cm?cmnd=Power%20",
                         "stat/livingroom_downlight_cold_2_2/POWER"},
                        {false,
                         "http://10.20.0.16/cm?cmnd=Power%20",
                         "stat/livingroom_downlight_cold_2_3/POWER"},
                        {false,
                         "http://10.20.0.17/cm?cmnd=Power%20",
                         "stat/livingroom_downlight_cold_2_4/POWER"}}},
    {"Warm", 2, {{false,
                  "http://10.20.0.10/cm?cmnd=Power%20",
                  "stat/livingroom_downlight_warm_1/POWER"},
                 {false,
                  "http://10.20.0.11/cm?cmnd=Power%20",
                  "stat/livingroom_downlight_warm_2/POWER"}}},
    {"Bright", 6, {{false,
                    "http://10.20.0.12/cm?cmnd=Power%20",
                    "stat/livingroom_downlight_cold_1_1/POWER"},
                   {false,
                    "http://10.20.0.13/cm?cmnd=Power%20",
                    "stat/livingroom_downlight_cold_1_2/POWER"},
                   {false,
                    "http://10.20.0.14/cm?cmnd=Power%20",
                    "stat/livingroom_downlight_cold_2_1/POWER"},
                   {false,
                    "http://10.20.0.15/cm?cmnd=Power%20",
                    "stat/livingroom_downlight_cold_2_2/POWER"},
                   {false,
                    "http://10.20.0.16/cm?cmnd=Power%20",
                    "stat/livingroom_downlight_cold_2_3/POWER"},
                   {false,
                    "http://10.20.0.17/cm?cmnd=Power%20",
                    "stat/livingroom_downlight_cold_2_4/POWER"}}},
    {"Door", 2, {{false,
                  "http://10.20.0.12/cm?cmnd=Power%20",
                  "stat/livingroom_downlight_cold_1_1/POWER"},
                 {false,
                  "http://10.20.0.13/cm?cmnd=Power%20",
                  "stat/livingroom_downlight_cold_1_2/POWER"}}},
    {"Couch", 2, {{false,
                   "http://10.20.0.14/cm?cmnd=Power%20",
                   "stat/livingroom_downlight_cold_2_1/POWER"},
                  {false,
                   "http://10.20.0.16/cm?cmnd=Power%20",
                   "stat/livingroom_downlight_cold_2_3/POWER"}}},
    {"TV Lights", 2, {{false,
                       "http://10.20.0.15/cm?cmnd=Power%20",
                       "stat/livingroom_downlight_cold_2_2/POWER"},
                      {false,
                       "http://10.20.0.17/cm?cmnd=Power%20",
                       "stat/livingroom_downlight_cold_2_4/POWER"}}},
    {"Bedroom", 1, {{false,
                     "http://10.20.0.20/cm?cmnd=Power%20",
                     "stat/bedroom_main_downlight/POWER"}}}};
const uint16_t kZoneCount = sizeof(Zones) / sizeof(Zone_t);
const uint32_t kDisplayTimeout = 15 * 1000;  // 15 Seconds.
uint32_t lastUpdate = millis();
bool displayStatus = true;
bool connected = false;  // MQTT status
const char *url;
const char *topic;

const uint16_t kHttpPort = 80;  // The TCP port the HTTP server is listening on.

WiFiClient mqttClient;
WiFiClient httpClient;

WiFiManager wifiManager;

const uint16_t kMqttBufferSize = 768;
PubSubClient mqtt_client(mqttClient);
const uint8_t kHostnameLength = 30;
const uint8_t kPortLength = 5;  // Largest value of uint16_t is "65535".
const uint8_t kUsernameLength = 15;
const uint8_t kPasswordLength = 20;

char Hostname[kHostnameLength + 1] = "ir_server";  // Default hostname.

char MqttServer[kHostnameLength + 1] = "10.0.0.4";
uint16_t kMqttPort = 1883;
char MqttUsername[kUsernameLength + 1] = "";
char MqttPassword[kPasswordLength + 1] = "";
char MqttPrefix[kHostnameLength + 1] = "";

bool getZonePower(const Zone_t zone) {
  for (uint8_t i = 0; i < zone.count; i++)
    if (zone.switches[i].on) return true;
  return false;
}

void setZonePower(Zone_t *zone, const bool on) {
  for (uint8_t i = 0; i < zone->count; i++)
    zone->switches[i].on = on;
}

void OnButtonClicked(void) {
  // Invert the on status, by setting them either all off or all on.
  const bool on = !getZonePower(Zones[zone_nr]);
  setZonePower(&(Zones[zone_nr]), on);
  HTTPClient http;
  for (uint8_t i = 0; i < Zones[zone_nr].count; i++) {
    char url[80] = "";
    strncpy(url, Zones[zone_nr].switches[i].url, 50);
    strncat(url, "%20", 3);
    strncat(url, on ? "ON" : "OFF", 3);
    if (http.begin(httpClient, url)) {
      http.GET();
      http.end();
    }
  }
  updateDisplay = true;
}

void OnButtonLeft(void) {
  zone_nr = (kZoneCount + zone_nr - 1) % kZoneCount;
  updateDisplay = true;
}
void OnButtonRight(void) {
  zone_nr = (zone_nr + 1) % kZoneCount;
  updateDisplay = true;
}

void doRestart(void) {
  delay(2000);  // Enough time for messages to be sent.
  ESP.restart();
  delay(5000);  // Enough time to ensure we don't return.
}

void setup_wifi(void) {
  delay(10);
  // We start by connecting to a WiFi network
  wifiManager.setTimeout(300);  // Time out after 5 mins.

  if (!wifiManager.autoConnect())
    // Reboot. A.k.a. "Have you tried turning it Off and On again?"
    doRestart();
}

void receivingMQTT(char const *topic, String const payload) {
  for (uint8_t i = 0; i < kZoneCount; i++) {
    for (uint8_t j = 0; j < Zones[i].count; j++) {
      if (!strcmp(topic, Zones[i].switches[j].topic)) {
        Zones[i].switches[j].on = !strcasecmp("ON", payload.c_str());
        updateDisplay = true;
        return;
      }
    }
  }
}

// Callback function, when we receive an MQTT value on the topics
// subscribed this function is called
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  byte* payload_copy = reinterpret_cast<byte*>(malloc(length + 1));
  if (payload_copy == NULL) {
    Serial.println("Can't allocate memory for `payload_copy`. Skipping callback!");
    return;
  }
  // Copy the payload to the new buffer
  memcpy(payload_copy, payload, length);
  // Conversion to a printable string
  payload_copy[length] = '\0';

  // launch the function to treat received data
  receivingMQTT(topic, (char *)payload_copy);

  // Free the memory
  free(payload_copy);
}

void subscribing(void) {
  for (uint8_t i = 0; i < kZoneCount; i++)
    for (uint8_t j = 0; j < Zones[i].count; j++)
      mqtt_client.subscribe(Zones[i].switches[j].topic);
}

void reconnect(void) {
  if (mqtt_client.connect("KitchenSwitch")) subscribing();
}

void setup() {
  Serial.begin(115200);

  if ( !Rotary.Begin() ) {
    Serial.println("unable to init rotate button");
  }
  // init callbacks
  Rotary.OnButtonClicked(OnButtonClicked);
  Rotary.OnButtonLeft(OnButtonLeft);
  Rotary.OnButtonRight(OnButtonRight);

  display.init();
  displayStatus = true;
  display.flipScreenVertically();
  display.setFont(ArialMT_Plain_16);

  setup_wifi();

  // Initialising the UI will init the display too.
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(0, 0, "KY-040 OK");
  display.drawString(0, 24, WiFi.localIP().toString().c_str());
  display.display();
  doUpdateDisplay();
  // Finish setup of the mqtt clent object.
  if (!mqtt_client.setBufferSize(kMqttBufferSize))
    Serial.println("Can't fully allocate MQTT buffer! Try a smaller value.");
  mqtt_client.setServer(MqttServer, kMqttPort);
  mqtt_client.setCallback(mqttCallback);
  reconnect();
}

void doUpdateDisplay(void) {
  // clear the display
  display.displayOn();
  displayStatus = true;
  display.clear();
  display.setFont(ArialMT_Plain_24);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(64, 0, Zones[zone_nr].name);
  display.drawString(64, 24, getZonePower(Zones[zone_nr]) ? "On" : "Off");
  lastUpdate = millis();
  // display.setTextAlignment(TEXT_ALIGN_RIGHT);
  // display.setFont(ArialMT_Plain_16);
  // display.drawString(64, 48, String(lastUpdate));
  // write the buffer to the display
  display.display();
  updateDisplay = false;
}

void loop() {
  if (updateDisplay) doUpdateDisplay();
  if (displayStatus && lastUpdate + kDisplayTimeout < millis()) {
    display.displayOff();
    displayStatus = false;
  }
  Rotary.Process( millis() );
  if (!mqtt_client.connected()) {
    // Serial.println("Disconnected. Reconnecting");
    reconnect();
  } else {
    mqtt_client.loop();
  }
}
