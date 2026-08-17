#pragma once
#ifndef ES_APP_GUIS_ARKOS4CLONE_LEDCONTROL_H
#define ES_APP_GUIS_ARKOS4CLONE_LEDCONTROL_H

#include <string>
#include <vector>

namespace LedControl
{
    // WS2812 brightness levels
    extern const std::vector<std::pair<std::string, std::string>> WS2812_BRIGHTNESS;

    // Dual GPIO LED paths (left/right joystick LEDs)
    extern const std::string DUAL_GPIO_LED_LEFT;
    extern const std::string DUAL_GPIO_LED_RIGHT;

    // Single GPIO LED path
    extern const std::string SINGLE_GPIO_LED;

    std::string getSavedWs2812Brightness();

    // Detection & configuration
    std::string detectLedType();
    std::string getDeviceName();
    std::string getCurrentLedColor();
    std::vector<std::pair<std::string, std::string>> getLedMenuItems(const std::string& ledType);

    // Apply
    void applyLedColor(const std::string& color, const std::string& brightness = "");
    void applyMcuLed(const std::string& color);
    void applyGpioLed(const std::string& color);
    void applyWs2812Led(const std::string& color, const std::string& brightness = "HIGH");
    void applyDualGpioLed(bool leftOn, bool rightOn);
    void applySingleGpioLed(bool on);
    void applyR36UltraV2Led(const std::string& color);
    void saveLedConfig(const std::string& color, const std::string& brightness = "");

    // Config & startup
    bool checkAndApplyLedOnStartup();

    // Power LED
    bool hasPowerLedRed();
    bool hasPowerLedBlue();
    bool hasArkOS4CloneLed();
    bool hasPowerLed();
    void applyPowerLed();
    void applyPowerLedOnStartup();
}

#endif // ES_APP_GUIS_ARKOS4CLONE_LEDCONTROL_H
