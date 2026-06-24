#include "guis/GuiArkOS4CloneSettings.h"
#include "guis/GuiMsgBox.h"
#include "guis/GuiTextEditPopupKeyboard.h"
#include "guis/GuiSettings.h"
#include "guis/GuiDetectDevice.h"
#include "components/SliderComponent.h"
#include "components/OptionListComponent.h"
#include "components/SwitchComponent.h"
#include "components/BusyComponent.h"
#include "components/TextComponent.h"
#include "components/BatteryIndicatorComponent.h"
#include "Window.h"
#include "ApiSystem.h"
#include "SystemConf.h"
#include "Log.h"
#include "AudioManager.h"
#include "VolumeControl.h"
#include "platform.h"
#include "utils/StringUtil.h"

#include <fstream>
#include <thread>
#include <regex>
#include <chrono>
#include <algorithm>
#include <cctype>

// ============================================================================
// Static Constants
// ============================================================================

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
static const std::vector<std::pair<std::string, std::string>> WS2812_BRIGHTNESS = {
    {"HIGH", "HIGH"}, {"MEDIUM", "MEDIUM"}, {"LOW", "LOW"}
};

// Dual GPIO LED paths (left/right joystick LEDs)
static const std::string DUAL_GPIO_LED_LEFT = "/sys/class/leds/joy-left/brightness";
static const std::string DUAL_GPIO_LED_RIGHT = "/sys/class/leds/joy-right/brightness";

static std::string getSavedWs2812Brightness()
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

// Check if WiFi is enabled (not blocked by rfkill)
static bool isWifiRfkillBlocked()
{
    std::string result = executeCommand("rfkill list wifi 2>/dev/null | grep -i 'Soft blocked' | head -1");
    return result.find("yes") != std::string::npos;
}

// Get active WiFi interface (wlan0, p2p0, etc.)
static std::string getActiveWifiInterface()
{
    // Check for connected wifi devices via nmcli
    std::string result = executeCommand("nmcli -t -f DEVICE,TYPE,STATE dev 2>/dev/null");
    if (!result.empty()) {
        std::istringstream stream(result);
        std::string line;
        while (std::getline(stream, line)) {
            // Format: device:type:state
            if (line.find(":wifi:") != std::string::npos && line.find(":connected") != std::string::npos) {
                size_t colonPos = line.find(':');
                if (colonPos != std::string::npos) {
                    std::string iface = line.substr(0, colonPos);
                    if (!iface.empty()) return iface;
                }
            }
        }
    }
    
    // Fallback: check operstate of common wifi interfaces
    std::vector<std::string> wifiInterfaces = {"p2p0", "wlan0", "wlan1"};
    for (const auto& iface : wifiInterfaces) {
        std::string operstate = executeCommand("cat /sys/class/net/" + iface + "/operstate 2>/dev/null");
        if (operstate == "up") return iface;
    }
    
    return "wlan0"; // Default fallback
}



static std::string getCurrentWifiSSID()
{
    // Method 1: nmcli active connection - check all wifi interfaces
    std::string result = executeCommand("nmcli -t -f NAME,DEVICE connection show --active 2>/dev/null");
    if (!result.empty()) {
        std::istringstream stream(result);
        std::string line;
        while (std::getline(stream, line)) {
            // Check for wlan, p2p, or any wifi interface
            if (line.find(":wlan") != std::string::npos || 
                line.find(":p2p") != std::string::npos ||
                line.find("wlan") != std::string::npos ||
                line.find("p2p") != std::string::npos) {
                size_t colonPos = line.find(':');
                if (colonPos != std::string::npos) {
                    std::string connName = line.substr(0, colonPos);
                    if (!connName.empty() && connName != "lo") {
                        return connName;
                    }
                }
            }
        }
    }
    
    // Method 2: iw dev - check active interface
    std::string iface = getActiveWifiInterface();
    std::string ssid = executeCommand("iw dev " + iface + " info 2>/dev/null | grep ssid");
    if (!ssid.empty()) {
        size_t pos = ssid.find("ssid ");
        if (pos != std::string::npos) {
            ssid = ssid.substr(pos + 5);
            ssid.erase(std::remove_if(ssid.begin(), ssid.end(), ::isspace), ssid.end());
            if (!ssid.empty() && ssid != "off/any") {
                return ssid;
            }
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

    // Joystick LED Settings submenu (only for supported devices)
    if (!detectLedType().empty()) {
        mMenu.addEntry(_("JOYSTICK LED"), true, [this] {
            openJoystickLedSettings();
        }, "");
    }

    // ArkOS4Clone Tools submenu (CPU/GPU/DMC/ZRAM settings)
    if (hasGpuFreqControl() || hasDmcFreqControl() || getCpuCoreCount() > 1) {
        mMenu.addEntry(_("ARKOS4CLONE TOOLS"), true, [this] {
            openToolsMenu();
        }, "");
    }

    // Power LED Settings submenu (only for supported devices)
    if (hasPowerLed()) {
        mMenu.addEntry(_("POWER LED"), true, [this] {
            openPowerLedSettings();
        }, "");
    }

    // USB Switch (R36Max2 only)
    if (isR36Max2()) {
        mMenu.addEntry(_("USB SWITCH"), true, [this] {
            openUsbSwitchSettings();
        }, "");
    }

    // Configure Input
    mMenu.addEntry(_("CONFIGURE INPUT"), true, [this] {
        Window* window = mWindow;
        window->pushGui(new GuiMsgBox(window, _("ARE YOU SURE YOU WANT TO CONFIGURE INPUT?"), _("YES"),
            [window] {
                window->pushGui(new GuiDetectDevice(window, false, nullptr));
            }, _("NO"), nullptr));
    }, "iconControllers");

    // Date & Time Settings
    mMenu.addEntry(_("DATE & TIME"), true, [this] {
        openDateTimeSettings();
    }, "");

    // Joypad Test
    mMenu.addEntry(_("JOYPAD TEST"), true, [this] {
        Window* window = mWindow;
        window->pushGui(new GuiMsgBox(window, _("ARE YOU SURE YOU WANT TO TEST JOYPAD?"), _("YES"),
            [window] {
                // Deinit ES resources
                AudioManager::getInstance()->deinit();
                VolumeControl::getInstance()->deinit();
                window->deinit(true);

                // Run sdljoytest on tty1
                system("sudo chmod 666 /dev/tty1");
                system("/usr/local/bin/sdljoytest 2>&1 > /dev/tty1");
                system("setterm -clear all > /dev/tty1");

                // Reinit ES resources
                window->init(true);
                VolumeControl::getInstance()->init();
                AudioManager::getInstance()->init();
            }, _("NO"), nullptr));
    }, "iconControllers");

    // View Info (SD Card Speed and CPU Binning)
    mMenu.addEntry(_("VIEW INFO"), true, [this] {
        openViewInfo();
    }, "");

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

    // WiFi enable/disable toggle
    bool wifiEnabled = !isWifiRfkillBlocked();
    auto wifiSwitch = std::make_shared<SwitchComponent>(mWindow);
    wifiSwitch->setState(wifiEnabled);
    wifiSwitch->setOnChangedCallback([this, wifiSwitch] {
        toggleWifi(wifiSwitch->getState());
        // Update WiFi status text and refresh network icon
        updateWifiStatusText();
    });
    s->addWithLabel(_("WIFI ENABLED"), wifiSwitch);

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

    // Remote Services Auto-Start toggle
    bool autoStartEnabled = isRemoteServicesAutoStart();
    auto autoStartSwitch = std::make_shared<SwitchComponent>(mWindow);
    autoStartSwitch->setState(autoStartEnabled);
    autoStartSwitch->setOnChangedCallback([this, autoStartSwitch] {
        toggleRemoteServicesAutoStart(autoStartSwitch->getState());
    });
    s->addWithLabel(_("REMOTE SERVICES AUTO-START"), autoStartSwitch);

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
        // Remove all whitespace including newlines
        wifiStatus.erase(std::remove_if(wifiStatus.begin(), wifiStatus.end(), ::isspace), wifiStatus.end());
        if (wifiStatus.empty()) {
            wifiStatus = _("NOT CONNECTED");
        }
        mWifiStatusText->setText(wifiStatus);
    }
    // Also refresh network icon in status bar
    if (mWindow->getBatteryIndicator()) {
        mWindow->getBatteryIndicator()->refreshNetworkState();
    }
}

bool GuiArkOS4CloneSettings::isWifiEnabled()
{
    return !isWifiRfkillBlocked();
}

void GuiArkOS4CloneSettings::toggleWifi(bool enable)
{
    if (enable) {
        executeCommand("sudo rfkill unblock wifi 2>/dev/null");
    } else {
        executeCommand("sudo rfkill block wifi 2>/dev/null");
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
    std::string iface = getActiveWifiInterface();
    std::string ssid = getCurrentWifiSSID();
    std::string ip = executeCommand("ip -f inet addr show " + iface + " 2>/dev/null | sed -En 's/.*inet ([0-9.]+).*/\\1/p'");
    std::string gateway = executeCommand("ip r 2>/dev/null | grep default | awk '{print $3}'");
    std::string dns = executeCommand("nmcli dev show " + iface + " 2>/dev/null | grep DNS | awk '{print $2}' | head -1");
    
    // Trim whitespace
    ip.erase(std::remove_if(ip.begin(), ip.end(), ::isspace), ip.end());
    gateway.erase(std::remove_if(gateway.begin(), gateway.end(), ::isspace), gateway.end());
    dns.erase(std::remove_if(dns.begin(), dns.end(), ::isspace), dns.end());

    std::string info;
    info += _("INTERFACE") + ": " + iface + "\n";
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

    std::string status = executeCommand("nmcli -t -f DEVICE,TYPE,STATE dev 2>/dev/null | grep wifi");
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
// Power LED Functions
// ============================================================================
// 驱动层处理充电监控和阈值逻辑，用户态只需设置阈值

bool GuiArkOS4CloneSettings::hasPowerLedRed()
{
    return Utils::FileSystem::exists(POWER_LED_RED);
}

bool GuiArkOS4CloneSettings::hasPowerLedBlue()
{
    return Utils::FileSystem::exists(POWER_LED_BLUE);
}

bool GuiArkOS4CloneSettings::hasArkOS4CloneLed()
{
    return Utils::FileSystem::exists(ARKOS4CLONE_LED);
}

bool GuiArkOS4CloneSettings::hasPowerLed()
{
    return hasPowerLedRed() || hasPowerLedBlue() || hasArkOS4CloneLed();
}

void GuiArkOS4CloneSettings::applyPowerLed()
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
            executeCommand("sudo sh -c 'echo " + std::to_string(redThreshold) + " > " + thresholdPath + "'");
        }
        // 阈值=0时，用户控制 brightness
        if (redThreshold == 0) {
            int brightness = (redMode == 1) ? 1 : 0; // ON=1, OFF=0
            executeCommand("sudo sh -c 'echo " + std::to_string(brightness) + " > " + POWER_LED_RED + "'");
        }
    }
    
    // BLUE LED
    if (hasPowerLedBlue()) {
        std::string thresholdPath = POWER_LED_BLUE.substr(0, POWER_LED_BLUE.rfind('/')) + "/battery_threshold";
        if (Utils::FileSystem::exists(thresholdPath)) {
            executeCommand("sudo sh -c 'echo " + std::to_string(blueThreshold) + " > " + thresholdPath + "'");
        }
        // 阈值=0时，用户控制 brightness
        if (blueThreshold == 0) {
            int brightness = (blueMode == 1) ? 1 : 0; // ON=1, OFF=0
            executeCommand("sudo sh -c 'echo " + std::to_string(brightness) + " > " + POWER_LED_BLUE + "'");
        }
    }
    
    // ArkOS4Clone dual-color LED
    if (hasArkOS4CloneLed()) {
        std::string thresholdPath = ARKOS4CLONE_LED.substr(0, ARKOS4CLONE_LED.rfind('/')) + "/battery_threshold";
        if (Utils::FileSystem::exists(thresholdPath)) {
            executeCommand("sudo sh -c 'echo " + std::to_string(arkosThreshold) + " > " + thresholdPath + "'");
        }
        // 阈值=0时，用户控制 brightness
        if (arkosThreshold == 0) {
            int brightness = (arkosMode == 1) ? 1 : 0; // RED=1, BLUE=0
            executeCommand("sudo sh -c 'echo " + std::to_string(brightness) + " > " + ARKOS4CLONE_LED + "'");
        }
    }
}

