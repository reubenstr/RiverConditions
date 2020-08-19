/*
* James River Conditions Dashboard
*
* Retrieves river condition data from various API then displays
* river safety indicators on a physical graphical map using LEDs
* as testing station locations.
*
* Reuben Strangelove
* Summer 2020
*
* Calls custom API midpoint which collects station data
* from two APIs endpoints (USGS and Water Reporter).
* Due to the large and complex json reponse from the endpoints, 
* The midpoint is reponsible for compressing the station data into 
* a smaller json chunk (with minimal data manipulation).
*
*
* MCU: ESP32 (ESP32 DEV KIT 1.0)
* Extra hardware: ILI9341 tft display, generic SD-Card reader, WS2812b led strips
*
* Locations (containing one or more stations) are cached on the SD card.
* Location parameters (name, area, station ids, etc.) are stored on the SD card: locations.json.
*/

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
#include "msTimer.h" // local library
#include "flasher.h" // local library

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
const int maxStationIds = 10;
const int maxLocations = 50;
struct Location
{
  String stationIds[maxStationIds]; // Station IDs.
  String shortName;                 // Short name of location.
  String area;                      // Name of general station area.
  LocationStatus status;            // Status of the location.
} locations[maxLocations];

bool sdStatus = false;
bool wifiStatus = false;
bool timeApiStatus = false;
bool dataApiStatus = false;

String dataApiErrorDate;
String dataApiErrorMessage;

int numLocations;
int selectedLoctionIndex;
String currentTime;
int displayScreen;

String timeZone = "EST";

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

bool SaveDataToSDCard(int locationIndex, String data)
{
  String path = "/locations/" + String(locationIndex) + ".json";
  Serial.printf("Writing file: %s\n", path.c_str());

  File file = SD.open(path, FILE_WRITE);
  if (!file)
  {
    Serial.println("Failed to open file for writing.");
    return false;
  }
  if (!file.print(data))
  {
    Serial.println("Write failed.");
    return false;
  }
  file.close();
  return true;
}

bool GetJsonFromSDCard(String fileName, String *locationDataJson)
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

  *locationDataJson = file.readString();

  file.close();
  return true;
}

bool InitLocationsFromSDCard()
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
      locations[i].stationIds[u] = doc["locations"][i]["stationIds"][u].as<String>();
    }
    locations[i].shortName = doc["locations"][i]["shortName"].as<String>();
    locations[i].area = doc["locations"][i]["area"].as<String>();
  }

  return true;
}

uint16_t safetyStringToColor(const char *str)
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
      PrintData(1, "Gauge Height:", doc["data"]["gaugeHeight"]["value"], "ft", safetyStringToColor(doc["data"]["gaugeHeight"]["safety"]));
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
  int statusSum = (int)sdStatus + (int)wifiStatus + (int)dataApiStatus + (int)timeApiStatus;

  if (oldStatusSum != statusSum)
  {
    oldStatusSum = statusSum;
    DisplayIndicator("SD", 110, 215, sdStatus ? ILI9341_GREEN : ILI9341_RED);
    DisplayIndicator("WIFI", 165, 215, wifiStatus ? ILI9341_GREEN : ILI9341_RED);

    int apiVal = (int)dataApiStatus + (int)timeApiStatus;
    DisplayIndicator("API", 260, 215, apiVal == 0 ? ILI9341_RED : apiVal == 1 ? ILI9341_YELLOW : apiVal == 2 ? ILI9341_GREEN : ILI9341_BLUE);
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

bool UpdateTime()
{
  String payload;
  String host = "http://worldclockapi.com/api/json/" + timeZone + "/now";

  Serial.print("Connecting to ");
  Serial.println(host);

  HTTPClient http;
  http.begin(host);
  int httpCode = http.GET();

  if (httpCode > 0)
  {
    Serial.print("HTTP code: ");
    Serial.println(httpCode);
    Serial.println("[RESPONSE]");
    payload = http.getString();
    Serial.println(payload);
    http.end();
  }
  else
  {
    Serial.print("Connection failed, HTTP client code: ");
    Serial.println(httpCode);
    http.end();
    return false;
  }

  DynamicJsonDocument doc(2048);
  DeserializationError error = deserializeJson(doc, payload);

  if (error)
  {
    Serial.print(F("DeserializeJson() failed: "));
    Serial.println(error.c_str());
    return false;
  }

  String currentTime = doc["currentDateTime"];
  Serial.printf("Current time: %s", currentTime.c_str());

  return true;
}

bool GetDataFromAPI(int loctionIndex)
{
  String payload;
  String host = "http://artofmystate.com/api/riverconditions.php?stationId=" + locations[loctionIndex].stationIds[0];

  for (int i = 1; i < maxStationIds; i++)
  {
    if (!locations[loctionIndex].stationIds[i].isEmpty())
    {
      host += "," + locations[loctionIndex].stationIds[i];
    }
  }

  Serial.print("Connecting to ");
  Serial.println(host);

  HTTPClient http;
  http.begin(host);
  int httpCode = http.GET();

  if (httpCode > 0)
  {
    Serial.print("HTTP code: ");
    Serial.println(httpCode);
    Serial.println("[RESPONSE]");
    payload = http.getString();
    Serial.println(payload);
    http.end();
  }
  else
  {
    Serial.print("Connection failed, HTTP client code: ");
    Serial.println(httpCode);
    http.end();
    return false;
  }

  DynamicJsonDocument doc(2048);
  DeserializationError error = deserializeJson(doc, payload);

  if (error)
  {
    Serial.print(F("DeserializeJson() failed: "));
    Serial.println(error.c_str());
    return false;
  }

  if (!doc["error"].isNull())
  {
    dataApiErrorDate = doc["date"].as<String>();
    dataApiErrorMessage = doc["message"].as<String>();
    return false;
  }

  SaveDataToSDCard(loctionIndex, payload);

  return true;
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

  if (!InitSDCard())
  {
    FatalError("Unable to init SD card.");
  }
  else
  {
    sdStatus = true;
  }

  if (!InitLocationsFromSDCard())
  {
    FatalError("Failed to get location init data.\n(locations.json required)");
  }

  DisplayLayout();

  UpdateIndicators();

  UpdateLocationIndicators();

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
}

void loop(void)
{

  /* Fair, Caution, Danger, No Data

  Fair:
  Bacteria below threshold.
  Streamflow below threshold.
  

  Caution:
  Bacteria above fair threshold.
  Streamflow above fair threshold.
  Any two in the caution area triggers a caution.

  Danger:
  Bacteria above caution threshold.
  Streamflow above caution threshold.
  AAny two in the danger area triggers a danger.

*/

  static msTimer timerApi(5000);
  static msTimer timerTime(0);

  if (timerTime.elapsed())
  {
    timerTime.setDelay(60000);
    timeApiStatus = UpdateTime();
  }

  if (timerApi.elapsed())
  {
    dataApiStatus = GetDataFromAPI(selectedLoctionIndex);

    if (++selectedLoctionIndex > numLocations - 1)
    {
      selectedLoctionIndex = 0;
    }
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
    Serial.printf("Time to print data on tft: %ums\n", (unsigned int)(millis() - m));
  }
}