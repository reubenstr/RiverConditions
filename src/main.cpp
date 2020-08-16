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

#define TFT_CS 5
#define TFT_DC 2

#define TFT_CLK 18
#define TFT_MOSI 23
#define TFT_MISO -1
#define TFT_RST 4

#define SD_CHIP_SELECT 22

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);

const char *ssid = "RedSky";
const char *password = "happyredcat";

// Stores static station data that is not provided by the API.
// Order of stations is the order of the LEDs.
struct Location
{
  String usgs[10];
  String wr[10];
  String shortName;
  String area;
} locations[30];

int numStations;

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

bool PopulateStationInitFromSDCard()
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

    for(int u = 0; u < doc["locations"][i]["USGS"].size(); u++)
    {
      locations[i].usgs[u] = doc["locations"][i]["USGS"][u].as<String>();
    }
    
    for(int w = 0; w < doc["locations"][w]["WR"].size(); u++)
    {
      locations[i].wr[w] = doc["locations"][i]["WR"][w].as<String>();
    }

    locations[i].shortName = doc["locations"][i]["shortName"].as<String>();
    locations[i].location = doc["locations"][i]["area"].as<String>();
  }

  return true;
}

void PrintData(int line, const char *text, const char *value, const char *units, uint16_t color)
{

  const int spacesPerLine = 25;
  int numCharacters = strlen(text) + strlen(value) + strlen(units);

  tft.setTextSize(2);
  tft.setCursor(5, 63 + line * 18);

  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK); 
  tft.printf(text);

  tft.setTextColor(color, ILI9341_BLACK);
  tft.print(value);

  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  tft.print(units);

  tft.printf("%-*s", spacesPerLine - numCharacters, "");
}

