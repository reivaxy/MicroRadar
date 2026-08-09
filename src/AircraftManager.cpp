#include "AircraftManager.h"

#include <algorithm>
#include <ArduinoJson.h>

constexpr int SCREEN_SIZE = 240;
constexpr int SCREEN_SIZE_DIV_2 = (SCREEN_SIZE / 2);

#define MAX_TRACKED_AIRCRAFT 30

void AircraftManager::Initialise()
{
    // get centre point + radius
    lat = configServer.GetStoredString("latitude").toDouble();
    lon = configServer.GetStoredString("longitude").toDouble();
    rad = configServer.GetStoredString("radius").toDouble();
    initRad = rad; 

    // configuration
    const String renderText = configServer.GetStoredString("infotext");
    const String renderTris = configServer.GetStoredString("triangle");
    if (!renderText.isEmpty()) displayInfoText = renderText == "true" ? true : false;
    if (!renderTris.isEmpty()) displayTriangles = renderTris == "true" ? true : false;

    // calculate how often we can call OpenSky API before being rate limited
    constexpr int MS_PER_DAY = 24 * 60 * 60 * 1000;
    constexpr int ANONYMOUS_TOKENS_PER_DAY = 400;
    constexpr int AUTHED_TOKENS_PER_DAY = 4000;
    constexpr int TOKEN_BUFFER = 3;
    int dailyRequestBudget = ANONYMOUS_TOKENS_PER_DAY - TOKEN_BUFFER; // non-authed tokens minus buffer

    const String token = authHandler.GetValidToken(configServer.GetStoredString("opensky-id"), configServer.GetStoredString("opensky-secret"));
    if (!token.isEmpty())
        dailyRequestBudget = AUTHED_TOKENS_PER_DAY - TOKEN_BUFFER; // authed tokens minus buffer

    fetchInterval = MS_PER_DAY / dailyRequestBudget;
    if (fetchInterval < 30000) fetchInterval = 30000; // Enforce minimum 30 seconds
    // display the fetch interval in seconds for debugging
    log_i("Fetch interval: %d seconds", fetchInterval / 1000);

    // Move the blocking OpenSky HTTP/token calls off the render thread and
    // onto a dedicated FreeRTOS task pinned to core 0 (Arduino's loop() runs
    // on core 1), so the sweep/radar animation in loop() never waits on the
    // network. Only trackedAircraft is shared between the two, guarded here.
    dataMutex = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCore(
        FetchTaskTrampoline,
        "AircraftFetch",
        8192,   // stack size - JSON parsing of the states/all payload needs headroom
        this,
        1,      // priority
        &fetchTaskHandle,
        0       // core 0
    );

    // display free memory for debugging
    log_i("Initial Free heap: %d bytes", ESP.getFreeHeap());
    // Display free max heap block size for debugging
    log_i("Initial Max alloc heap: %d bytes", ESP.getMaxAllocHeap());
}

void AircraftManager::FetchTaskTrampoline(void* param)
{
    static_cast<AircraftManager*>(param)->FetchLoop();
}

