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
* Extra hardware: TFT tft display, generic SD-Card reader, WS2812b led strips
*
* Locations (containing one or more stations) are cached on the SD card.
* Location parameters (name, area, station ids, etc.) are stored on the SD card: locations.json.
*/

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <SD.h>
#include <Adafruit_NeoPixel.h>
#include "utilities.h" // local library
#include "msTimer.h"   // local library
#include "flasher.h"   // local library

#include <TFT_eSPI.h> // https://github.com/Bodmer/TFT_eSPI


/*
The following defines are required for the TFT_eSPI library.
The are to be placed in the library's User_Setup.h.
Remove conflicting defines.
#define ILI9488_DRIVER 
#define TFT_WIDTH  320
#define TFT_HEIGHT 480
#define TFT_CS 5
#define TFT_DC 2
#define TFT_SCLK 18
#define TFT_MOSI 23
#define TFT_MISO 19
#define TFT_RST 4
*/

#define SD_CHIP_SELECT 22
#define PIN_STRIP_LOCATIONS 15

const int numLedLocations = 30;
const int daysDataIsValid = 7;
const int textIndent = 15;
const int textStatusY = 293;

//Adafruit_TFT tft = Adafruit_TFT(TFT_CS, TFT_DC, TFT_RST);
TFT_eSPI tft = TFT_eSPI();

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
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(TFT_RED);
  tft.setCursor(0, 0);
  tft.println("Fatal error.");
  tft.println();
  tft.print(errorMsg);

  Serial.println(errorMsg);

  while (1)
  {
  }
}

void charBounds(char c, int16_t *x, int16_t *y,
                int16_t *minx, int16_t *miny, int16_t *maxx, int16_t *maxy)
{
  if (c == '\n')
  {                         // Newline?
    *x = 0;                 // Reset x to zero,
    *y += tft.textsize * 8; // advance y one line
                            // min/max x/y unchaged -- that waits for next 'normal' character
  }
  else if (c != '\r')
  { // Normal char; ignore carriage returns
    if (/*wrap*/ false && ((*x + tft.textsize * 6) > tft.width()))
    {                         // Off right?
      *x = 0;                 // Reset x to zero,
      *y += tft.textsize * 8; // advance y one line
    }
    int x2 = *x + tft.textsize * 6 - 1, // Lower-right pixel of char
        y2 = *y + tft.textsize * 8 - 1;
    if (x2 > *maxx)
      *maxx = x2; // Track max x, y
    if (y2 > *maxy)
      *maxy = y2;
    if (*x < *minx)
      *minx = *x; // Track min x, y
    if (*y < *miny)
      *miny = *y;
    *x += tft.textsize * 6; // Advance x one char
  }
}

