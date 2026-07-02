#include <ArduinoJson.h> 
#include <WiFi.h>
#include <NetworkClientSecure.h>

#include "arduino_secrets.h"
//----LIBRARIES
//Air quality monitor libraries
#include "Adafruit_PM25AQI.h"
//Humidity and temperature sensor libraries
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <esp_task_wdt.h>

///////please enter your sensitive data in the Secret tab/arduino_secrets.h
const char *ssid = SECRET_SSID;
const char *password = SECRET_PASS;
//char ssid[] = SECRET_SSID;          // your network SSID (name)
//char pass[] = SECRET_PASS;          // your network password (use for WPA, or use as key for WEP)
char api_key[] = HAZELWOOD_API_KEY; // Hazelwood API Key 
int keyIndex = 0;            // your network key Index number (needed only for WEP)

char name[] = "test";
char path[64]; // size must be large enough
const char *server = "artsexcursionairquality.org";

float lat = 40.40662;
float lon = -79.94271;

//----MACROS
//Air quality monitor macros:
//PMS5003 RX no connect
//PMS5003 TX (Pin labeled RX on M4)
//PMS5003 reset 13
#define PM25AQI_RESET 13
//Humidity and temperature sensor macros:
#define SEALEVELPRESSURE_HPA (1013.25)

//----DEFINITIONS
//Air Quality monitor definitions:
Adafruit_PM25AQI aqi = Adafruit_PM25AQI();
PM25_AQI_Data data; //structure that stores the air quality data
//Humidity and temperature sensor definitions:
Adafruit_BME280 bme; //  We will use I2C to talk to the sensor

void setupWiFi() {
  Serial.print("\nAttempting to connect to SSID: ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);  // kick off once

  int numberOfTries = 20;  // ~40 seconds with 2s delays
  while (WiFi.status() != WL_CONNECTED && numberOfTries > 0) {
    esp_task_wdt_reset();
    Serial.print("Status: ");
    Serial.println(WiFi.status());
    delay(2000);
    numberOfTries--;
  }

  if (WiFi.status() == WL_CONNECTED) {
    printWifiStatus();
  } else {
    Serial.println("[WiFi] Failed to connect. Rebooting to try fresh.");
    delay(100);
    ESP.restart();  // let the watchdog approach handle this cleanly
  }
}

void setupSensors() {
  unsigned status; //variable to indicate status of the temperature and humidity sensor
  //initialize digital ports
  pinMode(PM25AQI_RESET, OUTPUT); //pin connected to reset of air quality monitor set to output
  digitalWrite(PM25AQI_RESET, HIGH); // make sure the air quality monitor is active

  //---- Air quality monitor initialization
  // Wait three seconds for sensor to boot up!
  delay(3000);
  // This is the hardware serial (i.e. UART which will talk to the air quality monitor)
  Serial1.begin(9600);
 
  if (! aqi.begin_UART(&Serial1)) { // connect to the sensor over hardware serial: UART
    Serial.println("Could not find PM 2.5 sensor!");
    while (1) delay(10);
  }
  Serial.println("PM25 found!");

  //---- Temperature and Humidity sensor initialization
  // default settings
    status = bme.begin();  
    if (!status) {
        Serial.println("Could not find a valid BME280 sensor, check wiring, address, sensor ID!");
        Serial.print("SensorID was: 0x"); Serial.println(bme.sensorID(),16);
        Serial.print("        ID of 0xFF probably means a bad address, a BMP 180 or BMP 085\n");
        Serial.print("   ID of 0x56-0x58 represents a BMP 280,\n");
        Serial.print("        ID of 0x60 represents a BME 280.\n");
        Serial.print("        ID of 0x61 represents a BME 680.\n");
        while (1) delay(10);
    }
    
    Serial.println("-- Default Test Temperature and Humidity --");
}

bool sendToServer(float temperature, float humidity, float aqi_pm25, float aqi_pm100, float aqi_pm03um) {
  esp_task_wdt_reset();
  Serial.println("Starting connection to server...");
  JsonDocument doc;
  doc["name"] = name;
  doc["lat"] = lat;
  doc["long"] = lon;
  doc["temperature"] = temperature;
  doc["humidity"] = humidity;
  doc["aqi_pm25"] = aqi_pm25;
  doc["aqi_pm100"] = aqi_pm100;
  doc["aqi_pm03um"] = aqi_pm03um;

  String jsonString;
  serializeJson(doc, jsonString);

  Serial.print("JSON: ");
  Serial.println(jsonString);

  // Fresh client for each request
  NetworkClientSecure client;
  client.setInsecure();  // skip cert validation for now — see note below

  esp_task_wdt_reset();
  
  bool success = false;
  if (client.connect(server, 443)) {
    Serial.println("connected to server");

    Serial.print("Sending with key length: ");
    Serial.println(strlen(api_key));

    // Build the entire request as one string and send it in one shot
    String request = "POST " + String(path) + " HTTP/1.1\r\n" +
                     "Host: " + String(server) + "\r\n" +
                     "Content-Type: application/json\r\n" +
                     "User-Agent: Arduino/1.0\r\n" +
                     "X-API-Key: " + String(api_key) + "\r\n" +
                     "Content-Length: " + String(jsonString.length()) + "\r\n" +
                     "Connection: close\r\n" +
                     "\r\n" +
                     jsonString;

    client.print(request);
    
    esp_task_wdt_reset();
    readResponse(&client);
    
    Serial.println("\n--- End ---");
    success = true;
  } else {
    Serial.println("Connection failed!");
  }

  client.stop();
  esp_task_wdt_reset();
  delay(3000);
  return success;
}

void readResponse(NetworkClient *client) {
  unsigned long timeout = millis();
  while (client->available() == 0) {
    esp_task_wdt_reset();
    if (millis() - timeout > 5000) {
      Serial.println(">>> Client Timeout !");
      client->stop();
      return;
    }
  }

  // Read all the lines of the reply from server and print them to Serial
  while (client->available()) {
    esp_task_wdt_reset();
    String line = client->readStringUntil('\r');
    Serial.print(line);
  }

  Serial.printf("\nClosing connection\n");
}

void setupWatchdog() {
  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = 16000,
    .idle_core_mask = 0,
    .trigger_panic = true
  };
  esp_task_wdt_init(&wdt_config);
  esp_task_wdt_add(NULL);  // add current task to watchdog

  Serial.print("Watchdog enabled with timeout: 16000");
}

