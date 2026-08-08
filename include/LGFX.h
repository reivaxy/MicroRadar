#pragma once

#include <LovyanGFX.hpp>

// LGFX config for a generic ESP32-WROOM-32 driving a GC9A01 240x240 round TFT.
// No touch, no backlight pin (backlight tied high on the module).
class LGFX : public lgfx::LGFX_Device
{
    lgfx::Panel_GC9A01 _panel;
    lgfx::Bus_SPI _bus;

public:
    LGFX(void)
    {
        {
            auto cfg = _bus.config();
            cfg.spi_host = VSPI_HOST;
            cfg.spi_mode = 0;
            cfg.freq_write = 40000000;
            cfg.freq_read = 16000000;
            cfg.spi_3wire = false;
            cfg.use_lock = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk = 18;   // SCL
            cfg.pin_mosi = 23;    // SDA
            cfg.pin_miso = -1;   // not used, GC9A01 is write-only
            cfg.pin_dc = 25;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg = _panel.config();
            cfg.pin_cs = 26;
            cfg.pin_rst = 27;
            cfg.pin_busy = -1;
            cfg.panel_width = 240;
            cfg.panel_height = 240;
            cfg.offset_rotation = 0;
            cfg.invert = true;   // GC9A01 panels typically need inverted colours
            cfg.rgb_order = false;
            _panel.config(cfg);
        }
        // No backlight control - GC9A01 module's BL pin is tied to 3.3V.
        setPanel(&_panel);
    }
};