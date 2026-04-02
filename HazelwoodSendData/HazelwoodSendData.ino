#include <SPI.h>
#include <WiFiNINA.h>
#include <ArduinoJson.h> 
// Configure the pins used for the ESP32 connection
  #define SPIWIFI       SPI  // The SPI port
  #define SPIWIFI_SS    13   // Chip select pin
  #define ESP32_RESETN  12   // Reset pin
  #define SPIWIFI_ACK   11   // a.k.a BUSY or READY pin
  #define ESP32_GPIO0   -1
  
#include "arduino_secrets.h"
///////please enter your sensitive data in the Secret tab/arduino_secrets.h
char ssid[] = SECRET_SSID;        // your network SSID (name)
char pass[] = SECRET_PASS;    // your network password (use for WPA, or use as key for WEP)
int keyIndex = 0;            // your network key Index number (needed only for WEP)

int status = WL_IDLE_STATUS;

//char name[] = "Melisa";
//char path[] = "/sensor_data/" + name;
char name[] = "Melisa";
char path[32]; // size must be large enough
char server[] = "artsexcursionairquality.org";

float lat = 40.40547;
float lon = -79.93315;

//----LIBRARIES
//Air quality monitor libraries
#include "Adafruit_PM25AQI.h"
//Humidity and temperature sensor libraries
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

//----MACROS
//Air quality monitor macros:
#define PM25AQI_RESET 13
//Humidity and temperature sensor macros:
#define SEALEVELPRESSURE_HPA (1013.25)

//----DEFINITIONS
//Air Quality monitor definitions:
Adafruit_PM25AQI aqi = Adafruit_PM25AQI();
PM25_AQI_Data data; //structure that stores the air quality data
//Humidity and temperature sensor definitions:
Adafruit_BME280 bme; //  We will use I2C to talk to the sensor

void setupWiFi()
{
  // check for the WiFi module:
  WiFi.setPins(SPIWIFI_SS, SPIWIFI_ACK, ESP32_RESETN, ESP32_GPIO0, &SPIWIFI);
  while (WiFi.status() == WL_NO_MODULE) {
    Serial.println("Communication with WiFi module failed!");
    // don't continue
    delay(1000);
  }

  // attempt to connect to Wifi network:
  Serial.print("Attempting to connect to SSID: ");
  Serial.println(ssid);
  // Connect to WPA/WPA2 network. Change this line if using open or WEP network:
  do {
    status = WiFi.begin(ssid, pass);
    Serial.println("Connecting ...");
    delay(100);     // wait until connection is ready!
  } while (status != WL_CONNECTED);

  Serial.println("Connected to wifi!");
  printWifiStatus();  
}

void setupSensors()
{
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


void setup() {
  //Initialize serial and wait for port to open:
  Serial.begin(9600);
  snprintf(path, sizeof(path), "/sensor_data/%s", name);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }

  setupWiFi();
  String fv = WiFi.firmwareVersion();
  Serial.print("Firmware version: ");
  Serial.println(fv);
  setupSensors();
}


void sendToServer(float temperature, float humidity, float aqi_pm25, float aqi_pm100)
{
  Serial.println("Starting connection to server...");
  JsonDocument doc;
  doc["name"] = name;
  doc["lat"] = lat;
  doc["long"] = lon;
  doc["temperature"] = temperature;
  doc["humidity"] = humidity;
  doc["aqi_pm25"] = aqi_pm25;
  doc["aqi_pm100"] = aqi_pm100;

  String jsonString;
  serializeJson(doc, jsonString);

  Serial.print("JSON: ");
  Serial.println(jsonString);

  // Fresh client for each request
  WiFiSSLClient freshClient;
  
  if (freshClient.connect(server, 443)) {
    Serial.println("connected to server");

    // Build the entire request as one string and send it in one shot
    String request = "POST " + String(path) + " HTTP/1.1\r\n" +
                     "Host: " + String(server) + "\r\n" +
                     "Content-Type: application/json\r\n" +
                     "User-Agent: Arduino/1.0\r\n" +
                     "Content-Length: " + String(jsonString.length()) + "\r\n" +
                     "Connection: close\r\n" +
                     "\r\n" +
                     jsonString;

    freshClient.print(request);
    freshClient.flush();

    // Wait for response
    unsigned long timeout = millis();
    while (freshClient.connected() && !freshClient.available()) {
      if (millis() - timeout > 10000) {
        Serial.println("Response timeout!");
        break;
      }
    }

    // Read response
    Serial.println("--- Response ---");
    while (freshClient.connected() || freshClient.available()) {
      if (freshClient.available()) {
        char c = freshClient.read();
        if (c >= 32 || c == '\n' || c == '\r') {
          Serial.print(c);
        }
      }
    }
    Serial.println("\n--- End ---");
  } else {
    Serial.println("Connection failed!");
  }

  freshClient.stop();
  delay(3000);
}

void loop() {
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

  sendToServer(temperature, humidity, data.aqi_pm25_us, data.aqi_pm100_us);

  Serial.println("Next Reading in One Hour");
  delay(3600000); //Wait to send next reading in one-hour
  //delay(60000);
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
