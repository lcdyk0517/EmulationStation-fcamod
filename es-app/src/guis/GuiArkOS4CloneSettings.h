#pragma once
#ifndef ES_APP_GUIGUIARKOS4CLONESETTINGS_H
#define ES_APP_GUIGUIARKOS4CLONESETTINGS_H

#include "GuiComponent.h"
#include "components/MenuComponent.h"
#include "components/ComponentList.h"
#include <string>
#include <vector>
#include <functional>

class GuiSettings;

class GuiArkOS4CloneSettings : public GuiComponent
{
public:
    GuiArkOS4CloneSettings(Window* window);
    ~GuiArkOS4CloneSettings() override;

    bool input(InputConfig* config, Input input) override;
    void update(int deltaTime) override;
    void render(const Transform4x4f& parentTrans) override;
    std::vector<HelpPrompt> getHelpPrompts() override;

private:
    // Position and push a GuiSettings submenu using the standard layout
    void pushSettingsMenu(GuiSettings* s);

    // Add a frequency option list (values converted to MHz for display)
    void addFreqSettings(GuiSettings* s, const std::string& logPrefix,
                         const std::string& label, const std::vector<std::string>& freqs,
                         const std::string& currentFreq, int divisor,
                         const std::function<void(const std::string&)>& setter);
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

    // Proxy Settings functions
    void openProxySettings();
    void createProxySettingsMenu();

    // ArkOS4Clone Tools functions
    void openToolsMenu();
    void openCpuSettings();
    void openGpuSettings();
    void openDmcSettings();
    void openZramSettings();
    void openBatteryPlusSettings();

    // Joystick Settings submenu
    void openJoystickSettings();
    void openJoystickLedSettings();

    // ADC Dead Zone functions
    void openDeadZoneSettings();

    // USB Switch functions (manual USB switch only)
    void openUsbSwitchSettings();

    // Power LED functions
    void openPowerLedSettings();

    // Date & Time functions
    void openDateTimeSettings();

    // View Info functions
    void openViewInfo();

    MenuComponent mMenu;
    std::vector<std::pair<std::string, int>> mWifiNetworks; // ssid, signal
    std::shared_ptr<TextComponent> mWifiStatusText;
    std::shared_ptr<TextComponent> mIpAddressText;
    bool mWaitingStickSwitchInput = false;
    bool mWaitingInputConfigInfo = false;
    int mInputConfigTimer = 0;
};

#endif // ES_APP_GUIGUIARKOS4CLONESETTINGS_H
