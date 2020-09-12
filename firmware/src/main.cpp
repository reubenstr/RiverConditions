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
#include "utilities.h" // local library
#include "msTimer.h"   // local library
#include "flasher.h"   // local library

#include <TFT_eSPI.h>  // https://github.com/Bodmer/TFT_eSPI
#include <JC_Button.h> // https://github.com/JChristensen/JC_Button

#include "FastLED.h"

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

#define PIN_BUTTON_RIGHT 36
#define PIN_BUTTON_SELECT 39
#define PIN_BUTTON_LEFT 34
#define PIN_INDICATOR_SIGN 32
#define PIN_INDICATOR_RIGHT 33
#define PIN_INDICATOR_SELECT 25
#define PIN_INDICATOR_LEFT 26
#define PIN_STRIP_LOCATIONS 27
#define PIN_SD_CHIP_SELECT 22

const unsigned long timeBetweenApiCalls = 60000; // Time in milliseconds between API calls for location data.
const unsigned long timeBetweenIndicatorUpdate = 300000;

const int numLEDs = 27; //23 locations and 4 legends
const int daysDataIsValid = 7;
const int textIndent = 15;
const int textStatusY = 293;

int indicatorBrightness = 127;
int signBrightness = 127;

TFT_eSPI tft = TFT_eSPI();

CRGB leds[numLEDs];

Button buttonLeft(PIN_BUTTON_LEFT, 25, false, true);
Button buttonSelect(PIN_BUTTON_SELECT, 25, false, true);
Button buttonRight(PIN_BUTTON_RIGHT, 25, false, true);

struct WifiCredentials
{
  String ssid;
  String password;
} wifiCredentials[10];

const char *wifiFilePath = "/wifi.txt";
int numWifiCredentials = 0;
String timeZone = "EST";

// Location data contains IDs of associated stations.
// Order of location is order of LEDs.
const int maxStationIds = 10;
const int maxLocations = 50;
struct Location
{
  String stationIds[maxStationIds]; // Station IDs.
  String shortName;                 // Short name of location.
  String area;                      // Name of general station area.
  String status;                    // Status of the location.
} locations[maxLocations];

bool sdStatus = false;
bool wifiStatus = false;
bool timeApiStatus = false;
bool dataApiStatus = false;

String dataApiErrorDate = "No error.";
String dataApiErrorMessage = "No error.";

int numLocations;
int selectedLoctionIndex;
int apiLoctionIndex = 0;
String currentTime;

int displayScreen;
const int numDisplayScreens = 2;

const uint32_t OFF = 0x0000000;
const uint32_t RED = 0x00FF0000;
const uint32_t GREEN = 0x0000FF00;
const uint32_t BLUE = 0x000000FF;
const uint32_t YELLOW = 0x00F0F000;

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

  DynamicJsonDocument doc(4096);
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

  Serial.printf("Number of locations found on SD card: %u.\n", numLocations);

  return true;
}

void UpdateLocationIndicators(bool allOffFlag = false)
{
  static msTimer timerUpdateStatusBuffer(0);
  static msTimer timerUpdateLEDs(50);

  FastLED.setBrightness(indicatorBrightness);

  // Turn off all indicators.
  if (allOffFlag)
  {
    for (int i = 0; i < numLEDs; i++)
    {
      leds[i] = CRGB::Black;
      delay(10);
      FastLED.show();
    }
    return;
  }

  // Refresh status data from SD card.
  if (timerUpdateStatusBuffer.elapsed())
  {
    timerUpdateStatusBuffer.setDelay(timeBetweenIndicatorUpdate);

    Serial.println("Getting location status from location data stored on SD card.");

    for (int i = 0; i < numLocations; i++)
    {
      String locationDataJson;
      if (GetJsonFromSDCard("/locations/" + String(i), &locationDataJson))
      {
        DynamicJsonDocument doc(2048);
        deserializeJson(doc, locationDataJson);
        locations[i].status = doc["station"]["locationStatus"].as<String>();
      }
    }
  }

  // Update all LEDs.
  static msTimer timerFlash(750);
  static bool flashToggle;

  if (timerFlash.elapsed())
  {
    flashToggle = !flashToggle;
  }

  if (timerUpdateLEDs.elapsed())
  {
    for (int i = 0; i < numLocations; i++)
    {
      leds[i] = locations[i].status == "Fair" ? GREEN : locations[i].status == "Caution" ? YELLOW : locations[i].status == "Danger" ? RED : locations[i].status == "N.A." ? OFF : OFF;
    }

    if (flashToggle)
      leds[selectedLoctionIndex] = CRGB::Blue;

    leds[numLEDs - 4] = CRGB::Green;
    leds[numLEDs - 3] = CRGB::Yellow;
    leds[numLEDs - 2] = CRGB::Red;
    leds[numLEDs - 1] = CRGB::Black;

    delay(10);
    FastLED.show();
  }
}

