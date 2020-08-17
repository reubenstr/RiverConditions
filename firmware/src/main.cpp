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
#include <Time.h>
#include "utilities.h"

#define TFT_CS 5
#define TFT_DC 2
#define TFT_CLK 18
#define TFT_MOSI 23
#define TFT_MISO -1
#define TFT_RST 4
#define SD_CHIP_SELECT 22
#define PIN_STRIP_LOCATIONS 15

const int numLedLocations = 30;
const int daysDataIsValid = 7;

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
  String stationIds[10]; // Station IDs.
  String shortName;      // Short name of location.
  String area;           // Name of general station area.
  LocationStatus status; // Status of the location.
} locations[30];

bool sdStatus = false;
bool wifiStatus = false;
bool apiStatus = false;

int numLocations;
int selectedLoctionIndex;
String currentTime;
int displayScreen;

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
    for (int u = 0; u < doc["locations"][i]["stationIds"].size(); u++)
    {
      locations[i].stationIds[u] = doc["locations"][i]["WR"][u].as<String>();
    }
    locations[i].shortName = doc["locations"][i]["shortName"].as<String>();
    locations[i].area = doc["locations"][i]["area"].as<String>();
  }

  return true;
}

uint16_t safetyStringToColor(const char * str)
{
  return !strcmp(str, "N.A.") ? ILI9341_WHITE : !strcmp(str, "Fair") ? ILI9341_GREEN : !strcmp(str, "Caution") ? ILI9341_YELLOW : !strcmp(str, "Danger") ? ILI9341_RED : ILI9341_WHITE;
}

void PrintData(int line, const char *text, const char *value, const char *units, uint16_t color)
{
  tft.setTextSize(2);
  tft.setCursor(5, 63 + line * 18);

  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  tft.printf("%-*s", 15, text);

  tft.setTextColor(color, ILI9341_BLACK);
  tft.printf("%-*s", 5, value);

  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  tft.printf("%-*s", 5, units);
}

void PrinInfo(int line, const char *text, uint16_t color)
{
  tft.setTextSize(2);
  tft.setCursor(5, 63 + line * 18);

  tft.setTextColor(color, ILI9341_BLACK);
  tft.printf("%-*s", 25, text);
}

