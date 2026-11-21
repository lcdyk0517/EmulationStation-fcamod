#include <string>
#pragma once
#ifndef ES_APP_GUIS_GUI_MENU_H
#define ES_APP_GUIS_GUI_MENU_H

#include "components/MenuComponent.h"
#include "components/OptionListComponent.h"
#include "GuiComponent.h"
#include "guis/arkos4clone/SdCardControl.h"

class GuiSettings;
class SystemData;

class GuiMenu : public GuiComponent
{
public:
	GuiMenu(Window* window, bool animate = true);

	bool input(InputConfig* config, Input input) override;
	void onSizeChanged() override;
	std::vector<HelpPrompt> getHelpPrompts() override;
	HelpStyle getHelpStyle() override;

	static void openThemeConfiguration(Window* mWindow, GuiComponent* s, std::shared_ptr<OptionListComponent<std::string>> theme_set, const std::string systemTheme = "");

private:
	void addEntry(std::string name, bool add_arrow, const std::function<void()>& func, const std::string iconName = "");	

	void addVersionInfo();
	void openCollectionSystemSettings();
	void openConfigInput();
	void openOtherSettings();
	void openQuitMenu();
	void openScraperSettings();
	void openScreensaverOptions();
	void openSoundSettings();
	void openUISettings();

	static void reloadAllGames(Window* window, bool deleteCurrentGui = false);

	void openUpdateSettings();
	void openEmulatorSettings();
	void openSystemEmulatorSettings(SystemData* system);

	void createInputTextRow(GuiSettings *gui, std::string title, const char *settingsID, bool password);
	void openDisplaySettings();
	void openDArkOSSettings();
	void openWifiSettings();
	void openJoystickLedSettings();
	void openFanSettings();
	void openPowerLedSettings();
	void openBatteryPlusSettings();
	void openJoystickSettings();
	void openGlobalHotkeys();
	void openInputControl();
	void openPerformanceSettingsGlobal();
	void openLoaderManagement();
	void openZramSettings();
	void openSdCardSettings();
	void doSdCardSwitch(SdCardControl::RomsMode mode);

	MenuComponent mMenu;
	TextComponent mVersion;

};

#endif // ES_APP_GUIS_GUI_MENU_H
