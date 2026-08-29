#include "guis/arkos4clone/ScreenControl.h"
#include "guis/arkos4clone/GammaControl.h"
#include "guis/arkos4clone/ArkOSUtil.h"
#include "guis/GuiSettings.h"
#include "guis/GuiMsgBox.h"
#include "guis/GuiTextEditPopupKeyboard.h"
#include "components/SliderComponent.h"
#include "components/SwitchComponent.h"
#include "components/OptionListComponent.h"
#include "components/TextComponent.h"
#include "Window.h"
#include "ApiSystem.h"
#include "SystemConf.h"
#include "Settings.h"
#include "EsLocale.h"
#include "math/Misc.h"
#include "resources/Font.h"
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"

#include <cstdio>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace ScreenControl
{

// ============================================================================
// Refresh Rate Control (MIPI-DSI panel timing switch)
// ============================================================================

// Locate the MIPI-DSI device directory that exposes the timing/timings sysfs
// files. Returns empty string if not found.
static std::string findDsiDevicePath()
{
    static std::string cached;
    if (!cached.empty()) return cached;
    if (Utils::FileSystem::exists("/sys/bus/mipi-dsi/devices")) {
        auto dirs = Utils::FileSystem::getDirContent("/sys/bus/mipi-dsi/devices");
        for (const auto& d : dirs) {
            if (Utils::FileSystem::exists(d + "/timings") &&
                Utils::FileSystem::exists(d + "/timing")) {
                cached = d;
                return cached;
            }
        }
    }
    return "";
}

static bool hasRefreshRateControl()
{
    return !findDsiDevicePath().empty();
}

// Parse the timings sysfs file. Returns a list of (index, label) pairs.
// File format:
//   count: 2
//   0: 640x480@78Hz
//   1: 640x480@60Hz (current)
static std::vector<std::pair<int, std::string>> getRefreshRates()
{
    std::vector<std::pair<int, std::string>> rates;
    std::string path = findDsiDevicePath();
    if (path.empty()) return rates;

    std::string content = ArkOSUtil::executeCommand("cat " + path + "/timings 2>/dev/null");
    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line)) {
        // Expected format: "<index>: <label> [(current)]"
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;

        std::string idxStr = line.substr(0, colon);
        idxStr.erase(std::remove_if(idxStr.begin(), idxStr.end(), ::isspace), idxStr.end());

        // Skip non-numeric index lines (e.g. "count: 2")
        if (idxStr.empty()) continue;
        for (char c : idxStr) {
            if (!isdigit((unsigned char)c)) { idxStr.clear(); break; }
        }
        if (idxStr.empty()) continue;

        std::string label = line.substr(colon + 1);
        // Trim leading whitespace
        size_t start = label.find_first_not_of(" \t");
        if (start != std::string::npos)
            label = label.substr(start);
        // Strip trailing "(current)" marker and whitespace
        size_t curPos = label.find("(current)");
        if (curPos != std::string::npos)
            label = label.substr(0, curPos);
        label = Utils::String::trim(label);

        int idx = atoi(idxStr.c_str());
        if (!label.empty())
            rates.push_back(std::make_pair(idx, label));
    }
    return rates;
}

static int getCurrentRefreshRateIndex()
{
    std::string path = findDsiDevicePath();
    if (path.empty()) return -1;

    std::string content = ArkOSUtil::executeCommand("cat " + path + "/timing 2>/dev/null");
    content = Utils::String::trim(content);
    if (content.empty()) return -1;
    return atoi(content.c_str());
}

static void setRefreshRateIndex(int index)
{
    std::string path = findDsiDevicePath();
    if (path.empty()) return;
    ArkOSUtil::executeCommand("sudo sh -c 'echo " + std::to_string(index) + " > " + path + "/timing'");
}

// Apply saved refresh rate on ES startup.
// Only applies when "RefreshRateAutoApply" is enabled and the saved index is valid.
void applyRefreshRateOnStartup()
{
    if (!Settings::getInstance()->getBool("RefreshRateAutoApply"))
        return;

    int savedIdx = Settings::getInstance()->getInt("RefreshRateIndex");
    if (savedIdx < 0)
        return;

    setRefreshRateIndex(savedIdx);
}