void AircraftManager::FetchLoop()
{
    // Runs forever on the background task. A short delay is enough since
    // FetchCycle() itself checks fetchInterval and returns immediately when
    // it isn't time yet - this just avoids busy-spinning the core.
    for (;;) {
        FetchCycle();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void AircraftManager::ForcedFetchCycle() 
{
    lastFetch = 0;
    FetchCycle();
}

void AircraftManager::FetchCycle()
{
    unsigned long now = millis();

    // fetch cycle
    if (now - lastFetch >= fetchInterval) {
        log_i("Fetching aircraft data from OpenSky API...");
        lastFetch = now;
        // auth
        const String token = authHandler.GetValidToken(
            configServer.GetStoredString("opensky-id"),
            configServer.GetStoredString("opensky-secret")
        );

        std::vector<std::pair<String, String>> headers = {};
        if (!token.isEmpty()) headers.push_back({ "Authorization", "Bearer " + token });

        // request - blocking, but now safely isolated on the background task/core
        HttpResult result = http.Get(
            "https://opensky-network.org/api/states/all",
            {
              {"lamin", String(lat - rad)},
              {"lamax", String(lat + rad)},
              {"lomin", String(lon - rad)},
              {"lomax", String(lon + rad)}
            },
            headers
        );

        // If request failed, skip this update
        if (!result.success) {
            Serial.print("[WARN] OpenSky API request failed: ");
            Serial.println(result.errorMessage);
            return;
        }

        // track
        JsonDocument doc;
        log_d("Aircraft JSON: %s", result.response.c_str());
        // log response size for debugging
        log_i("Aircraft JSON size: %d bytes", result.response.length());

        deserializeJson(doc, result.response);
        
        // log aircraft json data
        
        now = millis(); // override with post-parse timestamp

        // Only the map merge needs the mutex - Draw() iterates trackedAircraft
        // from the render thread concurrently with this background task.
        if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
            constexpr int STALE_TIMEOUT_MS = 30000;   // Remove aircraft not seen for 30 seconds
            
            // Parse and insert aircraft directly without temporary vector to avoid heap exhaustion.
            // Stop parsing once we reach MAX_TRACKED_AIRCRAFT to conserve memory.
            JsonArray states = doc["states"];
            int parseCount = 0;
            for (JsonVariant state : states) {
                // For new aircraft, check if we're at capacity before parsing
                const char* icao = state[0].as<const char*>();
                bool isExisting = false;
                for (auto& tracked : trackedAircraft) {
                    if (tracked.state.icao24 == icao) {
                        isExisting = true;
                        break;
                    }
                }
                
                // Skip new aircraft if we're at capacity
                if (!isExisting && trackedAircraft.size() >= MAX_TRACKED_AIRCRAFT) {
                    continue;
                }
                
                Aircraft ac = JsonParser::Parse<Aircraft>(state);
                
                // Find existing aircraft
                auto it = std::find_if(trackedAircraft.begin(), trackedAircraft.end(),
                    [&](const TrackedAircraft& ta) { return ta.state.icao24 == ac.icao24; });
                
                if (it == trackedAircraft.end()) {
                    // New aircraft - add it
                    trackedAircraft.push_back(TrackedAircraft{ ac, now });
                } else {
                    // Existing aircraft - update it
                    it->Update(ac, now);
                }
                
                if (++parseCount > 50) break;  // Safety: never parse more than 50 in one cycle
            }

            // Remove stale aircraft (not updated in STALE_TIMEOUT_MS)
            for (auto it = trackedAircraft.begin(); it != trackedAircraft.end(); ) {
                if ((now - it->lastSeen) > STALE_TIMEOUT_MS) {
                    it = trackedAircraft.erase(it);
                } else {
                    ++it;
                }
            }

            // Log how many planes were displayed for debugging purposes
            log_i("Stored %d aircraft", trackedAircraft.size());
            // display free memory for debugging
            log_i("Free heap: %d bytes", ESP.getFreeHeap());
            // Display free max heap block size for debugging
            log_i("Max alloc heap: %d bytes", ESP.getMaxAllocHeap());
            xSemaphoreGive(dataMutex);
        }
        
        // Clear the JsonDocument to free parsing memory
        doc.clear();
    }
}

void AircraftManager::Update()
{
    // Network fetching now runs on its own FreeRTOS task (spawned in
    // Initialise()) rather than here, so the render loop never blocks on it.
    // Left as a no-op so main.cpp's existing call site doesn't need to change.
}

void AircraftManager::Draw(LGFX_Sprite& backbuffer)
{
    DrawRadarCircles(backbuffer);
    DrawHeadingMarkings(backbuffer);
    static boolean alternate = false;
    static unsigned long lastAlternateToggle = 0;
    unsigned long now = millis();
    rad = configServer.GetStoredString("radius").toDouble();
    // if (newRad != rad) {
    //     rad = newRad;
    //     ForcedFetchCycle(); // force a fetch cycle to update the aircraft positions based on the new radius
    //     return;
    // }

    if (now - lastAlternateToggle >= 5000) {
        alternate = !alternate;
        lastAlternateToggle = now;
    }
    
    // trackedAircraft can be mutated concurrently by the background fetch
    // task (see FetchCycle()), so hold the mutex for the duration of the
    // iteration/draw - it's brief (a handful of aircraft, no I/O).
    if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
        // If max tracked aircraft is reached, display a red "MAX" indicator just underneath the range label at the top of the screen
        if (trackedAircraft.size() >= MAX_TRACKED_AIRCRAFT) {
            backbuffer.setTextSize(1);
            backbuffer.setTextColor(lgfx::color888(255, 0, 0));
            backbuffer.setTextDatum(lgfx::middle_center);
            backbuffer.drawString("MAX", SCREEN_SIZE_DIV_2, 10 + 3*backbuffer.fontHeight() + 2);
        }
        for (auto& tracked : trackedAircraft) {
            if (tracked.state.onGround) continue;

            tracked.Tick();
            auto [predLat, predLon] = tracked.GetDisplayPosition();
            auto [x, y] = ProjectCoordinateToScreen(predLat, predLon);

            if (displayInfoText)
                DrawAircraftInfo(backbuffer, x, y, tracked, alternate);

            if (displayTriangles)
                DrawAircraftIcon(backbuffer, x, y, tracked);
            else
                backbuffer.fillCircle(x, y, 3, lgfx::color888(0, 255, 0));
        }
        xSemaphoreGive(dataMutex);
    }
}