void FatalError(String errorMsg)
{

  UpdateLocationIndicators(true);

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

// Solution provided by : https://github.com/Bodmer/TFT_eSPI/issues/6
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

  while (!SD.begin(PIN_SD_CHIP_SELECT))
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
  const int spacesPerLine = 34;

  tft.setTextSize(2);
  tft.setCursor(textIndent, 87 + line * 21);

  tft.setTextColor(color, TFT_BLACK);
  tft.printf("%-*s", spacesPerLine, text);
}

void PrintTitle(const char *line1, const char *line2, uint16_t color)
{
  tft.setTextSize(3);
  tft.setTextColor(color, TFT_BLACK);
  tft.setCursor(textIndent, 14);
  tft.printf("%-24s", line1);
  tft.setCursor(textIndent, 44);
  tft.printf("%-24s", line2);
}

bool UpdateLocationDataOnScreen(int locationIndex, String *locationDataJson, int displayScreen)
{

  PrintTitle(locations[locationIndex].shortName.c_str(), locations[locationIndex].area.c_str(), TFT_WHITE);

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
    PrinInfo(8, "", TFT_RED);
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
      PrintData(5, "", "", "", TFT_BLUE);
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
      PrintData(2, "Water temperature:", doc["data"]["waterTempC"]["date"].as<String>().substring(0, 10).c_str(), "", color);

      color = AreDateTimesWithinNDays(currentTime, doc["data"]["eColiConcentration"]["date"].as<String>(), daysDataIsValid) ? TFT_GREEN : TFT_RED;
      PrintData(3, "E. Coli:", doc["data"]["eColiConcentration"]["date"].as<String>().substring(0, 10).c_str(), "", color);

      color = AreDateTimesWithinNDays(currentTime, doc["data"]["bacteriaThreshold"]["date"].as<String>(), daysDataIsValid) ? TFT_GREEN : TFT_RED;
      PrintData(4, "Bacteria threshold:", doc["data"]["bacteriaThreshold"]["date"].as<String>().substring(0, 10).c_str(), "", color);
    }
  }

  return true;
}

void UpdateDisplay()
{
  // Displaying the diagnostic screen takes priority
  if (displayScreen == 2)
  {
    PrintTitle("Diagnostics:", "", TFT_YELLOW);

    char buf[50];
    sprintf(buf, "Date/Time: %s", currentTime.substring(0, 19).c_str());
    PrinInfo(0, buf, TFT_YELLOW);
    sprintf(buf, "Selected location: %u", selectedLoctionIndex);
    PrinInfo(1, buf, TFT_YELLOW);
    sprintf(buf, "Next API location: %u", apiLoctionIndex);
    PrinInfo(2, buf, TFT_YELLOW);
    PrinInfo(3, "", TFT_YELLOW);
    sprintf(buf, "API Error: %s", dataApiErrorDate.c_str(), dataApiErrorMessage.c_str());
    PrinInfo(4, buf, TFT_YELLOW);
    sprintf(buf, "API Error: %s", dataApiErrorMessage.c_str());
    PrinInfo(5, buf, TFT_YELLOW);
    PrinInfo(6, "", TFT_YELLOW);
    PrinInfo(7, "", TFT_YELLOW);
    PrinInfo(8, "", TFT_YELLOW);
    return;
  }

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

    numWifiCredentials = doc["wifiCredentials"].size();

    for (int i = 0; i < numWifiCredentials; i++)
    {
      wifiCredentials[i].ssid = doc["wifiCredentials"][i]["ssid"].as<String>();
      wifiCredentials[i].password = doc["wifiCredentials"][i]["password"].as<String>();

      Serial.println(wifiCredentials[i].ssid);
      Serial.println(wifiCredentials[i].password);
    }

    timeZone = doc["timeZone"].as<String>();

    int indicatorBrightnessParameter = doc["indicatorBrightness"].as<int>();
    int signBrightnessParameter = doc["signBrightness"].as<int>();

    if (indicatorBrightnessParameter > 9 && indicatorBrightnessParameter < 256)
    {
      indicatorBrightness = indicatorBrightnessParameter;
    }

    if (signBrightnessParameter > 25 && signBrightnessParameter < 256)
    {
      signBrightness = signBrightnessParameter;
    }
  }
  file.close();
  return true;
}

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
  static int oldStatusSum = 99;
  int statusSum = (int)sdStatus + (int)wifiStatus + (int)dataApiStatus + (int)timeApiStatus;

  if (oldStatusSum != statusSum)
  {
    oldStatusSum = statusSum;
    DisplayIndicator("SD", 200, textStatusY, sdStatus ? TFT_GREEN : TFT_RED);
    DisplayIndicator("WIFI", 252, textStatusY, wifiStatus ? TFT_GREEN : TFT_RED);
    DisplayIndicator("TIME", 329, textStatusY, timeApiStatus ? TFT_GREEN : TFT_RED);
    DisplayIndicator("API", 407, textStatusY, dataApiStatus ? TFT_GREEN : TFT_RED);
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

  currentTime = doc["datetime"].as<String>();
  Serial.printf("Current time: %s\n", currentTime.c_str());

  return true;
}

