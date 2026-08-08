#pragma once

#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

class ConfigurationWebServer {
private:
    AsyncWebServer server;
    Preferences prefs;

    // `prefs` is touched from at least three different task contexts:
    // the AsyncWebServer's own internal task (GET "/" and POST "/save"),
    // AircraftManager's background fetch task, and setup()/Initialise().
    // Preferences/NVS isn't thread-safe, so concurrent begin()/end() calls
    // from two of these at once can corrupt the NVS handle and crash -
    // this mutex serialises all access to `prefs`.
    SemaphoreHandle_t prefsMutex = nullptr;

public:
    ConfigurationWebServer() : server(80), prefs() {}
    ConfigurationWebServer(int port) : server(port), prefs() {}

    void Initialise();
    void StartServer();
    [[nodiscard]] const String GetStoredString(const char* key);
};