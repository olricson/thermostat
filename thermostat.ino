#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_BMP280.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include "NewEncoder.h"

#define TEMP_MIN 15
#define TEMP_MAX 30
#define TEMP_STEP 2
#define DEFAULT_TEMP 18
#define TEMP_DEFAULT_COMP 0

NewEncoder myEncoder(12, 14, 0, (TEMP_MAX - TEMP_MIN) * TEMP_STEP, (DEFAULT_TEMP - TEMP_MIN) * TEMP_STEP, FULL_PULSE);

#define VERSION "v1.4.0"

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

#define OLED_RESET -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define CLK 12
#define DT 14
#define SW 2

#define ON_BATTERY false
#define DEBUG true

String target_temp = "18.0";

char *thermo_mode[] = {"chaud", "froid", "off"};
char *en_thermo_mode[] = {"heat", "cool", "off"};

int current_mode = 0;
int current_mode_addr = 0;

unsigned long lastButtonPress = 0;
unsigned long last_rotation_call = 0;
unsigned long last_action = 0;
unsigned long last_send = 0;

bool screen_on = true;
bool show_version = false;

Adafruit_BMP280 mySensor;

// Replace with your network credentials
const char *ssid = "";
const char *password = "";

const char *mqtt_broker = "";
const char *mqtt_user = "";
const char *mqtt_pwd = "";

const long sleep_time = 5 * 60 * 1000000;
const int addr_temp = 0;
const int addr_soc = sizeof(int);
const int addr_temp_comp = addr_soc + sizeof(int);
float temp_comp = TEMP_DEFAULT_COMP;

int button_pressed = 0;
bool select_mode = false;
int selected_mode = 0;
int16_t saved_encoder_value = 0;
bool new_state = false;

byte mac[6];

String name;

bool updating = false;

WiFiClient esp_client;
PubSubClient mqtt_client(esp_client);

void log_serial(String message)
{
  if (DEBUG)
  {
    Serial.print(message);
  }
}

void log_serialln(String message)
{
  if (DEBUG)
  {
    Serial.println(message);
  }
}

void wifi_disconnected(WiFiEvent_t event)
{
  if (DEBUG)
  {
    Serial.println("Disconnected from WIFI access point");
    Serial.println("Reconnecting...");
  }
  WiFi.disconnect();
  connect_wifi();
}

void connect_wifi()
{
  WiFi.macAddress(mac);
  if (DEBUG)
  {
    log_serialln("Wifi: ");
    log_serial("SSID: ");
    log_serialln(ssid);
    log_serial("PWD: ");
    log_serialln(password);

    Serial.print("MAC: ");
    Serial.print(mac[0], HEX);
    Serial.print(":");
    Serial.print(mac[1], HEX);
    Serial.print(":");
    Serial.print(mac[2], HEX);
    Serial.print(":");
    Serial.print(mac[3], HEX);
    Serial.print(":");
    Serial.print(mac[4], HEX);
    Serial.print(":");
    Serial.println(mac[5], HEX);
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password); // begin WiFi connection
  int count = 0;

  display.clearDisplay();
  display.setTextSize(2);              // Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE); // Draw white text
  display.setCursor(0, 0);             // Start at top-left corner
  display.println("Connecting");
  display.display();
  while (WiFi.status() != WL_CONNECTED)
  {
    log_serial(".");
    delay(1000);
    count++;
    if (count > 30)
    {
      log_serialln("Wifi connection timeout, going to sleep");
      ESP.restart();
    }
    count = 0;
  }

  log_serialln("");
  log_serial("Connected to ");
  log_serialln(ssid);
  log_serial("IP address: ");
  log_serialln(WiFi.localIP().toString());
  
  WiFi.onEvent(wifi_disconnected, WIFI_EVENT_STAMODE_DISCONNECTED);
}

void configure_ota()
{
  char charName[name.length() + 1];
  name.toCharArray(charName, name.length() + 1);
  ArduinoOTA.setHostname(charName);
  ArduinoOTA.onStart([]()
                     {
    String type;
    updating = true;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("Start updating " + type); });
  ArduinoOTA.onEnd([]()
                   {
    updating = false;
    Serial.println("\nEnd"); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
                        { Serial.printf("Progress: %u%%\r", (progress / (total / 100))); });
  ArduinoOTA.onError([](ota_error_t error)
                     {
    updating = false;
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
    } });
  ArduinoOTA.begin();
}