static void openRefreshRateSettings(Window* window)
{
    auto s = new GuiSettings(window, _("REFRESH RATE"));

    auto rates = getRefreshRates();
    int currentIdx = getCurrentRefreshRateIndex();

    if (rates.empty()) {
        window->pushGui(new GuiMsgBox(window,
            _("NO REFRESH RATES FOUND"),
            _("OK")));
        delete s;
        return;
    }

    // Current status display
    std::string currentLabel = _("UNKNOWN");
    for (const auto& r : rates) {
        if (r.first == currentIdx) {
            currentLabel = r.second;
            break;
        }
    }
    auto currentText = std::make_shared<TextComponent>(window, currentLabel,
        Font::get(FONT_SIZE_SMALL), 0x777777FF);
    s->addWithLabel(_("CURRENT"), currentText);

    // Refresh rate option list
    auto rateList = std::make_shared<OptionListComponent<std::string>>(window, _("REFRESH RATE"), false);
    bool found = false;
    for (const auto& r : rates) {
        bool selected = (r.first == currentIdx);
        if (selected) found = true;
        rateList->add(r.second, std::to_string(r.first), selected);
    }
    if (!found) rateList->selectFirstItem();

    s->addWithLabel(_("REFRESH RATE"), rateList);

    rateList->setSelectedChangedCallback([window, currentText, s, rates](const std::string& val) {
        if (val.empty()) return;
        int newIdx = atoi(val.c_str());
        setRefreshRateIndex(newIdx);

        // Save the selected index so it can be re-applied on startup
        Settings::getInstance()->setInt("RefreshRateIndex", newIdx);
        Settings::getInstance()->saveFile();

        // Look up the human-readable label for this index
        std::string label = val;
        for (const auto& r : rates) {
            if (r.first == newIdx) {
                label = r.second;
                break;
            }
        }

        // Update current display immediately
        currentText->setText(label);

        s->setVariable("reloadAll", true);

        window->pushGui(new GuiMsgBox(window,
            _("REFRESH RATE CHANGED") + "\n" + label,
            _("OK")));
    });

    // Auto-apply on startup toggle
    bool autoApply = Settings::getInstance()->getBool("RefreshRateAutoApply");
    auto autoSwitch = std::make_shared<SwitchComponent>(window);
    autoSwitch->setState(autoApply);
    s->addWithLabel(_("APPLY ON STARTUP"), autoSwitch);
    s->addSaveFunc([autoSwitch] {
        bool enabled = autoSwitch->getState();
        Settings::getInstance()->setBool("RefreshRateAutoApply", enabled);
        if (!enabled) {
            // Disabled: write -1 so startup won't touch timing
            Settings::getInstance()->setInt("RefreshRateIndex", -1);
        }
        Settings::getInstance()->saveFile();
    });

    window->pushGui(s);
}

// ============================================================================
// Gamma Settings
// ============================================================================

