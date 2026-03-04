#include "guis/GuiArkOS4CloneSettings.h"
#include "guis/GuiMsgBox.h"
#include "guis/GuiTextEditPopupKeyboard.h"
#include "guis/GuiSettings.h"
#include "components/OptionListComponent.h"
#include "components/SwitchComponent.h"
#include "components/BusyComponent.h"
#include "components/TextComponent.h"
#include "Window.h"
#include "ApiSystem.h"
#include "SystemConf.h"
#include "Log.h"
#include "utils/StringUtil.h"

#include <fstream>
#include <thread>
#include <regex>
#include <chrono>
#include <algorithm>

// ============================================================================
// Static Constants
// ============================================================================

// LED Configuration file path
static const std::string LED_CONFIG_FILE = "/home/ark/.es_joyled";

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
static const std::vector<std::pair<std::string, std::string>> WS2812_BRIGHTNESS = {
    {"HIGH", "HIGH"}, {"MEDIUM", "MEDIUM"}, {"LOW", "LOW"}
};

static std::string getSavedWs2812Brightness()
{
    std::ifstream file(LED_CONFIG_FILE);
    if (file.is_open()) {
        std::string line;
        while (std::getline(file, line)) {
            if (line.find("BRIGHTNESS=") == 0) {
                std::string brightness = line.substr(11);
                // Validate brightness value
                for (const auto& b : WS2812_BRIGHTNESS) {
                    if (b.first == brightness) {
                        return brightness;
                    }
                }
            }
        }
    }
    return "HIGH"; // Default brightness
}

// ============================================================================
// Helper Functions
// ============================================================================

static std::string executeCommand(const std::string& cmd)
{
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";
    
    char buffer[256];
    std::string result;
    while (fgets(buffer, sizeof(buffer), pipe)) {
        result += buffer;
    }
    pclose(pipe);
    return Utils::String::trim(result);
}