void show_info()
{
  display.clearDisplay();

  display.setTextSize(1);              // Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE); // Draw white text
  display.setCursor(0, 0);             // Start at top-left corner

  display.print("Version: ");
  display.println(VERSION);
  char charName[name.length() + 1];
  name.toCharArray(charName, name.length() + 1);
  display.println(charName);

  display.print("Temp comp: ");
  display.println(temp_comp);
  display.display();
}

void mqtt_callback(char *topic, byte *payload, unsigned int length)
{
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++)
  {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  char cp[length + 1];
  memcpy(cp, payload, length);
  cp[length] = 0;

  String temp = String(cp);
  String top = String(topic);
  if (top.endsWith("set"))
  {
    target_temp = temp;
  }
  else if (top.endsWith("mode")) {
    if (temp.equals("heat")) {
      update_current_mode(0);
    } else if (temp.equals("cool")) {
      update_current_mode(1);
    } else {
      update_current_mode(2);
    }
  }
  else if (top.endsWith("configure"))
  {
    Serial.println(temp);
    temp_comp = temp.toFloat();
    save_float_value(addr_temp_comp, temp_comp);
  }
}

void subscribe_topic(String topic) {

}

void connect_mqtt()
{
  name = "D1MiniTempSensor_";
  name.concat(String(mac[4], HEX));
  name.concat("_");
  name.concat(String(mac[5], HEX));

  char charName[name.length() + 1];
  name.toCharArray(charName, name.length() + 1);

  log_serial("Device name: ");
  log_serialln(name);

  mqtt_client.setServer(mqtt_broker, 1883);
  mqtt_client.setCallback(mqtt_callback);
  while (!mqtt_client.connected())
  {
    log_serial("Connecting to MQTT server [");
    log_serial(mqtt_broker);
    log_serial("] user:[");
    log_serial(mqtt_user);
    log_serial("] pwd:[");
    log_serial(mqtt_pwd);
    log_serial("]...");
    if (mqtt_client.connect(charName, mqtt_user, mqtt_pwd))
    {
      log_serialln("OK");
    }
    else
    {
      log_serial("KO, error : ");
      log_serial(String(mqtt_client.state()));
      log_serialln("Waiting 5s before retry...");
      delay(5000);
    }
  }

  String topic = "sensor/";
  topic.concat(name);
  topic.concat("/set");
  char charTopic[topic.length() + 1];
  topic.toCharArray(charTopic, topic.length() + 1);
  Serial.print("Subscribe to topic: ");
  Serial.println(charTopic);
  mqtt_client.subscribe(charTopic);

  topic = "sensor/";
  topic.concat(name);
  topic.concat("/configure");
  char charTopic2[topic.length() + 1];
  topic.toCharArray(charTopic2, topic.length() + 1);
  Serial.print("Subscribe to topic: ");
  Serial.println(charTopic2);
  mqtt_client.subscribe(charTopic2);

  topic = "sensor/";
  topic.concat(name);
  topic.concat("/set/mode");
  char charTopic3[topic.length() + 1];
  topic.toCharArray(charTopic3, topic.length() + 1);
  Serial.print("Subscribe to topic: ");
  Serial.println(charTopic3);
  mqtt_client.subscribe(charTopic3);

}

void blink()
{
  digitalWrite(LED_BUILTIN, HIGH);
  delay(2000);
  digitalWrite(LED_BUILTIN, LOW);
  // delay(2000);
}

float get_temp()
{
  float temp = mySensor.readTemperature();
  // if (DEBUG) {
  //   Serial.print("Raw value measured: ");
  //   Serial.println(temp);
  // }
  return temp - temp_comp;
}

float get_bat()
{
  pinMode(A0, INPUT);
  float raw = analogRead(A0);
  float volt = raw / 1023.0;
  return raw * 4.36 / 1024.0;
}

float get_bat_lvl(float bat)
{
  int lvl = (bat - 3.05) * 100 / (4.23 - 3.05);
  if (lvl > 100)
  {
    lvl = 100;
  }
  if (lvl < 0)
  {
    lvl = 0;
  }
  return lvl;
}

int get_pressure()
{
  return mySensor.readPressure();
}

int get_humidity()
{
  return 50;
}