static void openGammaSettings(Window* window)
{
    GuiSettings* s = new GuiSettings(window, _("GAMMA SETTINGS"));

    // Current gamma display
    float curR = GammaControl::getGammaR();
    float curG = GammaControl::getGammaG();
    float curB = GammaControl::getGammaB();

    char infoBuf[128];
    snprintf(infoBuf, sizeof(infoBuf), "R=%.2f  G=%.2f  B=%.2f", curR, curG, curB);
    auto gammaInfoText = std::make_shared<TextComponent>(window, infoBuf, Font::get(FONT_SIZE_SMALL), 0x777777FF);
    s->addWithLabel(_("GAMMA CURRENT"), gammaInfoText);

    // Set all channels (R=G=B)
    s->addEntry(_("GAMMA SET ALL"), true, [window, gammaInfoText] {
        window->pushGui(new GuiTextEditPopupKeyboard(window,
            _("ENTER GAMMA VALUE (0.30-2.00)"),
            "",
            [window, gammaInfoText](const std::string& valueStr) {
                try {
                    float value = std::stof(valueStr);
                    if (value >= 0.3f && value <= 2.0f) {
                        GammaControl::setGamma(value, value, value);
                        char newInfo[128];
                        snprintf(newInfo, sizeof(newInfo), "R=%.2f  G=%.2f  B=%.2f", value, value, value);
                        gammaInfoText->setText(newInfo);
                        window->pushGui(new GuiMsgBox(window,
                            _("GAMMA SET TO") + " " + valueStr,
                            _("OK")));
                    } else {
                        window->pushGui(new GuiMsgBox(window,
                            _("VALUE OUT OF RANGE"),
                            _("OK")));
                    }
                } catch (...) {
                    window->pushGui(new GuiMsgBox(window,
                        _("INVALID VALUE"),
                        _("OK")));
                }
            },
            false));
    }, "");

    // Set Red channel
    s->addEntry(_("GAMMA SET R"), true, [window, gammaInfoText] {
        window->pushGui(new GuiTextEditPopupKeyboard(window,
            _("ENTER RED GAMMA (0.30-2.00)"),
            "",
            [window, gammaInfoText](const std::string& valueStr) {
                try {
                    float value = std::stof(valueStr);
                    if (value >= 0.3f && value <= 2.0f) {
                        float g = GammaControl::getGammaG();
                        float b = GammaControl::getGammaB();
                        GammaControl::setGamma(value, g, b);
                        char newInfo[128];
                        snprintf(newInfo, sizeof(newInfo), "R=%.2f  G=%.2f  B=%.2f", value, g, b);
                        gammaInfoText->setText(newInfo);
                        window->pushGui(new GuiMsgBox(window,
                            _("GAMMA SET TO") + " R=" + valueStr,
                            _("OK")));
                    } else {
                        window->pushGui(new GuiMsgBox(window,
                            _("VALUE OUT OF RANGE"),
                            _("OK")));
                    }
                } catch (...) {
                    window->pushGui(new GuiMsgBox(window,
                        _("INVALID VALUE"),
                        _("OK")));
                }
            },
            false));
    }, "");

    // Set Green channel
    s->addEntry(_("GAMMA SET G"), true, [window, gammaInfoText] {
        window->pushGui(new GuiTextEditPopupKeyboard(window,
            _("ENTER GREEN GAMMA (0.30-2.00)"),
            "",
            [window, gammaInfoText](const std::string& valueStr) {
                try {
                    float value = std::stof(valueStr);
                    if (value >= 0.3f && value <= 2.0f) {
                        float r = GammaControl::getGammaR();
                        float b = GammaControl::getGammaB();
                        GammaControl::setGamma(r, value, b);
                        char newInfo[128];
                        snprintf(newInfo, sizeof(newInfo), "R=%.2f  G=%.2f  B=%.2f", r, value, b);
                        gammaInfoText->setText(newInfo);
                        window->pushGui(new GuiMsgBox(window,
                            _("GAMMA SET TO") + " G=" + valueStr,
                            _("OK")));
                    } else {
                        window->pushGui(new GuiMsgBox(window,
                            _("VALUE OUT OF RANGE"),
                            _("OK")));
                    }
                } catch (...) {
                    window->pushGui(new GuiMsgBox(window,
                        _("INVALID VALUE"),
                        _("OK")));
                }
            },
            false));
    }, "");

    // Set Blue channel
    s->addEntry(_("GAMMA SET B"), true, [window, gammaInfoText] {
        window->pushGui(new GuiTextEditPopupKeyboard(window,
            _("ENTER BLUE GAMMA (0.30-2.00)"),
            "",
            [window, gammaInfoText](const std::string& valueStr) {
                try {
                    float value = std::stof(valueStr);
                    if (value >= 0.3f && value <= 2.0f) {
                        float r = GammaControl::getGammaR();
                        float g = GammaControl::getGammaG();
                        GammaControl::setGamma(r, g, value);
                        char newInfo[128];
                        snprintf(newInfo, sizeof(newInfo), "R=%.2f  G=%.2f  B=%.2f", r, g, value);
                        gammaInfoText->setText(newInfo);
                        window->pushGui(new GuiMsgBox(window,
                            _("GAMMA SET TO") + " B=" + valueStr,
                            _("OK")));
                    } else {
                        window->pushGui(new GuiMsgBox(window,
                            _("VALUE OUT OF RANGE"),
                            _("OK")));
                    }
                } catch (...) {
                    window->pushGui(new GuiMsgBox(window,
                        _("INVALID VALUE"),
                        _("OK")));
                }
            },
            false));
    }, "");

    // Reset button
    s->addEntry(_("GAMMA RESET"), true, [window, gammaInfoText] {
        GammaControl::resetGamma();
        gammaInfoText->setText("R=1.00  G=1.00  B=1.00");
        window->pushGui(new GuiMsgBox(window,
            _("GAMMA RESET TO") + " 1.00",
            _("OK")));
    }, "");

    window->pushGui(s);
}

void openScreenSettings(Window* window)
{
    auto s = new GuiSettings(window, _("SCREEN SETTINGS"));

    // Brightness
    auto brightnessComponent = std::make_shared<SliderComponent>(window, 1.0f, 100.f, 1.0f, "%");
    brightnessComponent->setValue((float) ApiSystem::getInstance()->getBrightnessLevel());
    brightnessComponent->setOnValueChanged([](const float &newVal)
    {
        ApiSystem::getInstance()->setBrighness((int)Math::round(newVal));
    });
    s->addSaveFunc([brightnessComponent] {
        SystemConf::getInstance()->set("brightness.level", std::to_string((int)Math::round(brightnessComponent->getValue())));
    });
    s->addWithLabel(_("BRIGHTNESS"), brightnessComponent);

    // Brightness popup toggle
    auto brightnessPopup = std::make_shared<SwitchComponent>(window);
    brightnessPopup->setState(Settings::getInstance()->getBool("BrightnessPopup"));
    s->addWithLabel(_("SHOW OVERLAY WHEN BRIGHTNESS CHANGES"), brightnessPopup);
    s->addSaveFunc([brightnessPopup]
    {
        bool old_value = Settings::getInstance()->getBool("BrightnessPopup");
        if (old_value != brightnessPopup->getState())
            Settings::getInstance()->setBool("BrightnessPopup", brightnessPopup->getState());
    });

    // Refresh Rate switch (only for supported devices)
    if (hasRefreshRateControl()) {
        s->addEntry(_("REFRESH RATE"), true, [window] {
            openRefreshRateSettings(window);
        }, "");
    }

    // Gamma Settings (only for supported devices)
    if (GammaControl::isAvailable()) {
        s->addEntry(_("GAMMA SETTINGS"), true, [window] {
            openGammaSettings(window);
        }, "");
    }

    window->pushGui(s);
}

}
