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
#include <chrono>

// Helper function to execute command and get output
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

// Helper function to get current WiFi SSID using nmcli
static std::string getCurrentWifiSSID()
{
    // Method 1: nmcli active connection
    std::string result = executeCommand("nmcli -t -f NAME,DEVICE connection show --active 2>/dev/null");
    if (!result.empty()) {
        // Format: SSID:wlan0 or just SSID if connected
        std::istringstream stream(result);
        std::string line;
        while (std::getline(stream, line)) {
            if (line.find(":wlan") != std::string::npos || line.find("wlan") != std::string::npos) {
                // Extract SSID (before the colon or the whole line)
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
        // Format: ssid MyNetwork
        size_t pos = ssid.find("ssid ");
        if (pos != std::string::npos) {
            ssid = ssid.substr(pos + 5);
            if (!ssid.empty() && ssid != "off/any") return ssid;
        }
    }
    
    return "";
}

GuiArkOS4CloneSettings::GuiArkOS4CloneSettings(Window* window)
    : GuiComponent(window), mMenu(window, _("ARKOS4CLONE SETTINGS"))
{
    addChild(&mMenu);

    // Wi-Fi Settings submenu
    mMenu.addEntry(_("WIFI SETTINGS"), true, [this] {
        openWifiSettings();
    }, "iconWifi");

    mMenu.addButton(_("BACK"), "back", [this] {
        delete this;
    });

    setSize((float)Renderer::getScreenWidth(), (float)Renderer::getScreenHeight());
}

GuiArkOS4CloneSettings::~GuiArkOS4CloneSettings()
{
}

void GuiArkOS4CloneSettings::openWifiSettings()
{
    // Create a new WiFi settings menu that can be refreshed
    createWifiSettingsMenu();
}

void GuiArkOS4CloneSettings::createWifiSettingsMenu()
{
    auto s = new GuiSettings(mWindow, _("WIFI SETTINGS"));

    // Current WiFi Status - display only, not clickable
    std::string wifiStatus = getCurrentWifiSSID();
    if (wifiStatus.empty()) {
        wifiStatus = _("NOT CONNECTED");
    }
    // Save status text component for later update
    mWifiStatusText = std::make_shared<TextComponent>(mWindow, wifiStatus, ThemeData::getMenuTheme()->TextSmall.font, ThemeData::getMenuTheme()->TextSmall.color);
    s->addWithLabel(_("CURRENT NETWORK"), mWifiStatusText);

    // Scan WiFi Networks
    s->addEntry(_("SCAN WIFI NETWORKS"), true, [this] {
        scanWifi();
    }, "");

    // Activate existing connection
    s->addEntry(_("ACTIVATE EXISTING CONNECTION"), true, [this] {
        activateExistingConnection();
    }, "");

    // Delete existing connections
    s->addEntry(_("DELETE EXISTING CONNECTIONS"), true, [this] {
        deleteConnections();
    }, "");

    // Network Info
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

void GuiArkOS4CloneSettings::scanWifi()
{
    // Show busy dialog
    auto busy = new GuiComponent(mWindow);
    auto busyComp = new BusyComponent(mWindow);
    busy->addChild(busyComp);
    busyComp->setText(_("SCANNING WIFI NETWORKS"));
    busy->setSize((float)Renderer::getScreenWidth(), (float)Renderer::getScreenHeight());
    mWindow->pushGui(busy);

    // Scan using nmcli
    mWifiNetworks.clear();
    
    // Rescan first
    system("sudo nmcli device wifi rescan 2>/dev/null");
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // Get list
    std::string clist = executeCommand("sudo nmcli -t -f IN-USE,SSID,SIGNAL dev wifi 2>/dev/null");
    
    // Parse output
    std::istringstream stream(clist);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        
        // Parse: IN-USE:SSID:SIGNAL
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

    // Remove busy dialog
    mWindow->removeGui(busy);
    delete busy;

    // Show results
    if (mWifiNetworks.empty()) {
        mWindow->pushGui(new GuiMsgBox(mWindow, _("NO WIFI NETWORKS FOUND"), _("OK")));
        return;
    }

    // Create network selection menu
    auto s = new GuiSettings(mWindow, _("SELECT WIFI NETWORK"));

    // Sort by signal strength (descending)
    std::sort(mWifiNetworks.begin(), mWifiNetworks.end(),
        [](const std::pair<std::string, int>& a, const std::pair<std::string, int>& b) {
            return a.second > b.second;
        });

    // Remove duplicates (keep strongest)
    std::map<std::string, int> uniqueNetworks;
    for (auto& net : mWifiNetworks) {
        if (uniqueNetworks.find(net.first) == uniqueNetworks.end() || uniqueNetworks[net.first] < net.second) {
            uniqueNetworks[net.first] = net.second;
        }
    }

    // Add networks to menu with simple signal display
    for (auto& net : uniqueNetworks) {
        if (net.first.empty()) continue;
        
        // Simple signal display without unicode characters
        std::string signalStr = std::to_string(net.second) + "%";
        std::string entryName = net.first + " (" + signalStr + ")";
        
        // Capture SSID by value
        std::string ssid = net.first;
        s->addEntry(entryName, true, [this, ssid] {
            showWifiPasswordInput(ssid);
        }, "");
    }

    mWindow->pushGui(s);
}

void GuiArkOS4CloneSettings::activateExistingConnection()
{
    // Get existing connections
    std::string conns = executeCommand("ls -1 /etc/NetworkManager/system-connections/ 2>/dev/null | sed 's/\\.nmconnection$//'");
    
    if (conns.empty()) {
        mWindow->pushGui(new GuiMsgBox(mWindow, _("NO SAVED CONNECTIONS"), _("OK")));
        return;
    }

    // Get current connection
    std::string curSsid = getCurrentWifiSSID();

    auto s = new GuiSettings(mWindow, _("SELECT CONNECTION"));

    std::istringstream stream(conns);
    std::string conn;
    while (std::getline(stream, conn)) {
        if (conn.empty()) continue;
        
        std::string connName = conn;
        std::string displayName = connName;
        
        // Mark current connection
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
    // Show busy
    auto busy = new GuiComponent(mWindow);
    auto busyComp = new BusyComponent(mWindow);
    busy->addChild(busyComp);
    busyComp->setText(_("CONNECTING..."));
    busy->setSize((float)Renderer::getScreenWidth(), (float)Renderer::getScreenHeight());
    mWindow->pushGui(busy);

    // Disconnect current
    std::string curSsid = getCurrentWifiSSID();
    if (!curSsid.empty() && curSsid != connName) {
        executeCommand("nmcli con down \"" + curSsid + "\" 2>/dev/null");
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    
    // Connect to new
    std::string result = executeCommand("nmcli con up \"" + connName + "\" 2>&1");
    
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Remove busy
    mWindow->removeGui(busy);
    delete busy;

    std::string newSsid = getCurrentWifiSSID();
    if (newSsid == connName) {
        updateWifiStatusText();
        mWindow->pushGui(new GuiMsgBox(mWindow, 
            _("CONNECTED TO") + "\n" + connName, 
            _("OK")));
    } else {
        // Update status to show current state (previous network or not connected)
        updateWifiStatusText();
        mWindow->pushGui(new GuiMsgBox(mWindow, 
            _("CONNECTION FAILED") + "\n" + result, 
            _("OK")));
    }
}

void GuiArkOS4CloneSettings::deleteConnections()
{
    // Get existing connections
    std::string conns = executeCommand("ls -1 /etc/NetworkManager/system-connections/ 2>/dev/null | sed 's/\\.nmconnection$//'");
    
    if (conns.empty()) {
        mWindow->pushGui(new GuiMsgBox(mWindow, _("NO SAVED CONNECTIONS"), _("OK")));
        return;
    }

    // Get current connection
    std::string curSsid = getCurrentWifiSSID();

    auto s = new GuiSettings(mWindow, _("DELETE CONNECTION"));

    std::istringstream stream(conns);
    std::string conn;
    while (std::getline(stream, conn)) {
        if (conn.empty()) continue;
        
        std::string connName = conn;
        std::string displayName = connName;
        
        // Mark current connection
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
    // Use GuiTextEditPopupKeyboard for virtual keyboard support
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
    // Show busy dialog
    auto busy = new GuiComponent(mWindow);
    auto busyComp = new BusyComponent(mWindow);
    busy->addChild(busyComp);
    busyComp->setText(_("CONNECTING TO") + " " + ssid + "...");
    busy->setSize((float)Renderer::getScreenWidth(), (float)Renderer::getScreenHeight());
    mWindow->pushGui(busy);

    // Delete existing connection with same name first
    executeCommand("nmcli con delete \"" + ssid + "\" 2>/dev/null");
    
    // Connect using nmcli
    std::string result;
    if (password.empty()) {
        result = executeCommand("nmcli device wifi connect \"" + ssid + "\" 2>&1");
    } else {
        result = executeCommand("nmcli device wifi connect \"" + ssid + "\" password \"" + password + "\" 2>&1");
    }

    // Wait for connection
    std::this_thread::sleep_for(std::chrono::seconds(3));

    // Remove busy dialog
    mWindow->removeGui(busy);
    delete busy;

    // Check connection using nmcli status
    std::string status = executeCommand("nmcli -t -f DEVICE,STATE dev 2>/dev/null | grep wlan");
    bool connected = (status.find(":connected") != std::string::npos);
    
    // Also verify SSID
    std::string connectedSSID = getCurrentWifiSSID();
    if (connectedSSID.empty()) {
        // Try alternative check
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
        // Clean up failed connection
        executeCommand("sudo rm -f \"/etc/NetworkManager/system-connections/" + ssid + ".nmconnection\" 2>/dev/null");
        
        // Update status to show current state (previous network or not connected)
        updateWifiStatusText();
        
        std::string errorMsg = _("CONNECTION FAILED");
        if (result.find("Secrets were required") != std::string::npos) {
            errorMsg += "\n" + _("INVALID PASSWORD");
        } else if (result.find("not found") != std::string::npos || result.find("No network") != std::string::npos) {
            errorMsg += "\n" + _("NETWORK NOT FOUND");
        } else if (!result.empty()) {
            // Show raw error for debugging
            errorMsg += "\n" + result;
        }
        mWindow->pushGui(new GuiMsgBox(mWindow, errorMsg, _("OK")));
    }
}

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