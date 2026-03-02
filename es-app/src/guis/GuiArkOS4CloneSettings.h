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

private:
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
    
    MenuComponent mMenu;
    std::vector<std::pair<std::string, int>> mWifiNetworks; // ssid, signal
    std::shared_ptr<TextComponent> mWifiStatusText;
};

#endif // ES_APP_GUIGUIARKOS4CLONESETTINGS_H