#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <math.h>

// -------- YOUR SETTINGS --------
//*replace with your own wifi info*
const char* WIFI_SSID     = "WIFI_NAME";
const char* WIFI_PASSWORD = "WIFI_PASSWORD";

//coordinates of NYC *replace with your own*
const double HOME_LAT = 40.7128;
const double HOME_LON = 74.0060;
const double RADIUS_KM = 5.0;
// -------------------------------

// Display wiring
constexpr int PIN_TFT_CS   = 17;
constexpr int PIN_TFT_DC   = 15;
constexpr int PIN_TFT_RST  = 27;
constexpr int PIN_TFT_SCK  = 18;
constexpr int PIN_TFT_MOSI = 19;

// Piezo buzzer: positive pin to GP14, other pin to GND
constexpr int PIN_BUZZER = 14;

//buzzer duration and frequency *replace with your own*
constexpr uint32_t BUZZER_DURATION_MS = 300;
constexpr uint16_t BUZZER_FREQUENCY_HZ = 2500;

Adafruit_ST7789 tft(PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);

constexpr uint32_t FETCH_INTERVAL_MS = 30000;
constexpr int MAX_AIRCRAFT = 8;

struct Aircraft {
  String callsign;
  String country;
  String icao24;
  double lat;
  double lon;
  double altitudeM;
  double headingDeg;
};

Aircraft aircraft[MAX_AIRCRAFT];
int aircraftCount = 0;
uint32_t lastFetch = 0;

String lastDisplayedAircraft = "";
uint32_t buzzerOffAt = 0;
bool buzzerActive = false;

const uint16_t BG = ST77XX_BLACK;
const uint16_t FG = ST77XX_WHITE;
const uint16_t ACCENT = ST77XX_CYAN;
const uint16_t MUTED = 0x9CF3;
const uint16_t PANEL = 0x18E3;

// Draw a readable compass arrow. 0° is north, 90° is east, etc.
void drawDirectionArrow(int cx, int cy, double headingDeg) {
  if (headingDeg < 0) {
    tft.setTextSize(2);
    tft.setTextColor(MUTED, BG);
    tft.setCursor(cx - 32, cy - 8);
    tft.print("--");
    return;
  }

  double angle = radians(headingDeg);
  int tipX = cx + (int)(sin(angle) * 31);
  int tipY = cy - (int)(cos(angle) * 31);
  int tailX = cx - (int)(sin(angle) * 24);
  int tailY = cy + (int)(cos(angle) * 24);

  tft.drawLine(tailX, tailY, tipX, tipY, ST77XX_YELLOW);

  double leftAngle = angle + radians(150);
  double rightAngle = angle - radians(150);

  tft.drawLine(
    tipX, tipY,
    tipX + (int)(sin(leftAngle) * 13),
    tipY - (int)(cos(leftAngle) * 13),
    ST77XX_YELLOW
  );

  tft.drawLine(
    tipX, tipY,
    tipX + (int)(sin(rightAngle) * 13),
    tipY - (int)(cos(rightAngle) * 13),
    ST77XX_YELLOW
  );

  tft.fillCircle(cx, cy, 3, ST77XX_YELLOW);
}

const char* compassDirection(double headingDeg) {
  if (headingDeg < 0) return "--";

  static const char* directions[] = {
    "N", "NE", "E", "SE", "S", "SW", "W", "NW"
  };

  int index = (int)((headingDeg + 22.5) / 45.0) % 8;
  return directions[index];
}

String cleanCallsign(JsonVariant value) {
  String s = value.is<const char*>()
                 ? String(value.as<const char*>())
                 : "";
  s.trim();

  if (s.length() == 0) {
    s = "Unknown";
  }

  return s;
}