void send_data()
{
  last_send = millis();
  log_serialln("Sending data");
  mqtt_client.loop();
  StaticJsonBuffer<200> jsonBuffer;

  // create an object
  JsonObject &data = jsonBuffer.createObject();
  float bat = get_bat();
  int soc = 100;
  if (ON_BATTERY)
  {
    int soc = (int)get_bat_lvl(bat);
  }
  data["bat"] = bat;
  save_value(addr_soc, soc);
  float temp = get_temp();
  data["temp"] = temp;
  save_value(addr_temp, temp);
  data["pressure"] = get_pressure();
  data["bat_lvl"] = soc;
  // data["humidity"] = get_humidity();
  data["version"] = VERSION;

  char strData[100];
  data.printTo(strData);
  log_serial("Data: ");
  log_serialln(strData);
  String topic = "sensor/";
  topic.concat(name);

  log_serial("Publishing to topic: ");
  log_serialln(topic);

  char charTopic[topic.length() + 1];
  topic.toCharArray(charTopic, topic.length() + 1);
  bool status = mqtt_client.publish(charTopic, strData, true);
  log_serial("Publish status: ");
  log_serialln(String(status));
}

void configure_bme280()
{
  Wire.begin();
  if (!mySensor.begin(0x76))
  {
    Serial.println("Could not find a valid BME280 sensor, check wiring!");
    while (1)
      ;
  }
  log_serialln("Connected to sensor");
}

byte read_saved_value(int addr)
{
  EEPROM.begin(512);
  byte val = EEPROM.read(addr);
  if (DEBUG)
  {
    Serial.print("Value read: ");
    Serial.println(val);
  }
  return val;
}

void save_value(int addr, int value)
{
  EEPROM.begin(512);
  if (DEBUG)
  {
    Serial.print("Saving value: ");
    Serial.println(value);
  }
  EEPROM.write(addr, value);
  EEPROM.end();
}

void save_float_value(int addr, float value)
{
  EEPROM.begin(10);
  if (DEBUG)
  {
    Serial.print("Saving value: ");
    Serial.println(value);
  }
  EEPROM.write(addr, value);
  EEPROM.end();
}

void deep_sleep()
{
  log_serialln("Going to sleep");
  // EEPROM.end();
  ESP.deepSleep(sleep_time);
}

void configure_screen()
{
  pinMode(SW, INPUT_PULLUP);

  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS))
  {
    Serial.println(F("SSD1306 allocation failed"));
    ESP.restart();
  }
}

void setup()
{
  // pinMode(LED_BUILTIN, OUTPUT);
  if (DEBUG)
  {
    Serial.begin(115200);
  }

  update_current_mode(read_saved_value(current_mode_addr) % 3);

  configure_bme280();
  configure_screen();
  int prv_temp = (int)read_saved_value(addr_temp);
  int prv_bat = (int)read_saved_value(addr_soc);
  int t_temp_com = (float)read_saved_value(addr_temp_comp);
  if (t_temp_com > -20 && t_temp_com < 20)
  {
    temp_comp = t_temp_com;
  }

  int curr_temp = (int)get_temp();
  int curr_bat = (int)get_bat_lvl(get_bat());

  if (DEBUG)
  {
    Serial.print("prv_temp=");
    Serial.print(prv_temp);
    Serial.print("; cur_temp=");
    Serial.print(curr_temp);
    Serial.print("-- prv_bat=");
    Serial.print(prv_bat);
    Serial.print("; curr_bat=");
    Serial.println(curr_bat);
  }
  if (prv_temp != curr_temp || curr_bat < (prv_bat - 10) || curr_bat > (prv_bat + 10) || curr_bat < 5 || curr_bat > 98 || !ON_BATTERY)
  {
    connect_wifi();
    connect_mqtt();
    configure_ota();
    send_data();
  }
  if (ON_BATTERY)
  {
    deep_sleep();
  }
  
  myEncoder.attachCallback(updateEncoder);
  myEncoder.begin();
  attachInterrupt(digitalPinToInterrupt(SW), on_button, CHANGE);
  last_action = millis();
}

void send_new_state()
{
  StaticJsonBuffer<200> jsonBuffer;

  JsonObject &root = jsonBuffer.createObject();
  root["target"] = target_temp;
  root["mode"] = en_thermo_mode[selected_mode];

  Serial.print("New target_temp: ");
  Serial.println(target_temp);

  String topic = "sensor/";
  topic.concat(name);
  topic.concat("/target");

  log_serial("Publishing to topic: ");
  log_serialln(topic);

  char charTopic[topic.length() + 1];
  topic.toCharArray(charTopic, topic.length() + 1);
  // char charData[target_temp.length() + 1];
  // target_temp.toCharArray(charData, target_temp.length() + 1);

  String string_json;
  root.printTo(string_json);

  int ArrayLength = string_json.length() + 1; // The +1 is for the 0x00h Terminator
  char CharArray[ArrayLength];
  string_json.toCharArray(CharArray, ArrayLength);

  bool status = mqtt_client.publish(charTopic, CharArray, true);
  log_serial("Publish status: ");
  log_serialln(String(status));
  new_state = false;
}

