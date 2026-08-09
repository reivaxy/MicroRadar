#include "ConfigurationWebServer.h"
#include <ESPmDNS.h>

// HTML stored in flash
// %PLACEHOLDER% tokens are substituted at serve time by the template processor
static const char CONFIG_HTML[] PROGMEM = R"(
<html>
    <head>
        <meta name="viewport" content="width=device-width, initial-scale=1">
        <title>Configure Micro Radar</title>
        <script src="https://cdn.jsdelivr.net/npm/@tailwindcss/browser@4.3.0"></script>
        <meta charset="UTF-8">
    </head>
    <body class="font-mono bg-gray-900 text-green-500 min-h-screen p-4 sm:p-0 text-md sm:text-sm">
        <fieldset class="border border-green-500 p-5 w-full max-w-2xl mx-auto sm:m-10">
            <legend class="px-2">Configure Micro Radar</legend>

            <form id="cfg" action="/save" method="POST" class="flex flex-col gap-4 sm:gap-2">

                <div class="flex flex-col sm:flex-row gap-4 sm:gap-5">
                    <label class="flex flex-col sm:flex-row gap-2 flex-1">
                        <span>Latitude:</span>
                        <input
                            name="latitude"
                            type="number"
                            min="-90"
                            step="0.000001"
                            max="90"
                            value='%LATITUDE%'
                            class="border border-green-500 bg-gray-900 w-full px-3 py-2 text-lg sm:text-base sm:px-1 sm:py-0">
                    </label>

                    <label class="flex flex-col sm:flex-row gap-2 flex-1">
                        <span>Longitude:</span>
                        <input
                            name="longitude"
                            type="number"
                            min="-180"
                            step="0.000001"
                            max="180"
                            value='%LONGITUDE%'
                            class="border border-green-500 bg-gray-900 w-full px-3 py-2 text-lg sm:text-base sm:px-1 sm:py-0">
                    </label>
                </div>

                <label class="flex flex-col sm:flex-row items-start sm:items-center gap-2">
                    <span>Radius (in &deg;):</span>
                    <input
                        name="radius"
                        type="number"
                        min="0.00"
                        step="0.01"
                        max="2.50"
                        value='%RADIUS%'
                        class="flex-1 border border-green-500 bg-gray-900 w-full px-3 py-2 text-lg sm:text-base sm:px-1 sm:py-0">
                </label>

                <label class="flex flex-col sm:flex-row items-start sm:items-center gap-2">
                    <span>OpenSkyAPI Client ID:</span>
                    <input
                        name="opensky-id"
                        value='%OPENSKY_ID%'
                        class="flex-1 border border-green-500 bg-gray-900 w-full px-3 py-2 text-lg sm:text-base sm:px-1 sm:py-0">
                </label>

                <label class="flex flex-col sm:flex-row items-start sm:items-center gap-2">
                    <span>OpenSkyAPI Client Secret:</span>
                    <input
                        name="opensky-secret"
                        value='%OPENSKY_SECRET%'
                        class="flex-1 border border-green-500 bg-gray-900 w-full px-3 py-2 text-lg sm:text-base sm:px-1 sm:py-0">
                </label>

                <div class="flex flex-col sm:flex-row gap-4 sm:justify-between">
                    <label class="flex flex-col sm:flex-row items-start sm:items-center gap-2">
                        <span>Radar sweep:</span>
                        <input
                            name="scanline"
                            type="checkbox"
                            %SCANLINE%
                            class="px-3 sm:px-1 accent-green-500">
                    </label>
                    <label class="flex flex-col sm:flex-row items-start sm:items-center gap-2">
                        <span>Aircraft Info:</span>
                        <input
                            name="infotext"
                            type="checkbox"
                            %INFOTEXT%
                            class="px-3 sm:px-1 accent-green-500">
                    </label>
                    <label class="flex flex-col sm:flex-row items-start sm:items-center gap-2">
                        <span>Directional Aircraft:</span>
                        <input
                            name="triangle"
                            type="checkbox"
                            %TRIANGLE%
                            class="px-3 sm:px-1 accent-green-500">
                    </label>
                </div>

                <div class="flex flex-col sm:flex-row gap-4 sm:gap-5">
                    <input
                        type="submit"
                        value="Save"
                        class="bg-green-500 text-black mt-4 px-4 py-3 text-lg sm:text-base sm:px-2 sm:py-0 self-start cursor-pointer">

                        <div id="result" class="mt-4 px-1 sm:px-10"></div>
                </div>
            </form>
        </fieldset>

        <fieldset class="border border-green-500 p-5 w-full max-w-2xl mx-auto sm:m-10 mt-4">
            <legend class="px-2">Quick Radius Update</legend>

            <form id="radiusCfg" action="/save-radius" method="POST" class="flex flex-col gap-4 sm:gap-2 sm:flex-row sm:items-center">
                <label class="flex flex-col sm:flex-row items-start sm:items-center gap-2">
                    <span>Radius (in &deg;):</span>
                    <input
                        id="radiusValue"
                        name="radius"
                        type="number"
                        min="0.00"
                        step="0.01"
                        max="2.50"
                        value='%RADIUS%'
                        class="border border-green-500 bg-gray-900 px-3 py-2 text-lg sm:text-base sm:px-1 sm:py-0 w-24">
                </label>

                <div class="flex gap-2">
                    <button
                        type="button"
                        onclick='adjustRadius(-0.05)'
                        class="bg-green-500 text-black px-3 py-2 text-lg sm:text-base sm:px-2 sm:py-1 cursor-pointer font-bold">-</button>
                    <button
                        type="button"
                        onclick='adjustRadius(0.05)'
                        class="bg-green-500 text-black px-3 py-2 text-lg sm:text-base sm:px-2 sm:py-1 cursor-pointer font-bold">+</button>
                </div>

                <div id="radiusResult" class="px-1 sm:px-2"></div>
            </form>
        </fieldset>

        <fieldset class="border border-green-500 p-5 w-full max-w-2xl mx-auto sm:m-10 mt-4">
            <legend class="px-2">Quick Position Update</legend>

            <form id="positionCfg" class="flex flex-col gap-4 sm:gap-2">
                <div class="flex flex-col sm:flex-row gap-4 sm:gap-5 sm:items-center">
                    <label class="flex flex-col sm:flex-row items-start sm:items-center gap-2">
                        <span>Latitude:</span>
                        <input
                            id="latitudeValue"
                            name="latitude"
                            type="number"
                            step="0.000001"
                            value='%LATITUDE%'
                            class="border border-green-500 bg-gray-900 px-3 py-2 text-lg sm:text-base sm:px-1 sm:py-0 w-32">
                    </label>

                    <div class="flex gap-2">
                        <button
                            type="button"
                            onclick='adjustPosition("latitude", -1)'
                            class="bg-green-500 text-black px-3 py-2 text-lg sm:text-base sm:px-2 sm:py-1 cursor-pointer font-bold">↓</button>
                        <button
                            type="button"
                            onclick='adjustPosition("latitude", 1)'
                            class="bg-green-500 text-black px-3 py-2 text-lg sm:text-base sm:px-2 sm:py-1 cursor-pointer font-bold">↑</button>
                    </div>
                </div>

                <div class="flex flex-col sm:flex-row gap-4 sm:gap-5 sm:items-center">
                    <label class="flex flex-col sm:flex-row items-start sm:items-center gap-2">
                        <span>Longitude:</span>
                        <input
                            id="longitudeValue"
                            name="longitude"
                            type="number"
                            step="0.000001"
                            value='%LONGITUDE%'
                            class="border border-green-500 bg-gray-900 px-3 py-2 text-lg sm:text-base sm:px-1 sm:py-0 w-32">
                    </label>

                    <div class="flex gap-2">
                        <button
                            type="button"
                            onclick='adjustPosition("longitude", -1)'
                            class="bg-green-500 text-black px-3 py-2 text-lg sm:text-base sm:px-2 sm:py-1 cursor-pointer font-bold">←</button>
                        <button
                            type="button"
                            onclick='adjustPosition("longitude", 1)'
                            class="bg-green-500 text-black px-3 py-2 text-lg sm:text-base sm:px-2 sm:py-1 cursor-pointer font-bold">→</button>
                    </div>
                </div>

                <div id="positionResult" class="px-1 sm:px-2"></div>
            </form>
        </fieldset>

        <script>
            const radiusDebounce = {
                timeoutId: null,
                HYSTERESIS_MS: 300,
                
                submit: function() {
                    const form = document.getElementById('radiusCfg');
                    if (!form) return;
                    const formData = new FormData(form);
                    console.log('[radius] Submitting radius value:', formData.get('radius'));
                    fetch(form.action, { method: 'POST', body: formData })
                        .then(r => r.text())
                        .then(html => {
                            const resultDiv = document.getElementById('radiusResult');
                            if (resultDiv) resultDiv.innerHTML = html;
                        });
                },
                
                adjust: function(delta) {
                    const radiusInput = document.getElementById('radiusValue');
                    if (!radiusInput) return;
                    
                    let currentValue = parseFloat(radiusInput.value);
                    let newValue = currentValue + delta;
                    
                    // Clamp to min/max values
                    newValue = Math.max(0.00, Math.min(2.50, newValue));
                    radiusInput.value = newValue.toFixed(2);
                    
                    console.log('[radius] Adjust click, new value:', newValue.toFixed(2), 'timeout pending:', this.timeoutId !== null);
                    
                    // Clear any pending submission
                    if (this.timeoutId !== null) {
                        clearTimeout(this.timeoutId);
                        this.timeoutId = null;
                        console.log('[radius] Cleared previous timeout');
                    }
                    
                    // Schedule submission after hysteresis period
                    const self = this;
                    this.timeoutId = setTimeout(() => {
                        console.log('[radius] Timeout fired, submitting');
                        self.submit();
                        self.timeoutId = null;
                    }, this.HYSTERESIS_MS);
                }
            };
            
            function adjustRadius(delta) {
                radiusDebounce.adjust(delta);
            }

            const positionDebounce = {
                timeoutId: null,
                HYSTERESIS_MS: 300,
                
                submit: function() {
                    const latInput = document.getElementById('latitudeValue');
                    const lonInput = document.getElementById('longitudeValue');
                    if (!latInput || !lonInput) return;
                    
                    const formData = new FormData();
                    formData.append('latitude', latInput.value);
                    formData.append('longitude', lonInput.value);
                    
                    console.log('[position] Submitting position:', latInput.value, lonInput.value);
                    fetch('/save-position', { method: 'POST', body: formData })
                        .then(r => r.text())
                        .then(html => {
                            const resultDiv = document.getElementById('positionResult');
                            if (resultDiv) resultDiv.innerHTML = html;
                        });
                },
                
                adjust: function(field, direction) {
                    const input = document.getElementById(field + 'Value');
                    const radiusInput = document.getElementById('radiusValue');
                    if (!input || !radiusInput) return;
                    
                    const radius = parseFloat(radiusInput.value);
                    const offset = radius / 4; // Offset is 1/4 of the radius
                    let currentValue = parseFloat(input.value);
                    let newValue = currentValue + (offset * direction);
                    
                    // Clamp latitude to [-90, 90] and longitude to [-180, 180]
                    if (field === 'latitude') {
                        newValue = Math.max(-90, Math.min(90, newValue));
                    } else if (field === 'longitude') {
                        newValue = Math.max(-180, Math.min(180, newValue));
                    }
                    
                    input.value = newValue.toFixed(6);
                    
                    console.log('[position] Adjust', field, 'by', offset.toFixed(6), 'new value:', newValue.toFixed(6));
                    
                    // Clear any pending submission
                    if (this.timeoutId !== null) {
                        clearTimeout(this.timeoutId);
                        this.timeoutId = null;
                    }
                    
                    // Schedule submission after hysteresis period
                    const self = this;
                    this.timeoutId = setTimeout(() => {
                        console.log('[position] Timeout fired, submitting');
                        self.submit();
                        self.timeoutId = null;
                    }, this.HYSTERESIS_MS);
                }
            };
            
            function adjustPosition(field, direction) {
                positionDebounce.adjust(field, direction);
            }

            document.getElementById('cfg').addEventListener('submit', function(e) {
                e.preventDefault();
                fetch(this.action, { method: 'POST', body: new FormData(this) })
                    .then(r => r.text())
                    .then(html => document.getElementById('result').innerHTML = html);
            });
        </script>
    </body>