static std::string getCurrentWifiSSID()
{
    // Method 1: nmcli active connection
    std::string result = executeCommand("nmcli -t -f NAME,DEVICE connection show --active 2>/dev/null");
    if (!result.empty()) {
        std::istringstream stream(result);
        std::string line;
        while (std::getline(stream, line)) {
            if (line.find(":wlan") != std::string::npos || line.find("wlan") != std::string::npos) {
                size_t colonPos = line.find(':');
                if (colonPos != std::string::npos) {
                    std::string ssid = line.substr(0, colonPos);
                    if (!ssid.empty()) return ssid;
                }
            }
        }
    }
    
    // Method 2: iw dev
    std::string ssid = executeCommand("iw dev wlan0 info 2>/dev/null | grep ssid");
    if (!ssid.empty()) {
        size_t pos = ssid.find("ssid ");
        if (pos != std::string::npos) {
            ssid = ssid.substr(pos + 5);
            if (!ssid.empty() && ssid != "off/any") return ssid;
        }
    }
    
    return "";
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

GuiArkOS4CloneSettings::GuiArkOS4CloneSettings(Window* window)
    : GuiComponent(window), mMenu(window, _("ARKOS4CLONE SETTINGS"))
{
    addChild(&mMenu);

    // Wi-Fi Settings submenu
    mMenu.addEntry(_("WIFI SETTINGS"), true, [this] {
        openWifiSettings();
    }, "iconWifi");

    // Joystick LED Settings submenu
    mMenu.addEntry(_("JOYSTICK LED"), true, [this] {
        openJoystickLedSettings();
    }, "");

    // USB Switch (R36Max2 only)
    if (isR36Max2()) {
        mMenu.addEntry(_("USB SWITCH"), true, [this] {
            openUsbSwitchSettings();
        }, "");
    }

    mMenu.addButton(_("BACK"), "back", [this] {
        delete this;
    });

    setSize((float)Renderer::getScreenWidth(), (float)Renderer::getScreenHeight());
    
    // Position menu like other settings menus
    if (Renderer::isSmallScreen())
        mMenu.setPosition((Renderer::getScreenWidth() - mMenu.getSize().x()) / 2, (Renderer::getScreenHeight() - mMenu.getSize().y()) / 2);
    else
        mMenu.setPosition((mSize.x() - mMenu.getSize().x()) / 2, Renderer::getScreenHeight() * 0.15f);
}

GuiArkOS4CloneSettings::~GuiArkOS4CloneSettings()
{
}

// ============================================================================
// WiFi Functions
// ============================================================================

void GuiArkOS4CloneSettings::openWifiSettings()
{
    createWifiSettingsMenu();
}

void GuiArkOS4CloneSettings::createWifiSettingsMenu()
{
    auto s = new GuiSettings(mWindow, _("WIFI SETTINGS"));

    std::string wifiStatus = getCurrentWifiSSID();
    if (wifiStatus.empty()) {
        wifiStatus = _("NOT CONNECTED");
    }
    mWifiStatusText = std::make_shared<TextComponent>(mWindow, wifiStatus, ThemeData::getMenuTheme()->TextSmall.font, ThemeData::getMenuTheme()->TextSmall.color);
    mWifiStatusText->setLineSpacing(1.0f);
    s->addWithLabel(_("CURRENT NETWORK"), mWifiStatusText);

    // Remote Services toggle (SSH, Samba, FileBrowser, NTP)
    bool remoteEnabled = isRemoteServicesEnabled();
    auto remoteSwitch = std::make_shared<SwitchComponent>(mWindow);
    remoteSwitch->setState(remoteEnabled);
    remoteSwitch->setOnChangedCallback([this, remoteSwitch] {
        toggleRemoteServices(remoteSwitch->getState());
    });
    s->addWithLabel(_("REMOTE SERVICES"), remoteSwitch);

    // IP Address display
    std::string ipAddress = getIpAddress();
    if (ipAddress.empty()) {
        ipAddress = _("NOT CONNECTED");
    }
    // Set height > fontHeight to avoid truncation, but use lineSpacing 1.0f for correct rendering
    float ipHeight = ThemeData::getMenuTheme()->TextSmall.font->getHeight(1.0f) * 1.5f;
    mIpAddressText = std::make_shared<TextComponent>(mWindow, ipAddress, ThemeData::getMenuTheme()->TextSmall.font, ThemeData::getMenuTheme()->TextSmall.color, ALIGN_RIGHT);
    mIpAddressText->setLineSpacing(1.0f);
    mIpAddressText->setSize(Renderer::getScreenWidth() * 0.4f, ipHeight);
    s->addWithLabel(_("IP ADDRESS"), mIpAddressText);

    s->addEntry(_("SCAN WIFI NETWORKS"), true, [this] {
        scanWifi();
    }, "");

    s->addEntry(_("ACTIVATE EXISTING CONNECTION"), true, [this] {
        activateExistingConnection();
    }, "");

    s->addEntry(_("DELETE EXISTING CONNECTIONS"), true, [this] {
        deleteConnections();
    }, "");

    s->addEntry(_("NETWORK INFO"), true, [this] {
        showNetworkInfo();
    }, "");

    mWindow->pushGui(s);
}

void GuiArkOS4CloneSettings::updateWifiStatusText()
{
    if (mWifiStatusText) {
        std::string wifiStatus = getCurrentWifiSSID();
        if (wifiStatus.empty()) {
            wifiStatus = _("NOT CONNECTED");
        }
        mWifiStatusText->setText(wifiStatus);
    }
}

// ============================================================================
// Remote Services Functions
// ============================================================================

bool GuiArkOS4CloneSettings::isRemoteServicesEnabled()
{
    // Check if sshd process is running as indicator
    std::string result = executeCommand("pgrep -x sshd 2>/dev/null");
    return !result.empty();
}

std::string GuiArkOS4CloneSettings::getIpAddress()
{
    std::string ip = executeCommand("ip route | awk '/src/ { print $9; exit }' 2>/dev/null");
    ip = Utils::String::trim(ip);
    
    // Extract only IP address pattern (xxx.xxx.xxx.xxx) using regex
    std::regex ipPattern("(\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3})");
    std::smatch match;
    if (std::regex_search(ip, match, ipPattern)) {
        return match[1].str();
    }
    return ip;
}

void GuiArkOS4CloneSettings::toggleRemoteServices(bool enable)
{
    if (enable) {
        // Check if network is available
        std::string gateway = executeCommand("ip route | awk '/default/ { print $3; exit }' 2>/dev/null");
        if (Utils::String::trim(gateway).empty()) {
            return; // No network connection
        }
        
        // Enable NetworkManager-wait-online
        executeCommand("sudo systemctl enable NetworkManager-wait-online 2>/dev/null");
        executeCommand("sudo systemctl start NetworkManager-wait-online 2>/dev/null");
        
        // Enable NTP time sync
        executeCommand("sudo timedatectl set-ntp 1 2>/dev/null");
        
        // Start Samba services
        executeCommand("sudo systemctl start smbd 2>/dev/null");
        executeCommand("sudo systemctl start nmbd 2>/dev/null");
        
        // Start SSH service
        executeCommand("sudo systemctl start ssh.service 2>/dev/null");
        
        // Start FileBrowser
        executeCommand("sudo pkill -f filebrowser 2>/dev/null || true");
        executeCommand("sudo filebrowser -a 0.0.0.0 -p 80 -d /home/ark/.config/filebrowser.db -r / >/dev/null 2>&1 &");
    } else {
        // Disable NetworkManager-wait-online
        executeCommand("sudo systemctl disable NetworkManager-wait-online 2>/dev/null");
        executeCommand("sudo systemctl stop NetworkManager-wait-online 2>/dev/null");
        
        // Disable NTP time sync
        executeCommand("sudo timedatectl set-ntp 0 2>/dev/null");
        
        // Stop Samba services
        executeCommand("sudo systemctl stop smbd 2>/dev/null");
        executeCommand("sudo systemctl stop nmbd 2>/dev/null");
        
        // Stop SSH service
        executeCommand("sudo systemctl stop ssh.service 2>/dev/null");
        
        // Stop FileBrowser
        executeCommand("sudo pkill -f filebrowser 2>/dev/null || true");
    }
}

void GuiArkOS4CloneSettings::scanWifi()
{
    // Show busy dialog
    auto busy = new GuiComponent(mWindow);
    auto busyComp = new BusyComponent(mWindow);
    busy->addChild(busyComp);
    busyComp->setText(_("SCANNING WIFI NETWORKS"));
    busy->setSize((float)Renderer::getScreenWidth(), (float)Renderer::getScreenHeight());
    mWindow->pushGui(busy);

    mWifiNetworks.clear();
    
    system("sudo nmcli device wifi rescan 2>/dev/null");
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    std::string clist = executeCommand("sudo nmcli -t -f IN-USE,SSID,SIGNAL dev wifi 2>/dev/null");
    
    std::istringstream stream(clist);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        
        size_t pos1 = line.find(':');
        if (pos1 == std::string::npos) continue;
        
        size_t pos2 = line.find(':', pos1 + 1);
        
        std::string inUse = line.substr(0, pos1);
        std::string ssid;
        int signal = 0;
        
        if (pos2 != std::string::npos) {
            ssid = line.substr(pos1 + 1, pos2 - pos1 - 1);
            signal = atoi(line.substr(pos2 + 1).c_str());
        } else {
            ssid = line.substr(pos1 + 1);
        }
        
        if (ssid.empty() || ssid == "--" || ssid == "\\x00") continue;
        
        mWifiNetworks.push_back(std::make_pair(ssid, signal));
    }

    mWindow->removeGui(busy);
    delete busy;

    if (mWifiNetworks.empty()) {
        mWindow->pushGui(new GuiMsgBox(mWindow, _("NO WIFI NETWORKS FOUND"), _("OK")));
        return;
    }

    auto s = new GuiSettings(mWindow, _("SELECT WIFI NETWORK"));

    std::sort(mWifiNetworks.begin(), mWifiNetworks.end(),
        [](const std::pair<std::string, int>& a, const std::pair<std::string, int>& b) {
            return a.second > b.second;
        });

    std::map<std::string, int> uniqueNetworks;
    for (auto& net : mWifiNetworks) {
        if (uniqueNetworks.find(net.first) == uniqueNetworks.end() || uniqueNetworks[net.first] < net.second) {
            uniqueNetworks[net.first] = net.second;
        }
    }

    for (auto& net : uniqueNetworks) {
        if (net.first.empty()) continue;
        
        std::string signalStr = std::to_string(net.second) + "%";
        std::string entryName = net.first + " (" + signalStr + ")";
        
        std::string ssid = net.first;
        s->addEntry(entryName, true, [this, ssid] {
            showWifiPasswordInput(ssid);
        }, "");
    }

    mWindow->pushGui(s);
}

