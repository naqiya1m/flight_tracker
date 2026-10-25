#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include "GC9D01Display.h"
#include <math.h>

// -------- YOUR SETTINGS --------
//*replace with your own wifi info*
const char* WIFI_SSID     = "WIFI_NAME";
const char* WIFI_PASSWORD = "WIFI_PASSWORD";

//coordinates for NYC *replace with your own*
const double HOME_LAT = 40.7128;
const double HOME_LON = 74.0060;
const double RADIUS_KM = 5.0;
// -------------------------------

// Wiring from the supplied PCB schematic; these are raw ESP32 GPIO numbers.
// LCD: DIN=10, CLK=8, CS=20, DC=4, RST=3, BL=2, VCC=3.3V.
// Buzzer positive: XIAO D3 / GPIO5; negative: GND.
constexpr int PIN_BUZZER = 5;

//buzzer duration and frequency *replace with your own*
constexpr uint32_t BUZZER_DURATION_MS = 300;
constexpr uint16_t BUZZER_FREQUENCY_HZ = 2500;

GC9D01Display tft;
bool haveAircraftData = false;
String statusMessage = "Starting";
bool fetchAttempted = false;
uint32_t lastWiFiAttempt = 0;
bool wasConnected = false;

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
uint32_t nextFetchDelay = FETCH_INTERVAL_MS;

String lastDisplayedAircraft = "";
uint32_t buzzerOffAt = 0;
bool buzzerActive = false;

const uint16_t BG = 0x0000;
const uint16_t FG = 0xFFFF;
const uint16_t ACCENT = 0x07FF;
const uint16_t MUTED = 0x9CF3;
const uint16_t PANEL = 0x18E3;