void AircraftManager::DrawHeadingMarkings(LGFX_Sprite& backbuffer) const
{
    constexpr int CENTRE = SCREEN_SIZE_DIV_2 - 1;
    constexpr int OUTER = SCREEN_SIZE_DIV_2 - 1;
    constexpr int MAJOR_TICK_LEN = 10; // px, ticks at 0/90/180/270
    constexpr int MINOR_TICK_LEN = 8;  // px, sub-ticks every 30 degrees in between
    constexpr int LABEL_INSET = 20;    // px in from the outer ring for the heading text

    // bearing measured clockwise from north (0 = up, 90 = right, 180 = down, 270 = left)
    auto pointOnCircle = [&](float radius, float bearingDeg) -> std::pair<float, float> {
        float rad = bearingDeg * (PI / 180.0f);
        return { CENTRE + radius * sinf(rad), CENTRE - radius * cosf(rad) };
    };

    backbuffer.setTextSize(1);
    backbuffer.setTextColor(lgfx::color888(0, 255, 0));
    backbuffer.setTextDatum(lgfx::middle_center);
    backbuffer.setTextColor(lgfx::color888(0, 100, 0));
    for (int bearing = 0; bearing < 360; bearing += 15) {
        const bool isMajor = (bearing % 90 == 0);
        const int tickLen = isMajor ? MAJOR_TICK_LEN : MINOR_TICK_LEN;

        auto [x0, y0] = pointOnCircle(OUTER, bearing);
        auto [x1, y1] = pointOnCircle(OUTER - tickLen, bearing);
        backbuffer.drawLine((int)x0, (int)y0, (int)x1, (int)y1, lgfx::color888(0, 150, 0));

        if (isMajor) {
            auto [lx, ly] = pointOnCircle(OUTER - LABEL_INSET, bearing);

            // Per-label manual position nudges (in px). Positive x = right, positive y = down.
            switch (bearing) {
                case 0:   ly -= 3; break; 
                case 90:  lx += 3; break; 
                case 180: ly += 4; break; 
                case 270: lx += 3; break; 
            }

            backbuffer.drawString(String(bearing), (int)lx, (int)ly);

            // Range label sits directly under the "0" heading text, 2px below it.
            if (bearing == 0) {
                // Calibrated so rad=1.0 -> 100km, rad=0.5 -> 50km (km = rad * 100).
                // This is an approximation - real km-per-degree varies with latitude.
                const int km = static_cast<int>(rad * 100.0);
                const int rangeY = (int)ly + backbuffer.fontHeight() + 2;
                backbuffer.drawString("R:" + String(km) + "km", (int)lx, rangeY);
            }
        }
    }

    backbuffer.setTextDatum(lgfx::top_left); // restore default anchor for the other text draws (info boxes etc.)
}