void GuiArkOS4CloneSettings::activateExistingConnection()
{
    std::string conns = executeCommand("ls -1 /etc/NetworkManager/system-connections/ 2>/dev/null | sed 's/\\.nmconnection$//'");
    
    if (conns.empty()) {
        mWindow->pushGui(new GuiMsgBox(mWindow, _("NO SAVED CONNECTIONS"), _("OK")));
        return;
    }

    std::string curSsid = getCurrentWifiSSID();

    auto s = new GuiSettings(mWindow, _("SELECT CONNECTION"));

    std::istringstream stream(conns);
    std::string conn;
    while (std::getline(stream, conn)) {
        if (conn.empty()) continue;
        
        std::string connName = conn;
        std::string displayName = connName;
        
        if (connName == curSsid) {
            displayName = connName + " [" + _("CONNECTED") + "]";
        }
        
        s->addEntry(displayName, true, [this, connName] {
            activateConnection(connName);
        }, "");
    }

    mWindow->pushGui(s);
}

void GuiArkOS4CloneSettings::activateConnection(const std::string& connName)
{
    auto busy = new GuiComponent(mWindow);
    auto busyComp = new BusyComponent(mWindow);
    busy->addChild(busyComp);
    busyComp->setText(_("CONNECTING..."));
    busy->setSize((float)Renderer::getScreenWidth(), (float)Renderer::getScreenHeight());
    mWindow->pushGui(busy);

    std::string curSsid = getCurrentWifiSSID();
    if (!curSsid.empty() && curSsid != connName) {
        executeCommand("nmcli con down \"" + curSsid + "\" 2>/dev/null");
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    
    std::string result = executeCommand("nmcli con up \"" + connName + "\" 2>&1");
    
    std::this_thread::sleep_for(std::chrono::seconds(2));

    mWindow->removeGui(busy);
    delete busy;

    std::string newSsid = getCurrentWifiSSID();
    if (newSsid == connName) {
        updateWifiStatusText();
        mWindow->pushGui(new GuiMsgBox(mWindow, 
            _("CONNECTED TO") + "\n" + connName, 
            _("OK")));
    } else {
        updateWifiStatusText();
        mWindow->pushGui(new GuiMsgBox(mWindow, 
            _("CONNECTION FAILED") + "\n" + result, 
            _("OK")));
    }
}

void GuiArkOS4CloneSettings::deleteConnections()
{
    std::string conns = executeCommand("ls -1 /etc/NetworkManager/system-connections/ 2>/dev/null | sed 's/\\.nmconnection$//'");
    
    if (conns.empty()) {
        mWindow->pushGui(new GuiMsgBox(mWindow, _("NO SAVED CONNECTIONS"), _("OK")));
        return;
    }

    std::string curSsid = getCurrentWifiSSID();

    auto s = new GuiSettings(mWindow, _("DELETE CONNECTION"));

    std::istringstream stream(conns);
    std::string conn;
    while (std::getline(stream, conn)) {
        if (conn.empty()) continue;
        
        std::string connName = conn;
        std::string displayName = connName;
        
        if (connName == curSsid) {
            displayName = connName + " [" + _("CONNECTED") + "]";
        }
        
        s->addEntry(displayName, true, [this, connName] {
            mWindow->pushGui(new GuiMsgBox(mWindow,
                _("DELETE CONNECTION") + "?\n" + connName,
                _("YES"), [this, connName] {
                    executeCommand("sudo rm -f \"/etc/NetworkManager/system-connections/" + connName + ".nmconnection\"");
                    mWindow->pushGui(new GuiMsgBox(mWindow, _("DELETED"), _("OK")));
                },
                _("NO"), nullptr));
        }, "");
    }

    mWindow->pushGui(s);
}

void GuiArkOS4CloneSettings::showNetworkInfo()
{
    std::string ssid = getCurrentWifiSSID();
    std::string ip = executeCommand("ip -f inet addr show wlan0 2>/dev/null | sed -En 's/.*inet ([0-9.]+).*/\\1/p'");
    std::string gateway = executeCommand("ip r 2>/dev/null | grep default | awk '{print $3}'");
    std::string dns = executeCommand("nmcli dev show wlan0 2>/dev/null | grep DNS | awk '{print $2}' | head -1");

    std::string info;
    info += _("SSID") + ": " + (ssid.empty() ? _("NOT CONNECTED") : ssid) + "\n";
    info += _("IP") + ": " + (ip.empty() ? "-" : ip) + "\n";
    info += _("GATEWAY") + ": " + (gateway.empty() ? "-" : gateway) + "\n";
    info += _("DNS") + ": " + (dns.empty() ? "-" : dns);

    mWindow->pushGui(new GuiMsgBox(mWindow, info, _("OK")));
}

void GuiArkOS4CloneSettings::showWifiPasswordInput(const std::string& ssid)
{
    mWindow->pushGui(new GuiTextEditPopupKeyboard(mWindow, 
        _("PASSWORD FOR") + " " + ssid, 
        "",
        [this, ssid](const std::string& password) {
            connectWifi(ssid, password);
        }, 
        false, _("CONNECT")));
}

void GuiArkOS4CloneSettings::connectWifi(const std::string& ssid, const std::string& password)
{
    auto busy = new GuiComponent(mWindow);
    auto busyComp = new BusyComponent(mWindow);
    busy->addChild(busyComp);
    busyComp->setText(_("CONNECTING TO") + " " + ssid + "...");
    busy->setSize((float)Renderer::getScreenWidth(), (float)Renderer::getScreenHeight());
    mWindow->pushGui(busy);

    executeCommand("nmcli con delete \"" + ssid + "\" 2>/dev/null");
    
    std::string result;
    if (password.empty()) {
        result = executeCommand("nmcli device wifi connect \"" + ssid + "\" 2>&1");
    } else {
        result = executeCommand("nmcli device wifi connect \"" + ssid + "\" password \"" + password + "\" 2>&1");
    }

    std::this_thread::sleep_for(std::chrono::seconds(3));

    mWindow->removeGui(busy);
    delete busy;

    std::string status = executeCommand("nmcli -t -f DEVICE,STATE dev 2>/dev/null | grep wlan");
    bool connected = (status.find(":connected") != std::string::npos);
    
    std::string connectedSSID = getCurrentWifiSSID();
    if (connectedSSID.empty()) {
        connected = (result.find("successfully activated") != std::string::npos ||
                     result.find("successfully") != std::string::npos);
    } else {
        connected = (connectedSSID == ssid);
    }

    if (connected) {
        updateWifiStatusText();
        mWindow->pushGui(new GuiMsgBox(mWindow, 
            _("CONNECTED TO") + "\n" + ssid, 
            _("OK")));
    } else {
        executeCommand("sudo rm -f \"/etc/NetworkManager/system-connections/" + ssid + ".nmconnection\" 2>/dev/null");
        updateWifiStatusText();
        
        std::string errorMsg = _("CONNECTION FAILED");
        if (result.find("Secrets were required") != std::string::npos) {
            errorMsg += "\n" + _("INVALID PASSWORD");
        } else if (result.find("not found") != std::string::npos || result.find("No network") != std::string::npos) {
            errorMsg += "\n" + _("NETWORK NOT FOUND");
        } else if (!result.empty()) {
            errorMsg += "\n" + result;
        }
        mWindow->pushGui(new GuiMsgBox(mWindow, errorMsg, _("OK")));
    }
}

// ============================================================================
// USB Switch Functions (R36Max2 only)
// ============================================================================

// USB switch sysfs path for R36Max2
static const std::string USB_SWITCH_PATH = "/sys/devices/platform/ff2c0000.syscon/ff2c0000.syscon:usb2-phy@100";

bool GuiArkOS4CloneSettings::isR36Max2()
{
    std::string device = executeCommand("/usr/local/bin/console_detect -n 2>/dev/null");
    
    // Remove all non-alphanumeric characters (handles \r, \n, spaces, etc.)
    std::string clean;
    for (char c : device) {
        if (std::isalnum(c)) {
            clean += c;
        }
    }
    
    // Log for debugging (Info level for visibility)
    LOG(LogInfo) << "USB Switch: cleaned device name: [" << clean << "]";
    
    // Case-insensitive comparison
    std::transform(clean.begin(), clean.end(), clean.begin(), ::tolower);
    
    bool result = (clean == "r36max2");
    LOG(LogInfo) << "USB Switch: isR36Max2 = " << (result ? "true" : "false");
    
    return result;
}

bool GuiArkOS4CloneSettings::isUsbInternal()
{
    // Read current USB switch status
    // Internal USB: usb_switch_gpio=0, usb_switch_ext=1
    // External USB: usb_switch_gpio=1, usb_switch_ext=0
    
    std::string gpioPath = USB_SWITCH_PATH + "/usb_switch_gpio";
    std::string extPath = USB_SWITCH_PATH + "/usb_switch_ext";
    
    if (!Utils::FileSystem::exists(gpioPath) || !Utils::FileSystem::exists(extPath)) {
        return false; // Default to internal if files don't exist
    }
    
    std::string gpioValue = executeCommand("cat " + gpioPath + " 2>/dev/null");
    std::string extValue = executeCommand("cat " + extPath + " 2>/dev/null");
    
    int gpio = atoi(Utils::String::trim(gpioValue).c_str());
    int ext = atoi(Utils::String::trim(extValue).c_str());
    
    // Internal USB: gpio=0, ext=1
    return (gpio == 0 && ext == 1);
}

void GuiArkOS4CloneSettings::setUsbInternal(bool internal)
{
    std::string gpioPath = USB_SWITCH_PATH + "/usb_switch_gpio";
    std::string extPath = USB_SWITCH_PATH + "/usb_switch_ext";
    
    if (!Utils::FileSystem::exists(gpioPath) || !Utils::FileSystem::exists(extPath)) {
        return;
    }
    
    if (internal) {
        // Switch to internal USB: first enable ext, then disable gpio
        executeCommand("sudo sh -c 'echo 1 > " + extPath + "'");
        executeCommand("sudo sh -c 'echo 0 > " + gpioPath + "'");
    } else {
        // Switch to external USB: first enable gpio, then disable ext
        executeCommand("sudo sh -c 'echo 1 > " + gpioPath + "'");
        executeCommand("sudo sh -c 'echo 0 > " + extPath + "'");
    }
}

void GuiArkOS4CloneSettings::openUsbSwitchSettings()
{
    auto s = new GuiSettings(mWindow, _("USB SWITCH"));
    
    bool isInternal = isUsbInternal();
    
    auto usbOptions = std::make_shared<OptionListComponent<std::string>>(mWindow, _("USB MODE"), false);
    
    usbOptions->add(_("INTERNAL USB (BUILT-IN STORAGE)"), "internal", isInternal);
    usbOptions->add(_("EXTERNAL USB (OTG DEVICE)"), "external", !isInternal);
    
    usbOptions->setSelectedChangedCallback([this](const std::string& selected) {
        if (selected == "internal") {
            setUsbInternal(true);
        } else {
            setUsbInternal(false);
        }
    });
    
    s->addWithLabel(_("USB MODE"), usbOptions);
    
    mWindow->pushGui(s);
}

// ============================================================================
// LED Functions - Detection & Configuration
// ============================================================================

std::string GuiArkOS4CloneSettings::detectLedType()
{
    // Cache result to avoid repeated console_detect calls
    static std::string cachedLedType;
    static bool cached = false;
    
    if (cached) {
        return cachedLedType;
    }
    
    // Try console_detect first
    std::string output = executeCommand("/usr/local/bin/console_detect -s 2>/dev/null");
    if (!output.empty()) {
        std::istringstream stream(output);
        std::string line;
        while (std::getline(stream, line)) {
            if (line.find("LED_TYPE=") == 0) {
                std::string ledType = line.substr(9);
                if (!ledType.empty() && ledType != "unsupported") {
                    cachedLedType = ledType;
                    cached = true;
                    return cachedLedType;
                }
            }
        }
    }
    
    // Fallback: read from /boot/.console
    std::string deviceName = executeCommand("cat /boot/.console 2>/dev/null");
    deviceName = Utils::String::trim(deviceName);
    
    if (deviceName == "xf35h" || deviceName == "xf40h" || deviceName == "k36s" || deviceName == "r36tmax") {
        cachedLedType = "mcu_led";
    } else if (deviceName == "mymini" || deviceName == "r36ultra" || deviceName == "xgb36" || deviceName == "mini40") {
        cachedLedType = "gpio";
    } else if (deviceName == "dc40v" || deviceName == "dc35v" || deviceName == "xf28" || deviceName == "r36max2") {
        cachedLedType = "ws2812";
    }
    
    cached = true;
    return cachedLedType;
}

std::string GuiArkOS4CloneSettings::getDeviceName()
{
    std::string output = executeCommand("/usr/local/bin/console_detect -s 2>/dev/null");
    if (!output.empty()) {
        std::istringstream stream(output);
        std::string line;
        while (std::getline(stream, line)) {
            if (line.find("DEVICE_NAME=") == 0) {
                return line.substr(12);
            }
        }
    }
    return executeCommand("cat /boot/.console 2>/dev/null");
}

std::string GuiArkOS4CloneSettings::getCurrentLedColor()
{
    std::ifstream file(LED_CONFIG_FILE);
    if (file.is_open()) {
        std::string line;
        while (std::getline(file, line)) {
            if (line.find("COLOR=") == 0) {
                return line.substr(6);
            }
        }
    }
    return "off";
}

std::vector<std::pair<std::string, std::string>> GuiArkOS4CloneSettings::getLedMenuItems(const std::string& ledType)
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
    }
    
    return items;
}

