#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "ArduinoJson.h"
#include <SPI.h>
#include <SD.h>
#include <Adafruit_NeoPixel.h>

#define TFT_CS 5
#define TFT_DC 2
#define TFT_CLK 18
#define TFT_MOSI 23
#define TFT_MISO -1
#define TFT_RST 4
#define SD_CHIP_SELECT 22
#define PIN_STRIP_LOCATIONS 15

const int numLedLocations = 30;

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);

Adafruit_NeoPixel stripLocations = Adafruit_NeoPixel(numLedLocations, PIN_STRIP_LOCATIONS, NEO_GRB + NEO_KHZ800);

const char *ssid = "RedSky";
const char *password = "happyredcat";

enum LocationStatus
{
  danger,
  caution,
  fair,
  noData
};

// Location data contains IDs of associated stations.
// Order of location is order of LEDs.
struct Location
{
  String usgs[10];       // USGS station IDs.
  String wr[10];         // WaterReporter station IDs.
  String shortName;      // Short name of location.
  String area;           // Name of general station area.
  LocationStatus status; // Status of the location.
} locations[30];

int numLocations;

bool sdStatus = false;
bool wifiStatus = false;
bool apiStatus = false;

const uint32_t OFF = 0x0000000;
const uint32_t RED = 0x00FF0000;
const uint32_t GREEN = 0x0000FF00;
const uint32_t BLUE = 0x000000FF;
const uint32_t YELLOW = 0x00F0F000;

void FatalError(String errorMsg)
{
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(ILI9341_RED);
  tft.setCursor(0, 0);
  tft.println("Fatal error.");
  tft.println();
  tft.print(errorMsg);

  Serial.println(errorMsg);

  while (1)
  {
  }
}

bool GetDataFromAPI(int stationID)
{

  return true;
}

bool InitSDCard()
{
  int count = 0;

  Serial.println("Attempting to mount SD card...");

  while (!SD.begin(SD_CHIP_SELECT))
  {
    if (++count > 5)
    {
      Serial.println("Card Mount Failed.");
      return false;
    }
    delay(250);
  }

  Serial.println("SD card mounted.");
  return true;
}

bool GetJsonFromSDCard(String fileName, String *stationDataJson)
{

  String path = "/" + fileName + ".json";

  Serial.print("Reading file: ");
  Serial.println(path);

  File file = SD.open(path);

  if (!file)
  {
    Serial.println("Failed to open file for reading.");
    return false;
  }

  *stationDataJson = file.readString();

  file.close();
  return true;
}

bool PopulateLocationInitFromSDCard()
{

  String stationInitDataJson;

  if (!GetJsonFromSDCard("locations", &stationInitDataJson))
  {
    return false;
  }

  DynamicJsonDocument doc(2048);
  DeserializationError error = deserializeJson(doc, stationInitDataJson);

  if (error)
  {
    Serial.print(F("DeserializeJson() failed: "));
    Serial.println(error.c_str());
    return false;
  }

  numLocations = doc["locations"].size();

  for (int i = 0; i < numLocations; i++)
  {

    for (int u = 0; u < doc["locations"][i]["USGS"].size(); u++)
    {
      locations[i].usgs[u] = doc["locations"][i]["USGS"][u].as<String>();
    }

    for (int w = 0; w < doc["locations"][w]["WR"].size(); w++)
    {
      locations[i].wr[w] = doc["locations"][i]["WR"][w].as<String>();
    }

    locations[i].shortName = doc["locations"][i]["shortName"].as<String>();
    locations[i].area = doc["locations"][i]["area"].as<String>();
  }

  return true;
}

void PrintData(int line, const char *text, const char *value, const char *units, uint16_t color)
{

  //const int spacesPerLine = 25;
  // int numCharacters = strlen(text) + strlen(value) + strlen(units);
  //int numCharacters = 16 + strlen(value) + strlen(units);
  
  tft.setTextSize(2);
  tft.setCursor(5, 63 + line * 18);

  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  tft.printf("%-*s", 15, text);

  tft.setTextColor(color, ILI9341_BLACK);
  tft.printf("%-*s", 5, value);

  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  tft.print(units);

  //tft.printf("%-*s", spacesPerLine - numCharacters, "");
}