// Draw a readable compass arrow. 0° is north, 90° is east, etc.
void drawDirectionArrow(int cx, int cy, double headingDeg) {
  if (headingDeg < 0) {
    tft.setTextSize(2);
    tft.setTextColor(MUTED, BG);
    tft.setCursor(cx - 6, cy - 4);
    tft.print("--");
    return;
  }

  double angle = radians(headingDeg);
  int tipX = cx + (int)(sin(angle) * 16);
  int tipY = cy - (int)(cos(angle) * 16);
  int tailX = cx - (int)(sin(angle) * 12);
  int tailY = cy + (int)(cos(angle) * 12);

  tft.drawLine(tailX, tailY, tipX, tipY, 0xFFE0);

  double leftAngle = angle + radians(150);
  double rightAngle = angle - radians(150);

  tft.drawLine(
    tipX, tipY,
    tipX + (int)(sin(leftAngle) * 8),
    tipY - (int)(cos(leftAngle) * 8),
    0xFFE0
  );

  tft.drawLine(
    tipX, tipY,
    tipX + (int)(sin(rightAngle) * 8),
    tipY - (int)(cos(rightAngle) * 8),
    0xFFE0
  );

  tft.fillCircle(cx, cy, 3, 0xFFE0);
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
    tft.fillRect(x + 1, y + 1, w - 2, h - 2, 0xFFFF);
    for (int i = 0; i < 6; i++) {
      tft.fillRect(x + 1, y + 1 + i * 3, w - 2, 1, 0xF800);
    }
    tft.fillRect(x + 1, y + 1, 16, 12, 0x001F);
    for (int row = 0; row < 3; row++) {
      for (int col = 0; col < 4; col++) {
        tft.fillCircle(x + 4 + col * 3, y + 4 + row * 3, 1, 0xFFFF);
      }
    }
  } else if (country == "United Kingdom") {
    tft.fillRect(x + 1, y + 1, w - 2, h - 2, 0x001F);
    tft.drawLine(x + 2, y + 2, x + w - 3, y + h - 3, 0xFFFF);
    tft.drawLine(x + w - 3, y + 2, x + 2, y + h - 3, 0xFFFF);
    tft.fillRect(x + 15, y + 1, 8, h - 2, 0xF800);
    tft.fillRect(x + 1, y + 8, w - 2, 8, 0xF800);
  } else if (country == "Canada") {
    tft.fillRect(x + 1, y + 1, w - 2, h - 2, 0xFFFF);
    tft.fillRect(x + 1, y + 1, 9, h - 2, 0xF800);
    tft.fillRect(x + w - 10, y + 1, 9, h - 2, 0xF800);
    tft.fillTriangle(x + 19, y + 5, x + 15, y + 14, x + 23, y + 14, 0xF800);
    tft.fillRect(x + 18, y + 13, 3, 5, 0xF800);
  } else if (country == "Germany") {
    tft.fillRect(x + 1, y + 1, w - 2, 7, 0x0000);
    tft.fillRect(x + 1, y + 8, w - 2, 7, 0xF800);
    tft.fillRect(x + 1, y + 15, w - 2, 7, 0xFFE0);
  } else if (country == "France") {
    tft.fillRect(x + 1, y + 1, 12, h - 2, 0x001F);
    tft.fillRect(x + 13, y + 1, 12, h - 2, 0xFFFF);
    tft.fillRect(x + 25, y + 1, 12, h - 2, 0xF800);
  } else if (country == "Japan") {
    tft.fillRect(x + 1, y + 1, w - 2, h - 2, 0xFFFF);
    tft.fillCircle(x + 19, y + 12, 6, 0xF800);
  } else if (country == "China") {
    tft.fillRect(x + 1, y + 1, w - 2, h - 2, 0xF800);
    tft.fillCircle(x + 8, y + 7, 3, 0xFFE0);
  } else if (country == "Australia") {
    tft.fillRect(x + 1, y + 1, w - 2, h - 2, 0x001F);
    tft.fillRect(x + 2, y + 2, 15, 10, 0xF800);
    tft.drawLine(x + 2, y + 2, x + 16, y + 11, 0xFFFF);
    tft.drawLine(x + 16, y + 2, x + 2, y + 11, 0xFFFF);
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

void startBuzzer() {
  // Stop any previous tone, then start a new one.
  noTone(PIN_BUZZER);
  tone(PIN_BUZZER, BUZZER_FREQUENCY_HZ, BUZZER_DURATION_MS);
  buzzerActive = true;
  buzzerOffAt = millis() + BUZZER_DURATION_MS;
}

void updateBuzzer() {
  if (buzzerActive && (int32_t)(millis() - buzzerOffAt) >= 0) {
    noTone(PIN_BUZZER);
    buzzerActive = false;
  }
}

// Text is clipped by character count to fit the round screen.
void centered(String text, int y, int size, uint16_t color, int maxChars) {
  if (text.length() > (unsigned)maxChars) text = text.substring(0, maxChars);
  tft.setTextSize(size);
  tft.setTextColor(color, BG);
  tft.setCursor((160 - (int)text.length() * 6 * size) / 2, y);
  tft.print(text);
}

void drawStatus(const String& message) {
  statusMessage = message;
  Serial.println(message);
  tft.fillRect(0, 146, 160, 8, BG);
  centered(message, 146, 1, MUTED, 10);
  tft.flush();
}

void drawScreen() {
  tft.fillScreen(BG);
  tft.setTextWrap(false);
  centered("NEARBY FLIGHT", 14, 1, ACCENT, 13);
  if (!haveAircraftData || aircraftCount == 0) {
    centered(haveAircraftData ? "NO PLANES" : "WAITING", 60, 2, FG, 10);
    centered("within " + String(RADIUS_KM, 1) + " km", 88, 1, MUTED, 20);
    drawStatus(statusMessage);
    return;
  }
  int closestIndex = 0;
  double closestDistance = distanceKm(HOME_LAT, HOME_LON, aircraft[0].lat, aircraft[0].lon);
  for (int i = 1; i < aircraftCount; i++) {
    double d = distanceKm(HOME_LAT, HOME_LON, aircraft[i].lat, aircraft[i].lon);
    if (d < closestDistance) { closestIndex = i; closestDistance = d; }
  }
  Aircraft& closest = aircraft[closestIndex];
  centered(closest.callsign, 32, 2, FG, 10);
  drawCountryFlag(closest.country, 61, 55);
  centered(closest.country.length() ? closest.country : "Unknown", 83, 1, FG, 24);
  drawDirectionArrow(53, 111, closest.headingDeg);
  tft.setTextSize(1); tft.setTextColor(FG, BG);
  tft.setCursor(46, 129); tft.print(compassDirection(closest.headingDeg));
  tft.setCursor(83, 101); tft.print("DISTANCE");
  tft.setCursor(83, 114); tft.print(closestDistance, 1); tft.print(" km");
  String altitude = closest.altitudeM >= 0 ? String((int)(closest.altitudeM * 3.28084)) + " ft" : "-- ft";
  // Barometric altitude and direction of travel, as in the original sketch.
  centered(altitude, 137, 1, FG, 16);
  drawStatus(statusMessage);
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
  https.useHTTP10(true); // Stream JSON without HTTP/1.1 chunk framing.
  https.setConnectTimeout(10000);
  https.setTimeout(10000);
  if (!https.begin(client, url)) {
    drawStatus("Could not start HTTPS");
    return false;
  }

  https.addHeader("Accept", "application/json");
  const char* rateHeaders[] = {"X-Rate-Limit-Retry-After-Seconds"};
  https.collectHeaders(rateHeaders, 1);
  const int httpCode = https.GET();

  if (httpCode == 429) {
    long retrySeconds = https.header("X-Rate-Limit-Retry-After-Seconds").toInt();
    if (retrySeconds <= 0) retrySeconds = 3600;
    nextFetchDelay = (uint32_t)min(retrySeconds, 86400L) * 1000UL;
    drawStatus("Rate limit");
    https.end();
    return false;
  }
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

  if (!doc["states"].isNull() && !doc["states"].is<JsonArray>()) {
    drawStatus("Bad API data");
    return false;
  }
  nextFetchDelay = FETCH_INTERVAL_MS;
  haveAircraftData = true;
  statusMessage = "Updated";
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

    // Keep the nearest eight, rather than stopping at the first eight rows.
    if (aircraftCount < MAX_AIRCRAFT) aircraft[aircraftCount++] = a;
    else {
      int farthest = 0;
      for (int i = 1; i < MAX_AIRCRAFT; ++i)
        if (distanceKm(HOME_LAT, HOME_LON, aircraft[i].lat, aircraft[i].lon) >
            distanceKm(HOME_LAT, HOME_LON, aircraft[farthest].lat, aircraft[farthest].lon)) farthest = i;
      if (distanceKm(HOME_LAT, HOME_LON, a.lat, a.lon) <
          distanceKm(HOME_LAT, HOME_LON, aircraft[farthest].lat, aircraft[farthest].lon)) aircraft[farthest] = a;
    }
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
    startBuzzer();
  }

  // If no plane is found, clear the remembered aircraft. If one appears later,
  // it will count as newly displayed and buzz.
  if (aircraftCount == 0) {
    lastDisplayedAircraft = "";
  }

  return true;
}

void connectWiFi() {
  lastWiFiAttempt = millis();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  drawStatus("Wi-Fi joining");
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  if (!tft.begin()) {
    Serial.println("Display framebuffer allocation failed");
    while (true) delay(1000);
  }
  drawScreen();
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  if (String(WIFI_SSID) == "WIFI_NAME") drawStatus("Set Wi-Fi");
  else connectWiFi();
}

void loop() {
  updateBuzzer();
  if (String(WIFI_SSID) == "WIFI_NAME") { delay(50); return; }
  bool connected = WiFi.status() == WL_CONNECTED;
  if (!connected) {
    if (wasConnected) drawStatus("Wi-Fi offline");
    wasConnected = false;
    if (millis() - lastWiFiAttempt >= 20000) connectWiFi();
    delay(10);
    return;
  }
  if (!wasConnected) { drawStatus("Wi-Fi online"); wasConnected = true; }
  if (!fetchAttempted || millis() - lastFetch >= nextFetchDelay) {
    fetchAttempted = true;
    fetchAircraft();
    lastFetch = millis();
  }
  updateBuzzer();
  delay(10);
}
