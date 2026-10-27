# MicroRadar

An ESP32-based aircraft radar display system that tracks aircraft in real-time using the OpenSky Network API and displays them on a 240×240 LCD screen.

<img width="1862" height="1833" alt="20261025_102111" src="https://github.com/user-attachments/assets/a6580a4a-8449-482a-a93a-318d4a059f7c" />

This work is derived from this post: https://abidcg.blogspot.com/2026/07/micro-radar-web-flasher.html which is a project 
derived from this repository: https://github.com/AnthonySturdy/micro-radar 

It fixes some issues, such as crashing with full memory when you live near a busy airport, and adds features such as live zooming and zone shifting from the web interface.
It displays flight number, country, distance and altitude, alternating to not cluter the small screen.

## Features

### Core Functionality
- **Real-time Aircraft Tracking**: Fetches live aircraft data from OpenSky Network API
- **Radar Display**: Visualizes aircraft positions on a 240×240 LCD screen with:
  - Concentric radar circles representing distance zones
  - Compass headings (N, E, S, W)
  - Aircraft icons with altitude and heading indicators
  - Customizable display rendering

### Display Features
- **Aircraft Triangles**: Display aircraft heading as directional triangles (configurable)
- **Info Text**: Show detailed aircraft information (configurable)
- **Animated Radar Sweep**: Smooth rotating radar animation
- **Multi-core Rendering**: Background task handles data fetching while main thread handles rendering

### Configuration
- **Web-based Configuration Server**: Access settings via web interface on port 80
- **Persistent Storage**: Settings saved to ESP32 flash memory (NVS)
- **WiFi Manager**: Built-in WiFi setup hotspot if credentials not preconfigured
- **Optional Hardcoded WiFi**: Support for pre-baked SSID and password
 
<img width="744" height="702" alt="image" src="https://github.com/user-attachments/assets/d46ea2db-fb15-4faa-84c1-8d7a1518c6ba" />


### Configuration Parameters
- **Latitude** (`latitude`): Center latitude for radar display
- **Longitude** (`longitude`): Center longitude for radar display
- **Radius** (`radius`): Search radius in degrees (default: 1.0°)
- **Display Settings**:
  - `infotext`: Show aircraft information text (default: true)
  - `triangle`: Display aircraft heading triangles (default: true)
  - `scanline`: Enable scanline effect (default: true)
- **OpenSky Credentials**:
  - `opensky-id`: OpenSky API username (optional, for higher rate limits)
  - `opensky-secret`: OpenSky API authentication token (optional)

## Hardware Requirements

- **Board**: ESP32 Dev Module
- **Display**: LovyanGFX-compatible LCD screen (240×240 pixels)
- **Connection**: USB for programming and serial monitoring

## Software Dependencies

### PlatformIO Configuration
```ini
platform = espressif32
board = esp32dev
framework = arduino
```

### Libraries
- **LovyanGFX** (lovyan03): LCD graphics library
- **WiFiManager** (tzapu): WiFi setup and management
- **ESP Async WebServer** (mathieucarbou): Asynchronous web server
- **AsyncTCP** (mathieucarbou): TCP support for async server
- **ArduinoJson** (bblanchon): JSON parsing

### Build Configuration
- **C++ Standard**: C++17 (`-std=gnu++17`)
- **Partition Scheme**: Huge App (`huge_app.csv`)
- **Debug Level**: Level 3 (INFO)
- **Monitor Speed**: 115200 baud

## Project Structure

```
src/
├── main.cpp                      # Entry point, initialization, and render loop
├── AircraftManager.*             # Core radar logic and display rendering
├── ConfigurationWebServer.*      # Web-based settings interface
├── HttpRequestManager.*          # HTTP client for OpenSky API
├── OpenSkyAuthTokenHandler.*     # Authentication for OpenSky API
├── models/
│   ├── Aircraft.*                # Aircraft data model
│   └── TrackedAircraft.h         # Aircraft tracking state

include/
├── LGFX.h                        # Display configuration
├── DrawHelpers.h                 # Graphics utility functions
├── JsonParser.h                  # JSON parsing utilities
├── PlaneIcons.h                  # Aircraft icon definitions
└── WiFiManagerHelpers.h          # WiFi setup helpers
```

## Getting Started

### 1. Setup Hardware
- Connect the ESP32 board and LCD display according to LovyanGFX configuration
- Connect via USB to your computer

### 2. Install Dependencies
```bash
platformio lib install
```

### 3. Configure WiFi (Optional)
Edit `src/main.cpp` to add preconfigured WiFi credentials:
```cpp
const char* preconfiguredWifiSsid = "YOUR_SSID";
const char* preconfiguredWifiPassword = "YOUR_PASSWORD";
```

Or use the WiFi Manager setup hotspot for wireless configuration.

### 4. Build and Upload
```bash
platformio run --target upload
```

### 5. Configure Settings
1. Monitor the serial output to find the device's IP address
2. Open a web browser and navigate to `http://<device-ip>`
3. Enter your coordinates and OpenSky credentials
4. Save settings

### 6. Monitor Serial Output
```bash
platformio device monitor --port COM6
```

## API Configuration

### OpenSky Network
- **Free Account**: ~400 requests/hour, 1° resolution
- **Authenticated Account**: ~4000 requests/hour, 0.1° resolution
- **Sign up**: https://opensky-network.org/

To use an authenticated account:
1. Create an account at OpenSky Network
2. Enter your username in the `opensky-id` field
3. Enter your authentication token in the `opensky-secret` field

## Technical Details

### Multi-core Architecture
- **Core 0**: Rendering loop (LCD updates)
- **Core 1**: Background fetch task (OpenSky API requests)
- **Thread Safety**: Semaphore-protected shared data access

### Memory Management
- **NVS Flash**: Used for persistent configuration storage
- **Sprite Buffer**: 240×240 color depth 8 backbuffer for smooth rendering
- **Self-healing NVS**: Automatic recovery from corrupted NVS partitions

### Coordinate Projection
Aircraft positions are projected from geographic coordinates (latitude/longitude) to screen coordinates using the centered radar display.

## Troubleshooting

### NVS Errors
If you see repeated "nvs_open failed" errors, the device will automatically erase and reinitialize the NVS partition on the next boot.

### WiFi Connection Issues
- Check your SSID and password
- Ensure the ESP32 is within range
- Use the WiFi Manager hotspot to reconfigure: "MicroRadar-SSID" appears after boot

### OpenSky API Issues
- Verify your credentials are correct
- Check your API account quota usage
- Ensure internet connectivity

### Display Issues
- Verify LovyanGFX configuration in `include/LGFX.h`
- Check LCD wiring and SPI connections
- Monitor serial output for initialization errors

## License

See [LICENSE.txt](LICENSE.txt)

## References

- [OpenSky Network API](https://opensky-network.org/apidoc/index.html)
- [LovyanGFX Documentation](https://github.com/lovyan03/LovyanGFX)
- [PlatformIO Documentation](https://docs.platformio.org/)