bool UpdateLocationDataOnScreen(int locationIndex, String *locationDataJson)
{
  tft.setTextSize(3);
  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  tft.setCursor(5, 5);
  tft.printf("%-17s", locations[locationIndex].shortName.c_str());
  tft.setCursor(5, 30);
  tft.printf("%-17s", locations[locationIndex].area.c_str());

  tft.setTextSize(2);
  tft.setTextColor(ILI9341_GREEN, ILI9341_BLACK);

  DynamicJsonDocument doc(2048);
  DeserializationError error = deserializeJson(doc, *locationDataJson);

  if (error)
  {
    Serial.print(F("DeserializeJson() failed: "));
    Serial.println(error.c_str());

    char locationString[50];
    sprintf(locationString, "(filename: %u.json, was not found.)", locationIndex);

    PrintData(0, "", "Location not found on SD card.", "", ILI9341_RED);
    PrintData(1, "", "(data will be fetched from API shortly)", "", ILI9341_RED);
    PrintData(2, "", locationString, "", ILI9341_RED);
  }
  else
  {
    signed int streamFlow = doc["data"]["streamFlow"]["value"].as<signed int>();
    signed int gaugeHeight = doc["data"]["gaugeHeight"]["value"].as<signed int>();
    signed int waterTempC = doc["data"]["waterTempC"]["value"].as<signed int>();
    signed int eColiConcentration = doc["data"]["eColiConcentration"]["value"].as<signed int>();
    signed int bacteriaThreshold = doc["data"]["bacteriaThreshold"]["value"].as<signed int>();
    String lastModifed = doc["station"]["recordTime"];

    char streamFlowString[20];
    sprintf(streamFlowString, "%i", streamFlow);
    uint16_t streamFlowColor = streamFlow == -9 ? ILI9341_WHITE : streamFlow < 2000 ? ILI9341_GREEN : streamFlow < 4000 ? ILI9341_YELLOW : ILI9341_RED;

    char gaugeHeightString[20];    
    sprintf(gaugeHeightString, "%i", gaugeHeight);
    uint16_t gaugeHeightColor = gaugeHeight == -9 ? ILI9341_WHITE : gaugeHeight < 2 ? ILI9341_GREEN : gaugeHeight < 6 ? ILI9341_YELLOW : ILI9341_RED;

    char waterTempCString[20];   
    sprintf(waterTempCString, "%i", waterTempC);
    uint16_t waterTempCColor = waterTempC == -9 ? ILI9341_WHITE : waterTempC < 2 ? ILI9341_GREEN : waterTempC < 6 ? ILI9341_YELLOW : ILI9341_RED;

    char eColiConcentrationString[20];
    sprintf(eColiConcentrationString, "%i", eColiConcentration);
    uint16_t eColiConcentrationColor = eColiConcentration == -9 ? ILI9341_WHITE : eColiConcentration < 2 ? ILI9341_GREEN : eColiConcentration < 6 ? ILI9341_YELLOW : ILI9341_RED;

    char bacteriaThresholdString[20];
    sprintf(bacteriaThresholdString, "%i", bacteriaThreshold);
    uint16_t bacteriaThresholdColor = bacteriaThreshold == -9 ? ILI9341_WHITE : bacteriaThreshold < 2 ? ILI9341_GREEN : bacteriaThreshold < 6 ? ILI9341_YELLOW : ILI9341_RED;

    char lastModifedBuf[20];
    sprintf(lastModifedBuf, "%s  %s", lastModifed.substring(0, 10).c_str(), lastModifed.substring(11, 19).c_str());

    PrintData(0, "Stream Flow:", streamFlowString, "ft3/s", streamFlowColor);
    PrintData(1, "Gauge Height:", gaugeHeightString, "ft", gaugeHeightColor);
    PrintData(2, "Water temp.:", waterTempCString, "C", waterTempCColor);
    PrintData(3, "E-coli:", eColiConcentrationString, "C/sa", eColiConcentrationColor);
    PrintData(4, "Bac. threshold:", bacteriaThresholdString, "", bacteriaThresholdColor);    
    PrintData(6, "Date Retrieved:", "", "", ILI9341_WHITE);
    PrintData(7, lastModifedBuf, "", "", ILI9341_GREEN);
  }

  return true;
}

/*
bool GetParametersFromSDCard()
{
    File file = SD.open(wifiFilePath);

    Serial.println("Attempting to fetch parameters from SD card...");

    if (!file)
    {
        Serial.printf("Failed to open file: %s\n", wifiFilePath);
        file.close();
        return false;
    }
    else
    {
        DynamicJsonDocument doc(2048);
        DeserializationError error = deserializeJson(doc, file.readString());

        if (error)
        {
            Serial.print(F("DeserializeJson() failed: "));
            Serial.println(error.c_str());
            return false;
        }

        ssid = doc["ssid"].as<String>();
        password = doc["password"].as<String>();
        timeZone = doc["time zone"].as<String>();
        brightness = doc["brightness"].as<int>();         
    }
    file.close();
    return true;
}
*/

void DisplayLayout()
{
  int w = tft.width() - 1;
  int h = tft.height() - 1;

  tft.drawLine(0, 0, w, 0, ILI9341_BLUE);
  tft.drawLine(w, 0, w, h, ILI9341_BLUE);
  tft.drawLine(w, h, 0, h, ILI9341_BLUE);
  tft.drawLine(0, h, 0, 0, ILI9341_BLUE);

  tft.drawLine(0, 55, w, 55, ILI9341_BLUE);
  tft.drawLine(0, 205, w, 205, ILI9341_BLUE);

  tft.setTextSize(2);
  tft.setCursor(10, 215);
  tft.setTextColor(ILI9341_WHITE);
  tft.print("Status:");
}