void GuiArkOS4CloneSettings::openPowerLedSettings()
{
    // 新驱动架构：阈值逻辑由驱动处理
    // 用户只需设置阈值，驱动自动根据充电状态和阈值控制LED
    // 保留原有选项文字，内部映射到阈值：
    //   双色LED: BLUE/RED = 阈值0（用户控制），ABOVE X% BLUE = 阈值X
    //   独立LED: OFF/ON = 阈值0（用户控制），BELOW/ABOVE X% = 阈值X
    
    auto s = new GuiSettings(mWindow, _("POWER LED"));
    
    std::shared_ptr<OptionListComponent<std::string>> redList;
    std::shared_ptr<OptionListComponent<std::string>> blueList;
    std::shared_ptr<OptionListComponent<std::string>> arkosList;
    
    // Handle ArkOS4Clone dual-color LED
    if (hasArkOS4CloneLed()) {
        int mode = Settings::getInstance()->getInt("PowerLedArkOS4CloneMode");
        int threshold = Settings::getInstance()->getInt("PowerLedArkOS4CloneThreshold");
        if (mode < 0 || mode > 2) mode = 0;
        if (threshold < 0 || threshold > 90) threshold = 0;
        
        // Build selection value: blue/red = 阈值0, auto:X = 阈值X
        std::string selected;
        if (threshold > 0) {
            selected = "auto:" + std::to_string(threshold);
        } else {
            selected = (mode == 1) ? "red" : "blue";
        }
        
        arkosList = std::make_shared<OptionListComponent<std::string>>(mWindow, _("POWER LED"), false);
        arkosList->add(_("BLUE"), "blue", selected == "blue");
        arkosList->add(_("RED"), "red", selected == "red");
        for (int t = 90; t >= 10; t -= 10) {
            std::string val = "auto:" + std::to_string(t);
            std::string label = _("ABOVE") + std::string(" ") + std::to_string(t) + "% " + _("BLUE");
            arkosList->add(label, val, selected == val);
        }
        s->addWithLabel(_("POWER LED"), arkosList);
    }
    else {
        // Handle separate RED/BLUE LEDs
        int redMode = Settings::getInstance()->getInt("PowerLedRedMode");
        int blueMode = Settings::getInstance()->getInt("PowerLedBlueMode");
        int redThreshold = Settings::getInstance()->getInt("PowerLedRedThreshold");
        int blueThreshold = Settings::getInstance()->getInt("PowerLedBlueThreshold");
        
        if (redMode < 0 || redMode > 2) redMode = 0;
        if (blueMode < 0 || blueMode > 2) blueMode = 0;
        if (redThreshold < 0 || redThreshold > 90) redThreshold = 0;
        if (blueThreshold < 0 || blueThreshold > 90) blueThreshold = 0;
        
        // Build selection: threshold>0 = auto:X, threshold=0 + mode=0 = off, mode=1 = on
        std::string redSelected;
        if (redThreshold > 0) redSelected = "auto:" + std::to_string(redThreshold);
        else redSelected = (redMode == 1) ? "on" : "off";
        
        std::string blueSelected;
        if (blueThreshold > 0) blueSelected = "auto:" + std::to_string(blueThreshold);
        else blueSelected = (blueMode == 1) ? "on" : "off";
        
        // RED LED
        if (hasPowerLedRed()) {
            redList = std::make_shared<OptionListComponent<std::string>>(mWindow, _("RED LED"), false);
            redList->add(_("OFF"), "off", redSelected == "off");
            redList->add(_("ON"), "on", redSelected == "on");
            for (int t = 90; t >= 10; t -= 10) {
                std::string val = "auto:" + std::to_string(t);
                std::string label = _("BELOW") + std::string(" ") + std::to_string(t) + "%";
                redList->add(label, val, redSelected == val);
            }
            s->addWithLabel(_("RED LED"), redList);
        }
        
        // BLUE LED
        if (hasPowerLedBlue()) {
            blueList = std::make_shared<OptionListComponent<std::string>>(mWindow, _("BLUE LED"), false);
            blueList->add(_("OFF"), "off", blueSelected == "off");
            blueList->add(_("ON"), "on", blueSelected == "on");
            for (int t = 10; t <= 90; t += 10) {
                std::string val = "auto:" + std::to_string(t);
                std::string label = _("ABOVE") + std::string(" ") + std::to_string(t) + "%";
                blueList->add(label, val, blueSelected == val);
            }
            s->addWithLabel(_("BLUE LED"), blueList);
        }
    }
    
    // Save callback - 保存 mode 和 threshold，驱动自动处理
    s->addSaveFunc([this, redList, blueList, arkosList] {
        if (arkosList) {
            std::string val = arkosList->getSelected();
            int mode = 0, threshold = 0;
            if (val == "blue") {
                mode = 0; threshold = 0;
            } else if (val == "red") {
                mode = 1; threshold = 0;
            } else if (val.substr(0, 5) == "auto:") {
                mode = 2; threshold = atoi(val.substr(5).c_str());
            }
            Settings::getInstance()->setInt("PowerLedArkOS4CloneMode", mode);
            Settings::getInstance()->setInt("PowerLedArkOS4CloneThreshold", threshold);
        }
        
        if (redList) {
            std::string val = redList->getSelected();
            int mode = 0, threshold = 0;
            if (val == "off") {
                mode = 0; threshold = 0;
            } else if (val == "on") {
                mode = 1; threshold = 0;
            } else if (val.substr(0, 5) == "auto:") {
                mode = 2; threshold = atoi(val.substr(5).c_str());
            }
            Settings::getInstance()->setInt("PowerLedRedMode", mode);
            Settings::getInstance()->setInt("PowerLedRedThreshold", threshold);
        }
        
        if (blueList) {
            std::string val = blueList->getSelected();
            int mode = 0, threshold = 0;
            if (val == "off") {
                mode = 0; threshold = 0;
            } else if (val == "on") {
                mode = 1; threshold = 0;
            } else if (val.substr(0, 5) == "auto:") {
                mode = 2; threshold = atoi(val.substr(5).c_str());
            }
            Settings::getInstance()->setInt("PowerLedBlueMode", mode);
            Settings::getInstance()->setInt("PowerLedBlueThreshold", threshold);
        }
        
        Settings::getInstance()->saveFile();
        applyPowerLed();
    });
    
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
    
    cached = true;
    
    // Use console_detect -s for LED type
    std::string output = executeCommand("/usr/local/bin/console_detect -s 2>/dev/null");
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
    std::string color = Settings::getInstance()->getString("JoyLedColor");
    return color.empty() ? "off" : color;
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

// ============================================================================
// LED Functions - Apply
// ============================================================================

void GuiArkOS4CloneSettings::applyLedColor(const std::string& color, const std::string& brightness)
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
    
    std::string ledBlue = "/sys/class/leds/joy-blue/brightness";
    std::string ledGreen = "/sys/class/leds/joy-green/brightness";
    std::string ledRed = "/sys/class/leds/joy-red/brightness";
    
    // Disable triggers only for joystick LEDs
    executeCommand("sudo sh -c 'echo none > /sys/class/leds/joy-blue/trigger 2>/dev/null; echo none > /sys/class/leds/joy-green/trigger 2>/dev/null; echo none > /sys/class/leds/joy-red/trigger 2>/dev/null'");
    
    // Get max brightness
    int maxB = 1, maxG = 1, maxR = 1;
    std::string maxBPath = "/sys/class/leds/joy-blue/max_brightness";
    std::string maxGPath = "/sys/class/leds/joy-green/max_brightness";
    std::string maxRPath = "/sys/class/leds/joy-red/max_brightness";
    
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

bool GuiArkOS4CloneSettings::hasDualGpioLed()
{
    return Utils::FileSystem::exists(DUAL_GPIO_LED_LEFT) && Utils::FileSystem::exists(DUAL_GPIO_LED_RIGHT);
}

void GuiArkOS4CloneSettings::applyDualGpioLed(bool leftOn, bool rightOn)
{
    // Disable triggers for both LEDs
    executeCommand("sudo sh -c 'echo none > /sys/class/leds/joy-left/trigger 2>/dev/null; echo none > /sys/class/leds/joy-right/trigger 2>/dev/null'");
    
    // Get max brightness
    int maxLeft = 1, maxRight = 1;
    std::string maxLeftPath = "/sys/class/leds/joy-left/max_brightness";
    std::string maxRightPath = "/sys/class/leds/joy-right/max_brightness";
    
    if (Utils::FileSystem::exists(maxLeftPath)) {
        maxLeft = atoi(executeCommand("cat " + maxLeftPath).c_str());
    }
    if (Utils::FileSystem::exists(maxRightPath)) {
        maxRight = atoi(executeCommand("cat " + maxRightPath).c_str());
    }
    
    // Apply LED states
    if (Utils::FileSystem::exists(DUAL_GPIO_LED_LEFT)) {
        executeCommand("sudo sh -c 'echo " + std::to_string(leftOn ? maxLeft : 0) + " > " + DUAL_GPIO_LED_LEFT + "'");
    }
    if (Utils::FileSystem::exists(DUAL_GPIO_LED_RIGHT)) {
        executeCommand("sudo sh -c 'echo " + std::to_string(rightOn ? maxRight : 0) + " > " + DUAL_GPIO_LED_RIGHT + "'");
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

void GuiArkOS4CloneSettings::applyR36UltraV2Led(const std::string& color)
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
    executeCommand("sudo sh -c 'echo off > " + JOYLED_MODE_PATH + "'");
    
    if (color == "off") {
        return; // Already off
    }
    
    // Apply new color
    executeCommand("sudo sh -c 'echo " + color + " > " + JOYLED_MODE_PATH + "'");
}

// ============================================================================
// LED Functions - Config & Startup
// ============================================================================

void GuiArkOS4CloneSettings::saveLedConfig(const std::string& color, const std::string& brightness)
{
    Settings::getInstance()->setString("JoyLedColor", color);
    if (!brightness.empty()) {
        Settings::getInstance()->setString("JoyLedBrightness", brightness);
    }
    Settings::getInstance()->saveFile();
}

bool GuiArkOS4CloneSettings::checkAndApplyLedOnStartup()
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

void GuiArkOS4CloneSettings::applyPowerLedOnStartup()
{
    // 新驱动架构：启动时只需设置阈值，驱动自动处理充电监控和阈值逻辑
    if (hasPowerLed()) {
        applyPowerLed();
    }
}

void GuiArkOS4CloneSettings::openJoystickLedSettings()
{
    std::string ledType = detectLedType();
    std::string deviceName = getDeviceName();
    
    if (ledType.empty()) {
        mWindow->pushGui(new GuiMsgBox(mWindow, 
            _("UNSUPPORTED DEVICE") + "\n" + _("Joystick LED is not supported on this device."), 
            _("OK")));
        return;
    }
    
    auto s = new GuiSettings(mWindow, _("JOYSTICK LED"));
    
    // Special handling for r36ultra: let user choose V1 or V2 version
    if (deviceName == "r36ultra") {
        int savedVersion = Settings::getInstance()->getInt("R36UltraLedVersion");
        
        auto versionOptions = std::make_shared<OptionListComponent<std::string>>(mWindow, _("VERSION"), false);
        versionOptions->add(_("R36Ultra V1"), "v1", savedVersion == 1);
        versionOptions->add(_("R36Ultra V2"), "v2", savedVersion == 2);
        
        s->addWithLabel(_("VERSION"), versionOptions);
        
        // Determine current effective LED type based on saved version
        std::string currentLedType = (savedVersion == 2) ? "r36ultra_v2" : "gpio";
        
        // Get menu items for current version
        auto items = getLedMenuItems(currentLedType);
        std::string currentColor = getCurrentLedColor();
        
        // Check if currentColor is valid for current version
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
        
        // Shared version variable for callbacks
        auto currentVersion = std::make_shared<int>(savedVersion);
        
        // Handle LED color change - set callback first
        ledOptions->setSelectedChangedCallback([this, currentVersion](const std::string& selectedColor) {
            if (*currentVersion == 2) {
                applyR36UltraV2Led(selectedColor);
            } else {
                applyGpioLed(selectedColor);
            }
            Settings::getInstance()->setString("JoyLedColor", selectedColor);
            Settings::getInstance()->saveFile();
        });
        
        // Handle version change - dynamically refresh LED options
        versionOptions->setSelectedChangedCallback([this, ledOptions, currentVersion](const std::string& selectedVersion) {
            int newVersion = (selectedVersion == "v2") ? 2 : 1;
            int oldVersion = *currentVersion;
            
            // If version changed, turn off LED using OLD version's method first
            if (oldVersion != newVersion) {
                if (oldVersion == 2) {
                    applyR36UltraV2Led("off");
                } else {
                    applyGpioLed("off");
                }
            }
            
            // Update version
            *currentVersion = newVersion;
            
            // Save settings
            Settings::getInstance()->setInt("R36UltraLedVersion", newVersion);
            Settings::getInstance()->setString("JoyLedColor", "off");
            Settings::getInstance()->saveFile();
            
            // Update UI with new LED options
            ledOptions->clear();
            std::string newLedType = (newVersion == 2) ? "r36ultra_v2" : "gpio";
            auto newItems = getLedMenuItems(newLedType);
            for (auto& item : newItems) {
                ledOptions->add(item.second, item.first, item.first == "off");
            }
        });
        
        mWindow->pushGui(s);
        return;
    }
    
    // Special handling for dual-gpio: two switches for left/right joystick
    if (ledType == "dual-gpio") {
        // Read current LED states
        bool leftOn = false, rightOn = false;
        if (Utils::FileSystem::exists(DUAL_GPIO_LED_LEFT)) {
            std::string leftVal = executeCommand("cat " + DUAL_GPIO_LED_LEFT);
            leftOn = (atoi(leftVal.c_str()) > 0);
        }
        if (Utils::FileSystem::exists(DUAL_GPIO_LED_RIGHT)) {
            std::string rightVal = executeCommand("cat " + DUAL_GPIO_LED_RIGHT);
            rightOn = (atoi(rightVal.c_str()) > 0);
        }
        
        // Left joystick LED switch
        auto leftSwitch = std::make_shared<SwitchComponent>(mWindow);
        leftSwitch->setState(leftOn);
        s->addWithLabel(_("LEFT JOYSTICK LED"), leftSwitch);
        
        // Right joystick LED switch
        auto rightSwitch = std::make_shared<SwitchComponent>(mWindow);
        rightSwitch->setState(rightOn);
        s->addWithLabel(_("RIGHT JOYSTICK LED"), rightSwitch);
        
        // Apply immediately when switch changes
        leftSwitch->setOnChangedCallback([this, leftSwitch, rightSwitch]() {
            applyDualGpioLed(leftSwitch->getState(), rightSwitch->getState());
            Settings::getInstance()->setBool("JoyLedLeft", leftSwitch->getState());
            Settings::getInstance()->setBool("JoyLedRight", rightSwitch->getState());
            Settings::getInstance()->saveFile();
        });
        
        rightSwitch->setOnChangedCallback([this, leftSwitch, rightSwitch]() {
            applyDualGpioLed(leftSwitch->getState(), rightSwitch->getState());
            Settings::getInstance()->setBool("JoyLedLeft", leftSwitch->getState());
            Settings::getInstance()->setBool("JoyLedRight", rightSwitch->getState());
            Settings::getInstance()->saveFile();
        });
        
        mWindow->pushGui(s);
        return;
    }
    
    // Standard handling for other LED types
    auto items = getLedMenuItems(ledType);
    
    if (items.empty()) {
        mWindow->pushGui(new GuiMsgBox(mWindow, 
            _("UNSUPPORTED DEVICE") + "\n" + _("Joystick LED is not supported on this device."), 
            _("OK")));
        return;
    }
    
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
// Date & Time Functions
// ============================================================================

std::string GuiArkOS4CloneSettings::getCurrentDateTime()
{
    std::string result = executeCommand("date '+%Y-%m-%d %H:%M'");
    return result.empty() ? "2024-01-01 00:00" : result;
}

bool GuiArkOS4CloneSettings::setSystemTime(int year, int month, int day, int hour, int minute)
{
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "sudo date -s '%04d-%02d-%02d %02d:%02d:00' 2>/dev/null", year, month, day, hour, minute);
    executeCommand(cmd);
    
    // Sync to hardware clock
    executeCommand("sudo hwclock -w 2>/dev/null");
    
    return true;
}

bool GuiArkOS4CloneSettings::syncNetworkTime()
{
    // Enable NTP sync
    executeCommand("sudo timedatectl set-ntp 1 2>/dev/null");
    
    // Try ntpdate as fallback
    executeCommand("sudo ntpdate pool.ntp.org 2>/dev/null || sudo ntpdate time.google.com 2>/dev/null");
    
    // Sync to hardware clock
    executeCommand("sudo hwclock -w 2>/dev/null");
    
    return true;
}

void GuiArkOS4CloneSettings::openDateTimeSettings()
{
    auto s = new GuiSettings(mWindow, _("DATE & TIME"));
    auto theme = ThemeData::getMenuTheme();
    
    // Check network connection
    std::string gateway = executeCommand("ip route | awk '/default/ { print $3; exit }' 2>/dev/null");
    bool hasNetwork = !Utils::String::trim(gateway).empty();
    
    // Get current date/time
    std::string currentDateTime = getCurrentDateTime();
    int currentYear = 2024, currentMonth = 1, currentDay = 1, currentHour = 0, currentMinute = 0;
    sscanf(currentDateTime.c_str(), "%d-%d-%d %d:%d", &currentYear, &currentMonth, &currentDay, &currentHour, &currentMinute);
    
    // Display current time (extract only the datetime pattern, remove extra chars)
    std::string fullDateTime = executeCommand("date '+%Y-%m-%d %H:%M'");
    std::regex dtRegex("^[\\s\\r\\n]+|[\\s\\r\\n]+$");
    fullDateTime = std::regex_replace(fullDateTime, dtRegex, "");
    
    auto currentTimeText = std::make_shared<TextComponent>(mWindow, fullDateTime, 
        theme->Text.font, theme->Text.color, ALIGN_RIGHT);
    currentTimeText->setSize(Renderer::getScreenWidth() * 0.4f, theme->Text.font->getHeight() * 1.5f);
    s->addWithLabel(_("CURRENT"), currentTimeText);
    
    // Network sync button (always available when network connected)
    s->addEntry(_("SYNC WITH NETWORK"), hasNetwork, [this, hasNetwork] {
        if (!hasNetwork) {
            mWindow->pushGui(new GuiMsgBox(mWindow, _("NO NETWORK CONNECTION"), _("OK")));
            return;
        }
        syncNetworkTime();
        mWindow->pushGui(new GuiMsgBox(mWindow, _("TIME SYNCED SUCCESSFULLY"), _("OK")));
    }, "");
    
    // Manual adjustment only when offline
    if (!hasNetwork) {
        // Helper function to get days in month
        auto getDaysInMonth = [](int year, int month) -> int {
            static const int daysInMonth[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
            if (month == 2) {
                bool isLeap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
                return isLeap ? 29 : 28;
            }
            return daysInMonth[month];
        };
        
        // Year selector (2020-2040)
        auto yearList = std::make_shared<OptionListComponent<std::string>>(mWindow, _("YEAR"), false);
        for (int y = 2020; y <= 2040; y++) {
            yearList->add(std::to_string(y), std::to_string(y), y == currentYear);
        }
        s->addWithLabel(_("YEAR"), yearList);
        
        // Month selector
        auto monthList = std::make_shared<OptionListComponent<std::string>>(mWindow, _("MONTH"), false);
        for (int m = 1; m <= 12; m++) {
            char buf[8];
            snprintf(buf, sizeof(buf), "%02d", m);
            monthList->add(buf, buf, m == currentMonth);
        }
        s->addWithLabel(_("MONTH"), monthList);
        
        // Day selector
        auto dayList = std::make_shared<OptionListComponent<std::string>>(mWindow, _("DAY"), false);
        int maxDays = getDaysInMonth(currentYear, currentMonth);
        if (currentDay > maxDays) currentDay = maxDays;
        for (int d = 1; d <= maxDays; d++) {
            char buf[8];
            snprintf(buf, sizeof(buf), "%02d", d);
            dayList->add(buf, buf, d == currentDay);
        }
        s->addWithLabel(_("DAY"), dayList);
        
        // Update day list when year or month changes
        auto updateDays = [dayList, &getDaysInMonth](int year, int month) {
            int maxDays = getDaysInMonth(year, month);
            int selected = atoi(dayList->getSelected().c_str());
            if (selected > maxDays) selected = maxDays;
            
            dayList->clear();
            for (int d = 1; d <= maxDays; d++) {
                char buf[8];
                snprintf(buf, sizeof(buf), "%02d", d);
                dayList->add(buf, buf, d == selected);
            }
        };
        
        yearList->setSelectedChangedCallback([monthList, updateDays](const std::string& val) {
            updateDays(atoi(val.c_str()), atoi(monthList->getSelected().c_str()));
        });
        
        monthList->setSelectedChangedCallback([yearList, updateDays](const std::string& val) {
            updateDays(atoi(yearList->getSelected().c_str()), atoi(val.c_str()));
        });
        
        // Hour selector
        auto hourList = std::make_shared<OptionListComponent<std::string>>(mWindow, _("HOUR"), false);
        for (int h = 0; h < 24; h++) {
            char buf[8];
            snprintf(buf, sizeof(buf), "%02d", h);
            hourList->add(buf, buf, h == currentHour);
        }
        s->addWithLabel(_("HOUR"), hourList);
        
        // Minute selector (5-minute intervals)
        auto minuteList = std::make_shared<OptionListComponent<std::string>>(mWindow, _("MINUTE"), false);
        int currentMinuteRounded = (currentMinute / 5) * 5;
        for (int m = 0; m < 60; m += 5) {
            char buf[8];
            snprintf(buf, sizeof(buf), "%02d", m);
            minuteList->add(buf, buf, m == currentMinuteRounded);
        }
        s->addWithLabel(_("MINUTE"), minuteList);
        
        // Apply button
        s->addEntry(_("APPLY"), true, [this, yearList, monthList, dayList, hourList, minuteList] {
            setSystemTime(
                atoi(yearList->getSelected().c_str()),
                atoi(monthList->getSelected().c_str()),
                atoi(dayList->getSelected().c_str()),
                atoi(hourList->getSelected().c_str()),
                atoi(minuteList->getSelected().c_str())
            );
            mWindow->pushGui(new GuiMsgBox(mWindow, _("TIME SET SUCCESSFULLY"), _("OK")));
        }, "");
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

// ============================================================================
// ArkOS4Clone Tools Menu
// ============================================================================

void GuiArkOS4CloneSettings::openToolsMenu()
{
    auto s = new GuiSettings(mWindow, _("ARKOS4CLONE TOOLS"));
    
    // CPU Settings (always available on multi-core systems)
    if (getCpuCoreCount() > 1 || !getAvailableGovernors().empty()) {
        s->addEntry(_("CPU SETTINGS"), true, [this] {
            openCpuSettings();
        }, "");
    }
    
    // GPU Settings (only if GPU freq control is available)
    if (hasGpuFreqControl()) {
        s->addEntry(_("GPU SETTINGS"), true, [this] {
            openGpuSettings();
        }, "");
    }
    
    // DMC Settings (only if DMC freq control is available)
    if (hasDmcFreqControl()) {
        s->addEntry(_("DMC SETTINGS"), true, [this] {
            openDmcSettings();
        }, "");
    }
    
    // ZRAM Settings
    s->addEntry(_("ZRAM SETTINGS"), true, [this] {
        openZramSettings();
    }, "");
    
    mWindow->pushGui(s);
}

// ============================================================================
// CPU Settings
// ============================================================================

int GuiArkOS4CloneSettings::getCpuCoreCount()
{
    std::string result = executeCommand("ls -d /sys/devices/system/cpu/cpu[0-9]* 2>/dev/null | wc -l");
    return atoi(result.c_str());
}

int GuiArkOS4CloneSettings::getOnlineCpuCount()
{
    int count = 0;
    int total = getCpuCoreCount();
    for (int i = 0; i < total; i++) {
        std::string result = executeCommand("cat /sys/devices/system/cpu/cpu" + std::to_string(i) + "/online 2>/dev/null");
        // Remove all whitespace including newlines
        result.erase(std::remove_if(result.begin(), result.end(), ::isspace), result.end());
        if (result == "1" || result.empty()) {  // cpu0 has no online file but is always on
            count++;
        }
    }
    return count;
}

std::string GuiArkOS4CloneSettings::getCpuGovernor()
{
    std::string result = executeCommand("cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null");
    // Remove all whitespace including newlines
    result.erase(std::remove_if(result.begin(), result.end(), ::isspace), result.end());
    return result;
}

void GuiArkOS4CloneSettings::setCpuGovernor(const std::string& governor)
{
    int cores = getCpuCoreCount();
    for (int i = 0; i < cores; i++) {
        executeCommand("echo " + governor + " | sudo tee /sys/devices/system/cpu/cpu" + std::to_string(i) + "/cpufreq/scaling_governor >/dev/null 2>&1");
    }
}

std::string GuiArkOS4CloneSettings::getCpuMaxFreq()
{
    std::string result = executeCommand("cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq 2>/dev/null");
    result.erase(std::remove_if(result.begin(), result.end(), ::isspace), result.end());
    return result;
}

void GuiArkOS4CloneSettings::setCpuMaxFreq(const std::string& freq)
{
    int cores = getCpuCoreCount();
    for (int i = 0; i < cores; i++) {
        executeCommand("echo " + freq + " | sudo tee /sys/devices/system/cpu/cpu" + std::to_string(i) + "/cpufreq/scaling_max_freq >/dev/null 2>&1");
    }
}

std::vector<std::string> GuiArkOS4CloneSettings::getCpuAvailableFreqs()
{
    std::string result = executeCommand("cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_available_frequencies 2>/dev/null");
    std::vector<std::string> freqs;
    std::istringstream stream(result);
    std::string freq;
    while (stream >> freq) {
        freq.erase(std::remove_if(freq.begin(), freq.end(), ::isspace), freq.end());
        if (!freq.empty()) freqs.push_back(freq);
    }
    return freqs;
}

std::vector<std::string> GuiArkOS4CloneSettings::getAvailableGovernors()
{
    std::string result = executeCommand("cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_available_governors 2>/dev/null");
    std::vector<std::string> governors;
    std::istringstream stream(result);
    std::string gov;
    while (stream >> gov) {
        gov.erase(std::remove_if(gov.begin(), gov.end(), ::isspace), gov.end());
        if (!gov.empty()) governors.push_back(gov);
    }
    return governors;
}

void GuiArkOS4CloneSettings::setCpuCores(int count)
{
    int totalCores = getCpuCoreCount();
    // Enable all cores first
    for (int i = 0; i < totalCores; i++) {
        executeCommand("echo 1 | sudo tee /sys/devices/system/cpu/cpu" + std::to_string(i) + "/online >/dev/null 2>&1");
    }
    // Disable cores beyond count (keep cpu0 always on)
    for (int i = count; i < totalCores; i++) {
        executeCommand("echo 0 | sudo tee /sys/devices/system/cpu/cpu" + std::to_string(i) + "/online >/dev/null 2>&1");
    }
}

void GuiArkOS4CloneSettings::openCpuSettings()
{
    auto s = new GuiSettings(mWindow, _("CPU SETTINGS"));
    
    // CPU Cores
    int coreCount = getCpuCoreCount();
    int onlineCount = getOnlineCpuCount();
    LOG(LogDebug) << "CPU totalCores: " << coreCount << " onlineCores: " << onlineCount;
    if (coreCount > 1) {
        auto coreList = std::make_shared<OptionListComponent<std::string>>(mWindow, _("CPU CORES"), false);
        for (int i = 1; i <= coreCount; i++) {
            coreList->add(std::to_string(i), std::to_string(i), i == onlineCount);
        }
        s->addWithLabel(_("CPU CORES"), coreList);
        
        coreList->setSelectedChangedCallback([this](const std::string& val) {
            setCpuCores(atoi(val.c_str()));
        });
    }
    
    // CPU Governor
    auto governors = getAvailableGovernors();
    if (!governors.empty()) {
        auto govList = std::make_shared<OptionListComponent<std::string>>(mWindow, _("GOVERNOR"), false);
        std::string currentGov = getCpuGovernor();
        LOG(LogDebug) << "CPU currentGov: '" << currentGov << "'";
        bool found = false;
        for (const auto& gov : governors) {
            bool isSelected = (gov == currentGov);
            LOG(LogDebug) << "CPU gov option: '" << gov << "' selected: " << isSelected;
            if (isSelected) found = true;
            govList->add(gov, gov, isSelected);
        }
        if (!found && !governors.empty()) {
            govList->selectFirstItem();
        }
        s->addWithLabel(_("CPU GOVERNOR"), govList);
        
        govList->setSelectedChangedCallback([this](const std::string& val) {
            setCpuGovernor(val);
        });
    }
    
    // CPU Max Frequency
    auto freqs = getCpuAvailableFreqs();
    if (!freqs.empty()) {
        auto freqList = std::make_shared<OptionListComponent<std::string>>(mWindow, _("MAX FREQ"), false);
        std::string currentFreq = getCpuMaxFreq();
        LOG(LogDebug) << "CPU currentFreq: '" << currentFreq << "'";
        bool found = false;
        for (const auto& freq : freqs) {
            // Convert kHz to MHz for display
            int mhz = atoi(freq.c_str()) / 1000;
            bool isSelected = (freq == currentFreq);
            LOG(LogDebug) << "CPU freq option: '" << freq << "' selected: " << isSelected;
            if (isSelected) found = true;
            freqList->add(std::to_string(mhz) + " MHz", freq, isSelected);
        }
        if (!found && !freqs.empty()) {
            freqList->selectFirstItem();
        }
        s->addWithLabel(_("CPU MAX FREQ"), freqList);
        
        freqList->setSelectedChangedCallback([this](const std::string& val) {
            setCpuMaxFreq(val);
        });
    }
    
    mWindow->pushGui(s);
}

// ============================================================================
// GPU Settings
// ============================================================================

bool GuiArkOS4CloneSettings::hasGpuFreqControl()
{
    std::string result = executeCommand("ls -d /sys/class/devfreq/ff400000.gpu 2>/dev/null");
    return !Utils::String::trim(result).empty();
}

std::string GuiArkOS4CloneSettings::getGpuDevPath()
{
    return "/sys/class/devfreq/ff400000.gpu/";
}

std::string GuiArkOS4CloneSettings::getGpuMaxFreq()
{
    std::string gpuPath = getGpuDevPath();
    if (gpuPath.empty()) return "";
    std::string result = executeCommand("cat " + gpuPath + "max_freq 2>/dev/null");
    result.erase(std::remove_if(result.begin(), result.end(), ::isspace), result.end());
    return result;
}

void GuiArkOS4CloneSettings::setGpuMaxFreq(const std::string& freq)
{
    std::string gpuPath = getGpuDevPath();
    if (gpuPath.empty()) return;
    executeCommand("echo " + freq + " | sudo tee " + gpuPath + "max_freq >/dev/null 2>&1");
}

std::vector<std::string> GuiArkOS4CloneSettings::getGpuAvailableFreqs()
{
    std::string gpuPath = getGpuDevPath();
    if (gpuPath.empty()) return {};
    std::string result = executeCommand("cat " + gpuPath + "available_frequencies 2>/dev/null");
    std::vector<std::string> freqs;
    std::istringstream stream(result);
    std::string freq;
    while (stream >> freq) {
        freq.erase(std::remove_if(freq.begin(), freq.end(), ::isspace), freq.end());
        if (!freq.empty()) freqs.push_back(freq);
    }
    return freqs;
}

void GuiArkOS4CloneSettings::openGpuSettings()
{
    auto s = new GuiSettings(mWindow, _("GPU SETTINGS"));
    
    auto freqs = getGpuAvailableFreqs();
    if (!freqs.empty()) {
        auto freqList = std::make_shared<OptionListComponent<std::string>>(mWindow, _("MAX FREQ"), false);
        std::string currentFreq = getGpuMaxFreq();
        LOG(LogDebug) << "GPU currentFreq: '" << currentFreq << "'";
        bool found = false;
        for (const auto& freq : freqs) {
            // Convert Hz to MHz for display
            int mhz = atoi(freq.c_str()) / 1000000;
            bool isSelected = (freq == currentFreq);
            LOG(LogDebug) << "GPU freq option: '" << freq << "' selected: " << isSelected;
            if (isSelected) found = true;
            freqList->add(std::to_string(mhz) + " MHz", freq, isSelected);
        }
        if (!found && !freqs.empty()) {
            freqList->selectFirstItem();
        }
        s->addWithLabel(_("GPU MAX FREQ"), freqList);
        
        freqList->setSelectedChangedCallback([this](const std::string& val) {
            setGpuMaxFreq(val);
        });
    }
    
    mWindow->pushGui(s);
}

// ============================================================================
// DMC Settings
// ============================================================================

bool GuiArkOS4CloneSettings::hasDmcFreqControl()
{
    std::string result = executeCommand("ls /sys/class/devfreq/dmc/available_frequencies 2>/dev/null");
    return !Utils::String::trim(result).empty();
}

std::string GuiArkOS4CloneSettings::getDmcMaxFreq()
{
    std::string result = executeCommand("cat /sys/class/devfreq/dmc/max_freq 2>/dev/null");
    result.erase(std::remove_if(result.begin(), result.end(), ::isspace), result.end());
    return result;
}

void GuiArkOS4CloneSettings::setDmcMaxFreq(const std::string& freq)
{
    executeCommand("echo " + freq + " | sudo tee /sys/class/devfreq/dmc/max_freq >/dev/null 2>&1");
}

std::vector<std::string> GuiArkOS4CloneSettings::getDmcAvailableFreqs()
{
    std::string result = executeCommand("cat /sys/class/devfreq/dmc/available_frequencies 2>/dev/null");
    std::vector<std::string> freqs;
    std::istringstream stream(result);
    std::string freq;
    while (stream >> freq) {
        freq.erase(std::remove_if(freq.begin(), freq.end(), ::isspace), freq.end());
        if (!freq.empty()) freqs.push_back(freq);
    }
    return freqs;
}

void GuiArkOS4CloneSettings::openDmcSettings()
{
    auto s = new GuiSettings(mWindow, _("DMC SETTINGS"));
    
    auto freqs = getDmcAvailableFreqs();
    if (!freqs.empty()) {
        auto freqList = std::make_shared<OptionListComponent<std::string>>(mWindow, _("MAX FREQ"), false);
        std::string currentFreq = getDmcMaxFreq();
        LOG(LogDebug) << "DMC currentFreq: '" << currentFreq << "'";
        bool found = false;
        for (const auto& freq : freqs) {
            // Convert Hz to MHz for display
            int mhz = atoi(freq.c_str()) / 1000000;
            bool isSelected = (freq == currentFreq);
            LOG(LogDebug) << "DMC freq option: '" << freq << "' selected: " << isSelected;
            if (isSelected) found = true;
            freqList->add(std::to_string(mhz) + " MHz", freq, isSelected);
        }
        if (!found && !freqs.empty()) {
            freqList->selectFirstItem();
        }
        s->addWithLabel(_("DMC MAX FREQ"), freqList);
        
        freqList->setSelectedChangedCallback([this](const std::string& val) {
            setDmcMaxFreq(val);
        });
    }
    
    mWindow->pushGui(s);
}

// ============================================================================
// ZRAM Settings
// ============================================================================

std::string GuiArkOS4CloneSettings::getZramSize()
{
    std::string result = executeCommand("cat /sys/block/zram0/disksize 2>/dev/null");
    result.erase(std::remove_if(result.begin(), result.end(), ::isspace), result.end());
    if (result.empty()) return "0";
    long size = atol(result.c_str());
    // Convert to MB
    long mb = size / (1024 * 1024);
    return std::to_string(mb) + "M";
}

bool GuiArkOS4CloneSettings::isZramEnabled()
{
    std::string result = executeCommand("grep -q '^/dev/zram0' /proc/swaps 2>/dev/null && echo yes || echo no");
    result.erase(std::remove_if(result.begin(), result.end(), ::isspace), result.end());
    return result == "yes";
}

std::string GuiArkOS4CloneSettings::getZramCompAlgorithm()
{
    std::string result = executeCommand("cat /sys/block/zram0/comp_algorithm 2>/dev/null");
    // Parse current algorithm from format like "[lzo] lz4 lz4hc zstd"
    size_t start = result.find('[');
    size_t end = result.find(']');
    if (start != std::string::npos && end != std::string::npos && end > start) {
        return result.substr(start + 1, end - start - 1);
    }
    return "lz4";
}

std::vector<std::string> GuiArkOS4CloneSettings::getAvailableZramAlgorithms()
{
    std::vector<std::string> algos;
    std::string result = executeCommand("cat /sys/block/zram0/comp_algorithm 2>/dev/null");
    // Parse format like "[lzo] lz4 lz4hc zstd"
    std::string current;
    for (size_t i = 0; i < result.size(); i++) {
        char c = result[i];
        if (c == '[' || c == ']') continue;
        if (c == ' ' || c == '\n' || c == '\t') {
            if (!current.empty()) {
                algos.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        algos.push_back(current);
    }
    return algos;
}

void GuiArkOS4CloneSettings::toggleZram(bool enable, const std::string& size,
                                         const std::string& compAlgo)
{
    if (enable) {
        // Disable first if already enabled
        executeCommand("sudo swapoff /dev/zram0 2>/dev/null || true");
        // Reset zram
        executeCommand("echo 1 | sudo tee /sys/block/zram0/reset >/dev/null 2>&1");

        // Set compression algorithm (must be set after reset, before disksize)
        executeCommand("echo " + compAlgo + " | sudo tee /sys/block/zram0/comp_algorithm >/dev/null 2>&1");

        // Convert size string (e.g., "512M") to bytes
        long bytes = 536870912; // default 512M
        if (size == "128M") bytes = 134217728;
        else if (size == "256M") bytes = 268435456;
        else if (size == "512M") bytes = 536870912;
        else if (size == "1024M") bytes = 1073741824;

        // Set size in bytes
        executeCommand("echo " + std::to_string(bytes) + " | sudo tee /sys/block/zram0/disksize >/dev/null 2>&1");
        // Create swap and enable
        executeCommand("sudo mkswap /dev/zram0 >/dev/null 2>&1");
        executeCommand("sudo swapon -p 5 /dev/zram0 >/dev/null 2>&1");
    } else {
        executeCommand("sudo swapoff /dev/zram0 2>/dev/null || true");
        executeCommand("echo 1 | sudo tee /sys/block/zram0/reset >/dev/null 2>&1 || true");
    }
}

void GuiArkOS4CloneSettings::saveZramConfig(const std::string& size, const std::string& compAlgo)
{
    long bytes = 536870912;
    if (size == "128M") bytes = 134217728;
    else if (size == "256M") bytes = 268435456;
    else if (size == "512M") bytes = 536870912;
    else if (size == "1024M") bytes = 1073741824;

    std::string cmd = "echo -e 'ENABLED=1\\nALGORITHM=" + compAlgo +
                      "\\nSIZE=" + std::to_string(bytes) +
                      "' | sudo tee /etc/zram.conf >/dev/null 2>&1";
    executeCommand(cmd);
}

bool GuiArkOS4CloneSettings::isZramAutoStart()
{
    std::string result = executeCommand("systemctl is-enabled zram-swap.service 2>/dev/null");
    result.erase(std::remove_if(result.begin(), result.end(), ::isspace), result.end());
    return result == "enabled";
}

void GuiArkOS4CloneSettings::toggleZramAutoStart(bool enable, const std::string& size,
                                                  const std::string& compAlgo)
{
    if (enable) {
        saveZramConfig(size, compAlgo);
        executeCommand("sudo systemctl enable zram-swap.service 2>/dev/null || true");
    } else {
        executeCommand("sudo systemctl disable zram-swap.service 2>/dev/null || true");
    }
}

void GuiArkOS4CloneSettings::openZramSettings()
{
    auto s = new GuiSettings(mWindow, _("ZRAM SETTINGS"));

    // ZRAM Enable/Disable
    bool zramEnabled = isZramEnabled();
    auto zramSwitch = std::make_shared<SwitchComponent>(mWindow);
    zramSwitch->setState(zramEnabled);
    s->addWithLabel(_("ZRAM ENABLE"), zramSwitch);

    // ZRAM Compression Algorithm
    auto algoList = std::make_shared<OptionListComponent<std::string>>(mWindow, _("COMP ALGO"), false);
    std::vector<std::string> algos = getAvailableZramAlgorithms();
    std::string currentAlgo = getZramCompAlgorithm();
    if (algos.empty()) {
        algos.push_back("lz4");
    }
    bool algoFound = false;
    for (const auto& a : algos) {
        if (a == currentAlgo) algoFound = true;
    }
    if (!algoFound) currentAlgo = "lz4";
    for (const auto& a : algos) {
        algoList->add(a, a, a == currentAlgo);
    }
    s->addWithLabel(_("ZRAM COMP ALGO"), algoList);

    // ZRAM Size options
    auto sizeList = std::make_shared<OptionListComponent<std::string>>(mWindow, _("SIZE"), false);
    std::vector<std::string> sizes = {"128M", "256M", "512M", "1024M"};
    std::string currentSize = getZramSize();
    bool found = false;
    for (const auto& size : sizes) {
        if (size == currentSize) found = true;
    }
    if (!found) currentSize = "512M";
    for (const auto& size : sizes) {
        sizeList->add(size, size, size == currentSize);
    }
    s->addWithLabel(_("ZRAM SIZE"), sizeList);

    // Auto Start
    bool autoStart = isZramAutoStart();
    auto autoStartSwitch = std::make_shared<SwitchComponent>(mWindow);
    autoStartSwitch->setState(autoStart);
    s->addWithLabel(_("ZRAM AUTO START"), autoStartSwitch);

    // Enable/Disable callback
    zramSwitch->setOnChangedCallback([this, zramSwitch, sizeList, algoList, autoStartSwitch] {
        std::string selectedSize = sizeList->getSelected();
        if (selectedSize.empty()) selectedSize = "512M";
        std::string selectedAlgo = algoList->getSelected();
        if (selectedAlgo.empty()) selectedAlgo = "lz4";
        toggleZram(zramSwitch->getState(), selectedSize, selectedAlgo);
        if (autoStartSwitch->getState()) {
            saveZramConfig(selectedSize, selectedAlgo);
        }
    });

    // Compression algorithm change callback
    algoList->setSelectedChangedCallback([this, zramSwitch, sizeList, autoStartSwitch](const std::string& val) {
        if (zramSwitch->getState()) {
            std::string selectedSize = sizeList->getSelected();
            if (selectedSize.empty()) selectedSize = "512M";
            toggleZram(false);
            toggleZram(true, selectedSize, val);
        }
        if (autoStartSwitch->getState()) {
            std::string selectedSize = sizeList->getSelected();
            if (selectedSize.empty()) selectedSize = "512M";
            saveZramConfig(selectedSize, val);
        }
    });

    // Size change callback
    sizeList->setSelectedChangedCallback([this, zramSwitch, algoList, autoStartSwitch](const std::string& val) {
        if (zramSwitch->getState()) {
            std::string selectedAlgo = algoList->getSelected();
            if (selectedAlgo.empty()) selectedAlgo = "lz4";
            toggleZram(false);
            toggleZram(true, val, selectedAlgo);
        }
        if (autoStartSwitch->getState()) {
            std::string selectedAlgo = algoList->getSelected();
            if (selectedAlgo.empty()) selectedAlgo = "lz4";
            saveZramConfig(val, selectedAlgo);
        }
    });

    // Auto Start toggle callback
    autoStartSwitch->setOnChangedCallback([this, zramSwitch, sizeList, algoList, autoStartSwitch] {
        std::string selectedSize = sizeList->getSelected();
        if (selectedSize.empty()) selectedSize = "512M";
        std::string selectedAlgo = algoList->getSelected();
        if (selectedAlgo.empty()) selectedAlgo = "lz4";
        toggleZramAutoStart(autoStartSwitch->getState(), selectedSize, selectedAlgo);
    });

    mWindow->pushGui(s);
}
// Remote Services Auto-Start
// ============================================================================

bool GuiArkOS4CloneSettings::isRemoteServicesAutoStart()
{
    std::string result = executeCommand("systemctl is-enabled ssh.service 2>/dev/null");
    result.erase(std::remove_if(result.begin(), result.end(), ::isspace), result.end());
    return result.find("enabled") != std::string::npos;
}

void GuiArkOS4CloneSettings::toggleRemoteServicesAutoStart(bool enable)
{
    if (enable) {
        executeCommand("sudo systemctl enable ssh.service 2>/dev/null || true");
        executeCommand("sudo systemctl enable smbd 2>/dev/null || true");
        executeCommand("sudo systemctl enable nmbd 2>/dev/null || true");
    } else {
        executeCommand("sudo systemctl disable ssh.service 2>/dev/null || true");
        executeCommand("sudo systemctl disable smbd 2>/dev/null || true");
        executeCommand("sudo systemctl disable nmbd 2>/dev/null || true");
    }
}

// ============================================================================
// View Info Functions
// ============================================================================

std::string GuiArkOS4CloneSettings::getSdCardName(const std::string& device)
{
    std::string name = executeCommand("cat /sys/block/" + device + "/device/name 2>/dev/null");
    name.erase(std::remove_if(name.begin(), name.end(), ::isspace), name.end());
    if (name.empty()) {
        name = executeCommand("cat /sys/block/" + device + "/device/cid 2>/dev/null | cut -c1-8");
        name.erase(std::remove_if(name.begin(), name.end(), ::isspace), name.end());
    }
    return name.empty() ? _("UNKNOWN") : name;
}

std::string GuiArkOS4CloneSettings::getSdCardSpeed(const std::string& device)
{
    // Map device name to dmesg host name (mmcblk0 -> mmc0, mmcblk1 -> mmc1)
    std::string hostNum = (device == "mmcblk0") ? "mmc0" : "mmc1";
    
    // Parse existing kernel dmesg output
    // Format: "mmc0: new high speed SDXC card at address 0001"
    // Format: "mmc0: new ultra high speed SDR104 SDHC card at address 0001"
    std::string dmesgLine = executeCommand("dmesg | grep '" + hostNum + ": new' | tail -1");
    
    if (!dmesgLine.empty()) {
        // Check for UHS (ultra high speed)
        if (dmesgLine.find("ultra high speed") != std::string::npos) {
            // UHS Speed Grade: U3 (SDR104), U1 (SDR50/DDR50), Class 10 (SDR25/SDR12)
            if (dmesgLine.find("SDR104") != std::string::npos) return "UHS-I U3 (104MB/s)";
            if (dmesgLine.find("SDR50") != std::string::npos) return "UHS-I U1 (50MB/s)";
            if (dmesgLine.find("DDR50") != std::string::npos) return "UHS-I U1 DDR (50MB/s)";
            if (dmesgLine.find("SDR25") != std::string::npos) return "UHS-I Class 10 (25MB/s)";
            if (dmesgLine.find("SDR12") != std::string::npos) return "UHS-I Class 4 (12MB/s)";
            return "UHS-I";
        }
        // Check for High Speed
        if (dmesgLine.find("high speed") != std::string::npos) {
            return "Class 10 (25MB/s)";
        }
    }
    
    return _("N/A");
}

std::string GuiArkOS4CloneSettings::getCpuBinning()
{
    // Try to get CPU binning info from dmesg (added by rockchip-cpufreq.c)
    // Format: es_info: cpu_bin=X process=X scale=X volt_sel=X
    // volt_sel is the actual quality grade based on CPU leakage:
    // - lower volt_sel = lower leakage = better quality = can run at lower voltage
    // - higher volt_sel = higher leakage = worse quality = needs higher voltage
    std::string dmesgBin = executeCommand("dmesg | grep 'es_info: cpu_bin=' | tail -1");
    
    if (!dmesgBin.empty()) {
        // Parse the volt_sel value (actual quality indicator)
        std::string voltSel = executeCommand("echo '" + dmesgBin + "' | sed 's/.*volt_sel=\\(-*[0-9]*\\).*/\\1/'");
        voltSel.erase(std::remove_if(voltSel.begin(), voltSel.end(), ::isspace), voltSel.end());
        
        int voltVal = atoi(voltSel.c_str());
        
        // Rockchip CPU quality grades based on volt_sel:
        // volt_sel=0: L0 最佳体质 - lowest leakage, can run at lowest voltage
        // volt_sel=1: L1 良好体质 - good quality
        // volt_sel=2: L2 标准体质 - standard quality
        // volt_sel=3+: L3+ 一般体质 - higher leakage, needs more voltage
        // negative value: N/A - not detected
        
        if (voltVal < 0) return "N/A";
        if (voltVal == 0) return "L0 (" + std::string(_("BEST")) + ")";
        if (voltVal == 1) return "L1 (" + std::string(_("GOOD")) + ")";
        if (voltVal == 2) return "L2 (" + std::string(_("STANDARD")) + ")";
        if (voltVal == 3) return "L3 (" + std::string(_("AVERAGE")) + ")";
        
        return "L" + std::to_string(voltVal) + " (" + std::string(_("AVERAGE")) + ")";
    }
    
    return "N/A";
}

std::string GuiArkOS4CloneSettings::getCpuTemp()
{
    std::string temp = executeCommand("cat /sys/class/thermal/thermal_zone0/temp 2>/dev/null");
    temp.erase(std::remove_if(temp.begin(), temp.end(), ::isspace), temp.end());
    
    if (!temp.empty()) {
        // Convert millidegree to degree
        int tempVal = atoi(temp.c_str());
        if (tempVal > 1000) {
            tempVal = tempVal / 1000;
        }
        return std::to_string(tempVal) + "°C";
    }
    
    return _("N/A");
}

void GuiArkOS4CloneSettings::openViewInfo()
{
    auto s = new GuiSettings(mWindow, _("VIEW INFO"));
    
    // Check for SD card devices
    std::string sd1Exists = executeCommand("ls /dev/mmcblk0 2>/dev/null");
    std::string sd2Exists = executeCommand("ls /dev/mmcblk1 2>/dev/null");
    
    bool hasSd1 = !sd1Exists.empty();
    bool hasSd2 = !sd2Exists.empty();
    
    // SD Card 1 Info
    if (hasSd1) {
        std::string sd1Name = getSdCardName("mmcblk0");
        std::string sd1Size = executeCommand("cat /sys/block/mmcblk0/size 2>/dev/null | awk '{printf \"%.1fGB\", $1/2048/1024}'");
        sd1Size.erase(std::remove_if(sd1Size.begin(), sd1Size.end(), ::isspace), sd1Size.end());
        
        auto sd1Text = std::make_shared<TextComponent>(mWindow, 
            sd1Name + " (" + sd1Size + ")", 
            Font::get(FONT_SIZE_SMALL), 0x777777FF);
        s->addWithLabel(_("SD CARD 1"), sd1Text);
        
        auto sd1SpeedText = std::make_shared<TextComponent>(mWindow, 
            getSdCardSpeed("mmcblk0"), 
            Font::get(FONT_SIZE_SMALL), 0x777777FF);
        s->addWithLabel(_("SPEED"), sd1SpeedText);
    }
    
    // SD Card 2 Info
    if (hasSd2) {
        std::string sd2Name = getSdCardName("mmcblk1");
        std::string sd2Size = executeCommand("cat /sys/block/mmcblk1/size 2>/dev/null | awk '{printf \"%.1fGB\", $1/2048/1024}'");
        sd2Size.erase(std::remove_if(sd2Size.begin(), sd2Size.end(), ::isspace), sd2Size.end());
        
        auto sd2Text = std::make_shared<TextComponent>(mWindow, 
            sd2Name + " (" + sd2Size + ")", 
            Font::get(FONT_SIZE_SMALL), 0x777777FF);
        s->addWithLabel(_("SD CARD 2"), sd2Text);
        
        auto sd2SpeedText = std::make_shared<TextComponent>(mWindow, 
            getSdCardSpeed("mmcblk1"), 
            Font::get(FONT_SIZE_SMALL), 0x777777FF);
        s->addWithLabel(_("SPEED"), sd2SpeedText);
    }
    
    // Hardware Name - trim only leading/trailing whitespace, keep internal spaces
    std::string hardwareName = executeCommand("grep 'Hardware' /proc/cpuinfo 2>/dev/null | awk -F': ' '{print $2}'");
    // Trim leading whitespace
    size_t start = hardwareName.find_first_not_of(" \t\n\r");
    if (start != std::string::npos) {
        // Trim trailing whitespace
        size_t end = hardwareName.find_last_not_of(" \t\n\r");
        hardwareName = hardwareName.substr(start, end - start + 1);
    } else {
        hardwareName.clear();
    }
    if (!hardwareName.empty()) {
        auto hardwareText = std::make_shared<TextComponent>(mWindow, 
            hardwareName, 
            Font::get(FONT_SIZE_SMALL), 0x777777FF);
        s->addWithLabel(_("DEVICE"), hardwareText);
    }
    
    // CPU Binning (体制)
    std::string cpuBinning = getCpuBinning();
    auto cpuText = std::make_shared<TextComponent>(mWindow, 
        cpuBinning, 
        Font::get(FONT_SIZE_SMALL), 0x777777FF);
    s->addWithLabel(_("CPU GRADE"), cpuText);
    
    // CPU Temperature
    auto cpuTempText = std::make_shared<TextComponent>(mWindow, 
        getCpuTemp(), 
        Font::get(FONT_SIZE_SMALL), 0x777777FF);
    s->addWithLabel(_("CPU TEMP"), cpuTempText);
    
    mWindow->pushGui(s);
}
