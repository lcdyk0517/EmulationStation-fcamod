#include "guis/arkos4clone/SystemSettings.h"
#include "guis/arkos4clone/ArkOSUtil.h"
#include "Settings.h"
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"
#include "Log.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <algorithm>

namespace SystemSettings
{

// SDL button index -> linux keycode mapping (based on GO-Super Gamepad SDL mapping)
const int SDL_TO_LINUX_KEYCODE[] = {
    304, // 0:  BTN_SOUTH (B)
    305, // 1:  BTN_EAST (A)
    308, // 2:  BTN_WEST (Y)
    307, // 3:  BTN_NORTH (X)
    310, // 4:  BTN_TL (LB)
    311, // 5:  BTN_TR (RB)
    312, // 6:  BTN_TL2 (LT)
    313, // 7:  BTN_TR2 (RT)
    544, // 8:  BTN_DPAD_UP
    545, // 9:  BTN_DPAD_DOWN
    546, // 10: BTN_DPAD_LEFT
    547, // 11: BTN_DPAD_RIGHT
    704, // 12: BTN_TRIGGER_HAPPY1 (SELECT)
    705, // 13: BTN_TRIGGER_HAPPY2 (START)
    707, // 14: BTN_TRIGGER_HAPPY4 (THUMBL)
    706, // 15: BTN_TRIGGER_HAPPY3 (THUMBR)
    708, // 16: BTN_TRIGGER_HAPPY5 (F5)
    709, // 17: BTN_TRIGGER_HAPPY6 (F6)
};
const int SDL_TO_LINUX_KEYCODE_COUNT = sizeof(SDL_TO_LINUX_KEYCODE) / sizeof(SDL_TO_LINUX_KEYCODE[0]);

std::string keycodeToName(int keycode)
{
    switch (keycode) {
        case 304: return "BTN_B";
        case 305: return "BTN_A";
        case 307: return "BTN_X";
        case 308: return "BTN_Y";
        case 310: return "LB";
        case 311: return "RB";
        case 312: return "LT";
        case 313: return "RT";
        case 544: return "DPAD_UP";
        case 545: return "DPAD_DOWN";
        case 546: return "DPAD_LEFT";
        case 547: return "DPAD_RIGHT";
        case 704: return "SELECT";
        case 705: return "START";
        case 706: return "THUMBR";
        case 707: return "THUMBL";
        case 708: return "F5";
        case 709: return "F6";
        default: return "KEY_" + std::to_string(keycode);
    }
}

void applyStickSwitchOnStartup()
{
    if (!hasStickSwitchSupport()) return;
    if (!Settings::getInstance()->getBool("StickSwitchEnabled")) return;
    int savedKey = Settings::getInstance()->getInt("StickSwitchKey");
    if (savedKey > 0 && savedKey != 999) {
        setStickSwitchKey(savedKey);
    }
}

// ODROID Go3 joystick sysfs base (stick switch + ADC dead zone)
static const std::string ODROIDGO3_JOYPAD = "/sys/devices/platform/odroidgo3-joypad";

int getStickSwitchKey()
{
    std::string result = ArkOSUtil::executeCommand("cat " + ODROIDGO3_JOYPAD + "/stick_switch_key 2>/dev/null");
    try { return std::stoi(result); } catch (...) { return 0; }
}

void setStickSwitchKey(int keyCode)
{
    ArkOSUtil::executeCommand("sudo sh -c 'echo " + std::to_string(keyCode) + " > " + ODROIDGO3_JOYPAD + "/stick_switch_key'");
}

bool hasStickSwitchSupport()
{
    return getStickSwitchKey() != 0;
}

// USB switch sysfs path for R36Max2
static const std::string USB_SWITCH_PATH = "/sys/devices/platform/ff2c0000.syscon/ff2c0000.syscon:usb2-phy@100";

bool isUsbManualSwitch()
{
    std::string mode = ArkOSUtil::executeCommand("/usr/local/bin/console_detect -O 2>/dev/null");

    // Remove all non-alphanumeric characters (handles \r, \n, spaces, etc.)
    std::string clean;
    for (char c : mode) {
        if (std::isalnum(c)) {
            clean += c;
        }
    }

    // Log for debugging (Info level for visibility)
    LOG(LogInfo) << "USB Switch: mode = [" << clean << "]";

    // Case-insensitive comparison
    std::transform(clean.begin(), clean.end(), clean.begin(), ::tolower);

    bool result = (clean == "manual");
    LOG(LogInfo) << "USB Switch: isUsbManualSwitch = " << (result ? "true" : "false");

    return result;
}

bool isUsbInternal()
{
    // Read current USB switch status
    // Internal USB: usb_switch_gpio=0, usb_switch_ext=1
    // External USB: usb_switch_gpio=1, usb_switch_ext=0

    std::string gpioPath = USB_SWITCH_PATH + "/usb_switch_gpio";
    std::string extPath = USB_SWITCH_PATH + "/usb_switch_ext";

    if (!Utils::FileSystem::exists(gpioPath) || !Utils::FileSystem::exists(extPath)) {
        return false; // Default to internal if files don't exist
    }

    std::string gpioValue = ArkOSUtil::executeCommand("cat " + gpioPath + " 2>/dev/null");
    std::string extValue = ArkOSUtil::executeCommand("cat " + extPath + " 2>/dev/null");

    int gpio = atoi(Utils::String::trim(gpioValue).c_str());
    int ext = atoi(Utils::String::trim(extValue).c_str());

    // Internal USB: gpio=0, ext=1
    return (gpio == 0 && ext == 1);
}

void setUsbInternal(bool internal)
{
    std::string gpioPath = USB_SWITCH_PATH + "/usb_switch_gpio";
    std::string extPath = USB_SWITCH_PATH + "/usb_switch_ext";

    if (!Utils::FileSystem::exists(gpioPath) || !Utils::FileSystem::exists(extPath)) {
        return;
    }

    if (internal) {
        // Switch to internal USB: first enable ext, then disable gpio
        ArkOSUtil::executeCommand("sudo sh -c 'echo 1 > " + extPath + "'");
        ArkOSUtil::executeCommand("sudo sh -c 'echo 0 > " + gpioPath + "'");
    } else {
        // Switch to external USB: first enable gpio, then disable ext
        ArkOSUtil::executeCommand("sudo sh -c 'echo 1 > " + gpioPath + "'");
        ArkOSUtil::executeCommand("sudo sh -c 'echo 0 > " + extPath + "'");
    }
}

int getAdcDeadZone()
{
    std::string result = ArkOSUtil::executeCommand("cat " + ODROIDGO3_JOYPAD + "/adc_deadzone 2>/dev/null");
    try { return std::stoi(result); } catch (...) { return 0; }
}

void setAdcDeadZone(int value)
{
    ArkOSUtil::executeCommand("sudo sh -c 'echo " + std::to_string(value) + " > " + ODROIDGO3_JOYPAD + "/adc_deadzone'");
}

bool hasAdcDeadZoneSupport()
{
    return getAdcDeadZone() != 0;
}

void applyDeadZoneOnStartup()
{
    if (!hasAdcDeadZoneSupport()) return;
    int savedValue = Settings::getInstance()->getInt("AdcDeadZone");
    if (savedValue >= 10 && savedValue <= 1800) {
        setAdcDeadZone(savedValue);
    }
}

// Volume key ADC calibration sysfs nodes (rg351-keys ADC volume keys)
static const std::string RG351_KEYS_ADC_DOWN = "/sys/devices/platform/rg351-keys/adc_value_volume_down";
static const std::string RG351_KEYS_ADC_UP = "/sys/devices/platform/rg351-keys/adc_value_volume_up";

bool hasVolumeAdcSupport()
{
    static bool cached = false;
    static bool supported = false;

    if (cached) {
        return supported;
    }

    cached = true;

    std::string output = ArkOSUtil::executeCommand("/usr/local/bin/console_detect -v 2>/dev/null");

    // Remove all non-alphanumeric characters (handles \r, \n, spaces, etc.)
    std::string clean;
    for (char c : output) {
        if (std::isalnum((unsigned char)c)) {
            clean += c;
        }
    }

    // Case-insensitive comparison
    std::transform(clean.begin(), clean.end(), clean.begin(), ::tolower);

    LOG(LogInfo) << "Volume ADC: mode = [" << clean << "]";

    supported = (clean == "adc");
    LOG(LogInfo) << "Volume ADC: hasVolumeAdcSupport = " << (supported ? "true" : "false");

    return supported;
}

void setVolumeAdcKeyValues(int volumeDown, int volumeUp)
{
    ArkOSUtil::executeCommand("sudo sh -c 'echo " + std::to_string(volumeDown) + " > " + RG351_KEYS_ADC_DOWN + "'");
    ArkOSUtil::executeCommand("sudo sh -c 'echo " + std::to_string(volumeUp) + " > " + RG351_KEYS_ADC_UP + "'");
}

void applyVolumeAdcCalibrationOnStartup()
{
    if (!hasVolumeAdcSupport()) return;
    if (!Settings::getInstance()->getBool("VolumeAdcCalibration.hasData")) return;

    int down = Settings::getInstance()->getInt("VolumeAdcCalibration.down");
    int up = Settings::getInstance()->getInt("VolumeAdcCalibration.up");

    if (down < 0) down = 0;
    if (up < 0) up = 0;

    setVolumeAdcKeyValues(down, up);
}

std::string getCurrentDateTime()
{
    std::string result = ArkOSUtil::executeCommand("date '+%Y-%m-%d %H:%M'");
    return result.empty() ? "2024-01-01 00:00" : result;
}

bool setSystemTime(int year, int month, int day, int hour, int minute)
{
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "sudo date -s '%04d-%02d-%02d %02d:%02d:00' 2>/dev/null", year, month, day, hour, minute);
    ArkOSUtil::executeCommand(cmd);

    // Sync to hardware clock
    ArkOSUtil::executeCommand("sudo hwclock -w 2>/dev/null");

    return true;
}

bool syncNetworkTime()
{
    // Enable NTP sync
    ArkOSUtil::executeCommand("sudo timedatectl set-ntp 1 2>/dev/null");

    // Try ntpdate as fallback
    ArkOSUtil::executeCommand("sudo ntpdate pool.ntp.org 2>/dev/null || sudo ntpdate time.google.com 2>/dev/null");

    // Sync to hardware clock
    ArkOSUtil::executeCommand("sudo hwclock -w 2>/dev/null");

    return true;
}

}