void DisplayIndicator(String string, int x, int y, uint16_t color)
{
  int16_t x1, y1;
  uint16_t w, h;
  const int offset = 3;
  tft.setTextSize(2);
  tft.setCursor(x, y);
  tft.getTextBounds(string, x, y, &x1, &y1, &w, &h);
  tft.fillRect(x - offset, y - offset, w + offset, h + offset, color);
  tft.setTextColor(ILI9341_BLACK);
  tft.print(string);
}

void UpdateIndicators()
{
  DisplayIndicator("SD", 110, 215, sdStatus ? ILI9341_GREEN : ILI9341_RED);
  DisplayIndicator("WIFI", 165, 215, wifiStatus ? ILI9341_GREEN : ILI9341_RED);
  DisplayIndicator("API", 260, 215, apiStatus ? ILI9341_GREEN : ILI9341_RED);
}

void UpdateLocationIndicators()
{
  for (int i = 0; i < numLocations; i++)
  {
    stripLocations.setPixelColor(i, locations[i].status == fair ? GREEN : locations[i].status == caution ? YELLOW : locations[i].status == danger ? RED : locations[i].status == noData ? OFF : OFF);
  }
  stripLocations.show();
}

void setup()
{
  Serial.begin(115200);

  delay(10);
  Serial.println("River Conditions starting up...");

  stripLocations.begin();
  stripLocations.show();

  tft.begin();
  tft.fillScreen(ILI9341_BLACK);
  tft.setRotation(3);

  DisplayLayout();

  UpdateIndicators();

  if (!InitSDCard())
  {
    FatalError("Unable to init SD card.");
  }
  else
  {
    sdStatus = true;
  }

  if (!PopulateLocationInitFromSDCard())
  {
    FatalError("Failed to get location init data.\n(locations.json required)");
  }

  UpdateLocationIndicators();

  /*
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());


  HTTPClient http;
  http.begin("https://stations.waterreporter.org/19656/supplement.json"); //Specify destination for HTTP request

http.useHTTP10(true);

  int httpResponseCode = http.GET();

  Serial.print("HTTP CODE: ");
  Serial.println(httpResponseCode);

  if (httpResponseCode == 200)
  {

    Serial.print("Size: ");
    Serial.println(http.getSize());

Stream& response = http.getStream();

    DynamicJsonDocument doc(2048);

deserializeJson(doc, response);

    String stationName = doc["station"]["name"];
    Serial.println(stationName);

  }

  http.end();
  */

  /*
// Walk the JsonArray efficiently
for (JsonObject& elem : arr) {
JsonObject& forecast = elem["item"]["forecast"];
}
*/

  //String payload = http.getString();
  //payload = http.getString();
  //http.end();

  /*
    DynamicJsonDocument doc(850000);
    DeserializationError error = deserializeJson(doc, http.getString());
    if (error)
    {
        Serial.print(F("DeserializeJson() failed: "));
        Serial.println(error.c_str());
       // return false;
    }
    String stationName = doc["station"]["name"];
  Serial.println(stationName);
  */

  /*
 
  WiFiClient client;

  const int httpPort = 80;
  const char *host = "https://stations.waterreporter.org/19656/supplement.json";

  if (!client.connect(host, httpPort))
  {
    Serial.println("connection failed");
    return;
  }

  String url = "/19656/supplement.json";

  Serial.print("[Requesting URL: ");
  Serial.print(url);
  Serial.println("]");

  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: " + host + "\r\n" +
               "Connection: close\r\n\r\n");

  unsigned long timeout = millis();

  while (client.available() == 0)
  {
    if (millis() - timeout > 5000)
    {
      Serial.println(">>> Client Timeout !");
      client.stop();
    }
  }

  // Read all the lines of the reply from server and print them to Serial
  Serial.println("[RESPONSE]\n");
  while (client.available())
  {
    String line = client.readStringUntil('\r');
    Serial.print(line);
  }

  Serial.println();
  Serial.println("[closing connection]");

*/
}

void loop(void)
{

  delay(1000);

  static int locationIndex = 0;

  if (++locationIndex > numLocations - 1)
  {
    locationIndex = 0;
  }

  String stationDataJson;
  if (!GetJsonFromSDCard("/locations/" + String(locationIndex), &stationDataJson))
  {
    // Check SD card for connectivity.
    String path = "/locations.json";
    File file = SD.open(path);
    if (!file)
    {
      FatalError("SD card not detected.\nTurn off device and\nreinsert valid SD card.");
    }
    file.close();
  }

  UpdateLocationDataOnScreen(locationIndex, &stationDataJson);
}