// Solution provided by : https://github.com/Bodmer/TFT_eSPI/issues/6
void getTextBounds(const char *str, int16_t x, int16_t y,
                   int16_t *x1, int16_t *y1, uint16_t *w, uint16_t *h)
{
  uint8_t c; // Current character

  *x1 = x;
  *y1 = y;
  *w = *h = 0;

  int16_t minx = tft.width(), miny = tft.width(), maxx = -1, maxy = -1;

  while ((c = *str++))
    charBounds(c, &x, &y, &minx, &miny, &maxx, &maxy);

  if (maxx >= minx)
  {
    *x1 = minx;
    *w = maxx - minx + 1;
  }
  if (maxy >= miny)
  {
    *y1 = miny;
    *h = maxy - miny + 1;
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
  return !strcmp(str, "N.A.") ? TFT_WHITE : !strcmp(str, "Fair") ? TFT_GREEN : !strcmp(str, "Caution") ? TFT_YELLOW : !strcmp(str, "Danger") ? TFT_RED : TFT_WHITE;
}

void PrintData(int line, const char *text, const char *value, const char *units, uint16_t color)
{

  int spaces = strlen(value) > 5 ? 9 - (strlen(value) - 5) : 9;

  tft.setTextSize(2);
  tft.setCursor(textIndent, 87 + line * 21);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.printf("%-*s", 20, text);

  tft.setTextColor(color, TFT_BLACK);
  tft.printf("%-*s", 5, value);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.printf("%-*s", spaces, strcmp(value, "N.A.") == 0 ? "" : units);
}

void PrinInfo(int line, const char *text, uint16_t color)
{
  tft.setTextSize(2);
  tft.setCursor(textIndent, 87 + line * 21);

  tft.setTextColor(color, TFT_BLACK);
  tft.printf("%-*s", 25, text);
}

bool UpdateLocationDataOnScreen(int locationIndex, String *locationDataJson, int displayScreen)
{
  tft.setTextSize(3);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(textIndent, 14);
  tft.printf("%-24s", locations[locationIndex].shortName.c_str());
  tft.setCursor(textIndent, 44);
  tft.printf("%-24s", locations[locationIndex].area.c_str());

  tft.setTextSize(2);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);

  DynamicJsonDocument doc(2048);
  DeserializationError error = deserializeJson(doc, *locationDataJson);

  if (error)
  {
    Serial.print(F("DeserializeJson() failed: "));
    Serial.println(error.c_str());

    char locationString[50];
    sprintf(locationString, "(filename: %u.json, was not found.)", locationIndex);

    PrinInfo(0, "Location data not found", TFT_RED);
    PrinInfo(1, "on SD card.", TFT_RED);
    PrinInfo(2, "", TFT_RED);
    PrinInfo(3, "Data will be downloaded", TFT_RED);
    PrinInfo(4, "from API shortly.", TFT_RED);
    PrinInfo(5, "", TFT_RED);
    PrinInfo(6, "", TFT_RED);
    PrinInfo(7, "", TFT_RED);
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

    char lastModifedDateBuf[20];
    sprintf(lastModifedDateBuf, "%s", lastModifed.substring(0, 10).c_str());
    char lastModifedTimeBuf[20];
    sprintf(lastModifedTimeBuf, "%s", lastModifed.substring(11, 19).c_str());

    if (displayScreen == 0)
    {
      PrintData(0, "Stream Flow:", doc["data"]["streamFlow"]["value"], "ft3/s", safetyStringToColor(doc["data"]["streamFlow"]["safety"]));
      PrintData(1, "Gauge Height:", doc["data"]["gaugeHeight"]["value"], "ft", safetyStringToColor(doc["data"]["gaugeHeight"]["safety"]));
      PrintData(2, "Water temperature:", doc["data"]["waterTempC"]["value"], "C", safetyStringToColor(doc["data"]["waterTempC"]["safety"]));
      PrintData(3, "E. Coli:", doc["data"]["eColiConcentration"]["value"], "col/samp.", safetyStringToColor(doc["data"]["eColiConcentration"]["safety"]));
      PrintData(4, "Bacteria threshold:", doc["data"]["bacteriaThreshold"]["safety"], "", safetyStringToColor(doc["data"]["bacteriaThreshold"]["safety"]));
      PrintData(6, "Station type(s):", stationTypes[stationTypeIndex], "", TFT_BLUE);
      PrintData(7, "Date Retrieved:", lastModifedDateBuf, "", TFT_WHITE);
      PrintData(8, "(from endpoint)", lastModifedTimeBuf, "", TFT_WHITE);
    }
    else if (displayScreen == 1)
    {

      uint16_t color = AreDateTimesWithinNDays(currentTime, doc["data"]["streamFlow"]["date"].as<String>(), daysDataIsValid) ? TFT_GREEN : TFT_RED;
      PrintData(0, "Stream Flow:", doc["data"]["streamFlow"]["date"].as<String>().substring(0, 10).c_str(), "", color);

      color = AreDateTimesWithinNDays(currentTime, doc["data"]["gaugeHeight"]["date"].as<String>(), daysDataIsValid) ? TFT_GREEN : TFT_RED;
      PrintData(1, "Gauge Height:", doc["data"]["gaugeHeight"]["date"].as<String>().substring(0, 10).c_str(), "", color);

      color = AreDateTimesWithinNDays(currentTime, doc["data"]["waterTempC"]["date"].as<String>(), daysDataIsValid) ? TFT_GREEN : TFT_RED;
      PrintData(2, "Water temp.:", doc["data"]["waterTempC"]["date"].as<String>().substring(0, 10).c_str(), "", color);

      color = AreDateTimesWithinNDays(currentTime, doc["data"]["eColiConcentration"]["date"].as<String>(), daysDataIsValid) ? TFT_GREEN : TFT_RED;
      PrintData(3, "E-coli:", doc["data"]["eColiConcentration"]["date"].as<String>().substring(0, 10).c_str(), "", color);

      color = AreDateTimesWithinNDays(currentTime, doc["data"]["bacteriaThreshold"]["date"].as<String>(), daysDataIsValid) ? TFT_GREEN : TFT_RED;
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
  int t = 5;

  tft.fillScreen(TFT_BLACK);

  // Perimeter
  tft.fillRect(0, 0, w, t, TFT_BLUE);
  tft.fillRect(w - t, 0, w, h, TFT_BLUE);
  tft.fillRect(0, h - t, tft.width(), 5, TFT_BLUE);
  tft.fillRect(0, 0, 0 + t, h, TFT_BLUE);

  // Lines across.
  tft.fillRect(0, 73, tft.width(), 5, TFT_BLUE);
  tft.fillRect(0, 280, tft.width(), 5, TFT_BLUE);

  tft.setTextSize(2);
  tft.setCursor(textIndent, textStatusY);
  tft.setTextColor(TFT_WHITE);
  tft.print("System Status:");
}

void DisplayIndicator(String string, int x, int y, uint16_t color)
{
  int16_t x1, y1;
  uint16_t w, h;
  const int offset = 2;
  tft.setTextSize(2);
  tft.setCursor(x, y);

  getTextBounds(string.c_str(), x, y, &x1, &y1, &w, &h);
  tft.fillRect(x - offset, y - offset, w + offset, h + offset, color);
  tft.setTextPadding(5);
  tft.setTextColor(TFT_BLACK, color);
  tft.print(string);
}

void UpdateIndicators()
{
  static int oldStatusSum = 5;
  int statusSum = (int)sdStatus + (int)wifiStatus + (int)dataApiStatus + (int)timeApiStatus;

  if (oldStatusSum != statusSum)
  {
    oldStatusSum = statusSum;
    int apiVal = (int)dataApiStatus + (int)timeApiStatus;

    DisplayIndicator("SD", 210, textStatusY, sdStatus ? TFT_GREEN : TFT_RED);
    DisplayIndicator("WIFI", 267, textStatusY, wifiStatus ? TFT_GREEN : TFT_RED);
    DisplayIndicator("API", 347, textStatusY, apiVal == 0 ? TFT_RED : apiVal == 1 ? TFT_YELLOW : apiVal == 2 ? TFT_GREEN : TFT_BLUE);
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
  // String host = "http://worldclockapi.com/api/json/" + timeZone + "/now"; // currentDateTime
  String host = "http://worldtimeapi.org/api/timezone/" + timeZone;

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

  String currentTime = doc["datetime"];
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
  tft.fillScreen(TFT_BLACK);
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

  ////////////////////

  //drawSdJpeg("/jamesriver.jpg", 0, 0);
  //while(1){};

  ////////////////////

  UpdateLocationIndicators();

  tft.setTextSize(2);
  tft.setTextColor(TFT_GREEN);
  tft.printf("Connecting to WiFi\n");
  tft.printf("SSID: %s\n", ssid);
  tft.printf("Password: %s\n", password);

  Serial.printf("Connecting to SSID: %s, with password: %s\n", ssid, password);


  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    tft.print(".");
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  DisplayLayout();

  UpdateIndicators();
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
  Any two in the danger area triggers a danger.

*/

  static msTimer timerApi(5000);
  static msTimer timerTime(0);

  if (WiFi.status() == WL_CONNECTED)
  {
    wifiStatus = true;

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
  }
  else
  {
    wifiStatus = true;
    dataApiStatus = false;
    timeApiStatus = false;
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