void setup() {
  //Initialize serial and wait for port to open:
  Serial.begin(9600);
  snprintf(path, sizeof(path), "/api/sensor_data/%s", name);
  
  unsigned long start = millis();
  while (!Serial && millis() - start < 3000) {
    ; // wait up to 3s for serial, then continue regardless
  }

  setupWatchdog(); //Enable Watch Dog
  
  esp_task_wdt_reset();
  setupWiFi(); //Connect to Wifi
  
  esp_task_wdt_reset();
  setupSensors(); //Connect to Sensors

  esp_task_wdt_reset();
}

int sendFailureCount = 0;
const int MAX_SEND_FAILURES = 5;

void loop() {
  esp_task_wdt_reset();

  Serial.println("Waiting for PM2.5 sensor...");
  while (!aqi.read(&data)) {
      delay(500); //delay half a second
  }
  Serial.println("PM2.5 ready!");

  float temperature = bme.readTemperature();
  Serial.print("Temperature: ");
  Serial.print(temperature);
  Serial.println(" C");

  float humidity = bme.readHumidity();
  Serial.print("Humidity: ");
  Serial.print(humidity);
  Serial.println(" %");

  Serial.print("PM2.5 AQI US: ");
  Serial.println(data.aqi_pm25_us);

  Serial.print("PM10 AQI US: ");
  Serial.println(data.aqi_pm100_us);

  Serial.print("PM1.0: "); Serial.print(data.pm10_standard);
  Serial.print(" PM2.5: "); Serial.print(data.pm25_standard);
  Serial.print(" PM10: "); Serial.println(data.pm100_standard);
  Serial.print("Particles >0.3um: "); Serial.println(data.particles_03um);

  bool sent = sendToServer(temperature, humidity, data.aqi_pm25_us, data.aqi_pm100_us, data.particles_03um);

  Serial.println("Next Reading in Five Minutes");

  if (sent) {
    sendFailureCount = 0;
  } else {
    sendFailureCount++;
    Serial.print("Send failure count: ");
    Serial.println(sendFailureCount);
    
    if (sendFailureCount >= MAX_SEND_FAILURES) {
      Serial.println("Too many failures, rebooting.");
      //Reboot
      ESP.restart();
    }
  }

  for (int i = 0; i < 300; i++) {
    esp_task_wdt_reset();
    delay(1000);
  }
}

void printWifiStatus() {
  // print the SSID of the network you're attached to:
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  // print your board's IP address:
  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

  // print the received signal strength:
  long rssi = WiFi.RSSI();
  Serial.print("signal strength (RSSI):");
  Serial.print(rssi);
  Serial.println(" dBm");
}