bool GetDataFromAPI(int loctionIndex)
{
  String payload;
  String host = "http://artofmystate.com/api/riverconditions.php?stationId=" + locations[loctionIndex].stationIds[0];

  // Add remaining stations to query URL.
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
    dataApiErrorDate = currentTime;
    dataApiErrorMessage = httpCode;
    http.end();
    return false;
  }

  DynamicJsonDocument doc(2048);
  DeserializationError jsonError = deserializeJson(doc, payload);

  if (jsonError)
  {
    Serial.print(F("DeserializeJson() failed: "));
    Serial.println(jsonError.c_str());
    dataApiErrorDate = currentTime;
    dataApiErrorMessage = jsonError.c_str();
    return false;
  }

  if (doc["error"].as<bool>() == true)
  {
    dataApiErrorDate = doc["date"].as<String>();
    dataApiErrorMessage = doc["message"].as<String>();
    return false;
  }

  SaveDataToSDCard(loctionIndex, payload);

  return true;
}

void CheckButtons()
{

  // Illuminate buttons when pressed.
  digitalWrite(PIN_INDICATOR_LEFT, buttonLeft.isPressed());
  digitalWrite(PIN_INDICATOR_SELECT, buttonSelect.isPressed());
  digitalWrite(PIN_INDICATOR_RIGHT, buttonRight.isPressed());

  buttonLeft.read();
  buttonSelect.read();
  buttonRight.read();

  if (buttonLeft.wasPressed())
  {
    if (selectedLoctionIndex == 0)
    {
      selectedLoctionIndex = numLocations - 1;
    }
    else
    {
      selectedLoctionIndex--;
    }
  }

  if (buttonSelect.wasPressed())
  {
    if (++displayScreen > numDisplayScreens - 1)
    {
      displayScreen = 0;
    }
  }

  if (buttonSelect.pressedFor(3000))
  {
    displayScreen = 2;
  }

  if (buttonRight.wasPressed())
  {
    if (++selectedLoctionIndex > numLocations - 1)
    {
      selectedLoctionIndex = 0;
    }
  }
}

