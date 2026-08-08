#pragma once

#include <map>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "models/TrackedAircraft.h"
#include "ConfigurationWebServer.h"
#include "OpenSkyAuthTokenHandler.h"
#include "LGFX.h"
#include "PlaneIcons.h"

class AircraftManager
{
private:
    double lat = 0.0;
    double lon = 0.0;
    double rad = 0.2;
    std::map<String, TrackedAircraft> trackedAircraft;

    bool displayInfoText = true;
    bool displayTriangles = true;

    unsigned long fetchInterval = 0;
    unsigned long lastFetch = 999999;

    ConfigurationWebServer& configServer;
    OpenSkyAuthTokenHandler& authHandler;
    HttpRequestManager& http;
    LGFX& tft;

    // The OpenSky HTTP/token calls are blocking and can take a second or more
    // over TLS. Running them in loop() stalls the sweep/radar animation every
    // fetchInterval (~22s with an authed account). Instead, the fetch runs on
    // its own FreeRTOS task on the second core; this mutex guards the only
    // piece of state both the render thread (Draw) and the fetch task touch.
    SemaphoreHandle_t dataMutex = nullptr;
    TaskHandle_t fetchTaskHandle = nullptr;
    static void FetchTaskTrampoline(void* param);
    void FetchLoop();  // runs forever on the background task
    void FetchCycle(); // one fetch-and-merge pass (the old Update() body)

    void DrawRadarCircles(LGFX_Sprite& backbuffer) const;
    void DrawHeadingMarkings(LGFX_Sprite& backbuffer) const;
    std::pair<int, int> ProjectCoordinateToScreen(float predLat, float predLon) const;
    void DrawAircraftInfo(LGFX_Sprite& backbuffer, int x, int y, const TrackedAircraft& tracked) const;
    void DrawAircraftIcon(LGFX_Sprite& backbuffer, int x, int y, const TrackedAircraft& tracked) const;

public:
    AircraftManager(ConfigurationWebServer& config, OpenSkyAuthTokenHandler& auth, HttpRequestManager& httpManager, LGFX& tftGfx)
        : configServer(config), authHandler(auth), http(httpManager), tft(tftGfx)
    {
    }
    ~AircraftManager() = default;

    void Initialise();
    void Update();
    void Draw(LGFX_Sprite& backbuffer);
};