// ============================================================================
// LED Functions - Apply
// ============================================================================

void GuiArkOS4CloneSettings::applyLedColor(const std::string& color, const std::string& brightness)
{
    std::string ledType = detectLedType();
    
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
    }
}

void GuiArkOS4CloneSettings::applyMcuLed(const std::string& color)
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
        executeCommand("sudo sh -c 'echo " + std::to_string(GPIO_NUM) + " > " + gpioExport + "'");
    }
    // Always ensure direction is out
    executeCommand("sudo sh -c 'echo out > " + gpioDir + "/direction'");
    
    if (color == "off") {
        executeCommand("sudo sh -c 'echo 0 > " + gpioDir + "/value'");
        return;
    }
    
    executeCommand("sudo sh -c 'echo 1 > " + gpioDir + "/value'");
    
    auto it = MCU_MODES.find(color);
    if (it != MCU_MODES.end() && Utils::FileSystem::exists(mcuLedBin)) {
        executeCommand("sudo " + mcuLedBin + " " + it->second);
    }
}

void GuiArkOS4CloneSettings::applyGpioLed(const std::string& color)
{
    // Security: validate color string
    if (!color.empty() && color.find_first_not_of("abcdefghijklmnopqrstuvwxyz_") != std::string::npos) {
        return;
    }
    
    std::string ledBlue = "/sys/class/leds/blue:joy/brightness";
    std::string ledGreen = "/sys/class/leds/green:joy/brightness";
    std::string ledRed = "/sys/class/leds/red:joy/brightness";
    
    // Disable triggers only for joystick LEDs
    executeCommand("sudo sh -c 'echo none > /sys/class/leds/blue:joy/trigger 2>/dev/null; echo none > /sys/class/leds/green:joy/trigger 2>/dev/null; echo none > /sys/class/leds/red:joy/trigger 2>/dev/null'");
    
    // Get max brightness
    int maxB = 1, maxG = 1, maxR = 1;
    std::string maxBPath = "/sys/class/leds/blue:joy/max_brightness";
    std::string maxGPath = "/sys/class/leds/green:joy/max_brightness";
    std::string maxRPath = "/sys/class/leds/red:joy/max_brightness";
    
    if (Utils::FileSystem::exists(maxBPath)) {
        maxB = atoi(executeCommand("cat " + maxBPath).c_str());
    }
    if (Utils::FileSystem::exists(maxGPath)) {
        maxG = atoi(executeCommand("cat " + maxGPath).c_str());
    }
    if (Utils::FileSystem::exists(maxRPath)) {
        maxR = atoi(executeCommand("cat " + maxRPath).c_str());
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
        executeCommand("sudo sh -c 'echo " + std::to_string(b) + " > " + ledBlue + "'");
    }
    if (Utils::FileSystem::exists(ledGreen)) {
        executeCommand("sudo sh -c 'echo " + std::to_string(g) + " > " + ledGreen + "'");
    }
    if (Utils::FileSystem::exists(ledRed)) {
        executeCommand("sudo sh -c 'echo " + std::to_string(r) + " > " + ledRed + "'");
    }
}