void AircraftManager::DrawRadarCircles(LGFX_Sprite& backbuffer) const
{
    constexpr int CENTRE = SCREEN_SIZE_DIV_2 - 1;
    constexpr int OUTER = SCREEN_SIZE_DIV_2 - 1;
    constexpr int RING_THICKNESS = 1; // px, increase for thicker range rings

    auto drawRing = [&](int radius, uint32_t color) {
        const int inner = std::max(0, radius - RING_THICKNESS);
        backbuffer.fillArc(CENTRE, CENTRE, inner, radius, 0, 360, color);
    };
    uint8_t circleColor = 100;
    drawRing(OUTER, lgfx::color888(0, circleColor, 0));
    drawRing((OUTER / 3) * 2, lgfx::color888(0, circleColor, 0));
    drawRing(OUTER / 3, lgfx::color888(0, circleColor, 0));
}

std::pair<int, int> AircraftManager::ProjectCoordinateToScreen(float predLat, float predLon) const
{
    const float dLon = predLon - lon;
    const float dLat = predLat - lat;

    const float normLon = (dLon + rad) / (2.0f * rad);
    const float normLat = (dLat + rad) / (2.0f * rad);

    const int x = static_cast<int>(normLon * SCREEN_SIZE);
    const int y = static_cast<int>(SCREEN_SIZE - (normLat * SCREEN_SIZE));

    return { x, y };
}

void AircraftManager::DrawAircraftInfo(LGFX_Sprite& backbuffer, int x, int y, const TrackedAircraft& tracked, bool alternate) const
{
    const int lineHeight = tft.fontHeight() + 1;

    backbuffer.setTextSize(1);
    backbuffer.setTextColor(lgfx::color888(0, 255, 0));
    if (alternate) {
        backbuffer.drawString(tracked.state.callsign, x + 5, y + 5);
        backbuffer.drawString(String(tracked.state.velocity) + "m/s", x + 5, y + 5 + lineHeight);
    } else {
        backbuffer.drawString(tracked.state.originCountry, x + 5, y + 5);
        backbuffer.drawString(String(tracked.state.baroAltitude) + "m", x + 5, y + 5 + lineHeight);
    }
}

void AircraftManager::DrawAircraftIcon(LGFX_Sprite& backbuffer, int x, int y, const TrackedAircraft& tracked) const
{
    // normalise heading into [0, 360)
    float heading = fmodf(tracked.state.trueTrack, 360.0f);
    if (heading < 0) heading += 360.0f;

    // Create a temporary sprite holding the "nose up" icon, then rotate it onto
    // the backbuffer at the aircraft's true heading. This replaces the old
    // 4-direction bucketing with a smooth, per-degree rotation.
    LGFX_Sprite rotated(&backbuffer);
    rotated.setColorDepth(16);
    rotated.createSprite(PLANE_ICON_SIZE, PLANE_ICON_SIZE);

    rotated.setSwapBytes(true);
    rotated.pushImage(0, 0, PLANE_ICON_SIZE, PLANE_ICON_SIZE, planeUp, PLANE_ICON_TRANSPARENT);
    rotated.setSwapBytes(false);

    // NOTE: transparent colour must be passed here too, otherwise the icon's
    // black square background paints over the radar rings instead of the
    // rings showing through around the plane shape.
    rotated.pushRotateZoom(&backbuffer, x, y, heading, 1.0f, 1.0f, PLANE_ICON_TRANSPARENT);

    rotated.deleteSprite();
}