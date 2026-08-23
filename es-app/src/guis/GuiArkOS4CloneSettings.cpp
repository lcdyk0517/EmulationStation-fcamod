#include "guis/GuiArkOS4CloneSettings.h"
#include "guis/GuiMsgBox.h"
#include "guis/GuiTextEditPopupKeyboard.h"
#include "guis/GuiSettings.h"
#include "guis/GuiDetectDevice.h"
#include "guis/GuiJoystickCalibration.h"
#include "guis/arkos4clone/ArkOSUtil.h"
#include "guis/arkos4clone/WifiManager.h"
#include "guis/arkos4clone/LedControl.h"
#include "guis/arkos4clone/HardwareInfo.h"
#include "guis/arkos4clone/SystemSettings.h"
#include "guis/arkos4clone/BatteryPlus.h"
#include "components/SliderComponent.h"
#include "components/OptionListComponent.h"
#include "components/SwitchComponent.h"
#include "components/BusyComponent.h"
#include "components/TextComponent.h"
#include "components/BatteryIndicatorComponent.h"
#include "Window.h"
#include "ApiSystem.h"
#include "SystemConf.h"
#include "Settings.h"
#include "Log.h"
#include "AudioManager.h"
#include "VolumeControl.h"
#include "platform.h"
#include "utils/StringUtil.h"
#include "utils/FileSystemUtil.h"

#include <fstream>
#include <thread>
#include <regex>
#include <chrono>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <map>

using namespace ArkOSUtil;
using namespace WifiManager;
using namespace LedControl;
using namespace HardwareInfo;
using namespace SystemSettings;

// ============================================================================
// Constructor / Destructor
// ============================================================================