bool UpdateLocationDataOnScreen(int locationIndex, String *locationDataJson, int displayScreen)
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

    PrinInfo(0, "Location data not found", ILI9341_RED);
    PrinInfo(1, "on SD card.", ILI9341_RED);
    PrinInfo(2, "", ILI9341_RED);
    PrinInfo(3, "Data will be downloaded", ILI9341_RED);
    PrinInfo(4, "from API shortly.", ILI9341_RED);
    PrinInfo(5, "", ILI9341_RED);
    PrinInfo(6, "", ILI9341_RED);
    PrinInfo(7, "", ILI9341_RED);
  }
  else
  {
    // Get data from json.
    String usgsId = doc["station"]["usgsId"].as<String>();
    String wrId = doc["station"]["wrId"].as<String>();
    String lastModifed = doc["station"]["recordTime"];

    // Init displaying variables.
    const char stationTypes[4][9] = {"N/A     ", "USGS    ", "WR      ", "USGS, WR"};
    int stationTypeIndex = !usgsId.isEmpty() && !wrId.isEmpty() ? 3 : usgsId.isEmpty() ? 1 : wrId.isEmpty() ? 2 : 0;

    char lastModifedBuf[20];
    sprintf(lastModifedBuf, "%s  %s", lastModifed.substring(0, 10).c_str(), lastModifed.substring(11, 19).c_str());

    if (displayScreen == 0)
    {
      PrintData(0, "Stream Flow:", doc["data"]["streamFlow"]["value"], "ft3/s", safetyStringToColor(doc["data"]["streamFlow"]["safety"]));
      PrintData(1, "Gauge Height:",doc["data"]["gaugeHeight"]["value"], "ft", safetyStringToColor(doc["data"]["gaugeHeight"]["safety"]));
      PrintData(2, "Water temp.:", doc["data"]["waterTempC"]["value"], "C", safetyStringToColor(doc["data"]["waterTempC"]["safety"]));
      PrintData(3, "E-coli:", doc["data"]["eColiConcentration"]["value"], "C/sa", safetyStringToColor(doc["data"]["eColiConcentration"]["safety"]));
      PrintData(4, "Bac. threshold:", doc["data"]["bacteriaThreshold"]["value"], "", safetyStringToColor(doc["data"]["bacteriaThreshold"]["safety"]));
      PrintData(5, "Station types:", stationTypes[stationTypeIndex], "", ILI9341_BLUE);
      PrinInfo(6, "Date Retrieved:", ILI9341_WHITE);
      PrinInfo(7, lastModifedBuf, ILI9341_WHITE);
    }
    else if (displayScreen == 1)
    {

      uint16_t color = AreDateTimesWithinNDays(currentTime, doc["data"]["streamFlow"]["date"].as<String>(), daysDataIsValid) ? ILI9341_GREEN : ILI9341_RED;
      PrintData(0, "Stream Flow:", doc["data"]["streamFlow"]["date"].as<String>().substring(0, 10).c_str(), "", color);

      color = AreDateTimesWithinNDays(currentTime, doc["data"]["gaugeHeight"]["date"].as<String>(), daysDataIsValid) ? ILI9341_GREEN : ILI9341_RED;
      PrintData(1, "Gauge Height:", doc["data"]["gaugeHeight"]["date"].as<String>().substring(0, 10).c_str(), "", color);

      color = AreDateTimesWithinNDays(currentTime, doc["data"]["waterTempC"]["date"].as<String>(), daysDataIsValid) ? ILI9341_GREEN : ILI9341_RED;
      PrintData(2, "Water temp.:", doc["data"]["waterTempC"]["date"].as<String>().substring(0, 10).c_str(), "", color);

      color = AreDateTimesWithinNDays(currentTime, doc["data"]["eColiConcentration"]["date"].as<String>(), daysDataIsValid) ? ILI9341_GREEN : ILI9341_RED;
      PrintData(3, "E-coli:", doc["data"]["eColiConcentration"]["date"].as<String>().substring(0, 10).c_str(), "", color);

      color = AreDateTimesWithinNDays(currentTime, doc["data"]["bacteriaThreshold"]["date"].as<String>(), daysDataIsValid) ? ILI9341_GREEN : ILI9341_RED;
      PrintData(4, "Bac. threshold:", doc["data"]["bacteriaThreshold"]["date"].as<String>().substring(0, 10).c_str(), "", color);
    }
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
  static int oldStatusSum = 5;
  int statusSum = (int)sdStatus + (int)wifiStatus + (int)apiStatus;

  if (oldStatusSum != statusSum)
  {
    oldStatusSum = statusSum;
    DisplayIndicator("SD", 110, 215, sdStatus ? ILI9341_GREEN : ILI9341_RED);
    DisplayIndicator("WIFI", 165, 215, wifiStatus ? ILI9341_GREEN : ILI9341_RED);
    DisplayIndicator("API", 260, 215, apiStatus ? ILI9341_GREEN : ILI9341_RED);
  }
}

void UpdateLocationIndicators()
{
  static int oldSelectedLoctionIndex;

  if (oldSelectedLoctionIndex != selectedLoctionIndex)
  {
    oldSelectedLoctionIndex = selectedLoctionIndex;

    for (int i = 0; i < numLocations; i++)
    {
      stripLocations.setPixelColor(i, locations[i].status == fair ? GREEN : locations[i].status == caution ? YELLOW : locations[i].status == danger ? RED : locations[i].status == noData ? OFF : OFF);
    }
    stripLocations.show();
  }
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

  // TEMP TEMP TEMP
  currentTime = "2020-08-15T19:15:00.000-04:00";

  delay(1000);

  if (++selectedLoctionIndex > numLocations - 1)
  {
    selectedLoctionIndex = 0;
  }

  UpdateIndicators();

  UpdateLocationIndicators();

  static int oldSelectedLoctionIndex;

  if (oldSelectedLoctionIndex != selectedLoctionIndex)
  {
    oldSelectedLoctionIndex = selectedLoctionIndex;

    String locationDataJson;
    if (!GetJsonFromSDCard("/locations/" + String(selectedLoctionIndex), &locationDataJson))
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

    unsigned long m = millis();
    UpdateLocationDataOnScreen(selectedLoctionIndex, &locationDataJson, displayScreen);
    Serial.printf("Time to print data: %u\n", millis() - m);
  }
}