// Draw simple flags for common OpenSky country names.
// Unknown countries get a boxed question mark.
void drawCountryFlag(const String& country, int x, int y) {
  const int w = 38;
  const int h = 24;

  tft.drawRect(x, y, w, h, FG);

  if (country == "United States") {
    tft.fillRect(x + 1, y + 1, w - 2, h - 2, ST77XX_WHITE);
    for (int i = 0; i < 6; i++) {
      tft.fillRect(x + 1, y + 1 + i * 3, w - 2, 1, ST77XX_RED);
    }
    tft.fillRect(x + 1, y + 1, 16, 12, ST77XX_BLUE);
    for (int row = 0; row < 3; row++) {
      for (int col = 0; col < 4; col++) {
        tft.fillCircle(x + 4 + col * 3, y + 4 + row * 3, 1, ST77XX_WHITE);
      }
    }
  } else if (country == "United Kingdom") {
    tft.fillRect(x + 1, y + 1, w - 2, h - 2, ST77XX_BLUE);
    tft.drawLine(x + 2, y + 2, x + w - 3, y + h - 3, ST77XX_WHITE);
    tft.drawLine(x + w - 3, y + 2, x + 2, y + h - 3, ST77XX_WHITE);
    tft.fillRect(x + 15, y + 1, 8, h - 2, ST77XX_RED);
    tft.fillRect(x + 1, y + 8, w - 2, 8, ST77XX_RED);
  } else if (country == "Canada") {
    tft.fillRect(x + 1, y + 1, w - 2, h - 2, ST77XX_WHITE);
    tft.fillRect(x + 1, y + 1, 9, h - 2, ST77XX_RED);
    tft.fillRect(x + w - 10, y + 1, 9, h - 2, ST77XX_RED);
    tft.fillTriangle(x + 19, y + 5, x + 15, y + 14, x + 23, y + 14, ST77XX_RED);
    tft.fillRect(x + 18, y + 13, 3, 5, ST77XX_RED);
  } else if (country == "Germany") {
    tft.fillRect(x + 1, y + 1, w - 2, 7, ST77XX_BLACK);
    tft.fillRect(x + 1, y + 8, w - 2, 7, ST77XX_RED);
    tft.fillRect(x + 1, y + 15, w - 2, 7, ST77XX_YELLOW);
  } else if (country == "France") {
    tft.fillRect(x + 1, y + 1, 12, h - 2, ST77XX_BLUE);
    tft.fillRect(x + 13, y + 1, 12, h - 2, ST77XX_WHITE);
    tft.fillRect(x + 25, y + 1, 12, h - 2, ST77XX_RED);
  } else if (country == "Japan") {
    tft.fillRect(x + 1, y + 1, w - 2, h - 2, ST77XX_WHITE);
    tft.fillCircle(x + 19, y + 12, 6, ST77XX_RED);
  } else if (country == "China") {
    tft.fillRect(x + 1, y + 1, w - 2, h - 2, ST77XX_RED);
    tft.fillCircle(x + 8, y + 7, 3, ST77XX_YELLOW);
  } else if (country == "Australia") {
    tft.fillRect(x + 1, y + 1, w - 2, h - 2, ST77XX_BLUE);
    tft.fillRect(x + 2, y + 2, 15, 10, ST77XX_RED);
    tft.drawLine(x + 2, y + 2, x + 16, y + 11, ST77XX_WHITE);
    tft.drawLine(x + 16, y + 2, x + 2, y + 11, ST77XX_WHITE);
  } else {
    tft.fillRect(x + 1, y + 1, w - 2, h - 2, PANEL);
    tft.setTextColor(FG, PANEL);
    tft.setTextSize(2);
    tft.setCursor(x + 14, y + 4);
    tft.print("?");
  }
}

double distanceKm(double lat1, double lon1, double lat2, double lon2) {
  const double earthKm = 6371.0;
  const double dLat = radians(lat2 - lat1);
  const double dLon = radians(lon2 - lon1);

  const double a =
      sin(dLat / 2) * sin(dLat / 2) +
      cos(radians(lat1)) * cos(radians(lat2)) *
      sin(dLon / 2) * sin(dLon / 2);

  return earthKm * 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
}

void startBuzzerForTwoSeconds() {
  // Stop any previous tone, then start a new one.
  noTone(PIN_BUZZER);
  tone(PIN_BUZZER, BUZZER_FREQUENCY_HZ);
  buzzerActive = true;
  buzzerOffAt = millis() + BUZZER_DURATION_MS;
}