GuiArkOS4CloneSettings::GuiArkOS4CloneSettings(Window* window)
    : GuiComponent(window), mMenu(window, Utils::String::toUpper(getSystemName()) + " SETTINGS")
{
    addChild(&mMenu);

    // Wi-Fi Settings submenu
    mMenu.addEntry(_("WIFI SETTINGS"), true, [this] {
        openWifiSettings();
    }, "iconWifi");

    // Joystick Settings submenu
    mMenu.addEntry(_("JOYSTICK SETTINGS"), true, [this] {
        openJoystickSettings();
    }, "");

    // ArkOS4Clone Tools submenu (CPU/GPU/DMC/ZRAM settings)
    if (hasGpuFreqControl() || hasDmcFreqControl() || getCpuCoreCount() > 1) {
        mMenu.addEntry(_("TOOLS"), true, [this] {
            openToolsMenu();
        }, "");
    }

    // Power LED Settings submenu (only for supported devices)
    if (hasPowerLed()) {
        mMenu.addEntry(_("POWER LED"), true, [this] {
            openPowerLedSettings();
        }, "");
    }

    // USB Switch (manual USB switch only)
    if (isUsbManualSwitch()) {
        mMenu.addEntry(_("USB SWITCH"), true, [this] {
            openUsbSwitchSettings();
        }, "");
    }

    // BatteryPlus Settings
    if (BatteryPlus::isAvailable()) {
        mMenu.addEntry(_("BATTERYPLUS"), true, [this] {
            openBatteryPlusSettings();
        }, "");
    }

    // Configure Input
    mMenu.addEntry(_("CONFIGURE INPUT"), true, [this] {
        mWaitingInputConfigInfo = true;
        mInputConfigTimer = 5000;
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

void GuiArkOS4CloneSettings::pushSettingsMenu(GuiSettings* s)
{
    s->setSize((float)Renderer::getScreenWidth(), (float)Renderer::getScreenHeight());
    if (Renderer::isSmallScreen())
        s->setPosition((Renderer::getScreenWidth() - s->getSize().x()) / 2, (Renderer::getScreenHeight() - s->getSize().y()) / 2);
    else
        s->setPosition((mSize.x() - s->getSize().x()) / 2, Renderer::getScreenHeight() * 0.15f);
    mWindow->pushGui(s);
}

void GuiArkOS4CloneSettings::addFreqSettings(GuiSettings* s, const std::string& logPrefix,
                                             const std::string& label, const std::vector<std::string>& freqs,
                                             const std::string& currentFreq, int divisor,
                                             const std::function<void(const std::string&)>& setter)
{
    if (freqs.empty()) return;

    LOG(LogDebug) << logPrefix << " currentFreq: '" << currentFreq << "'";

    auto freqList = std::make_shared<OptionListComponent<std::string>>(mWindow, _("MAX FREQ"), false);
    bool found = false;
    for (const auto& freq : freqs) {
        int mhz = atoi(freq.c_str()) / divisor;
        bool isSelected = (freq == currentFreq);
        LOG(LogDebug) << logPrefix << " freq option: '" << freq << "' selected: " << isSelected;
        if (isSelected) found = true;
        freqList->add(std::to_string(mhz) + " MHz", freq, isSelected);
    }
    if (!found) freqList->selectFirstItem();

    s->addWithLabel(label, freqList);
    freqList->setSelectedChangedCallback([setter](const std::string& val) {
        setter(val);
    });
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

    // Status Icons toggle (WiFi/Bluetooth icons + es-status-daemon)
    bool statusIconsEnabled = Settings::getInstance()->getBool("networkIcon") || Settings::getInstance()->getBool("bluetoothIcon");
    auto statusIconsSwitch = std::make_shared<SwitchComponent>(mWindow);
    statusIconsSwitch->setState(statusIconsEnabled);
    s->addWithLabel(_("SHOW STATUS ICONS"), statusIconsSwitch);
    s->addSaveFunc([this, s, statusIconsSwitch] {
        bool enable = statusIconsSwitch->getState();
        bool changed = false;
        changed |= Settings::getInstance()->setBool("networkIcon", enable);
        changed |= Settings::getInstance()->setBool("bluetoothIcon", enable);

        if (enable) {
            executeCommand("sudo systemctl start es-status-daemon.service 2>/dev/null");
            executeCommand("sudo systemctl enable es-status-daemon.service 2>/dev/null");
        } else {
            executeCommand("sudo systemctl stop es-status-daemon.service 2>/dev/null");
            executeCommand("sudo systemctl disable es-status-daemon.service 2>/dev/null");
        }

        if (changed) {
            s->setVariable("reloadAll", true);
        }
    });

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

    s->addEntry(_("PROXY SETTINGS"), true, [this] {
        openProxySettings();
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
        executeCommand("nmcli con down " + shellQuote(curSsid) + " 2>/dev/null");
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    std::string result = executeCommand("nmcli con up " + shellQuote(connName) + " 2>&1");

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
                    executeCommand("sudo rm -f " + shellQuote("/etc/NetworkManager/system-connections/" + connName + ".nmconnection"));
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

    executeCommand("nmcli con delete " + shellQuote(ssid) + " 2>/dev/null");

    std::string result;
    if (password.empty()) {
        result = executeCommand("nmcli device wifi connect " + shellQuote(ssid) + " 2>&1");
    } else {
        result = executeCommand("nmcli device wifi connect " + shellQuote(ssid) + " password " + shellQuote(password) + " 2>&1");
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
        executeCommand("sudo rm -f " + shellQuote("/etc/NetworkManager/system-connections/" + ssid + ".nmconnection") + " 2>/dev/null");
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
// Joystick Settings
// ============================================================================

void GuiArkOS4CloneSettings::openJoystickSettings()
{
    GuiSettings* s = new GuiSettings(mWindow, _("JOYSTICK SETTINGS"));

    // Joystick LED (only for supported devices)
    if (!detectLedType().empty()) {
        s->addEntry(_("JOYSTICK LED"), true, [this] {
            openJoystickLedSettings();
        }, "");
    }

    // Joystick Dead Zone (only for supported devices)
    if (hasAdcDeadZoneSupport()) {
        s->addEntry(_("JOYSTICK DEAD ZONE"), true, [this] {
            openDeadZoneSettings();
        }, "");
    }

    // Joystick Calibration
    s->addEntry(_("JOYSTICK CALIBRATION"), true, [this] {
        Window* window = mWindow;
        window->pushGui(new GuiJoystickCalibration(window));
    }, "");

    // Analog Right Stick Emulation (only for supported devices)
    int switchKey = getStickSwitchKey();
    if (switchKey != 0) {
        bool isEnabled = Settings::getInstance()->getBool("StickSwitchEnabled");

        auto analogSwitch = std::make_shared<SwitchComponent>(mWindow);
        analogSwitch->setState(isEnabled);
        analogSwitch->setOnChangedCallback([this, analogSwitch] {
            bool enabled = analogSwitch->getState();
            if (enabled) {
                int savedKey = Settings::getInstance()->getInt("StickSwitchKey");
                if (savedKey <= 0 || savedKey == 999) savedKey = 304;
                setStickSwitchKey(savedKey);
                Settings::getInstance()->setInt("StickSwitchKey", savedKey);
                Settings::getInstance()->setBool("StickSwitchEnabled", true);
            } else {
                setStickSwitchKey(999);
                Settings::getInstance()->setBool("StickSwitchEnabled", false);
            }
            Settings::getInstance()->saveFile();
        });
        s->addWithLabel(_("ANALOG STICK EMULATION"), analogSwitch);

        if (isEnabled) {
            s->addEntry(_("CHANGE EMULATION BUTTON"), true, [this, s] {
                mWindow->pushGui(new GuiMsgBox(mWindow,
                    _("CHANGE THE BUTTON FOR ANALOG STICK EMULATION?"),
                    _("YES"), [this, s] {
                        mWaitingStickSwitchInput = true;
                        delete s;
                    },
                    _("NO"), nullptr));
            }, "");
        }
    }

    pushSettingsMenu(s);
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

    // Special handling for single-gpio: one switch for joystick LED
    if (ledType == "single-gpio") {
        // Read current LED state
        bool on = false;
        if (Utils::FileSystem::exists(SINGLE_GPIO_LED)) {
            std::string val = executeCommand("cat " + SINGLE_GPIO_LED);
            on = (atoi(val.c_str()) > 0);
        }

        // Joystick LED switch
        auto ledSwitch = std::make_shared<SwitchComponent>(mWindow);
        ledSwitch->setState(on);
        s->addWithLabel(_("JOYSTICK LED"), ledSwitch);

        // Apply immediately when switch changes
        ledSwitch->setOnChangedCallback([this, ledSwitch]() {
            applySingleGpioLed(ledSwitch->getState());
            Settings::getInstance()->setBool("JoyLedOn", ledSwitch->getState());
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
    if (mWaitingStickSwitchInput && input.value != 0) {
        int linuxKeycode = input.id;
        if (input.type == TYPE_BUTTON && input.id >= 0 && input.id < SDL_TO_LINUX_KEYCODE_COUNT) {
            linuxKeycode = SDL_TO_LINUX_KEYCODE[input.id];
        }

        setStickSwitchKey(linuxKeycode);
        Settings::getInstance()->setInt("StickSwitchKey", linuxKeycode);
        Settings::getInstance()->setBool("StickSwitchEnabled", true);
        Settings::getInstance()->saveFile();
        mWaitingStickSwitchInput = false;

        std::string msg = std::string(_("EMULATION BUTTON SET TO")) + " " + keycodeToName(linuxKeycode);
        mWindow->pushGui(new GuiMsgBox(mWindow, msg, _("OK")));
        return true;
    }

    if (input.value != 0 && config->isMappedTo(BUTTON_BACK, input)) {
        delete this;
        return true;
    }
    return GuiComponent::input(config, input);
}

void GuiArkOS4CloneSettings::update(int deltaTime)
{
    if (mInputConfigTimer > 0) {
        mInputConfigTimer -= deltaTime;
        if (mInputConfigTimer <= 0) {
            mWaitingInputConfigInfo = false;
            Window* window = mWindow;
            window->pushGui(new GuiMsgBox(window, _("DO YOU WANT TO CONTINUE?"),
                _("YES"), [window] {
                    window->pushGui(new GuiDetectDevice(window, false, nullptr));
                }, _("NO"), nullptr));
        }
    }
}

void GuiArkOS4CloneSettings::render(const Transform4x4f& parentTrans)
{
    GuiComponent::render(parentTrans);

    if (mWaitingInputConfigInfo) {
        Renderer::setMatrix(Transform4x4f::Identity());
        Renderer::drawRect(0.0f, 0.0f, Renderer::getScreenWidth(), Renderer::getScreenHeight(), 0x000000CC);
        auto theme = ThemeData::getMenuTheme();
        auto font = theme->TextSmall.font;
        std::string line1 = _("THIS OPTION IS FOR CONFIGURING EXTERNAL CONTROLLERS");
        std::string line2 = _("NOT REMAPPING THE BUILT-IN CONTROLS");
        std::string line3 = std::to_string((mInputConfigTimer + 999) / 1000) + "s";
        auto size1 = font->sizeText(line1);
        auto size2 = font->sizeText(line2);
        auto size3 = font->sizeText(line3);
        float totalH = size1.y() + size2.y() + size3.y() + 20;
        float y1 = (Renderer::getScreenHeight() - totalH) / 2;
        float y2 = y1 + size1.y() + 10;
        float y3 = y2 + size2.y() + 10;
        auto c1 = font->buildTextCache(line1, Vector2f((Renderer::getScreenWidth() - size1.x()) / 2, y1), 0xFFFFFFFF, size1.x());
        font->renderTextCache(c1); delete c1;
        auto c2 = font->buildTextCache(line2, Vector2f((Renderer::getScreenWidth() - size2.x()) / 2, y2), 0xAAAAAAFF, size2.x());
        font->renderTextCache(c2); delete c2;
        auto c3 = font->buildTextCache(line3, Vector2f((Renderer::getScreenWidth() - size3.x()) / 2, y3), 0xFFFFFFFF, size3.x());
        font->renderTextCache(c3); delete c3;
    }

    if (mWaitingStickSwitchInput) {
        Renderer::setMatrix(Transform4x4f::Identity());
        Renderer::drawRect(0.0f, 0.0f, Renderer::getScreenWidth(), Renderer::getScreenHeight(), 0x000000CC);
        auto theme = ThemeData::getMenuTheme();
        auto font = theme->TextSmall.font;
        std::string line1 = _("PRESS THE BUTTON TO MAP");
        std::string line2 = _("PRESS BACK TO CANCEL");
        auto size1 = font->sizeText(line1);
        auto size2 = font->sizeText(line2);
        float totalH = size1.y() + size2.y() + 10;
        float y1 = (Renderer::getScreenHeight() - totalH) / 2;
        float y2 = y1 + size1.y() + 10;
        auto c1 = font->buildTextCache(line1, Vector2f((Renderer::getScreenWidth() - size1.x()) / 2, y1), 0xFFFFFFFF, size1.x());
        font->renderTextCache(c1); delete c1;
        auto c2 = font->buildTextCache(line2, Vector2f((Renderer::getScreenWidth() - size2.x()) / 2, y2), 0xAAAAAAFF, size2.x());
        font->renderTextCache(c2); delete c2;
    }
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
    auto s = new GuiSettings(mWindow, _("TOOLS"));

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
        addFreqSettings(s, "CPU", _("CPU MAX FREQ"), freqs, getCpuMaxFreq(), 1000,
                        [this](const std::string& val) { setCpuMaxFreq(val); });
    }

    mWindow->pushGui(s);
}

// ============================================================================
// GPU Settings
// ============================================================================

void GuiArkOS4CloneSettings::openGpuSettings()
{
    auto s = new GuiSettings(mWindow, _("GPU SETTINGS"));

    auto freqs = getGpuAvailableFreqs();
    if (!freqs.empty()) {
        addFreqSettings(s, "GPU", _("GPU MAX FREQ"), freqs, getGpuMaxFreq(), 1000000,
                        [this](const std::string& val) { setGpuMaxFreq(val); });
    }

    mWindow->pushGui(s);
}

// ============================================================================
// DMC Settings
// ============================================================================

void GuiArkOS4CloneSettings::openDmcSettings()
{
    auto s = new GuiSettings(mWindow, _("DMC SETTINGS"));

    auto freqs = getDmcAvailableFreqs();
    if (!freqs.empty()) {
        addFreqSettings(s, "DMC", _("DMC MAX FREQ"), freqs, getDmcMaxFreq(), 1000000,
                        [this](const std::string& val) { setDmcMaxFreq(val); });
    }

    mWindow->pushGui(s);
}

// ============================================================================
// ZRAM Settings
// ============================================================================

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

// ============================================================================
// BatteryPlus Settings
// ============================================================================

void GuiArkOS4CloneSettings::openBatteryPlusSettings()
{
    auto s = new GuiSettings(mWindow, _("BATTERYPLUS"));

    // Battery info display
    std::string percent = BatteryPlus::getPercent();
    std::string status = BatteryPlus::getChargeStatus();
    int voltage = BatteryPlus::getVoltageMv();
    std::string mode = BatteryPlus::getMode();

    std::string info = "Percent: " + percent + "  Status: " + status;
    if (voltage > 0)
        info += "  Voltage: " + std::to_string(voltage) + "mV";
    info += "  Mode: " + mode;
    s->addWithLabel(_("BATTERY INFO"), std::make_shared<TextComponent>(mWindow, info, Font::get(FONT_SIZE_SMALL), 0x777777FF));

    // Enable/Disable toggle
    bool enabled = BatteryPlus::isEnabled();
    auto enableSwitch = std::make_shared<SwitchComponent>(mWindow);
    enableSwitch->setState(enabled);
    s->addWithLabel(_("ENABLE BATTERYPLUS"), enableSwitch);

    // Mode selection (voltage/pmic)
    auto modeList = std::make_shared<OptionListComponent<std::string>>(mWindow, _("BATTERY MODE"), false);
    modeList->add(_("VOLTAGE MODE"), "voltage", mode == "voltage");
    modeList->add(_("PMIC MODE"), "pmic", mode == "pmic");
    s->addWithLabel(_("BATTERY MODE"), modeList);

    // Enable/Disable callback
    enableSwitch->setOnChangedCallback([enableSwitch] {
        BatteryPlus::setEnabled(enableSwitch->getState());
    });

    // Mode change callback
    modeList->setSelectedChangedCallback([](const std::string& val) {
        BatteryPlus::setMode(val);
    });

    // Delete records button
    s->addEntry(_("DELETE USAGE RECORDS"), true, [this] {
        Window* window = mWindow;
        window->pushGui(new GuiMsgBox(window,
            _("DELETE ALL BATTERY USAGE RECORDS? THIS WILL RESET CALIBRATION."),
            _("YES"), [window] {
                BatteryPlus::deleteRecords();
                window->pushGui(new GuiMsgBox(window, _("RECORDS DELETED. SERVICE RESTARTED."), _("OK")));
            },
            _("NO"), nullptr));
    }, "");

    mWindow->pushGui(s);
}

// ============================================================================
// Proxy Settings Functions
// ============================================================================

void GuiArkOS4CloneSettings::openProxySettings()
{
    createProxySettingsMenu();
}

void GuiArkOS4CloneSettings::createProxySettingsMenu()
{
    auto s = new GuiSettings(mWindow, _("PROXY SETTINGS"));
    auto settings = Settings::getInstance();

    // Enable Proxy toggle
    auto proxySwitch = std::make_shared<SwitchComponent>(mWindow);
    proxySwitch->setState(settings->getBool("ProxyEnabled"));
    proxySwitch->setOnChangedCallback([proxySwitch] {
        Settings::getInstance()->setBool("ProxyEnabled", proxySwitch->getState());
    });
    s->addWithLabel(_("ENABLE PROXY"), proxySwitch);

    // Proxy Type selection (HTTP/SOCKS5)
    std::string currentType = settings->getString("ProxyType");
    if (currentType.empty()) currentType = "http";
    auto proxyType = std::make_shared<OptionListComponent<std::string>>(mWindow, _("PROXY TYPE"), false);
    proxyType->add(_("HTTP"), "http", currentType == "http");
    proxyType->add(_("SOCKS5"), "socks5", currentType == "socks5");
    s->addWithLabel(_("PROXY TYPE"), proxyType);

    // Proxy Host
    std::string currentHost = settings->getString("ProxyHost");
    if (currentHost.empty()) currentHost = "192.168.31.237";
    s->addEntry(_("PROXY HOST") + ": " + currentHost, true, [this, s] {
        mWindow->pushGui(new GuiTextEditPopupKeyboard(mWindow,
            _("PROXY HOST"),
            Settings::getInstance()->getString("ProxyHost"),
            [](const std::string& newVal) {
                Settings::getInstance()->setString("ProxyHost", newVal);
            }, false));
    });

    // Proxy Port
    std::string currentPort = settings->getString("ProxyPort");
    if (currentPort.empty()) currentPort = "10808";
    s->addEntry(_("PROXY PORT") + ": " + currentPort, true, [this, s] {
        mWindow->pushGui(new GuiTextEditPopupKeyboard(mWindow,
            _("PROXY PORT"),
            Settings::getInstance()->getString("ProxyPort"),
            [](const std::string& newVal) {
                Settings::getInstance()->setString("ProxyPort", newVal);
            }, false));
    });

    // No Proxy
    std::string currentNoProxy = settings->getString("ProxyNoProxy");
    if (currentNoProxy.empty()) currentNoProxy = "localhost,127.0.0.1,::1";
    s->addEntry(_("NO PROXY") + ": " + currentNoProxy, true, [this, s] {
        mWindow->pushGui(new GuiTextEditPopupKeyboard(mWindow,
            _("NO PROXY"),
            Settings::getInstance()->getString("ProxyNoProxy"),
            [](const std::string& newVal) {
                Settings::getInstance()->setString("ProxyNoProxy", newVal);
            }, false));
    });

    s->addSaveFunc([settings, proxySwitch, proxyType] {
        std::string newType = proxyType->getSelected();
        if (newType.empty()) newType = "http";
        settings->setString("ProxyType", newType);
        settings->saveFile();
    });

    mWindow->pushGui(s);
}

// ============================================================================
// View Info Functions
// ============================================================================

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

// ============================================================================
// ADC Dead Zone Functions
// ============================================================================

void GuiArkOS4CloneSettings::openDeadZoneSettings()
{
    GuiSettings* s = new GuiSettings(mWindow, _("JOYSTICK DEAD ZONE"));

    int currentValue = getAdcDeadZone();
    int savedValue = Settings::getInstance()->getInt("AdcDeadZone");
    if (savedValue < 10 || savedValue > 1800) savedValue = currentValue;

    // Preset values
    auto deadZoneOptions = std::make_shared<OptionListComponent<std::string>>(mWindow, _("DEAD ZONE VALUE"), false);

    std::vector<int> presets = {32, 64, 128, 256, 384, 512, 768};
    bool foundPreset = false;
    for (int preset : presets) {
        std::string label = std::to_string(preset);
        bool selected = (savedValue == preset);
        if (selected) foundPreset = true;
        deadZoneOptions->add(label, label, selected);
    }

    // Custom option
    if (!foundPreset && savedValue >= 10 && savedValue <= 1800) {
        deadZoneOptions->add(_("CUSTOM") + " (" + std::to_string(savedValue) + ")", std::to_string(savedValue), true);
    }

    s->addWithLabel(_("DEAD ZONE VALUE"), deadZoneOptions);

    deadZoneOptions->setSelectedChangedCallback([this](const std::string& selectedValue) {
        int value = std::stoi(selectedValue);
        setAdcDeadZone(value);
        Settings::getInstance()->setInt("AdcDeadZone", value);
        Settings::getInstance()->saveFile();
    });

    // Custom input button
    s->addEntry(_("CUSTOM DEAD ZONE"), true, [this, s] {
        mWindow->pushGui(new GuiTextEditPopupKeyboard(mWindow,
            _("ENTER DEAD ZONE VALUE (10-1800)"),
            "",
            [this](const std::string& valueStr) {
                try {
                    int value = std::stoi(valueStr);
                    if (value >= 10 && value <= 1800) {
                        setAdcDeadZone(value);
                        Settings::getInstance()->setInt("AdcDeadZone", value);
                        Settings::getInstance()->saveFile();
                        mWindow->pushGui(new GuiMsgBox(mWindow,
                            _("DEAD ZONE SET TO") + " " + std::to_string(value),
                            _("OK")));
                    } else {
                        mWindow->pushGui(new GuiMsgBox(mWindow,
                            _("VALUE OUT OF RANGE"),
                            _("OK")));
                    }
                } catch (...) {
                    mWindow->pushGui(new GuiMsgBox(mWindow,
                        _("INVALID VALUE"),
                        _("OK")));
                }
            },
            false, _("OK")));
    }, "");

    pushSettingsMenu(s);
}
