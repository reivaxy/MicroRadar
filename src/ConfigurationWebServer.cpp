#include "ConfigurationWebServer.h"
#include "AircraftManager.h"
#include <ESPmDNS.h>
#include <Update.h>
#include <esp_ota_ops.h>

// Forward declaration
class AircraftManager;
extern AircraftManager aircraftManager;

// Global OTA status tracking
static String g_otaErrorMsg = "";
volatile bool g_otaInProgress = false;

// HTML stored in flash
// %PLACEHOLDER% tokens are substituted at serve time by the template processor
static const char CONFIG_HTML[] PROGMEM = R"(
<html>
    <head>
        <meta name="viewport" content="width=device-width, initial-scale=1">
        <title>Configure Micro Radar</title>
        <script src="https://cdn.jsdelivr.net/npm/@tailwindcss/browser@4.3.0"></script>
        <meta charset="UTF-8">
        <script>
            function updateRadiusStep() {
                const radiusInput = document.getElementById('radiusValue');
                if (!radiusInput) return;
                
                const value = parseFloat(radiusInput.value);
                radiusInput.step = value < 0.1 ? '0.005' : '0.01';
            }
            
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
                
                adjust: function(direction) {
                    const radiusInput = document.getElementById('radiusValue');
                    if (!radiusInput) return;
                    
                    let currentValue = parseFloat(radiusInput.value);
                    let step = currentValue < 0.1 ? 0.005 : 0.01;
                    let newValue = currentValue + (step * direction);
                    
                    // Clamp to min/max values
                    newValue = Math.max(0.00, Math.min(2.50, newValue));
                    radiusInput.value = newValue.toFixed(3);
                    
                    updateRadiusStep();
                    console.log('[radius] Adjust click, new value:', newValue.toFixed(3), 'step:', step, 'timeout pending:', this.timeoutId !== null);
                    
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
            
            function adjustRadius(direction) {
                radiusDebounce.adjust(direction);
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

            function resetLocation() {
                console.log('[position] Resetting location to saved values');
                fetch('/reset-position', { method: 'POST' })
                    .then(r => r.text())
                    .then(html => {
                        const resultDiv = document.getElementById('positionResult');
                        if (resultDiv) resultDiv.innerHTML = html;
                        // Reload the page to refresh the position values
                        setTimeout(() => location.reload(), 500);
                    });
            }

            // OTA Update Handler - must be global for onclick to work
            window.startOTAUpdate = function() {
                const fileInput = document.getElementById('firmwareFile');
                const resultDiv = document.getElementById('otaResult');
                const statusDiv = document.getElementById('otaStatus');
                const statusText = document.getElementById('otaStatusText');
                const progressDiv = document.getElementById('otaProgress');
                const uploadBtn = document.getElementById('otaUploadBtn');

                // Null check all elements before using them
                if (!fileInput || !resultDiv || !statusDiv || !statusText || !progressDiv || !uploadBtn) {
                    console.error('[OTA] Missing required DOM elements:', {
                        fileInput: !!fileInput, resultDiv: !!resultDiv, statusDiv: !!statusDiv,
                        statusText: !!statusText, progressDiv: !!progressDiv, uploadBtn: !!uploadBtn
                    });
                    alert('Error: OTA form elements not found. Please refresh the page.');
                    return;
                }

                const file = fileInput.files[0];

                if (!file) {
                    resultDiv.innerHTML = '<span class="text-red-500">Please select a file</span>';
                    return;
                }

                if (!file.name.endsWith('.bin')) {
                    resultDiv.innerHTML = '<span class="text-red-500">File must be a .bin file</span>';
                    return;
                }

                resultDiv.innerHTML = '';
                statusText.textContent = 'Uploading firmware...';
                statusDiv.style.display = 'block';
                progressDiv.style.display = 'flex';
                uploadBtn.disabled = true;
                fileInput.disabled = true;

                const xhr = new XMLHttpRequest();
                const formData = new FormData();
                formData.append('firmware', file);

                xhr.upload.addEventListener('progress', (e) => {
                    if (e.lengthComputable) {
                        const percentComplete = (e.loaded / e.total) * 100;
                        const progressBar = document.getElementById('otaProgressBar');
                        const progressText = document.getElementById('otaProgressText');
                        // NOTE: never write a raw percent sign in this page, the server's
                        // template processor would treat it as a placeholder delimiter.
                        // Use \x25 in JS and &#37; in HTML.
                        if (progressBar) progressBar.style.width = percentComplete + '\x25';
                        if (progressText) progressText.textContent = Math.round(percentComplete) + '\x25';
                    }
                });

                xhr.addEventListener('load', () => {
                    // Ensure progress shows completion
                    const progressBar = document.getElementById('otaProgressBar');
                    const progressText = document.getElementById('otaProgressText');
                    if (progressBar) progressBar.style.width = '100\x25';
                    if (progressText) progressText.textContent = '100\x25';
                    
                    if (xhr.status === 200) {
                        resultDiv.innerHTML = '<span class="text-green-500 font-bold">Upload successful! Device is updating and will restart...</span>';
                        if (statusDiv) statusDiv.style.display = 'none';
                        if (progressDiv) progressDiv.style.display = 'none';
                    } else {
                        resultDiv.innerHTML = '<span class="text-red-500">Error: Upload failed (HTTP ' + xhr.status + ')</span>';
                        if (statusDiv) statusDiv.style.display = 'none';
                        if (progressDiv) progressDiv.style.display = 'none';
                        if (uploadBtn) uploadBtn.disabled = false;
                    }
                });

                xhr.addEventListener('error', () => {
                    resultDiv.innerHTML = '<span class="text-red-500">Connection error during upload</span>';
                    if (statusDiv) statusDiv.style.display = 'none';
                    if (progressDiv) progressDiv.style.display = 'none';
                    if (uploadBtn) uploadBtn.disabled = false;
                });

                xhr.open('POST', '/ota-update');
                xhr.send(formData);
            };

            document.addEventListener('DOMContentLoaded', function() {
                const cfg = document.getElementById('cfg');
                if (cfg) {
                    cfg.addEventListener('submit', function(e) {
                        e.preventDefault();
                        fetch(this.action, { method: 'POST', body: new FormData(this) })
                            .then(r => r.text())
                            .then(html => document.getElementById('result').innerHTML = html);
                    });
                }
                
                const radiusInput = document.getElementById('radiusValue');
                if (radiusInput) {
                    updateRadiusStep();
                    radiusInput.addEventListener('input', updateRadiusStep);
                }
            });
        </script>
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
                        step="0.005"
                        max="2.50"
                        value='%RADIUS%'
                        class="border border-green-500 bg-gray-900 px-3 py-2 text-lg sm:text-base sm:px-1 sm:py-0 w-24">
                </label>

                <div class="flex gap-2">
                    <button
                        type="button"
                        onclick='adjustRadius(-1)'
                        class="bg-green-500 text-black px-3 py-2 text-lg sm:text-base sm:px-2 sm:py-1 cursor-pointer font-bold">-</button>
                    <button
                        type="button"
                        onclick='adjustRadius(1)'
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
                            value='%TMP_LAT%'
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
                            value='%TMP_LON%'
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

                <button
                    type="button"
                    onclick='resetLocation()'
                    class="bg-green-500 text-black mt-4 px-4 py-3 text-lg sm:text-base sm:px-2 sm:py-1 cursor-pointer self-start">
                    Reset Location
                </button>
            </form>
        </fieldset>

        <fieldset class="border border-green-500 p-5 w-full max-w-2xl mx-auto sm:m-10 mt-4">
            <legend class="px-2">Firmware Update (OTA)</legend>

            <form id="otaCfg" enctype="multipart/form-data" class="flex flex-col gap-4 sm:gap-2">
                <div class="flex flex-col gap-2">
                    <label class="flex flex-col items-start gap-2">
                        <span>Select Firmware File (.bin):</span>
                        <input
                            id="firmwareFile"
                            name="firmware"
                            type="file"
                            accept=".bin"
                            class="border border-green-500 bg-gray-900 px-3 py-2 text-lg sm:text-base w-full">
                    </label>
                    <button
                        id="otaUploadBtn"
                        type="button"
                        onclick='startOTAUpdate()'
                        class="bg-green-500 text-black px-4 py-3 text-lg sm:text-base sm:px-2 sm:py-1 cursor-pointer self-start font-bold">
                        Upload and Update
                    </button>
                </div>
                <div id="otaProgress" class="flex flex-col gap-2" style="display:none;">
                    <div class="w-full bg-gray-700 border border-green-500 h-6">
                        <div id="otaProgressBar" class="bg-green-500 h-full" style="width:0&#37;;"></div>
                    </div>
                    <span id="otaProgressText" class="text-sm text-green-400">0&#37;</span>
                </div>
                <div id="otaStatus" class="mt-2 px-1" style="display:none;">
                    <span id="otaStatusText" class="text-yellow-500">Uploading firmware...</span>
                </div>
                <div id="otaResult" class="mt-2 px-1"></div>
            </form>
        </fieldset>
    </body>
</html>
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
        log_i("[GET] Handling request to config web server...");
        
        // Reject requests during OTA to avoid interfering with firmware update
        if (g_otaInProgress) {
            log_w("[GET] Rejecting request - OTA in progress");
            request->send(503, "text/plain", "Device is performing OTA update");
            return;
        }

        // read all values up front so the processor lambda can capture by value
        String latitude, longitude, tmpLat, tmpLon, radius, openskyClientId, openskySecret, scanlineEnabled, infoTextEnabled, triangleEnabled;
        if (prefsMutex == nullptr || xSemaphoreTake(prefsMutex, portMAX_DELAY) == pdTRUE) {
            prefs.begin("config", true);
            latitude = prefs.getString("latitude", "");
            longitude = prefs.getString("longitude", "");
            tmpLat = prefs.getString("tmpLat", "");
            tmpLon = prefs.getString("tmpLon", "");
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
            [latitude, longitude, tmpLat, tmpLon, radiusFormatted, openskyClientId, openskySecret, scanlineEnabled, infoTextEnabled, triangleEnabled]
            (const String& var) -> String {
                if (var == "LATITUDE")       return latitude;
                if (var == "LONGITUDE")      return longitude;
                if (var == "TMP_LAT")       return tmpLat;
                if (var == "TMP_LON")       return tmpLon;
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
        log_i("[POST] Handling form submission to config web server...");
        
        // Reject requests during OTA to avoid interfering with firmware update
        if (g_otaInProgress) {
            log_w("[POST /save] Rejecting request - OTA in progress");
            request->send(503, "text/plain", "Device is performing OTA update");
            return;
        }

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
            // when saving main form, reset the temporary values to match the saved ones
            auto* paramLat = request->getParam("latitude", true);
            prefs.putString("tmpLat", paramLat != nullptr ? paramLat->value() : "");
            auto* paramLon = request->getParam("longitude", true);
            prefs.putString("tmpLon", paramLon != nullptr ? paramLon->value() : "");
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
        // Reject requests during OTA to avoid interfering with firmware update
        if (g_otaInProgress) {
            log_w("[POST /save-radius] Rejecting request - OTA in progress");
            request->send(503, "text/plain", "Device is performing OTA update");
            return;
        }
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
        // Reject requests during OTA to avoid interfering with firmware update
        if (g_otaInProgress) {
            log_w("[POST /save-position] Rejecting request - OTA in progress");
            request->send(503, "text/plain", "Device is performing OTA update");
            return;
        }
        log_i("[POST] Handling quick position update...");

        const auto* latParam = request->getParam("latitude", true);
        const auto* lonParam = request->getParam("longitude", true);
        
        if (latParam != nullptr && lonParam != nullptr) {
            if (prefsMutex == nullptr || xSemaphoreTake(prefsMutex, portMAX_DELAY) == pdTRUE) {
                prefs.begin("config", false);
                prefs.putString("tmpLat", latParam->value());
                prefs.putString("tmpLon", lonParam->value());
                prefs.end();
                if (prefsMutex != nullptr) xSemaphoreGive(prefsMutex);
            }
            request->send(200, "text/html", "Position updated successfully!");
        } else {
            request->send(400, "text/html", "Error: latitude or longitude parameter missing");
        }
        }
    );

    // Handle reset position to saved values
    server.on("/reset-position", HTTP_POST, [&](AsyncWebServerRequest* request) {
        log_i("[POST] Handling position reset to saved values...");

        if (prefsMutex == nullptr || xSemaphoreTake(prefsMutex, portMAX_DELAY) == pdTRUE) {
            prefs.begin("config", false);
            String latitude = prefs.getString("latitude", "");
            String longitude = prefs.getString("longitude", "");
            log_i("[POST] Resetting position to saved values: latitude=%s, longitude=%s", latitude.c_str(), longitude.c_str());
            prefs.putString("tmpLat", latitude);
            prefs.putString("tmpLon", longitude);
            prefs.end();
            
            if (prefsMutex != nullptr) xSemaphoreGive(prefsMutex);
            request->send(200, "text/html", "Location reset to saved values!");
        } else {
            log_e("[POST] Error: could not access preferences for position reset");
            request->send(400, "text/html", "Error: could not access preferences");
        }
    }
    );

    // Handle OTA firmware update
    server.on("/ota-update", HTTP_POST, 
        [&](AsyncWebServerRequest* request) {
            log_i("[OTA] POST completion handler");
            
            // Re-enable WiFi after OTA
            WiFi.mode(WIFI_STA);
            
            // Check if there was an error during upload
            if (!g_otaErrorMsg.isEmpty()) {
                log_e("[OTA] Error captured: %s", g_otaErrorMsg.c_str());
                String msg = g_otaErrorMsg;
                g_otaErrorMsg = "";
                request->send(400, "text/plain", msg);
                return;
            }
            
            // Check for Update object errors
            if (Update.hasError()) {
                log_e("[OTA] Update error: %s", Update.errorString());
                request->send(400, "text/plain", "Update failed: " + String(Update.errorString()));
                return;
            }
            
            // Success case
            if (Update.isFinished()) {
                log_i("[OTA] Update successful, restarting device...");
                request->send(200, "text/plain", "Update successful! Restarting device...");
                delay(1000);
                ESP.restart();
                return;
            }
            
            request->send(400, "text/plain", "Update not completed");
        },
        [&](AsyncWebServerRequest* request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
            if (index == 0) {
                log_i("[OTA] Starting OTA: %s", filename.c_str());
                g_otaErrorMsg = "";
                g_otaInProgress = true;
                
                // Suspend the background aircraft fetch task to prevent interference
                log_i("[OTA] Suspending aircraft fetch task");
                aircraftManager.SuspendFetchTask();
                
                // Check file extension
                if (!filename.endsWith(".bin")) {
                    g_otaErrorMsg = "Invalid file type (must be .bin)";
                    log_e("[OTA] %s", g_otaErrorMsg.c_str());
                    g_otaInProgress = false;
                    aircraftManager.ResumeFetchTask();
                    return;
                }
                
                // Get OTA partition
                const esp_partition_t *running = esp_ota_get_running_partition();
                const esp_partition_t *next = esp_ota_get_next_update_partition(running);
                if (!next) {
                    g_otaErrorMsg = "No OTA partition available";
                    log_e("[OTA] %s", g_otaErrorMsg.c_str());
                    g_otaInProgress = false;
                    aircraftManager.ResumeFetchTask();
                    return;
                }
                
                size_t partSize = next->size;
                log_i("[OTA] OTA partition: 0x%x, size: %u bytes", next->address, partSize);
                log_i("[OTA] Running partition: 0x%x", running->address);
                
                // Begin OTA
                if (!Update.begin(partSize)) {
                    g_otaErrorMsg = "Update.begin() failed: ";
                    g_otaErrorMsg += Update.errorString();
                    log_e("[OTA] %s", g_otaErrorMsg.c_str());
                    g_otaInProgress = false;
                    aircraftManager.ResumeFetchTask();
                    return;
                }
                log_i("[OTA] Update.begin() successful");
            }
            
            // Write data to flash
            if (len > 0 && g_otaErrorMsg.isEmpty()) {
                if (!Update.isRunning()) {
                    g_otaErrorMsg = "Update not running at chunk";
                    log_e("[OTA] %s", g_otaErrorMsg.c_str());
                    return;
                }
                
                // Update library handles its own synchronization internally
                size_t written = Update.write(data, len);
                
                if (written != len) {
                    g_otaErrorMsg = "Write failed: wrote ";
                    g_otaErrorMsg += written;
                    g_otaErrorMsg += " of ";
                    g_otaErrorMsg += len;
                    g_otaErrorMsg += " bytes";
                    log_e("[OTA] %s at index %d", g_otaErrorMsg.c_str(), index);
                    Update.abort();
                }
            }
            
            // Finalize
            if (final) {
                log_i("[OTA] Finalizing update...");
                if (g_otaErrorMsg.isEmpty()) {
                    // Update library handles its own synchronization
                    bool endSuccess = Update.end(true);
                    
                    if (!endSuccess) {
                        g_otaErrorMsg = "Update.end() failed: ";
                        g_otaErrorMsg += Update.errorString();
                        log_e("[OTA] %s", g_otaErrorMsg.c_str());
                    } else {
                        log_i("[OTA] Update verified and finalized successfully");
                    }
                } else {
                    log_e("[OTA] Aborting due to error: %s", g_otaErrorMsg.c_str());
                    Update.abort();
                }
                g_otaInProgress = false;
                
                // Resume fetch task and restore system state
                aircraftManager.ResumeFetchTask();
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