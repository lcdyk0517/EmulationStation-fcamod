#include "guis/arkos4clone/LedControl.h"
#include "guis/arkos4clone/ArkOSUtil.h"
#include "Settings.h"
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"
#include "EsLocale.h"

#include <map>
#include <sstream>
#include <cstdlib>
#include <algorithm>

namespace LedControl
{

// Power LED sysfs paths (separate red/blue LEDs)
static const std::string POWER_LED_RED = "/sys/class/leds/led-red/brightness";
static const std::string POWER_LED_BLUE = "/sys/class/leds/led-blue/brightness";

// Dual-color power LED (ArkOS4Clone devices: 0=blue/green, 1=red)
static const std::string ARKOS4CLONE_LED = "/sys/class/leds/arkos4clone-led/brightness";

// MCU LED mode names (for mcu_led backend)
static const std::map<std::string, std::string> MCU_MODES = {
    {"red", "Red"}, {"green", "Green"}, {"blue", "Blue"}, {"white", "White"},
    {"orange", "Orange"}, {"purple", "Purple"}, {"cyan", "Cyan"},
    {"breath_red", "Breathing_Red"}, {"breath_green", "Breathing_Green"},
    {"breath_blue", "Breathing_Blue"}, {"breath_white", "Breathing_White"},
    {"breath_orange", "Breathing_Orange"}, {"breath_purple", "Breathing_Purple"},
    {"breath_cyan", "Breathing_Cyan"}, {"breath", "Breathing"}, {"flow", "Flow"}
};

// WS2812 mode names (for ws2812 backend)
static const std::map<std::string, std::string> WS2812_MODES = {
    {"scrolling", "Scrolling"}, {"breathing", "Breathing"},
    {"breathing_red", "Breathing_Red"}, {"breathing_green", "Breathing_Green"}, {"breathing_blue", "Breathing_Blue"},
    {"breathing_blue_red", "Breathing_Blue_Red"}, {"breathing_green_blue", "Breathing_Green_Blue"},
    {"breathing_red_green", "Breathing_Red_Green"}, {"breathing_red_green_blue", "Breathing_Red_Green_Blue"},
    {"red_green_blue", "Red_Green_Blue"}, {"blue_red", "Blue_Red"}, {"blue", "Blue"},
    {"green_blue", "Green_Blue"}, {"green", "Green"}, {"red_green", "Red_Green"}, {"red", "Red"}
};

// WS2812 brightness levels
const std::vector<std::pair<std::string, std::string>> WS2812_BRIGHTNESS = {
    {"HIGH", "HIGH"}, {"MEDIUM", "MEDIUM"}, {"LOW", "LOW"}
};

// Dual GPIO LED paths (left/right joystick LEDs)
const std::string DUAL_GPIO_LED_LEFT = "/sys/class/leds/joy-left/brightness";
const std::string DUAL_GPIO_LED_RIGHT = "/sys/class/leds/joy-right/brightness";

// Single GPIO LED path
const std::string SINGLE_GPIO_LED = "/sys/class/leds/joy-led/brightness";

std::string getSavedWs2812Brightness()
{
    std::string brightness = Settings::getInstance()->getString("JoyLedBrightness");
    // Validate brightness value
    for (const auto& b : WS2812_BRIGHTNESS) {
        if (b.first == brightness) {
            return brightness;
        }
    }
    return "HIGH"; // Default brightness
}

std::string detectLedType()
{
    // Cache result to avoid repeated console_detect calls
    static std::string cachedLedType;
    static bool cached = false;

    if (cached) {
        return cachedLedType;
    }

    cached = true;

    // Use console_detect -s for LED type
    std::string output = ArkOSUtil::executeCommand("/usr/local/bin/console_detect -s 2>/dev/null");
    if (!output.empty()) {
        std::istringstream stream(output);
        std::string line;
        while (std::getline(stream, line)) {
            if (line.find("LED_TYPE=") == 0) {
                std::string ledType = line.substr(9);
                if (!ledType.empty() && ledType != "unsupported") {
                    cachedLedType = ledType;
                    return cachedLedType;
                }
            }
        }
    }

    return cachedLedType; // empty if not detected
}

std::string getDeviceName()
{
    std::string output = ArkOSUtil::executeCommand("/usr/local/bin/console_detect -s 2>/dev/null");
    if (!output.empty()) {
        std::istringstream stream(output);
        std::string line;
        while (std::getline(stream, line)) {
            if (line.find("DEVICE_NAME=") == 0) {
                return line.substr(12);
            }
        }
    }
    return ArkOSUtil::executeCommand("cat /boot/.console 2>/dev/null");
}

std::string getCurrentLedColor()
{
    std::string color = Settings::getInstance()->getString("JoyLedColor");
    return color.empty() ? "off" : color;
}

std::vector<std::pair<std::string, std::string>> getLedMenuItems(const std::string& ledType)
{
    std::vector<std::pair<std::string, std::string>> items;

    if (ledType == "mcu_led") {
        items.push_back({"off", _("TURN OFF LED")});
        items.push_back({"red", _("SOLID RED")});
        items.push_back({"green", _("SOLID GREEN")});
        items.push_back({"blue", _("SOLID BLUE")});
        items.push_back({"orange", _("SOLID ORANGE")});
        items.push_back({"purple", _("SOLID PURPLE")});
        items.push_back({"cyan", _("SOLID CYAN")});
        items.push_back({"white", _("SOLID WHITE")});
        items.push_back({"breath_red", _("BREATHING RED")});
        items.push_back({"breath_green", _("BREATHING GREEN")});
        items.push_back({"breath_blue", _("BREATHING BLUE")});
        items.push_back({"breath_orange", _("BREATHING ORANGE")});
        items.push_back({"breath_purple", _("BREATHING PURPLE")});
        items.push_back({"breath_cyan", _("BREATHING CYAN")});
        items.push_back({"breath_white", _("BREATHING WHITE")});
        items.push_back({"flow", _("FLOW EFFECT")});
    } else if (ledType == "gpio") {
        items.push_back({"off", _("TURN OFF LED")});
        items.push_back({"red", _("SOLID RED")});
        items.push_back({"green", _("SOLID GREEN")});
        items.push_back({"blue", _("SOLID BLUE")});
        items.push_back({"white", _("SOLID WHITE")});
        items.push_back({"orange", _("SOLID ORANGE")});
        items.push_back({"yellow", _("SOLID YELLOW")});
        items.push_back({"purple", _("SOLID PURPLE")});
    } else if (ledType == "ws2812") {
        items.push_back({"off", _("TURN OFF LED")});
        items.push_back({"scrolling", _("SCROLLING EFFECT")});
        items.push_back({"breathing", _("BREATHING")});
        items.push_back({"breathing_red", _("BREATHING RED")});
        items.push_back({"breathing_green", _("BREATHING GREEN")});
        items.push_back({"breathing_blue", _("BREATHING BLUE")});
        items.push_back({"breathing_blue_red", _("BREATHING MAGENTA")});
        items.push_back({"breathing_green_blue", _("BREATHING CYAN")});
        items.push_back({"breathing_red_green", _("BREATHING YELLOW")});
        items.push_back({"breathing_red_green_blue", _("BREATHING RGB")});
        items.push_back({"red", _("SOLID RED")});
        items.push_back({"green", _("SOLID GREEN")});
        items.push_back({"blue", _("SOLID BLUE")});
        items.push_back({"red_green", _("SOLID YELLOW")});
        items.push_back({"green_blue", _("SOLID CYAN")});
        items.push_back({"blue_red", _("SOLID MAGENTA")});
        items.push_back({"red_green_blue", _("SOLID WHITE")});
    } else if (ledType == "r36ultra_v2") {
        // R36Ultra V2 joystick LED via /sys/class/leds/joyled/mode
        items.push_back({"off", _("TURN OFF LED")});
        items.push_back({"red", _("SOLID RED")});
        items.push_back({"red_green", _("SOLID YELLOW")});
        items.push_back({"green", _("SOLID GREEN")});
        items.push_back({"green_blue", _("SOLID CYAN")});
        items.push_back({"blue", _("SOLID BLUE")});
        items.push_back({"blue_red", _("SOLID MAGENTA")});
        items.push_back({"red_green_blue", _("SOLID WHITE")});
        items.push_back({"breathing", _("BREATHING")});
        items.push_back({"scrolling", _("SCROLLING EFFECT")});
    }

    return items;
}

void applyLedColor(const std::string& color, const std::string& brightness)
{
    std::string ledType = detectLedType();
    std::string deviceName = getDeviceName();

    // Special handling for r36ultra: check saved version
    if (deviceName == "r36ultra") {
        int version = Settings::getInstance()->getInt("R36UltraLedVersion");
        if (version == 2) {
            applyR36UltraV2Led(color);
        } else {
            applyGpioLed(color);
        }
        saveLedConfig(color);
        return;
    }

    if (ledType == "mcu_led") {
        applyMcuLed(color);
        saveLedConfig(color);
    } else if (ledType == "gpio") {
        applyGpioLed(color);
        saveLedConfig(color);
    } else if (ledType == "ws2812") {
        std::string bri = brightness.empty() ? "HIGH" : brightness;
        applyWs2812Led(color, bri);
        saveLedConfig(color, bri);
    } else if (ledType == "r36ultra_v2") {
        applyR36UltraV2Led(color);
        saveLedConfig(color);
    }
}

void applyMcuLed(const std::string& color)
{
    const int GPIO_NUM = 65;
    std::string gpioDir = "/sys/class/gpio/gpio" + std::to_string(GPIO_NUM);
    std::string gpioExport = "/sys/class/gpio/export";
    std::string mcuLedBin = "/usr/bin/mcu_led";

    // Security: validate color string
    if (!color.empty() && color.find_first_not_of("abcdefghijklmnopqrstuvwxyz_") != std::string::npos) {
        return;
    }

    // Export GPIO if needed
    if (!Utils::FileSystem::exists(gpioDir + "/direction")) {
        ArkOSUtil::executeCommand("sudo sh -c 'echo " + std::to_string(GPIO_NUM) + " > " + gpioExport + "'");
    }
    // Always ensure direction is out
    ArkOSUtil::executeCommand("sudo sh -c 'echo out > " + gpioDir + "/direction'");

    if (color == "off") {
        ArkOSUtil::executeCommand("sudo sh -c 'echo 0 > " + gpioDir + "/value'");
        return;
    }

    ArkOSUtil::executeCommand("sudo sh -c 'echo 1 > " + gpioDir + "/value'");

    auto it = MCU_MODES.find(color);
    if (it != MCU_MODES.end() && Utils::FileSystem::exists(mcuLedBin)) {
        ArkOSUtil::executeCommand("sudo " + mcuLedBin + " " + it->second);
    }
}

void applyGpioLed(const std::string& color)
{
    // Security: validate color string
    if (!color.empty() && color.find_first_not_of("abcdefghijklmnopqrstuvwxyz_") != std::string::npos) {
        return;
    }

    std::string ledBlue = "/sys/class/leds/joy-blue/brightness";
    std::string ledGreen = "/sys/class/leds/joy-green/brightness";
    std::string ledRed = "/sys/class/leds/joy-red/brightness";

    // Disable triggers only for joystick LEDs
    ArkOSUtil::executeCommand("sudo sh -c 'echo none > /sys/class/leds/joy-blue/trigger 2>/dev/null; echo none > /sys/class/leds/joy-green/trigger 2>/dev/null; echo none > /sys/class/leds/joy-red/trigger 2>/dev/null'");

    // Get max brightness
    int maxB = 1, maxG = 1, maxR = 1;
    std::string maxBPath = "/sys/class/leds/joy-blue/max_brightness";
    std::string maxGPath = "/sys/class/leds/joy-green/max_brightness";
    std::string maxRPath = "/sys/class/leds/joy-red/max_brightness";

    if (Utils::FileSystem::exists(maxBPath)) {
        maxB = atoi(ArkOSUtil::executeCommand("cat " + maxBPath).c_str());
    }
    if (Utils::FileSystem::exists(maxGPath)) {
        maxG = atoi(ArkOSUtil::executeCommand("cat " + maxGPath).c_str());
    }
    if (Utils::FileSystem::exists(maxRPath)) {
        maxR = atoi(ArkOSUtil::executeCommand("cat " + maxRPath).c_str());
    }

    int b = 0, g = 0, r = 0;

    if (color == "red") {
        r = maxR;
    } else if (color == "green") {
        g = maxG;
    } else if (color == "blue") {
        b = maxB;
    } else if (color == "white") {
        b = maxB; g = maxG; r = maxR;
    } else if (color == "orange" || color == "yellow") {
        g = maxG; r = maxR;
    } else if (color == "purple") {
        b = maxB; r = maxR;
    }
    // else: color == "off" or unknown, all remain 0

    // Apply colors
    if (Utils::FileSystem::exists(ledBlue)) {
        ArkOSUtil::executeCommand("sudo sh -c 'echo " + std::to_string(b) + " > " + ledBlue + "'");
    }
    if (Utils::FileSystem::exists(ledGreen)) {
        ArkOSUtil::executeCommand("sudo sh -c 'echo " + std::to_string(g) + " > " + ledGreen + "'");
    }
    if (Utils::FileSystem::exists(ledRed)) {
        ArkOSUtil::executeCommand("sudo sh -c 'echo " + std::to_string(r) + " > " + ledRed + "'");
    }
}

void applyDualGpioLed(bool leftOn, bool rightOn)
{
    // Disable triggers for both LEDs
    ArkOSUtil::executeCommand("sudo sh -c 'echo none > /sys/class/leds/joy-left/trigger 2>/dev/null; echo none > /sys/class/leds/joy-right/trigger 2>/dev/null'");

    // Get max brightness
    int maxLeft = 1, maxRight = 1;
    std::string maxLeftPath = "/sys/class/leds/joy-left/max_brightness";
    std::string maxRightPath = "/sys/class/leds/joy-right/max_brightness";

    if (Utils::FileSystem::exists(maxLeftPath)) {
        maxLeft = atoi(ArkOSUtil::executeCommand("cat " + maxLeftPath).c_str());
    }
    if (Utils::FileSystem::exists(maxRightPath)) {
        maxRight = atoi(ArkOSUtil::executeCommand("cat " + maxRightPath).c_str());
    }

    // Apply LED states
    if (Utils::FileSystem::exists(DUAL_GPIO_LED_LEFT)) {
        ArkOSUtil::executeCommand("sudo sh -c 'echo " + std::to_string(leftOn ? maxLeft : 0) + " > " + DUAL_GPIO_LED_LEFT + "'");
    }
    if (Utils::FileSystem::exists(DUAL_GPIO_LED_RIGHT)) {
        ArkOSUtil::executeCommand("sudo sh -c 'echo " + std::to_string(rightOn ? maxRight : 0) + " > " + DUAL_GPIO_LED_RIGHT + "'");
    }
}

void applySingleGpioLed(bool on)
{
    if (!Utils::FileSystem::exists(SINGLE_GPIO_LED)) {
        return;
    }

    // Disable trigger
    ArkOSUtil::executeCommand("sudo sh -c 'echo none > /sys/class/leds/joy-led/trigger 2>/dev/null'");

    // Get max brightness
    int maxBrightness = 1;
    std::string maxBrightnessPath = "/sys/class/leds/joy-led/max_brightness";
    if (Utils::FileSystem::exists(maxBrightnessPath)) {
        maxBrightness = atoi(ArkOSUtil::executeCommand("cat " + maxBrightnessPath).c_str());
    }

    // Apply LED state
    ArkOSUtil::executeCommand("sudo sh -c 'echo " + std::to_string(on ? maxBrightness : 0) + " > " + SINGLE_GPIO_LED + "'");
}

void applyWs2812Led(const std::string& color, const std::string& brightness)
{
    // Security: validate color string
    if (!color.empty() && color.find_first_not_of("abcdefghijklmnopqrstuvwxyz_") != std::string::npos) {
        return;
    }

    // Security: validate brightness string
    std::string validBrightness = "HIGH";
    for (const auto& b : WS2812_BRIGHTNESS) {
        if (b.first == brightness) {
            validBrightness = brightness;
            break;
        }
    }

    std::string ws2812Bin = "/usr/bin/ws2812";

    if (!Utils::FileSystem::exists(ws2812Bin)) {
        return;
    }

    // Kill existing ws2812 process
    ArkOSUtil::executeCommand("sudo pkill -f '^" + ws2812Bin + "' 2>/dev/null || true");

    if (color == "off") {
        // Run OFF command to actually turn off the LEDs
        ArkOSUtil::executeCommand("sudo " + ws2812Bin + " OFF 2>/dev/null || true");
        return;
    }

    auto it = WS2812_MODES.find(color);
    if (it != WS2812_MODES.end()) {
        ArkOSUtil::executeCommand("sudo nohup " + ws2812Bin + " " + it->second + " " + validBrightness + " >/dev/null 2>&1 </dev/null &");
    }
}

void applyR36UltraV2Led(const std::string& color)
{
    // R36Ultra V2 joystick LED via /sys/class/leds/joyled/mode
    static const std::string JOYLED_MODE_PATH = "/sys/class/leds/joyled/mode";

    // Security: validate color string
    if (!color.empty() && color.find_first_not_of("abcdefghijklmnopqrstuvwxyz_") != std::string::npos) {
        return;
    }

    if (!Utils::FileSystem::exists(JOYLED_MODE_PATH)) {
        return;
    }

    // Always turn off first before changing color
    ArkOSUtil::executeCommand("sudo sh -c 'echo off > " + JOYLED_MODE_PATH + "'");

    if (color == "off") {
        return; // Already off
    }

    // Apply new color
    ArkOSUtil::executeCommand("sudo sh -c 'echo " + color + " > " + JOYLED_MODE_PATH + "'");
}

void saveLedConfig(const std::string& color, const std::string& brightness)
{
    Settings::getInstance()->setString("JoyLedColor", color);
    if (!brightness.empty()) {
        Settings::getInstance()->setString("JoyLedBrightness", brightness);
    }
    Settings::getInstance()->saveFile();
}

bool checkAndApplyLedOnStartup()
{
    std::string ledType = detectLedType();
    std::string deviceName = getDeviceName();

    // Special handling for dual-gpio
    if (ledType == "dual-gpio") {
        bool leftOn = Settings::getInstance()->getBool("JoyLedLeft");
        bool rightOn = Settings::getInstance()->getBool("JoyLedRight");
        applyDualGpioLed(leftOn, rightOn);
        return leftOn || rightOn;
    }

    // Special handling for single-gpio
    if (ledType == "single-gpio") {
        bool on = Settings::getInstance()->getBool("JoyLedOn");
        applySingleGpioLed(on);
        return on;
    }

    // Special handling for r36ultra (V1/V2 version)
    if (deviceName == "r36ultra") {
        int version = Settings::getInstance()->getInt("R36UltraLedVersion");
        std::string savedColor = Settings::getInstance()->getString("JoyLedColor");

        if (savedColor.empty() || savedColor == "off") {
            return false;
        }

        if (version == 2) {
            applyR36UltraV2Led(savedColor);
        } else {
            applyGpioLed(savedColor);
        }
        return true;
    }

    // Read saved color and brightness from Settings
    std::string savedColor = Settings::getInstance()->getString("JoyLedColor");
    std::string savedBrightness = Settings::getInstance()->getString("JoyLedBrightness");

    // Early exit if no color or off
    if (savedColor.empty() || savedColor == "off") {
        return false;
    }

    // Apply the saved color
    if (ledType == "mcu_led") {
        applyMcuLed(savedColor);
    } else if (ledType == "gpio") {
        applyGpioLed(savedColor);
    } else if (ledType == "ws2812") {
        std::string brightness = savedBrightness.empty() ? "HIGH" : savedBrightness;
        applyWs2812Led(savedColor, brightness);
    }

    return true;
}

bool hasPowerLedRed()
{
    return Utils::FileSystem::exists(POWER_LED_RED);
}

bool hasPowerLedBlue()
{
    return Utils::FileSystem::exists(POWER_LED_BLUE);
}

bool hasArkOS4CloneLed()
{
    return Utils::FileSystem::exists(ARKOS4CLONE_LED);
}

bool hasPowerLed()
{
    return hasPowerLedRed() || hasPowerLedBlue() || hasArkOS4CloneLed();
}

void applyPowerLed()
{
    if (!hasPowerLed()) {
        return;
    }

    // 新驱动架构：
    // - 阈值>0：驱动自动根据电量阈值控制LED（充电/充满优先级最高）
    // - 阈值=0：用户控制，需要手动写 brightness

    int redMode = Settings::getInstance()->getInt("PowerLedRedMode");
    int blueMode = Settings::getInstance()->getInt("PowerLedBlueMode");
    int arkosMode = Settings::getInstance()->getInt("PowerLedArkOS4CloneMode");
    int redThreshold = Settings::getInstance()->getInt("PowerLedRedThreshold");
    int blueThreshold = Settings::getInstance()->getInt("PowerLedBlueThreshold");
    int arkosThreshold = Settings::getInstance()->getInt("PowerLedArkOS4CloneThreshold");

    // Defaults
    if (redMode < 0 || redMode > 2) redMode = 0;
    if (blueMode < 0 || blueMode > 2) blueMode = 0;
    if (arkosMode < 0 || arkosMode > 2) arkosMode = 0;
    if (redThreshold < 0 || redThreshold > 90) redThreshold = 0;
    if (blueThreshold < 0 || blueThreshold > 90) blueThreshold = 0;
    if (arkosThreshold < 0 || arkosThreshold > 90) arkosThreshold = 0;

    // RED LED
    if (hasPowerLedRed()) {
        std::string thresholdPath = POWER_LED_RED.substr(0, POWER_LED_RED.rfind('/')) + "/battery_threshold";
        if (Utils::FileSystem::exists(thresholdPath)) {
            ArkOSUtil::executeCommand("sudo sh -c 'echo " + std::to_string(redThreshold) + " > " + thresholdPath + "'");
        }
        // 阈值=0时，用户控制 brightness
        if (redThreshold == 0) {
            int brightness = (redMode == 1) ? 1 : 0; // ON=1, OFF=0
            ArkOSUtil::executeCommand("sudo sh -c 'echo " + std::to_string(brightness) + " > " + POWER_LED_RED + "'");
        }
    }

    // BLUE LED
    if (hasPowerLedBlue()) {
        std::string thresholdPath = POWER_LED_BLUE.substr(0, POWER_LED_BLUE.rfind('/')) + "/battery_threshold";
        if (Utils::FileSystem::exists(thresholdPath)) {
            ArkOSUtil::executeCommand("sudo sh -c 'echo " + std::to_string(blueThreshold) + " > " + thresholdPath + "'");
        }
        // 阈值=0时，用户控制 brightness
        if (blueThreshold == 0) {
            int brightness = (blueMode == 1) ? 1 : 0; // ON=1, OFF=0
            ArkOSUtil::executeCommand("sudo sh -c 'echo " + std::to_string(brightness) + " > " + POWER_LED_BLUE + "'");
        }
    }

    // ArkOS4Clone dual-color LED
    if (hasArkOS4CloneLed()) {
        std::string thresholdPath = ARKOS4CLONE_LED.substr(0, ARKOS4CLONE_LED.rfind('/')) + "/battery_threshold";
        if (Utils::FileSystem::exists(thresholdPath)) {
            ArkOSUtil::executeCommand("sudo sh -c 'echo " + std::to_string(arkosThreshold) + " > " + thresholdPath + "'");
        }
        // 阈值=0时，用户控制 brightness
        if (arkosThreshold == 0) {
            int brightness = (arkosMode == 1) ? 1 : 0; // RED=1, BLUE=0
            ArkOSUtil::executeCommand("sudo sh -c 'echo " + std::to_string(brightness) + " > " + ARKOS4CLONE_LED + "'");
        }
    }
}

void applyPowerLedOnStartup()
{
    if (hasPowerLed()) {
        applyPowerLed();
    }
}

}
