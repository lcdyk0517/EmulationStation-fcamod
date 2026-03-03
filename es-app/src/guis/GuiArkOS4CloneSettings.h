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
    
    // Joystick LED functions
    void openJoystickLedSettings();
    static std::string detectLedType();
    static std::string getDeviceName();
    static std::string getCurrentLedColor();
    static void applyLedColor(const std::string& color, const std::string& brightness = "");
    static void applyMcuLed(const std::string& color);
    static void applyGpioLed(const std::string& color);
    static void applyWs2812Led(const std::string& color, const std::string& brightness = "HIGH");
    static void saveLedConfig(const std::string& color, const std::string& brightness = "");
    static std::vector<std::pair<std::string, std::string>> getLedMenuItems(const std::string& ledType);
    
    MenuComponent mMenu;
    std::vector<std::pair<std::string, int>> mWifiNetworks; // ssid, signal
    std::shared_ptr<TextComponent> mWifiStatusText;
    std::shared_ptr<TextComponent> mIpAddressText;
};

#endif // ES_APP_GUIGUIARKOS4CLONESETTINGS_H