#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <SPI.h>
#include "SparkFunBME280.h"


BME280 mySensor;

// Replace with your network credentials
const char* ssid = "ALO_2G";
const char* password = "TjGcR3011123";

const char* mqtt_broker = "192.168.0.48";
const char* mqtt_user = "";
const char* mqtt_pwd = "";

const bool DEBUG = false;

const char* name = "D1MiniTempSensor2";
const char* topic = "sensor/D1MiniTempSensor2";

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
  if (DEBUG) {
    log_serialln("Wifi: ");
    log_serial("SSID: ");
    log_serialln(ssid);
    log_serial("PWD: ");
    log_serialln(password);
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
    if (mqtt_client.connect(name, mqtt_user, mqtt_pwd)) {
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
  return volt*3.7;
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
  data["bat"] = get_bat();
  data["temp"] = get_temp();
  data["pressure"] = get_pressure();
  //data["humidity"] = get_humidity();

  char strData[100];
  data.printTo(strData);
  log_serial("Data: ");
  log_serialln(strData);
  mqtt_client.publish(topic, strData, true);
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
  mySensor.setMode(0)
  mySensor.setMode(1)


  log_serialln("Sensor is taking a measurement");
  while(mySensor.isMeasuring());
  log_serialln("Measurement done");
  
}

void setup() {
  //pinMode(LED_BUILTIN, OUTPUT);
  if (DEBUG) {
    Serial.begin(115200);
  }
  connect_wifi();
  connect_mqtt();
  configure_bme280();
  send_data();
  mySensor.setMode(0)
  log_serialln("Going to sleep");
  ESP.deepSleep(60 * 1000000);
}

void loop() {
}