</html>
)";

void ConfigurationWebServer::Initialise() {
    // Created here rather than in the constructor: this object is a global,
    // so its constructor runs during C++ static init before setup() - too
    // early for FreeRTOS primitives on ESP32, where xSemaphoreCreateMutex()
    // can silently return null. Initialise() only ever runs from setup(),
    // well after the scheduler/heap are ready.
    prefsMutex = xSemaphoreCreateMutex();

    // start mDNS and check result
    if (!MDNS.begin("microradar")) {
        Serial.println("[WARN] Failed to start mDNS. Continuing without mDNS...");
    }

    // Handle visit to config web server
    server.on("/", HTTP_GET, [&](AsyncWebServerRequest* request) {
        Serial.println("[GET] Handling request to config web server...");

        // read all values up front so the processor lambda can capture by value
        String latitude, longitude, radius, openskyClientId, openskySecret, scanlineEnabled, infoTextEnabled, triangleEnabled;
        if (prefsMutex == nullptr || xSemaphoreTake(prefsMutex, portMAX_DELAY) == pdTRUE) {
            prefs.begin("config", true);
            latitude = prefs.getString("latitude", "");
            longitude = prefs.getString("longitude", "");
            radius = prefs.getString("radius", "1.0");
            openskyClientId = prefs.getString("opensky-id", "");
            openskySecret = prefs.getString("opensky-secret", "");
            scanlineEnabled = prefs.getString("scanline", "true");
            infoTextEnabled = prefs.getString("infotext", "true");
            triangleEnabled = prefs.getString("triangle", "true");
            prefs.end();
            if (prefsMutex != nullptr) xSemaphoreGive(prefsMutex);
        }

        // mask secret before sending to client
        std::fill(openskySecret.begin(), openskySecret.end(), '*');

        // template processor called once per %PLACEHOLDER% token found in CONFIG_HTML.
        // Format radius to 2 decimal places
        char radiusFormatted[8];
        snprintf(radiusFormatted, sizeof(radiusFormatted), "%.2f", radius.toFloat());
        AsyncWebServerResponse* response = request->beginResponse(
            200, "text/html",
            (const uint8_t*)CONFIG_HTML, sizeof(CONFIG_HTML) - 1,
            [latitude, longitude, radiusFormatted, openskyClientId, openskySecret, scanlineEnabled, infoTextEnabled, triangleEnabled]
            (const String& var) -> String {
                if (var == "LATITUDE")       return latitude;
                if (var == "LONGITUDE")      return longitude;
                if (var == "RADIUS")         return String(radiusFormatted);
                if (var == "OPENSKY_ID")     return openskyClientId;
                if (var == "OPENSKY_SECRET") return openskySecret;
                if (var == "SCANLINE")       return scanlineEnabled == "true" ? "checked" : "";
                if (var == "INFOTEXT")       return infoTextEnabled == "true" ? "checked" : "";
                if (var == "TRIANGLE")       return triangleEnabled == "true" ? "checked" : "";
                return "";
            }
        );
        request->send(response);
        }
    );

    // Handle save submission to web server
    server.on("/save", HTTP_POST, [&](AsyncWebServerRequest* request) {
        Serial.println("[POST] Handling form submission to config web server...");

        // safe parameter retrieval helper lambda
        auto TrySaveParam = [request, this](const char* paramName) {
            const auto* param = request->getParam(paramName, true);
            if (param == nullptr)
                return false;

            prefs.putString(paramName, param->value());
            return true;
            };

        if (prefsMutex == nullptr || xSemaphoreTake(prefsMutex, portMAX_DELAY) == pdTRUE) {
            prefs.begin("config", false);

            TrySaveParam("latitude");
            TrySaveParam("longitude");
            TrySaveParam("radius");
            TrySaveParam("opensky-id");

            const auto* param = request->getParam("opensky-secret", true);
            if (param != nullptr) {
                const String& secret = param->value();
                if (secret.indexOf('*') == -1) { // Special handling for secret: don't overwrite with masked value
                    prefs.putString("opensky-secret", secret);
                }
            }

            prefs.putString("scanline", request->hasParam("scanline", true) ? "true" : "false");
            prefs.putString("triangle", request->hasParam("triangle", true) ? "true" : "false");
            prefs.putString("infotext", request->hasParam("infotext", true) ? "true" : "false");
            prefs.end();

            if (prefsMutex != nullptr) xSemaphoreGive(prefsMutex);
        }

        request->send(200, "text/html", "Saved - restarting device...");
        ESP.restart();
        }
    );

    // Handle quick radius update (no restart)
    server.on("/save-radius", HTTP_POST, [&](AsyncWebServerRequest* request) {
        log_i("[POST] Handling quick radius update...");

        const auto* param = request->getParam("radius", true);
        if (param != nullptr) {
            if (prefsMutex == nullptr || xSemaphoreTake(prefsMutex, portMAX_DELAY) == pdTRUE) {
                prefs.begin("config", false);
                prefs.putString("radius", param->value());
                prefs.end();
                if (prefsMutex != nullptr) xSemaphoreGive(prefsMutex);
            }
            request->send(200, "text/html", "Radius updated successfully!");
        } else {
            request->send(400, "text/html", "Error: radius parameter missing");
        }
        }
    );

    // Handle quick position update (no restart)
    server.on("/save-position", HTTP_POST, [&](AsyncWebServerRequest* request) {
        log_i("[POST] Handling quick position update...");

        const auto* latParam = request->getParam("latitude", true);
        const auto* lonParam = request->getParam("longitude", true);
        
        if (latParam != nullptr && lonParam != nullptr) {
            if (prefsMutex == nullptr || xSemaphoreTake(prefsMutex, portMAX_DELAY) == pdTRUE) {
                prefs.begin("config", false);
                prefs.putString("latitude", latParam->value());
                prefs.putString("longitude", lonParam->value());
                prefs.end();
                if (prefsMutex != nullptr) xSemaphoreGive(prefsMutex);
            }
            request->send(200, "text/html", "Position updated successfully!");
        } else {
            request->send(400, "text/html", "Error: latitude or longitude parameter missing");
        }
        }
    );

    StartServer();
}

void ConfigurationWebServer::StartServer() {
    // Split out from Initialise() so callers can retry just the bind step.
    // A fixed delay before this can't reliably guarantee the old WiFiManager
    // captive-portal socket on port 80 has released - actual teardown time
    // varies (e.g. longer if a browser still had it open when the portal
    // exited). Retrying begin() itself, rather than gambling on one delay,
    // is safe here: ESPAsyncWebServer's begin() just re-attempts the
    // underlying tcp_bind()/tcp_listen() each call.
    server.begin();
}

const String ConfigurationWebServer::GetStoredString(const char* key)
{
    String value = "";
    if (prefsMutex == nullptr || xSemaphoreTake(prefsMutex, portMAX_DELAY) == pdTRUE) {
        prefs.begin("config", true);
        value = prefs.getString(key, "");
        prefs.end();
        if (prefsMutex != nullptr) xSemaphoreGive(prefsMutex);
    }
    return value;
}