void updateBuzzer() {
  if (buzzerActive && (int32_t)(millis() - buzzerOffAt) >= 0) {
    noTone(PIN_BUZZER);
    buzzerActive = false;
  }
}

void drawStatus(const String& message) {
  tft.fillRect(0, 226, 240, 14, BG);
  tft.setTextSize(1);
  tft.setTextColor(MUTED, BG);
  tft.setCursor(6, 229);
  tft.print(message);
}

void drawScreen() {
  tft.fillScreen(BG);
  tft.setTextWrap(false);

  tft.setTextSize(2);
  tft.setTextColor(ACCENT, BG);
  tft.setCursor(8, 7);
  tft.print("NEARBY FLIGHT");
  tft.drawFastHLine(8, 29, 224, PANEL);

  if (aircraftCount == 0) {
    tft.setTextSize(3);
    tft.setTextColor(FG, BG);
    tft.setCursor(22, 85);
    tft.print("NO PLANES");

    tft.setTextSize(2);
    tft.setTextColor(MUTED, BG);
    tft.setCursor(35, 125);
    tft.print("within ");
    tft.print((int)RADIUS_KM);
    tft.print(" km");

    drawStatus(WiFi.status() == WL_CONNECTED ? "Wi-Fi connected" : "Wi-Fi offline");
    return;
  }

  // Choose the closest aircraft.
  int closestIndex = 0;
  double closestDistance = distanceKm(
      HOME_LAT, HOME_LON,
      aircraft[0].lat, aircraft[0].lon
  );

  for (int i = 1; i < aircraftCount; i++) {
    double d = distanceKm(
        HOME_LAT, HOME_LON,
        aircraft[i].lat, aircraft[i].lon
    );
    if (d < closestDistance) {
      closestIndex = i;
      closestDistance = d;
    }
  }

  Aircraft& closest = aircraft[closestIndex];

  tft.setTextColor(FG, BG);
  tft.setTextSize(3);
  tft.setCursor(8, 39);
  String label = closest.callsign;
  if (label.length() > 10) label = label.substring(0, 10);
  tft.print(label);

  drawCountryFlag(closest.country, 8, 82);
  tft.setTextSize(2);
  tft.setTextColor(FG, BG);
  tft.setCursor(56, 84);
  String countryLabel = closest.country;
  if (countryLabel.length() > 15) countryLabel = countryLabel.substring(0, 15);
  if (countryLabel.length() == 0) countryLabel = "Unknown";
  tft.print(countryLabel);

  tft.drawRoundRect(8, 119, 105, 86, 8, PANEL);
  tft.setTextSize(1);
  tft.setTextColor(MUTED, BG);
  tft.setCursor(20, 125);
  tft.print("HEADING");

  drawDirectionArrow(60, 164, closest.headingDeg);

  tft.setTextSize(2);
  tft.setTextColor(FG, BG);
  tft.setCursor(43, 184);
  tft.print(compassDirection(closest.headingDeg));

  tft.drawRoundRect(121, 119, 111, 40, 8, PANEL);
  tft.setTextSize(1);
  tft.setTextColor(MUTED, BG);
  tft.setCursor(132, 124);
  tft.print("DISTANCE");
  tft.setTextSize(2);
  tft.setTextColor(FG, BG);
  tft.setCursor(132, 138);
  tft.print(closestDistance, 1);
  tft.print(" km");

  tft.drawRoundRect(121, 165, 111, 40, 8, PANEL);
  tft.setTextSize(1);
  tft.setTextColor(MUTED, BG);
  tft.setCursor(132, 170);
  tft.print("ALTITUDE");
  tft.setTextSize(2);
  tft.setTextColor(FG, BG);
  tft.setCursor(132, 184);
  if (closest.altitudeM >= 0) {
    tft.print((int)(closest.altitudeM * 3.28084));
    tft.print(" ft");
  } else {
    tft.print("--");
  }

  drawStatus(WiFi.status() == WL_CONNECTED ? "Wi-Fi connected" : "Wi-Fi offline");
}