void GuiArkOS4CloneSettings::applyWs2812Led(const std::string& color, const std::string& brightness)
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
    executeCommand("sudo pkill -f '^" + ws2812Bin + "' 2>/dev/null || true");
    
    if (color == "off") {
        // Run OFF command to actually turn off the LEDs
        executeCommand("sudo " + ws2812Bin + " OFF 2>/dev/null || true");
        return;
    }
    
    auto it = WS2812_MODES.find(color);
    if (it != WS2812_MODES.end()) {
        executeCommand("sudo nohup " + ws2812Bin + " " + it->second + " " + validBrightness + " >/dev/null 2>&1 </dev/null &");
    }
}

// ============================================================================
// LED Functions - Config & Startup
// ============================================================================

void GuiArkOS4CloneSettings::saveLedConfig(const std::string& color, const std::string& brightness)
{
    std::string deviceName = getDeviceName();
    
    std::ofstream file(LED_CONFIG_FILE);
    if (file.is_open()) {
        file << "DEVICE=" << deviceName << "\n";
        file << "COLOR=" << color << "\n";
        if (!brightness.empty()) {
            file << "BRIGHTNESS=" << brightness << "\n";
        }
        file.close();
    }
}

bool GuiArkOS4CloneSettings::checkAndApplyLedOnStartup()
{
    // Early exit if no config file
    if (!Utils::FileSystem::exists(LED_CONFIG_FILE)) {
        return false;
    }
    
    // Read saved device, color and brightness
    std::string savedDevice, savedColor, savedBrightness;
    std::ifstream file(LED_CONFIG_FILE);
    if (file.is_open()) {
        std::string line;
        while (std::getline(file, line)) {
            if (line.find("DEVICE=") == 0) {
                savedDevice = line.substr(7);
            } else if (line.find("COLOR=") == 0) {
                savedColor = line.substr(6);
            } else if (line.find("BRIGHTNESS=") == 0) {
                savedBrightness = line.substr(11);
            }
        }
        file.close();
    }
    
    // Early exit if no color or off
    if (savedColor.empty() || savedColor == "off") {
        return false;
    }
    
    // Check if device matches
    std::string currentDevice = getDeviceName();
    if (savedDevice != currentDevice) {
        Utils::FileSystem::removeFile(LED_CONFIG_FILE);
        return false;
    }
    
    // Apply the saved color
    std::string ledType = detectLedType();
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

void GuiArkOS4CloneSettings::openJoystickLedSettings()
{
    std::string ledType = detectLedType();
    
    if (ledType.empty()) {
        mWindow->pushGui(new GuiMsgBox(mWindow, 
            _("UNSUPPORTED DEVICE") + "\n" + _("Joystick LED is not supported on this device."), 
            _("OK")));
        return;
    }
    
    auto items = getLedMenuItems(ledType);
    
    if (items.empty()) {
        mWindow->pushGui(new GuiMsgBox(mWindow, 
            _("UNSUPPORTED DEVICE") + "\n" + _("Joystick LED is not supported on this device."), 
            _("OK")));
        return;
    }
    
    auto s = new GuiSettings(mWindow, _("JOYSTICK LED"));
    
    std::string currentColor = getCurrentLedColor();
    
    // Check if currentColor is valid (UI dirty data tolerance)
    bool colorFound = false;
    for (auto& item : items) {
        if (item.first == currentColor) {
            colorFound = true;
            break;
        }
    }
    if (!colorFound) {
        currentColor = "off";
    }
    
    auto ledOptions = std::make_shared<OptionListComponent<std::string>>(mWindow, _("LED MODE"), false);
    
    for (auto& item : items) {
        ledOptions->add(item.second, item.first, item.first == currentColor);
    }
    
    s->addWithLabel(_("LED MODE"), ledOptions);
    
    // Add brightness option for WS2812 only
    std::shared_ptr<OptionListComponent<std::string>> brightnessOptions;
    if (ledType == "ws2812") {
        brightnessOptions = std::make_shared<OptionListComponent<std::string>>(mWindow, _("BRIGHTNESS"), false);
        std::string currentBrightness = getSavedWs2812Brightness();
        
        for (const auto& b : WS2812_BRIGHTNESS) {
            brightnessOptions->add(b.second, b.first, b.first == currentBrightness);
        }
        
        s->addWithLabel(_("BRIGHTNESS"), brightnessOptions);
    }
    
    // Apply LED color immediately when selection changes
    ledOptions->setSelectedChangedCallback([this, brightnessOptions, ledType](const std::string& selectedColor) {
        std::string selectedBrightness;
        if (ledType == "ws2812" && brightnessOptions) {
            selectedBrightness = brightnessOptions->getSelected();
        }
        applyLedColor(selectedColor, selectedBrightness);
    });
    
    // Apply brightness immediately when selection changes (WS2812 only)
    if (ledType == "ws2812" && brightnessOptions) {
        brightnessOptions->setSelectedChangedCallback([this, ledOptions](const std::string& selectedBrightness) {
            std::string selectedColor = ledOptions->getSelected();
            applyLedColor(selectedColor, selectedBrightness);
        });
    }
    
    mWindow->pushGui(s);
}

// ============================================================================
// GuiComponent Interface
// ============================================================================

bool GuiArkOS4CloneSettings::input(InputConfig* config, Input input)
{
    if (input.value != 0 && config->isMappedTo(BUTTON_BACK, input)) {
        delete this;
        return true;
    }
    return GuiComponent::input(config, input);
}

void GuiArkOS4CloneSettings::render(const Transform4x4f& parentTrans)
{
    GuiComponent::render(parentTrans);
}

std::vector<HelpPrompt> GuiArkOS4CloneSettings::getHelpPrompts()
{
    std::vector<HelpPrompt> prompts = mMenu.getHelpPrompts();
    prompts.push_back(HelpPrompt(BUTTON_BACK, _("BACK")));
    return prompts;
}
