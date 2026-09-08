#pragma once
#ifndef ES_APP_GUIS_ARKOS4CLONE_SYSTEMSETTINGS_H
#define ES_APP_GUIS_ARKOS4CLONE_SYSTEMSETTINGS_H

#include <string>

namespace SystemSettings
{
    // SDL button index -> linux keycode mapping (based on GO-Super Gamepad SDL mapping)
    extern const int SDL_TO_LINUX_KEYCODE[];
    extern const int SDL_TO_LINUX_KEYCODE_COUNT;
    std::string keycodeToName(int keycode);

    // Analog stick switch functions
    int getStickSwitchKey();
    void setStickSwitchKey(int keyCode);
    bool hasStickSwitchSupport();
    void applyStickSwitchOnStartup();

    // ADC Dead Zone functions
    int getAdcDeadZone();
    void setAdcDeadZone(int value);
    bool hasAdcDeadZoneSupport();
    void applyDeadZoneOnStartup();

    // Volume key ADC calibration (devices reporting "adc" via console_detect -v)
    bool hasVolumeAdcSupport();
    void setVolumeAdcKeyValues(int volumeDown, int volumeUp);
    void applyVolumeAdcCalibrationOnStartup();

    // USB Switch functions (manual USB switch only)
    bool isUsbManualSwitch();
    bool isUsbInternal();
    void setUsbInternal(bool internal);

    // Date & Time functions
    std::string getCurrentDateTime();
    bool setSystemTime(int year, int month, int day, int hour, int minute);
    bool syncNetworkTime();
}

#endif // ES_APP_GUIS_ARKOS4CLONE_SYSTEMSETTINGS_H