bool fetchAircraft() {
  const double latDelta = RADIUS_KM / 111.0;
  const double lonScale = cos(radians(HOME_LAT));
  const double lonDelta =
      RADIUS_KM / (111.0 * max(0.01, fabs(lonScale)));

  String url = "https://opensky-network.org/api/states/all?lamin=";
  url += String(HOME_LAT - latDelta, 4);
  url += "&lomin=";
  url += String(HOME_LON - lonDelta, 4);
  url += "&lamax=";
  url += String(HOME_LAT + latDelta, 4);
  url += "&lomax=";
  url += String(HOME_LON + lonDelta, 4);

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  if (!https.begin(client, url)) {
    drawStatus("Could not start HTTPS");
    return false;
  }

  https.addHeader("Accept", "application/json");
  const int httpCode = https.GET();

  if (httpCode != HTTP_CODE_OK) {
    String message = "HTTP error ";
    message += httpCode;
    drawStatus(message);
    https.end();
    return false;
  }

  JsonDocument doc;
  DeserializationError error =
      deserializeJson(doc, https.getStream());
  https.end();

  if (error) {
    drawStatus("JSON parse error");
    return false;
  }

  aircraftCount = 0;
  JsonArray states = doc["states"].as<JsonArray>();

  for (JsonVariant row : states) {
    if (row.isNull() || row.size() < 11) continue;
    if (row[5].isNull() || row[6].isNull()) continue;

    const double lon = row[5].as<double>();
    const double lat = row[6].as<double>();

    if (distanceKm(HOME_LAT, HOME_LON, lat, lon) > RADIUS_KM) continue;

    Aircraft a;
    a.icao24 = row[0].is<const char*>() ? row[0].as<String>() : "";
    a.callsign = cleanCallsign(row[1]);
    a.country = row[2].is<const char*>() ? row[2].as<String>() : "";
    a.lon = lon;
    a.lat = lat;
    a.altitudeM = row[7].isNull() ? -1 : row[7].as<double>();
    a.headingDeg = row[10].isNull() ? -1 : row[10].as<double>();

    aircraft[aircraftCount++] = a;
    if (aircraftCount >= MAX_AIRCRAFT) break;
  }

  // Pick closest aircraft for comparison with the one already displayed.
  String currentDisplayedAircraft = "";
  if (aircraftCount > 0) {
    int closestIndex = 0;
    double closestDistance = distanceKm(
        HOME_LAT, HOME_LON,
        aircraft[0].lat, aircraft[0].lon
    );

    for (int i = 1; i < aircraftCount; i++) {
      double d = distanceKm(
          HOME_LAT, HOME_LON,
          aircraft[i].lat, aircraft[i].lon
      );
      if (d < closestDistance) {
        closestIndex = i;
        closestDistance = d;
      }
    }
    currentDisplayedAircraft = aircraft[closestIndex].icao24;
  }

  drawScreen();

  // Buzz when a new/different aircraft becomes the displayed closest plane.
  if (currentDisplayedAircraft.length() > 0 &&
      currentDisplayedAircraft != lastDisplayedAircraft) {
    lastDisplayedAircraft = currentDisplayedAircraft;
    startBuzzerForTwoSeconds();
  }

  // If no plane is found, clear the remembered aircraft. If one appears later,
  // it will count as newly displayed and buzz.
  if (aircraftCount == 0) {
    lastDisplayedAircraft = "";
  }

  return true;
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  tft.fillScreen(BG);
  tft.setTextColor(FG, BG);
  tft.setTextSize(2);
  tft.setCursor(8, 20);
  tft.print("Connecting Wi-Fi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    tft.print(".");
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_BUZZER, OUTPUT);
  noTone(PIN_BUZZER);

  SPI.setSCK(PIN_TFT_SCK);
  SPI.setTX(PIN_TFT_MOSI);
  SPI.begin();

  tft.init(240, 240);
  tft.setRotation(0);
  tft.fillScreen(BG);

  connectWiFi();
  drawScreen();
}

void loop() {
  updateBuzzer();

  if (WiFi.status() != WL_CONNECTED) {
    WiFi.disconnect();
    connectWiFi();
  }

  if (lastFetch == 0 ||
      millis() - lastFetch >= FETCH_INTERVAL_MS) {
    lastFetch = millis();
    fetchAircraft();
  }

  updateBuzzer();
  delay(10);
}
