#pragma once
#ifndef ES_APP_GUIGUIARKOS4CLONESETTINGS_H
#define ES_APP_GUIGUIARKOS4CLONESETTINGS_H

#include "GuiComponent.h"
#include "components/MenuComponent.h"
#include "components/ComponentList.h"
#include <string>
#include <vector>
#include <map>

class GuiArkOS4CloneSettings : public GuiComponent
{
public:
    GuiArkOS4CloneSettings(Window* window);
    ~GuiArkOS4CloneSettings() override;

    bool input(InputConfig* config, Input input) override;
    void render(const Transform4x4f& parentTrans) override;
    std::vector<HelpPrompt> getHelpPrompts() override;
    
    // Static function for startup - public for main.cpp access
    static bool checkAndApplyLedOnStartup();
    static void applyPowerLedOnStartup();
    static void applyPowerLed();

private:
    // WiFi functions
    void openWifiSettings();
    void createWifiSettingsMenu();
    void scanWifi();
    void showWifiPasswordInput(const std::string& ssid);
    void connectWifi(const std::string& ssid, const std::string& password);
    void activateExistingConnection();
    void activateConnection(const std::string& connName);
    void deleteConnections();
    void showNetworkInfo();
    void updateWifiStatusText();
    
    // Remote Services functions
    static bool isRemoteServicesEnabled();
    static std::string getIpAddress();
    static void toggleRemoteServices(bool enable);
    static bool isRemoteServicesAutoStart();
    static void toggleRemoteServicesAutoStart(bool enable);
    
    // Hotspot functions
    void showHotspotSettings();
    static std::string getHotspotSsid();
    static bool isHotspotSupported();
    static bool isHotspotEnabled();
    static void toggleHotspot(bool enable, const std::string& ssid = "", const std::string& password = "");
    
    // ArkOS4Clone Tools functions
    void openToolsMenu();
    void openCpuSettings();
    void openGpuSettings();
    void openDmcSettings();
    void openZramSettings();
    
    // CPU helpers
    static int getCpuCoreCount();
    static int getOnlineCpuCount();
    static std::string getCpuGovernor();
    static void setCpuGovernor(const std::string& governor);
    static std::string getCpuMaxFreq();
    static void setCpuMaxFreq(const std::string& freq);
    static std::vector<std::string> getCpuAvailableFreqs();
    static std::vector<std::string> getAvailableGovernors();
    static void setCpuCores(int count);
    
    // GPU helpers
    static bool hasGpuFreqControl();
    static std::string getGpuDevPath();
    static std::string getGpuMaxFreq();
    static void setGpuMaxFreq(const std::string& freq);
    static std::vector<std::string> getGpuAvailableFreqs();
    
    // DMC helpers
    static bool hasDmcFreqControl();
    static std::string getDmcMaxFreq();
    static void setDmcMaxFreq(const std::string& freq);
    static std::vector<std::string> getDmcAvailableFreqs();
    
    // ZRAM helpers
    static std::string getZramSize();
    static bool isZramEnabled();
    static void toggleZram(bool enable, const std::string& size = "512M");
    
    // Joystick LED functions
    void openJoystickLedSettings();
    static std::string detectLedType();
    static std::string getDeviceName();
    static std::string getCurrentLedColor();
    static void applyLedColor(const std::string& color, const std::string& brightness = "");
    static void applyMcuLed(const std::string& color);
    static void applyGpioLed(const std::string& color);
    static void applyWs2812Led(const std::string& color, const std::string& brightness = "HIGH");
    static void applyDualGpioLed(bool leftOn, bool rightOn);
    static void applyR36UltraV2Led(const std::string& color);
    static bool hasDualGpioLed();
    static void saveLedConfig(const std::string& color, const std::string& brightness = "");
    static std::vector<std::pair<std::string, std::string>> getLedMenuItems(const std::string& ledType);
    
    // USB Switch functions (R36Max2 only)
    void openUsbSwitchSettings();
    static bool isR36Max2();
    static bool isUsbInternal();
    static void setUsbInternal(bool internal);
    
    // Power LED functions
    void openPowerLedSettings();
    static bool hasPowerLed();
    static bool hasPowerLedRed();
    static bool hasPowerLedBlue();
    static bool hasArkOS4CloneLed();
    
    // Date & Time functions
    void openDateTimeSettings();
    static std::string getCurrentDateTime();
    static bool setSystemTime(int year, int month, int day, int hour, int minute);
    static bool syncNetworkTime();
    
    MenuComponent mMenu;
    std::vector<std::pair<std::string, int>> mWifiNetworks; // ssid, signal
    std::shared_ptr<TextComponent> mWifiStatusText;
    std::shared_ptr<TextComponent> mIpAddressText;
};

#endif // ES_APP_GUIGUIARKOS4CLONESETTINGS_H