void update_current_mode(int mode) {
  current_mode = mode;
  save_value(current_mode_addr, current_mode);
}

void loop()
{
  if (!ON_BATTERY)
  {
    int cnt = 0;
    while (cnt < 60)
    {
      if (!updating)
      {
        update_screen();
        mqtt_client.loop();
      }

      ArduinoOTA.handle();
      cnt++;
    }
    if (new_state) {
      send_new_state();
    }
    if (millis() - last_send > 60000 && !updating)
    {
      send_data();
    }

    if (button_pressed != 0 && millis() - button_pressed > 1500)
    { 
      saved_encoder_value = myEncoder.getValue();
      select_mode = !select_mode;
      selected_mode = current_mode;
    }
  }
}

void update_screen()
{
  if (screen_on && (millis() - last_action) > 5000 && !select_mode)
  {
    screen_on = false;
  }

  if (screen_on)
  {
    display.ssd1306_command(SSD1306_DISPLAYON);
  }
  else
  {
    display.ssd1306_command(SSD1306_DISPLAYOFF);
  }
  if (screen_on)
  {
    if (show_version)
    {
      show_info();
      delay(5000);
      show_version = false;
      last_action = millis();
    }
    else if (select_mode)
    {
      display.clearDisplay();

      display.setTextSize(2);              // Normal 1:1 pixel scale
      display.setTextColor(SSD1306_WHITE); // Draw white text
      display.setCursor(0, 0);             // Start at top-left corner
      display.println(" Mode:");
      for (int i = 0; i < 3; i++)
      {
        display.setTextColor(SSD1306_WHITE);
        display.print("  ");
        if (i == selected_mode)
        {
          display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
        }
        else
        {
          display.setTextColor(SSD1306_WHITE);
        }
        display.println(thermo_mode[i]);
      }
      display.display();
    }
    else
    {
      display.clearDisplay();

      display.setTextSize(2);              // Normal 1:1 pixel scale
      display.setTextColor(SSD1306_WHITE); // Draw white text
      display.setCursor(0, 0);             // Start at top-left corner
      display.print(thermo_mode[current_mode]);
      display.print(" ");
      display.print(String(float(round(get_temp() * 2) / 2), 1));

      display.setTextSize(6);
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(SCREEN_WIDTH / 2 - 35, SCREEN_HEIGHT / 2 - 10);

      if (target_temp.toInt() < 10)
      {
        display.print(0);
      }
      else
      {
        display.print(target_temp.charAt(0));
      }
      display.print(target_temp.charAt(1));

      display.setTextSize(1);
      display.print(".");
      if (target_temp.length() > 2)
      {
        display.print(target_temp.charAt(3));
      }
      else
      {
        display.print("0");
      }

      display.display();
    }
  }
}

ICACHE_RAM_ATTR void updateEncoder(NewEncoder *encPtr, const volatile NewEncoder::EncoderState *state, void *uPtr)
{
  if (updating)
  {
    return;
  }

  last_action = millis();
  screen_on = true;

  if (select_mode)
  {
    selected_mode = state->currentValue % 3;
    return;
  }

  last_action = millis();
  screen_on = true;
  float new_temp = TEMP_MIN + state->currentValue * 0.5;
  target_temp = String(new_temp, 1);
  new_state = true;
}

ICACHE_RAM_ATTR void on_button()
{

  if ((millis() - last_action) > 20)
  {
    if (digitalRead(SW))
    {
      on_release();
    }
    else
    {
      on_press();
    }
  }

  last_action = millis();
}

ICACHE_RAM_ATTR void on_press()
{
  if (updating)
  {
    return;
  }
  Serial.println("on_press");
  screen_on = true;
  button_pressed = millis();

  if (select_mode)
  {
    Serial.printf("Selected mode: %s\n", thermo_mode[selected_mode]);
    select_mode = false;
    update_current_mode(selected_mode);
    new_state = true;
    myEncoder = saved_encoder_value;
  }
  else if (target_temp == "15" || target_temp == "15.0")
  {
    show_version = true;
  }
}

ICACHE_RAM_ATTR void on_release()
{
  if (updating)
  {
    return;
  }
  Serial.println("on_release");

  button_pressed = 0;
}