bool UpdateLocationDataOnScreen(int locationIndex, String *locationDataJson)
{

  //tft.fillRect(5, 40, tft.width() - 10, 180, ILI9341_BLACK);
  tft.setTextSize(3);
  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  tft.setCursor(5, 5);
  tft.printf("%-17s", locations[locationIndex].shortName.c_str());
  tft.setCursor(5, 30);
  tft.printf("%-17s", locations[locationIndex].location.c_str());

  tft.setTextSize(2);
  tft.setTextColor(ILI9341_GREEN, ILI9341_BLACK);

  DynamicJsonDocument doc(2048);
  DeserializationError error = deserializeJson(doc, *locationDataJson);

  if (error)
  {
    Serial.print(F("DeserializeJson() failed: "));
    Serial.println(error.c_str());

    uint16_t color;
    static unsigned int x = -1;
    x++;
    color = ILI9341_RED;
    //PrintData(0, "Stream Flow: ", 9 * x, " ft3/s", color);

    /*
    PrintDataText(0, "Station Data Incomplete");
    PrintDataText(1, "(Data will be downloaded)");
    PrintDataText(2, "");
    PrintDataText(3, "");
    PrintDataText(4, "");
    */
  }
  else
  {

    unsigned int streamFlow = doc["data"]["streamFlow"]["value"].as<unsigned int>();
    unsigned int gaugeHeight = doc["data"]["gaugeHeight"]["value"].as<unsigned int>();
    unsigned int waterTempC = doc["data"]["waterTempC"]["value"].as<unsigned int>();
    unsigned int eColiConcentration = doc["data"]["eColiConcentration"]["value"].as<unsigned int>();
    unsigned int bacteriaThreshold = doc["data"]["bacteriaThreshold"]["value"].as<unsigned int>();
    String lastModifed = doc["station"]["recordTime"];

    char streamFlowString[20];
    itoa(streamFlow, streamFlowString, 10);
    uint16_t streamFlowColor = streamFlow == -9 ? ILI9341_WHITE : streamFlow < 2000 ? ILI9341_GREEN : streamFlow < 4000 ? ILI9341_YELLOW : ILI9341_RED;

    char gaugeHeightString[20];
    itoa(gaugeHeight, gaugeHeightString, 10);
    uint16_t gaugeHeightColor = gaugeHeight == -9 ? ILI9341_WHITE : gaugeHeight < 2 ? ILI9341_GREEN : gaugeHeight < 6 ? ILI9341_YELLOW : ILI9341_RED;

    char waterTempCString[20];
    itoa(waterTempC, waterTempCString, 10);
    uint16_t waterTempCColor = waterTempC == -9 ? ILI9341_WHITE : waterTempC < 2 ? ILI9341_GREEN : waterTempC < 6 ? ILI9341_YELLOW : ILI9341_RED;

    char eColiConcentrationString[20];
    itoa(eColiConcentration, eColiConcentrationString, 10);
    uint16_t eColiConcentrationColor = eColiConcentration == -9 ? ILI9341_WHITE : eColiConcentration < 2 ? ILI9341_GREEN : eColiConcentration < 6 ? ILI9341_YELLOW : ILI9341_RED;

    char bacteriaThresholdString[20];
    itoa(bacteriaThreshold, bacteriaThresholdString, 10);
    uint16_t bacteriaThresholdColor = bacteriaThreshold == -9 ? ILI9341_WHITE : bacteriaThreshold < 2 ? ILI9341_GREEN : bacteriaThreshold < 6 ? ILI9341_YELLOW : ILI9341_RED;

    PrintData(0, "Stream Flow: ", streamFlowString, " ft3/s", streamFlowColor);
    PrintData(1, "Gauge Height: ",gaugeHeightString , " ft", gaugeHeightColor);
    PrintData(2, "Water temp.: ", waterTempCString, " C", waterTempCColor);
    PrintData(3, "E-coli: ", eColiConcentrationString, " C/sa", eColiConcentrationColor);
    PrintData(4, "Bac. threshold: ", bacteriaThresholdString, "", bacteriaThresholdColor);

    char lastModifedBuf[20];
    sprintf(lastModifedBuf, "%s | %s", lastModifed.substring(0,10).c_str(), lastModifed.substring(11,18).c_str());
    PrintData(6, "Date Retrieved: ", "", "", ILI9341_WHITE);
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
        ;
        cycleDelay = doc["cycle delay"].as<int>();
        ;
        metalSpot[0].percentage = doc["au alert percentage"].as<float>();
        ;
        metalSpot[1].percentage = doc["ag alert percentage"].as<float>();
        ;
        metalSpot[2].percentage = doc["pt alert percentage"].as<float>();
        ;
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

void DisplayData()
{
  tft.setCursor(5, 5);
  tft.setTextSize(3);
  tft.setTextColor(ILI9341_BLUE);
  tft.println("Opossum Creek");
  tft.println("Lynchburg, VA");
  tft.setTextSize(2);
  tft.setTextColor(ILI9341_GREEN);

  tft.println();
  tft.println("Status: Active");
  tft.println();
  tft.println("Discharge: 1253 ft3");
  tft.println();
  tft.println("Gage Height: 156 feet");
  tft.println();
  tft.println("Last Checked: 2020-05-05 : 10:45:78");
}

void PrintHeap()
{
}

void setup()
{
  Serial.begin(115200);

  delay(10);
  Serial.println("River Conditions starting up...");

  tft.begin();

  // read diagnostics (optional but can help debug problems)
  /*
  uint8_t x = tft.readcommand8(ILI9341_RDMODE);
  Serial.print("Display Power Mode: 0x");
  Serial.println(x, HEX);
  x = tft.readcommand8(ILI9341_RDMADCTL);
  Serial.print("MADCTL Mode: 0x");
  Serial.println(x, HEX);
  x = tft.readcommand8(ILI9341_RDPIXFMT);
  Serial.print("Pixel Format: 0x");
  Serial.println(x, HEX);
  x = tft.readcommand8(ILI9341_RDIMGFMT);
  Serial.print("Image Format: 0x");
  Serial.println(x, HEX);
  x = tft.readcommand8(ILI9341_RDSELFDIAG);
  Serial.print("Self Diagnostic: 0x");
  Serial.println(x, HEX);
  */

  tft.fillScreen(ILI9341_BLACK);
  tft.setRotation(3);

  bool sdStatus = true;
  bool wifiStatus = false;
  bool apiStatus = false;

  DisplayLayout();

  DisplayIndicator("SD", 110, 215, sdStatus ? ILI9341_GREEN : ILI9341_RED);
  DisplayIndicator("WIFI", 165, 215, wifiStatus ? ILI9341_GREEN : ILI9341_RED);
  DisplayIndicator("API", 260, 215, apiStatus ? ILI9341_GREEN : ILI9341_RED);

  if (!InitSDCard())
  {
    FatalError("Unable to init SD card.");
  }

  if (!PopulateStationInitFromSDCard())
  {
    FatalError("Failed to get station init data.");
  }

  /*
  const int stations[3] = {12, 14, 16};

  StaticJsonDocument<4096> doc;
  JsonArray arrayStations = doc.createNestedArray("stations");

  for (int i = 0; i < sizeof(stations) / sizeof(stations[0]); i++)
  {
    StaticJsonDocument<1024> arrayStationData;

    arrayStationData["id"] = stations[i];
    arrayStationData["name"] = "N/A";
    arrayStationData["isActive"] = true;
    arrayStationData["description"] = "station description";
    arrayStationData["ledIndex"] = i;

    JsonObject nestedVariable = arrayStationData.createNestedObject("data");

    JsonObject bacteria_threshold = nestedVariable.createNestedObject("bacteria_threshold");
    bacteria_threshold["date"] = "10-10-10";
    bacteria_threshold["value"] = -1;
    JsonObject water_temp_c = nestedVariable.createNestedObject("water_temp_c");
    water_temp_c["date"] = "10-10-10";
    water_temp_c["value"] = -1;
    JsonObject e_coli_concentration = nestedVariable.createNestedObject("e_coli_concentration");
    e_coli_concentration["date"] = "10-10-10";
    e_coli_concentration["value"] = -1;
    JsonObject stream_flow = nestedVariable.createNestedObject("stream_flow");
    stream_flow["date"] = "10-10-10";
    stream_flow["value"] = -1;
    JsonObject gauge_height = nestedVariable.createNestedObject("gauge_height");
    gauge_height["date"] = "10-10-10";
    gauge_height["value"] = -1;

    arrayStations.add(arrayStationData);
  }

  Serial.println();
  serializeJsonPretty(doc, Serial);

  //DisplayData();
  */

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

  if (++locationIndex > numStations - 1)
  {
    locationIndex = 0;
  }

  String stationDataJson;

  if (!GetJsonFromSDCard(String(locationIndex), &stationDataJson))
  {
    //FatalError("Failed to get station " + stations[stationIndex].id + "'s data.");
    UpdateLocationDataOnScreen(locationIndex, &stationDataJson);
  }
  else
  {
    UpdateLocationDataOnScreen(locationIndex, &stationDataJson);
  }

  //Serial.print(stationDataJson);
}