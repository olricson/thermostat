#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <SPI.h>
#include "SparkFunBME280.h"
#include <EEPROM.h>

BME280 mySensor;

// Replace with your network credentials
const char* ssid = "";
const char* password = "";

const char* mqtt_broker = "192.168.0.48";
const char* mqtt_user = "";
const char* mqtt_pwd = "";

const long sleep_time = 5 * 60 * 1000000;

const bool DEBUG = false;
byte mac[6];

String name;

WiFiClient esp_client;
PubSubClient mqtt_client(esp_client);

void log_serial(String message) {
  if (DEBUG) {
    Serial.print(message);
  }
}

void log_serialln(String message) {
  if (DEBUG) {
    Serial.println(message);
  }
}

void connect_wifi() {
  WiFi.macAddress(mac);
  if (DEBUG) {
    log_serialln("Wifi: ");
    log_serial("SSID: ");
    log_serialln(ssid);
    log_serial("PWD: ");

    Serial.print("MAC: ");
    Serial.print(mac[0],HEX);
    Serial.print(":");
    Serial.print(mac[1],HEX);
    Serial.print(":");
    Serial.print(mac[2],HEX);
    Serial.print(":");
    Serial.print(mac[3],HEX);
    Serial.print(":");
    Serial.print(mac[4],HEX);
    Serial.print(":");
    Serial.println(mac[5],HEX);
  }
  WiFi.begin(ssid, password); //begin WiFi connection

  while (WiFi.status() != WL_CONNECTED) {
    log_serial(".");
    delay(1000);
  }

  log_serialln("");
  log_serial("Connected to ");
  log_serialln(ssid);
  log_serial("IP address: ");
  log_serialln(WiFi.localIP().toString());
}

void connect_mqtt() {
  name = "D1MiniTempSensor_";
  name.concat(String(mac[4], HEX));
  name.concat("_");
  name.concat(String(mac[5], HEX));

  char charName[name.length() + 1];
  name.toCharArray(charName, name.length() + 1);

  log_serial("Device name: ");
  log_serialln(name);

  mqtt_client.setServer(mqtt_broker, 1883);
  //client_mqtt.setCallback(callback);
  //Boucle jusqu'à obtenur une reconnexion
  while (!mqtt_client.connected()) {
    log_serial("Connecting to MQTT server [");
    log_serial(mqtt_broker);
    log_serial("] user:[");
    log_serial(mqtt_user);
    log_serial("] pwd:[");
    log_serial(mqtt_pwd);
    log_serial("]...");
    if (mqtt_client.connect(charName, mqtt_user, mqtt_pwd)) {
      log_serialln("OK");
    } else {
      log_serial("KO, error : ");
      log_serial(String(mqtt_client.state()));
      log_serialln("Waiting 5s before retry...");
      delay(5000);
    }
  }
}

void blink() {
  digitalWrite(LED_BUILTIN, HIGH);
  delay(2000);
  digitalWrite(LED_BUILTIN, LOW);
  //delay(2000);
}

int get_temp() {
  return mySensor.readTempC();
}

float get_bat() {
  pinMode(A0, INPUT);
  float raw = analogRead(A0);
  float volt=raw/1023.0;
  return raw * 4.36 / 1024.0;
}

float get_bat_lvl(float bat) {
  int lvl = (bat - 3.05) * 100 / (4.23 - 3.05);
  if (lvl > 100 ) {
    lvl = 100;
  }
  if (lvl < 0 ) {
    lvl = 0;
  }
  return lvl;
}

int get_pressure() {
  return mySensor.readFloatPressure();
}

int get_humidity() {
  return 50;
}

void send_data() {
  log_serialln("Sending data");
  mqtt_client.loop();
  StaticJsonBuffer<200> jsonBuffer;

// create an object
  JsonObject& data = jsonBuffer.createObject();
  float bat = get_bat();
  data["bat"] = bat;
  save_value(1, bat);
  int temp =  get_temp();
  data["temp"] = temp;
  save_value(0, temp);
  data["pressure"] = get_pressure();
  data["bat_lvl"] = get_bat_lvl(bat);
  //data["humidity"] = get_humidity();

  char strData[100];
  data.printTo(strData);
  log_serial("Data: ");
  log_serialln(strData);
  String topic = "sensor/";
  topic.concat(name);

  log_serial("Publishing to topic: ");
  log_serialln(topic);

  char charTopic[topic.length()+1];
  topic.toCharArray(charTopic, topic.length()+1);
  mqtt_client.publish(charTopic, strData, true);
  delay(500);
}

void configure_bme280() {
  Wire.begin();
  mySensor.setI2CAddress(0x76);
  if (mySensor.beginI2C() == false) //Begin communication over I2C
  {
    log_serialln("The sensor did not respond. Please check wiring.");
    while(1); //Freeze
  }

  log_serialln("Connected to sensor");
  mySensor.setMode(0);
  mySensor.setMode(1);


  log_serialln("Sensor is taking a measurement");
  while(mySensor.isMeasuring());
  log_serialln("Measurement done");

}

int read_saved_value(int addr) {
  byte val = EEPROM.read(addr);
  if (DEBUG) {
    Serial.print("Value read: ");
    Serial.println(val);
  }
  return val;
}

void save_value(int addr, int value) {
  if (DEBUG) {
    Serial.print("Saving value: ");
    Serial.println(value);
  }
  EEPROM.write(addr, value);
}

void setup() {
  //pinMode(LED_BUILTIN, OUTPUT);
  if (DEBUG) {
    Serial.begin(115200);
  }
  EEPROM.begin(512);

  configure_bme280();

  if (read_saved_value(0) != get_temp() || read_saved_value(1) != get_bat()+10) {
    connect_wifi();
    connect_mqtt();
    send_data();
    mySensor.setMode(0);
  }
  log_serialln("Going to sleep");
  EEPROM.end();
  ESP.deepSleep(sleep_time);
}

void loop() {
}