bool ConnectWifi()
{
  int wifiCredentialsIndex = 0;

  while (1)
  {
    tft.fillScreen(TFT_BLACK);
    tft.setCursor(0, 0);
    tft.setTextSize(2);
    tft.setTextColor(TFT_GREEN);
    tft.printf("Connecting to WiFi\n");
    tft.printf("SSID: %s\n", wifiCredentials[wifiCredentialsIndex].ssid.c_str());
    tft.printf("Password: %s\n", wifiCredentials[wifiCredentialsIndex].password.c_str());

    Serial.printf("Connecting to SSID: %s, with password: %s\n", wifiCredentials[wifiCredentialsIndex].ssid.c_str(), wifiCredentials[wifiCredentialsIndex].password.c_str());

    WiFi.begin(wifiCredentials[wifiCredentialsIndex].ssid.c_str(), wifiCredentials[wifiCredentialsIndex].password.c_str());

    int count = 0;
    while (count++ < 10)
    {
      delay(500);
      tft.print(".");
      Serial.print(".");

      if (WiFi.status() == WL_CONNECTED)
      {
        return true;
      }
    }

    Serial.println("");

    if (++wifiCredentialsIndex == numWifiCredentials)
    {
      // Wifi not connected, attempted all credentials.
      return false;
    }
  }
}

void setup()
{
  Serial.begin(115200);

  delay(10);
  Serial.println("River Conditions starting up...");

  FastLED.addLeds<APA106, PIN_STRIP_LOCATIONS>(leds, numLEDs);

  buttonLeft.begin();
  buttonSelect.begin();
  buttonRight.begin();

  pinMode(PIN_INDICATOR_LEFT, OUTPUT);
  pinMode(PIN_INDICATOR_SELECT, OUTPUT);
  pinMode(PIN_INDICATOR_RIGHT, OUTPUT);
  pinMode(PIN_INDICATOR_SIGN, OUTPUT);

  tft.begin();
  tft.fillScreen(TFT_BLACK);
  delay(25); // Delay required to allow rotation to take effect.
  tft.setRotation(1);
  delay(25); // Delay required to allow rotation to take effect.

  if (!InitSDCard())
  {
    FatalError("Unable to init SD card.");
  }
  else
  {
    sdStatus = true;
  }

  if (!GetParametersFromSDCard())
  {
    FatalError("Failed to get parameters from SD card.\n(wifi.txt required)");
  }

  if (!InitLocationsFromSDCard())
  {
    FatalError("Failed to get location init data.\n(locations.json required)");
  }

  UpdateLocationIndicators();

  const int indicatorSignChannel = 0;
  ledcSetup(0, 500, 8);
  ledcAttachPin(PIN_INDICATOR_SIGN, 0);
  ledcWrite(indicatorSignChannel, signBrightness);

  if (ConnectWifi())
  {
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
  }
  else
  {
    Serial.println("WiFi not connected.");
  }

  DisplayLayout();
}

void loop(void)
{
  static msTimer timerTime(0);
  static msTimer timerApi(0);

  CheckButtons();

  UpdateIndicators();

  UpdateLocationIndicators();

  // Screen display timeout.
  static int OldDisplayScreen;
  static msTimer timerDelayScreen(6000);
  if (OldDisplayScreen != displayScreen)
  {
    OldDisplayScreen = displayScreen;
    timerDelayScreen.resetDelay();
    Serial.printf("Display screen changed to screen: %u.\n", displayScreen);
    UpdateDisplay();
  }

  if (timerDelayScreen.elapsed())
  {
    displayScreen = 0;
    OldDisplayScreen = displayScreen;
  }

  // Update location on screen.
  static msTimer timerUpdateScreen(60000);
  static int oldSelectedLoctionIndex = 99;
  if (timerUpdateScreen.elapsed())
  {
    if (displayScreen == 2)
    {
      timerUpdateScreen.setDelay(500);
    }
    else
    {
      timerUpdateScreen.setDelay(60000);
    }
    oldSelectedLoctionIndex = 99;
  }
  if (oldSelectedLoctionIndex != selectedLoctionIndex)
  {
    oldSelectedLoctionIndex = selectedLoctionIndex;
    UpdateDisplay();
  }

  
  // Fetch data from API(s).
  if (WiFi.status() == WL_CONNECTED)
  {
    wifiStatus = true;

    if (timerTime.elapsed())
    {
      timerTime.setDelay(timeBetweenApiCalls);
      timeApiStatus = UpdateTime();
    }

    if (timerApi.elapsed())
    {
      timerApi.setDelay(timeBetweenApiCalls);
      dataApiStatus = GetDataFromAPI(apiLoctionIndex);

      if (++apiLoctionIndex > numLocations - 1)
      {
        apiLoctionIndex = 0;
      }
    }
  }
  else
  {
    wifiStatus = true;
    dataApiStatus = false;
    timeApiStatus = false;
  }
}