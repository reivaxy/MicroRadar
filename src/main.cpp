#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <nvs_flash.h>
#include <Preferences.h>

#include "LGFX.h"
#include "WiFiManagerHelpers.h"
#include "ConfigurationWebServer.h"
#include "HttpRequestManager.h"
#include "OpenSkyAuthTokenHandler.h"
#include "AircraftManager.h"
#include "DrawHelpers.h"
#include "models/Aircraft.h"
#include "models/TrackedAircraft.h"

// Optional hard-coded Wi-Fi credentials. Leave both blank to skip pre-baking them and use the setup hotspot instead.
const char* preconfiguredWifiSsid = "";
const char* preconfiguredWifiPassword = "";

constexpr int SCREEN_SIZE = 240;
constexpr int SCREEN_SIZE_DIV_2 = (SCREEN_SIZE / 2);

LGFX tft;
LGFX_Sprite backbuffer(&tft);

WiFiManager wm;
ConfigurationWebServer configServer;
HttpRequestManager http;
OpenSkyAuthTokenHandler authHandler(http);

AircraftManager aircraftManager(configServer, authHandler, http, tft);

bool renderScanlines = true; // cached once in setup() from stored config, see below

void setup()
{
  Serial.begin(115200);
  // delay(1000); // avoids immediate serial output being cut off - uncomment if needed

  // A board previously flashed with a different partition table (or with any
  // otherwise corrupted/mismatched NVS content) can leave the NVS partition
  // unable to open cleanly - this shows up as repeated "nvs_open failed"
  // errors from Preferences and can eventually crash. Self-heal by erasing
  // and reformatting NVS once if the initial mount reports it's unusable.
  esp_err_t nvsResult = nvs_flash_init();
  if (nvsResult == ESP_ERR_NVS_NO_FREE_PAGES || nvsResult == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    Serial.println("[WARN] NVS partition unusable, erasing and reinitialising...");
    nvs_flash_erase();
    nvs_flash_init();
  }

  // Opening the "config" namespace read-only while it doesn't exist yet
  // triggers a known Arduino-ESP32 NVS bug: enough consecutive failed opens
  // aborts the chip. Turns out the same bug fires on consecutive failed KEY
  // lookups too (nvs_get_str NOT_FOUND), not just a missing namespace - so
  // the robust fix is to make sure every key the app reads actually exists
  // with a sensible default the first time, so no lookup ever comes back
  // NOT_FOUND. isKey() avoids clobbering anything already saved.
  {
    Preferences bootstrapPrefs;
    bootstrapPrefs.begin("config", false);
    auto EnsureDefault = [&](const char* key, const char* defaultValue) {
      if (!bootstrapPrefs.isKey(key)) bootstrapPrefs.putString(key, defaultValue);
    };
    EnsureDefault("latitude", "");
    EnsureDefault("longitude", "");
    EnsureDefault("radius", "1.0");
    EnsureDefault("infotext", "true");
    EnsureDefault("triangle", "true");
    EnsureDefault("scanline", "true");
    EnsureDefault("opensky-id", "");
    EnsureDefault("opensky-secret", "");
    bootstrapPrefs.end();
  }

  // initialise LGFX + screen
  tft.init();
  tft.setRotation(0);

  backbuffer.setColorDepth(8);
  backbuffer.createSprite(SCREEN_SIZE, SCREEN_SIZE);

  // establish WiFi connection
  tft.fillScreen(lgfx::color888(0, 0, 0));
  tft.setTextColor(lgfx::color888(0, 255, 0));
  tft.drawCentreString("Connecting to WiFi...", tft.width() / 2, tft.height() / 2);

  WiFiManagerHelpers::ConfigureWiFiManager(wm, tft);

  if (strlen(preconfiguredWifiSsid) > 0) {
    WiFi.begin(preconfiguredWifiSsid, preconfiguredWifiPassword);
    WiFi.waitForConnectResult();
  }

  wm.autoConnect(WiFiManagerHelpers::WiFiManagerName);
  
  // Wait for DHCP to fully complete and stabilize
  delay(1000);

  // begin background server for configuration
  configServer.Initialise(); // registers routes + mDNS, plus a first bind attempt

  // WiFiManager's own captive-portal web server also binds port 80. Its
  // socket's actual teardown time after "config portal exiting" varies (e.g.
  // longer if a browser still had it open) - a single fixed delay is a
  // guess that isn't reliably long enough. Instead, retry just the bind step
  // a few times with short waits in between; StartServer() is safe to call
  // again if an earlier attempt lost the race.
  for (int attempt = 0; attempt < 6; attempt++) {
    delay(500);
    configServer.StartServer();
  }

  // initialise aircraft manager
  aircraftManager.Initialise();

  // "scanline" only ever changes right before a reboot (the /save endpoint
  // restarts the device), so read it once here rather than hitting flash/NVS
  // every single frame in loop() - the latter can fail repeatedly and crash
  // on a fresh device where the "config" namespace doesn't exist yet.
  const String storedScanline = configServer.GetStoredString("scanline");
  renderScanlines = storedScanline.isEmpty() || storedScanline == "true";

  // Round 240x240 panel is fully occupied by the radar sprite - no title/credit overlay.
}

void loop()
{
  aircraftManager.Update();

  // draw cycle
  backbuffer.fillScreen(lgfx::color888(0, 0, 0));

  if (renderScanlines) {
        DrawScanLines(backbuffer,
      SCREEN_SIZE_DIV_2 - 1,
      SCREEN_SIZE_DIV_2 - 1,
      SCREEN_SIZE_DIV_2 - 1 + (std::cos(millis() / 3000.0f) * SCREEN_SIZE_DIV_2),
      SCREEN_SIZE_DIV_2 - 1 + (std::sin(millis() / 3000.0f) * SCREEN_SIZE_DIV_2),
      20, 128, 5, 1, SCREEN_SIZE_DIV_2 - 1 // last arg clamps the trail to the outer ring's radius
    );
  }

  aircraftManager.Draw(backbuffer);

  // radar sprite fills the entire round panel - no title/credit overlay
  backbuffer.pushSprite(0, 0);
}

