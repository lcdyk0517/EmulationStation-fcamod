#include <string>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <cstdlib>
#include <sstream>
#include "guis/GuiMenu.h"
#include "guis/GuiTools.h"
#include "components/OptionListComponent.h"
#include "components/SliderComponent.h"
#include "components/SwitchComponent.h"
#include "guis/GuiCollectionSystemsOptions.h"
#include "guis/GuiDetectDevice.h"
#include "guis/GuiGeneralScreensaverOptions.h"
#include "guis/GuiMsgBox.h"
#include "guis/GuiWifi.h"
#include "guis/GuiJoystickCalibration.h"
#include "guis/arkos4clone/HardwareInfo.h"
#include "guis/GuiScraperStart.h"
#include "guis/GuiSettings.h"
#include "views/UIModeController.h"
#include "views/ViewController.h"
#include "CollectionSystemManager.h"
#include "EmulationStation.h"
#include "Scripting.h"
#include "SystemData.h"
#include "VolumeControl.h"
#include <SDL_events.h>
#include <algorithm>
#include "AudioManager.h"
#include "resources/TextureData.h"
#include "animations/LambdaAnimation.h"
#include "guis/GuiThemeInstall.h"
#include "GuiGamelistOptions.h" // grid sizes
#include "platform.h"
#include "renderers/Renderer.h" // setSwapInterval()
#include "guis/GuiTextEditPopupKeyboard.h"
#include "scrapers/ThreadedScraper.h"
#include "ApiSystem.h"
#include "views/gamelist/IGameListView.h"
#include "components/BatteryIndicatorComponent.h"

//#include <go2/display.h>
#include "SystemConf.h"

namespace
{
std::string readTextFile(const char* path)
{
	std::ifstream file(path);
	if (!file.is_open())
		return "";

	std::stringstream ss;
	ss << file.rdbuf();
	return ss.str();
}
}  // namespace

GuiMenu::GuiMenu(Window* window, bool animate) : GuiComponent(window), mMenu(window, _("MAIN MENU")), mVersion(window)
{
	// dArkOS3562 设置: KODI / 显示设置与信息 / 设定输入 集中到二级菜单
	addEntry(_("dArkOS3562 SETTINGS"), true, [this] { openDArkOSSettings(); }, "iconSystem");

	auto theme = ThemeData::getMenuTheme();

	bool isFullUI = UIModeController::getInstance()->isUIModeFull();	
	
	if (isFullUI)
	{
		addEntry(_("UI SETTINGS"), true, [this] { openUISettings(); }, "iconUI");
	}

	addEntry(_("SOUND SETTINGS"), true, [this] { openSoundSettings(); }, "iconSound");

	if (isFullUI)
	{
		addEntry(_("GAME COLLECTION SETTINGS"), true, [this] { openCollectionSystemSettings(); }, "iconGames");

		// Emulator settings 已移到各机种的 SELECT 菜单 (GuiGamelistOptions)
		/*
		for (auto system : SystemData::sSystemVector)
		{
			if (system->isCollection() || system->getSystemEnvData()->mEmulators.size() == 0 || (system->getSystemEnvData()->mEmulators.size() == 1 && system->getSystemEnvData()->mEmulators[0].mCores.size() <= 1))
				continue;

			addEntry(_("EMULATOR SETTINGS"), true, [this] { openEmulatorSettings(); }, "iconSystem");
			break;
		}
		*/
		
		addEntry(_("SCRAPER"), true, [this] { openScraperSettings(); }, "iconScraper");

#if WIN32
		addEntry(_("DOWNLOADS AND UPDATES"), true, [this] { openUpdateSettings(); }, "iconUpdates");
#endif

    // Tools Menu
    mMenu.addEntry(_("OPTIONS"), true, [this, window] {
        window->pushGui(new GuiTools(window));
    }, "iconOptions");

		addEntry(_("ADVANCED SETTINGS"), true, [this] { openOtherSettings(); }, "iconAdvanced");
	}
	
	addEntry(_("QUIT"), !Settings::getInstance()->getBool("ShowOnlyExit"), [this] {openQuitMenu(); }, "iconQuit");

    if (Utils::FileSystem::exists("/opt/system/Bluetooth.sh") && !std::string(getShOutput(R"(cat /sys/class/graphics/fb0/modes | grep -o -P '(?<=:).*(?=p-)' | cut -dx -f1 | grep 720)")).empty())
     {
	    addEntry(_("BAT") + ": " + std::string(getShOutput(R"(cat /sys/class/power_supply/battery/capacity)")) + "%" + " | " + _("SND") + ": " + std::string(getShOutput(R"(current_volume)")) + " | " + _("BRT") + ": " + std::to_string(ApiSystem::getInstance()->getBrightnessLevel()) + "% | " + _("BT") + ": " + std::string(getShOutput(R"(if [ -z $(systemctl status bluetooth | grep "enabled;") ]; then echo "Off"; else echo "On"; fi)")), false, [this] {  });
        addEntry(_("WIFI") + ": " + std::string(getShOutput(R"(if [ -z $(rfkill -n -o TYPE,SOFT | grep wlan | grep -w unblocked) ]; then echo "Off"; else cat /sys/class/net/wlan0/operstate; fi)")), false, [this] {  });
     } else if (Utils::FileSystem::exists("/opt/system/Bluetooth.sh")) {
	    addEntry(_("BAT") + ": " + std::string(getShOutput(R"(if [ -f /tmp/battery.percent ]; then cat /tmp/battery.percent; else cat /sys/class/power_supply/battery/capacity; fi)")) + "%" + " | " + _("SND") + ": " + std::string(getShOutput(R"(current_volume)")) + " | " + _("BRT") + ": " + std::to_string(ApiSystem::getInstance()->getBrightnessLevel()) + "% | " + _("BT") + ": " + std::string(getShOutput(R"(if [ -z $(systemctl status bluetooth | grep "enabled;") ]; then echo "Off"; else echo "On"; fi)")) + " | " + _("WIFI") + ": " + std::string(getShOutput(R"(if [ -z $(rfkill -n -o TYPE,SOFT | grep wlan | grep -w unblocked) ]; then echo "Off"; else cat /sys/class/net/wlan0/operstate; fi)")), false, [this] {  });
     } else {
	    addEntry(_("BAT") + ": " + std::string(getShOutput(R"(if [ -f /tmp/battery.percent ]; then cat /tmp/battery.percent; else cat /sys/class/power_supply/battery/capacity; fi)")) + "%" + " | " + _("SND") + ": " + std::string(getShOutput(R"(current_volume)")) + " | " + _("BRT") + ": " + std::to_string(ApiSystem::getInstance()->getBrightnessLevel()) + "% | " + _("WIFI") + ": " + std::string(getShOutput(R"(if [[ -z $(rfkill -n -o TYPE,SOFT | grep wlan | grep -w unblocked) ]]; then echo "Off"; else cat /sys/class/net/wlan0/operstate; fi)")), false, [this] {  });
     }

    if (!std::string(getShOutput(R"(rfkill -n -o TYPE,SOFT | grep wlan | grep -w unblocked)")).empty() && std::string(getShOutput(R"(cat /sys/class/graphics/fb0/modes | grep -o -P '(?<=:).*(?=p-)' | cut -dx -f1 | grep 720)")).empty()) {
	    addEntry(_("SSID") + ": " + std::string(getShOutput(R"(iw dev wlan0 info | grep ssid | cut -c 7-30)")), false, [this] {  });
	    addEntry(_("Distro Version") + ": " + std::string(getShOutput(R"(cat /usr/share/plymouth/themes/text.plymouth | grep title | cut -c 7-50)")) + " | " + _("IP") + ": " + std::string(getShOutput(R"(ip -f inet addr show wlan0 | sed -En -e 's/.*inet ([0-9.]+).*/\1/p')")), false, [this] {  });
	} else if (!std::string(getShOutput(R"(rfkill -n -o TYPE,SOFT | grep wlan | grep -w unblocked)")).empty()) {
	    addEntry(_("SSID") + ": " + std::string(getShOutput(R"(iw dev wlan0 info | grep ssid | cut -c 7-30)")), false, [this] {  });
	    addEntry(_("IP") + ": " + std::string(getShOutput(R"(ip -f inet addr show wlan0 | sed -En -e 's/.*inet ([0-9.]+).*/\1/p')")), false, [this] {  });
	    addEntry(_("Distro Version") + ": " + std::string(getShOutput(R"(cat /usr/share/plymouth/themes/text.plymouth | grep title | cut -c 7-50)")), false, [this] {  });
	} else {
	    addEntry(_("Distro Version") + ": " + std::string(getShOutput(R"(cat /usr/share/plymouth/themes/text.plymouth | grep title | cut -c 7-50)")), false, [this] {  });
	}

	addChild(&mMenu);
	addVersionInfo();

	setSize(mMenu.getSize());

	if (animate)	
		animateTo(
			Vector2f((Renderer::getScreenWidth() - mSize.x()) / 2, Renderer::getScreenHeight() * 0.9),
			Vector2f((Renderer::getScreenWidth() - mSize.x()) / 2, (Renderer::getScreenHeight() - mSize.y()) / 2));
	else
		setPosition((Renderer::getScreenWidth() - mSize.x()) / 2, (Renderer::getScreenHeight() - mSize.y()) / 2);
}

void GuiMenu::openDArkOSSettings()
{
	auto s = new GuiSettings(mWindow, _("dArkOS3562 SETTINGS"));

	// WIFI 二级菜单 (开关 / 网络 / 远程服务), 置顶
	// 硬件能力 (console_detect -s): 摇杆灯控制方式 / 风扇
	const std::string hwInfo = getShOutput("/usr/local/bin/console_detect -s 2>/dev/null");
	const bool hasStickLed = hwInfo.find("LED_TYPE=unsupported") == std::string::npos &&
				 hwInfo.find("LED_TYPE=none") == std::string::npos;
	const bool hasFan = hwInfo.find("FAN=yes") != std::string::npos;

	s->addEntry(_("WIFI"), true, [this] { openWifiSettings(); }, "iconNetwork");

	if (Utils::FileSystem::exists("/var/run/drmConn"))
		s->addEntry(_("DISPLAY SETTINGS"), true, [this] { openDisplaySettings(); }, "iconBrightnessctl");

	if (UIModeController::getInstance()->isUIModeFull())
		s->addEntry(_("CONFIGURE INPUT"), true, [this] { openConfigInput(); }, "iconControllers");

	if (Utils::FileSystem::exists("/sys/devices/platform/play_joystick") ||
	    Utils::FileSystem::exists("/sys/devices/platform/rk3562-joystick"))
		s->addEntry(_("JOYSTICK SETTINGS"), true, [this] { openJoystickSettings(); }, "iconControllers");
	if (hasStickLed)
		s->addEntry(_("JOYSTICK LED"), true, [this] { openJoystickLedSettings(); }, "iconBrightnessctl");
	if (Utils::FileSystem::exists("/sys/devices/platform/power-leds"))
		s->addEntry(_("POWER LED"), true, [this] { openPowerLedSettings(); }, "iconBrightnessctl");
	if (Utils::FileSystem::exists("/usr/local/bin/batteryplus"))
		s->addEntry(_("BATTERYPLUS"), true, [this] { openBatteryPlusSettings(); }, "iconSystem");
	if (hasFan && Utils::FileSystem::exists("/usr/local/bin/fanctl"))
		s->addEntry(_("FAN CONTROL"), true, [this] { openFanSettings(); }, "iconAdvanced");
	if (!HardwareInfo::getCpuAvailableFreqs().empty() ||
	    HardwareInfo::hasGpuFreqControl() || HardwareInfo::hasDmcFreqControl())
		s->addEntry(_("PERFORMANCE SETTINGS"), true, [this] { openPerformanceSettingsGlobal(); }, "iconAdvanced");
	if (Utils::FileSystem::exists("/sys/block/zram0"))
		s->addEntry(_("ZRAM"), true, [this] { openZramSettings(); }, "iconSystem");

	// KODI 放最下面
	if (Settings::getInstance()->getBool("EnableKodi") == 1 && Utils::FileSystem::exists("/usr/local/bin/Kodi.sh"))
	{
		s->addEntry(_("KODI MEDIA CENTER"), true, [this]
		{
			Window* window = mWindow;
			if (!ApiSystem::getInstance()->launchKodi(window))
				LOG(LogWarning) << "Shutdown terminated with non-zero result!";
		}, "iconKodi");
	}

	mWindow->pushGui(s);
}

void GuiMenu::openWifiSettings()
{
	auto s = new GuiSettings(mWindow, _("WIFI"));

	auto wifiSwitch = std::make_shared<SwitchComponent>(mWindow);
	wifiSwitch->setState(getShOutput("nmcli -t -f WIFI radio 2>/dev/null") == "enabled");
	wifiSwitch->setOnChangedCallback([wifiSwitch]
	{
		if (system(wifiSwitch->getState() ? "sudo nmcli radio wifi on" : "sudo nmcli radio wifi off") != 0)
			LOG(LogWarning) << "wifi radio toggle failed";
	});
	s->addWithLabel(_("ENABLE WIFI"), wifiSwitch);

	s->addEntry(_("WIFI LIST"), true, [this] {
		mWindow->pushGui(new GuiWifi(mWindow));
	}, "iconNetwork");

	auto remoteSwitch = std::make_shared<SwitchComponent>(mWindow);
	remoteSwitch->setState(getShOutput("sudo /usr/local/bin/remote-services.sh status 2>/dev/null") == "1");
	remoteSwitch->setOnChangedCallback([remoteSwitch]
	{
		const std::string cmd = std::string("sudo /usr/local/bin/remote-services.sh ") +
			(remoteSwitch->getState() ? "on" : "off");
		if (system(cmd.c_str()) != 0)
			LOG(LogWarning) << "remote services toggle failed";
	});
	s->addWithLabel(_("REMOTE SERVICES"), remoteSwitch);

	mWindow->pushGui(s);
}

void GuiMenu::openJoystickLedSettings()
{
	auto s = new GuiSettings(mWindow, _("JOYSTICK LED"));

	// 两种硬件: mcu_led (UART 字节协议) / ws2812 (SPI, 效果是常驻进程)
	const std::string hwInfo = getShOutput("/usr/local/bin/console_detect -s 2>/dev/null");
	const bool isWs2812 = hwInfo.find("LED_TYPE=ws2812") != std::string::npos;

	int ledEnabled = 0, ledMode = 3, ledBri = 5;
	std::string wsMode = "Joystick", wsBri = "HIGH";

	std::istringstream ledCfg(readTextFile("/home/ark/.config/led.cfg"));
	std::string line;
	while (std::getline(ledCfg, line))
	{
		const auto eq = line.find('=');
		if (eq == std::string::npos)
			continue;

		const std::string key = line.substr(0, eq);
		const std::string value = line.substr(eq + 1);

		if (key == "ENABLED")
			ledEnabled = atoi(value.c_str());
		else if (key == "MODE")
			ledMode = atoi(value.c_str());
		else if (key == "BRI")
			ledBri = atoi(value.c_str());
		else if (key == "WS_MODE" && !value.empty())
			wsMode = value;
		else if (key == "WS_BRI" && !value.empty())
			wsBri = value;
	}

	auto ledSwitch = std::make_shared<SwitchComponent>(mWindow);
	ledSwitch->setState(ledEnabled == 1);
	ledSwitch->setOnChangedCallback([ledSwitch]
	{
		const std::string cmd = std::string("sudo /usr/local/bin/ledctl ") + (ledSwitch->getState() ? "on" : "off");
		if (system(cmd.c_str()) != 0)
			LOG(LogWarning) << "ledctl toggle failed";
	});
	s->addWithLabel(_("LED LIGHTING"), ledSwitch);

	if (isWs2812)
	{
		static const std::pair<const char*, const char*> wsEffects[] = {
			{ "Joystick", "Joystick" },
			{ "Scrolling", "Scrolling" },
			{ "Breathing", "Breathing" },
			{ "Breathing Red", "Breathing_Red" },
			{ "Breathing Green", "Breathing_Green" },
			{ "Breathing Blue", "Breathing_Blue" },
			{ "Breathing Blue Red", "Breathing_Blue_Red" },
			{ "Breathing Green Blue", "Breathing_Green_Blue" },
			{ "Breathing Red Green", "Breathing_Red_Green" },
			{ "Breathing Red Green Blue", "Breathing_Red_Green_Blue" },
			{ "Red Green Blue", "Red_Green_Blue" },
			{ "Blue Red", "Blue_Red" },
			{ "Blue", "Blue" },
			{ "Green Blue", "Green_Blue" },
			{ "Green", "Green" },
			{ "Red Green", "Red_Green" },
			{ "Red", "Red" },
		};

		auto ledEffect = std::make_shared<OptionListComponent<std::string>>(mWindow, _("LED EFFECT"), false);
		bool ledModeFound = false;
		for (const auto& entry : wsEffects)
		{
			ledEffect->add(_(entry.first), entry.second, wsMode == entry.second);
			if (wsMode == entry.second)
				ledModeFound = true;
		}
		if (!ledModeFound)
			ledEffect->selectFirstItem();

		ledEffect->setSelectedChangedCallback([](const std::string& mode)
		{
			const std::string cmd = "sudo /usr/local/bin/ledctl mode " + mode;
			if (system(cmd.c_str()) != 0)
				LOG(LogWarning) << "ledctl mode failed";
		});
		s->addWithLabel(_("LED EFFECT"), ledEffect);

		static const std::pair<const char*, const char*> wsBrightness[] = {
			{ "Low Brightness", "LOW" }, { "Medium Brightness", "MEDIUM" }, { "High Brightness", "HIGH" },
		};

		auto ledBrightness = std::make_shared<OptionListComponent<std::string>>(mWindow, _("LED BRIGHTNESS"), false);
		bool ledBriFound = false;
		for (const auto& entry : wsBrightness)
		{
			ledBrightness->add(_(entry.first), entry.second, wsBri == entry.second);
			if (wsBri == entry.second)
				ledBriFound = true;
		}
		if (!ledBriFound)
			ledBrightness->selectFirstItem();

		ledBrightness->setSelectedChangedCallback([](const std::string& value)
		{
			const std::string cmd = "sudo /usr/local/bin/ledctl bri " + value;
			if (system(cmd.c_str()) != 0)
				LOG(LogWarning) << "ledctl bri failed";
		});
		s->addWithLabel(_("LED BRIGHTNESS"), ledBrightness);
	}
	else
	{
		static const std::pair<const char*, int> ledEffects[] = {
			{ "Red", 3 }, { "Green", 1 }, { "Blue", 2 }, { "Yellow", 5 }, { "Cyan", 4 },
			{ "Magenta", 6 }, { "Rainbow", 7 }, { "Scrolling", 8 }, { "Breathing Red", 19 },
			{ "Breathing Green", 17 }, { "Breathing Blue", 18 }, { "Breathing Rainbow", 23 },
			{ "Breathing Cycle", 24 },
		};

		auto ledEffect = std::make_shared<OptionListComponent<std::string>>(mWindow, _("LED EFFECT"), false);
		bool ledModeFound = false;
		for (const auto& effect : ledEffects)
		{
			ledEffect->add(_(effect.first), std::to_string(effect.second), effect.second == ledMode);
			if (effect.second == ledMode)
				ledModeFound = true;
		}
		if (!ledModeFound)
			ledEffect->selectFirstItem();

		ledEffect->setSelectedChangedCallback([](const std::string& mode)
		{
			const std::string cmd = "sudo /usr/local/bin/ledctl mode " + mode;
			if (system(cmd.c_str()) != 0)
				LOG(LogWarning) << "ledctl mode failed";
		});
		s->addWithLabel(_("LED EFFECT"), ledEffect);

		// 硬件只支持三档亮度: 0=低, 1=中, 2=高
		static const char* ledBrightnessLabels[3] = { "Low Brightness", "Medium Brightness", "High Brightness" };

		auto ledBrightness = std::make_shared<OptionListComponent<std::string>>(mWindow, _("LED BRIGHTNESS"), false);
		bool ledBriFound = false;
		for (int bri = 0; bri <= 2; bri++)
		{
			const std::string value = std::to_string(bri);
			ledBrightness->add(_(ledBrightnessLabels[bri]), value, bri == ledBri);
			if (bri == ledBri)
				ledBriFound = true;
		}
		if (!ledBriFound)
			ledBrightness->selectFirstItem();

		ledBrightness->setSelectedChangedCallback([](const std::string& value)
		{
			const std::string cmd = "sudo /usr/local/bin/ledctl bri " + value;
			if (system(cmd.c_str()) != 0)
				LOG(LogWarning) << "ledctl bri failed";
		});
		s->addWithLabel(_("LED BRIGHTNESS"), ledBrightness);
	}

	mWindow->pushGui(s);
}

void GuiMenu::openZramSettings()
{
	auto s = new GuiSettings(mWindow, _("ZRAM"));

	auto pickSize = [](const std::string& bytes) -> std::string
	{
		if (bytes == "134217728") return "128M";
		if (bytes == "268435456") return "256M";
		if (bytes == "536870912") return "512M";
		return "1024M";
	};

	std::string size = pickSize(HardwareInfo::getZramSize());
	std::string algo = HardwareInfo::getZramCompAlgorithm();
	const bool enabled = HardwareInfo::isZramEnabled();

	auto zramSwitch = std::make_shared<SwitchComponent>(mWindow);
	zramSwitch->setState(enabled);
	s->addWithLabel(_("ZRAM"), zramSwitch);

	auto sizeList = std::make_shared<OptionListComponent<std::string>>(mWindow, _("ZRAM SIZE"), false);
	for (const char* v : { "512M", "1024M", "2048M" })
		sizeList->add(v, v, size == v);
	s->addWithLabel(_("ZRAM SIZE"), sizeList);

	auto algoList = std::make_shared<OptionListComponent<std::string>>(mWindow, _("ZRAM ALGORITHM"), false);
	{
		std::vector<std::string> algos = HardwareInfo::getAvailableZramAlgorithms();
		if (algos.empty())
			algos = { "lz4", "zstd", "lzo-rle", "lzo" };
		bool found = false;
		for (const auto& a : algos)
		{
			algoList->add(a, a, a == algo);
			if (a == algo)
				found = true;
		}
		if (!found)
			algoList->selectFirstItem();
	}
	s->addWithLabel(_("ZRAM ALGORITHM"), algoList);

	auto autoSwitch = std::make_shared<SwitchComponent>(mWindow);
	autoSwitch->setState(HardwareInfo::isZramAutoStart());
	s->addWithLabel(_("ZRAM AUTO START"), autoSwitch);

	zramSwitch->setOnChangedCallback([zramSwitch, sizeList, algoList]
	{
		const bool on = zramSwitch->getState();
		HardwareInfo::toggleZram(on, sizeList->getSelected(), algoList->getSelected());
		HardwareInfo::saveZramConfig(sizeList->getSelected(), algoList->getSelected(), on);
	});

	auto applySizeAlgo = [sizeList, algoList, zramSwitch]
	{
		if (!zramSwitch->getState())
			return;
		HardwareInfo::toggleZram(true, sizeList->getSelected(), algoList->getSelected());
		HardwareInfo::saveZramConfig(sizeList->getSelected(), algoList->getSelected(), true);
	};
	sizeList->setSelectedChangedCallback([applySizeAlgo](const std::string&) { applySizeAlgo(); });
	algoList->setSelectedChangedCallback([applySizeAlgo](const std::string&) { applySizeAlgo(); });

	autoSwitch->setOnChangedCallback([autoSwitch, sizeList, algoList, zramSwitch]
	{
		HardwareInfo::toggleZramAutoStart(autoSwitch->getState(), sizeList->getSelected(),
		                                  algoList->getSelected(), zramSwitch->getState());
	});

	mWindow->pushGui(s);
}

void GuiMenu::openPerformanceSettingsGlobal()
{
	auto s = new GuiSettings(mWindow, _("PERFORMANCE SETTINGS"));


	// CPU 最高频率
	auto cpuFreqs = HardwareInfo::getCpuAvailableFreqs();
	if (!cpuFreqs.empty())
	{
		auto list = std::make_shared<OptionListComponent<std::string>>(mWindow, _("CPU MAX FREQ"), false);
		const std::string current = HardwareInfo::getCpuMaxFreq();

		bool found = false;
		for (const auto& freq : cpuFreqs)
		{
			const std::string label = std::to_string(atoi(freq.c_str()) / 1000) + " MHz";
			list->add(label, freq, freq == current);
			if (freq == current)
				found = true;
		}
		if (!found)
			list->selectFirstItem();

		list->setSelectedChangedCallback([](const std::string& value) {
			HardwareInfo::setCpuMaxFreq(value);
		});
		s->addWithLabel(_("CPU MAX FREQ"), list);
	}

	// CPU 核心数
	const int coreCount = HardwareInfo::getCpuCoreCount();
	if (coreCount > 1)
	{
		auto list = std::make_shared<OptionListComponent<std::string>>(mWindow, _("CPU CORES"), false);
		const int online = HardwareInfo::getOnlineCpuCount();

		for (int i = 1; i <= coreCount; i++)
		{
			const std::string value = std::to_string(i);
			list->add(value, value, i == online);
		}

		list->setSelectedChangedCallback([](const std::string& value) {
			HardwareInfo::setCpuCores(atoi(value.c_str()));
		});
		s->addWithLabel(_("CPU CORES"), list);
	}

	// GPU 最高频率
	if (HardwareInfo::hasGpuFreqControl())
	{
		auto gpuFreqs = HardwareInfo::getGpuAvailableFreqs();
		if (!gpuFreqs.empty())
		{
			auto list = std::make_shared<OptionListComponent<std::string>>(mWindow, _("GPU MAX FREQ"), false);
			const std::string current = HardwareInfo::getGpuMaxFreq();

			bool found = false;
			for (const auto& freq : gpuFreqs)
			{
				const std::string label = std::to_string(atoi(freq.c_str()) / 1000000) + " MHz";
				list->add(label, freq, freq == current);
				if (freq == current)
					found = true;
			}
			if (!found)
				list->selectFirstItem();

			list->setSelectedChangedCallback([](const std::string& value) {
				HardwareInfo::setGpuMaxFreq(value);
			});
			s->addWithLabel(_("GPU MAX FREQ"), list);
		}
	}

	// DMC 最高频率
	if (HardwareInfo::hasDmcFreqControl())
	{
		auto dmcFreqs = HardwareInfo::getDmcAvailableFreqs();
		if (!dmcFreqs.empty())
		{
			auto list = std::make_shared<OptionListComponent<std::string>>(mWindow, _("DMC MAX FREQ"), false);
			const std::string current = HardwareInfo::getDmcMaxFreq();

			bool found = false;
			for (const auto& freq : dmcFreqs)
			{
				const std::string label = std::to_string(atoi(freq.c_str()) / 1000000) + " MHz";
				list->add(label, freq, freq == current);
				if (freq == current)
					found = true;
			}
			if (!found)
				list->selectFirstItem();

			list->setSelectedChangedCallback([](const std::string& value) {
				HardwareInfo::setDmcMaxFreq(value);
			});
			s->addWithLabel(_("DMC MAX FREQ"), list);
		}
	}

	// eMMC loader (RK3562 idblock: DDR 初始化 + SPL) 版本. BootROM 固定从
	// eMMC 读这段, 普通刷机不会更新它; 不是 v1.10 时给一个更新入口,
	// 写引导区有风险, 先弹确认框, 备份由 loader-update 内的
	// write-loader-emmc.sh 自动做到 /home/ark/loader-backup-*.bin
	{
		const std::string loaderVer = getShOutput("sudo -n /usr/local/bin/loader-version 2>/dev/null");
		if (!loaderVer.empty())
		{
			const bool needUpdate = (loaderVer != "v1.10");
			std::string label = std::string(_("EMMC LOADER")) + ": " + loaderVer;
			if (needUpdate)
				label += "  (" + std::string(_("UPDATE")) + ")";

			if (needUpdate)
			{
				s->addEntry(label, true, [this]
				{
					mWindow->pushGui(new GuiMsgBox(mWindow,
						_("Writing the eMMC loader is RISKY: a power loss during the write can brick the device. The current loader is backed up to /home/ark first. Update the eMMC loader to v1.10 now?"),
						_("YES, UPDATE"), [this]
						{
							const std::string out = getShOutput("sudo -n /usr/local/bin/loader-update 2>&1");
							mWindow->pushGui(new GuiMsgBox(mWindow,
								out.empty() ? std::string(_("Done.")) : out,
								_("OK"), nullptr, ICON_INFORMATION));
						},
						_("CANCEL"), nullptr, ICON_WARNING));
				}, "iconAdvanced");
			}
			else
			{
				// 已经是最新版本: 仅展示版本号. 必须给空 lambda 而不是 nullptr ——
				// MenuComponent 对空 std::function 调 func() 会抛 bad_function_call
				// 直接 abort(134), ES 被 systemd 重启.
				s->addEntry(label, false, [] {}, "iconAdvanced");
			}
		}

		// 有备份就给回滚入口 (loader-update / write-loader-emmc.sh 写入前都会留备份)
		const std::string backup = getShOutput("ls -1t /home/ark/loader-backup-*.bin 2>/dev/null | head -n1");
		if (!backup.empty())
		{
			const std::string backupName = Utils::FileSystem::getFileName(backup);
			s->addEntry(std::string(_("RESTORE EMMC LOADER")) + ": " + backupName, true, [this]
			{
				mWindow->pushGui(new GuiMsgBox(mWindow,
					_("Restoring writes the eMMC boot area from the backup. A power loss during the write can brick the device. The current loader is backed up first. Restore now?"),
					_("YES, RESTORE"), [this]
					{
						const std::string out = getShOutput("sudo -n /usr/local/bin/loader-restore 2>&1");
						mWindow->pushGui(new GuiMsgBox(mWindow,
							out.empty() ? std::string(_("Done.")) : out,
							_("OK"), nullptr, ICON_INFORMATION));
					},
					_("CANCEL"), nullptr, ICON_WARNING));
			}, "iconAdvanced");
		}
	}

	mWindow->pushGui(s);
}

void GuiMenu::openJoystickSettings()
{
	auto s = new GuiSettings(mWindow, _("JOYSTICK SETTINGS"));

	const std::string node = Utils::FileSystem::exists("/sys/devices/platform/play_joystick")
		? "/sys/devices/platform/play_joystick"
		: "/sys/devices/platform/rk3562-joystick";

	// 死区: 与 ArkOS 相同, 直接写驱动的 adc_deadzone (mV), 预设 32..768
	int currentDz = Settings::getInstance()->getInt("AdcDeadZone");
	{
		const std::string raw = getShOutput("cat " + node + "/adc_deadzone 2>/dev/null");
		if (!raw.empty())
			currentDz = atoi(raw.c_str());
	}

	auto dzList = std::make_shared<OptionListComponent<std::string>>(mWindow, _("DEAD ZONE VALUE"), false);
	static const int dzPresets[] = { 32, 64, 128, 256, 384, 512, 768 };
	bool dzFound = false;
	if (currentDz == 0)
	{
		dzList->add(_("DEFAULT"), "0", true);
		dzFound = true;
	}
	for (int preset : dzPresets)
	{
		const std::string value = std::to_string(preset);
		const bool selected = currentDz == preset;
		dzList->add(value, value, selected);
		if (selected)
			dzFound = true;
	}
	if (!dzFound)
	{
		const std::string value = std::to_string(currentDz);
		dzList->add(_("CUSTOM") + " (" + value + ")", value, true);
	}

	dzList->setSelectedChangedCallback([node](const std::string& value)
	{
		Settings::getInstance()->setInt("AdcDeadZone", atoi(value.c_str()));
		Settings::getInstance()->saveFile();
		const std::string cmd = "sudo sh -c 'echo " + value + " > " + node + "/adc_deadzone'";
		if (system(cmd.c_str()) != 0)
			LOG(LogWarning) << "adc_deadzone apply failed";
	});
	s->addWithLabel(_("DEAD ZONE VALUE"), dzList);

	s->addEntry(_("CUSTOM DEAD ZONE"), true, [this, node]
	{
		mWindow->pushGui(new GuiTextEditPopupKeyboard(mWindow,
			_("ENTER DEAD ZONE VALUE (10-1800)"), "", [this, node](const std::string& value)
		{
			int dz = -1;
			try { dz = std::stoi(value); } catch (...) { }
			if (dz < 10 || dz > 1800)
			{
				mWindow->pushGui(new GuiMsgBox(mWindow, _("VALUE OUT OF RANGE"), _("OK")));
				return;
			}
			Settings::getInstance()->setInt("AdcDeadZone", dz);
			Settings::getInstance()->saveFile();
			const std::string cmd = "sudo sh -c 'echo " + std::to_string(dz) +
				" > " + node + "/adc_deadzone'";
			system(cmd.c_str());
		}, false, _("OK")));
	}, "");

	// AB / XY 互换
	auto abSwitch = std::make_shared<SwitchComponent>(mWindow);
	abSwitch->setState(Settings::getInstance()->getBool("ButtonSwapAb"));
	abSwitch->setOnChangedCallback([abSwitch, node]
	{
		Settings::getInstance()->setBool("ButtonSwapAb", abSwitch->getState());
		Settings::getInstance()->saveFile();
		const std::string cmd = std::string("sudo sh -c 'echo ") +
			(abSwitch->getState() ? "1" : "0") + " > " + node + "/swap_ab'";
		if (system(cmd.c_str()) != 0)
			LOG(LogWarning) << "swap_ab apply failed";
	});
	s->addWithLabel(_("SWAP A/B"), abSwitch);

	auto xySwitch = std::make_shared<SwitchComponent>(mWindow);
	xySwitch->setState(Settings::getInstance()->getBool("ButtonSwapXy"));
	xySwitch->setOnChangedCallback([xySwitch, node]
	{
		Settings::getInstance()->setBool("ButtonSwapXy", xySwitch->getState());
		Settings::getInstance()->saveFile();
		const std::string cmd = std::string("sudo sh -c 'echo ") +
			(xySwitch->getState() ? "1" : "0") + " > " + node + "/swap_xy'";
		if (system(cmd.c_str()) != 0)
			LOG(LogWarning) << "swap_xy apply failed";
	});
	s->addWithLabel(_("SWAP X/Y"), xySwitch);

	// L2/R2: 模拟轴 (ABS_Z/ABS_RZ) + 数字键双路; 可关掉线性上报只留数字键
	auto trigSwitch = std::make_shared<SwitchComponent>(mWindow);
	trigSwitch->setState(getShOutput("cat " + node + "/linear_triggers 2>/dev/null") != "0");
	trigSwitch->setOnChangedCallback([trigSwitch, node]
	{
		Settings::getInstance()->setBool("LinearTriggers", trigSwitch->getState());
		Settings::getInstance()->saveFile();
		const std::string cmd = std::string("sudo sh -c 'echo ") +
			(trigSwitch->getState() ? "1" : "0") + " > " + node + "/linear_triggers'";
		if (system(cmd.c_str()) != 0)
			LOG(LogWarning) << "linear_triggers apply failed";
	});
	s->addWithLabel(_("LINEAR TRIGGERS"), trigSwitch);

	s->addEntry(_("JOYSTICK CALIBRATION"), true, [this]
	{
		mWindow->pushGui(new GuiJoystickCalibration(mWindow));
	}, "iconControllers");

	mWindow->pushGui(s);
}

void GuiMenu::openBatteryPlusSettings()
{
	auto theme = ThemeData::getMenuTheme();
	auto s = new GuiSettings(mWindow, _("BATTERYPLUS"));

	// 读取当前状态
	const std::string percent = getShOutput("cat /tmp/battery.percent 2>/dev/null");
	const std::string status = getShOutput("cat /sys/class/power_supply/battery/status 2>/dev/null");
	const std::string voltageUv = getShOutput("cat /sys/class/power_supply/battery/voltage_now 2>/dev/null");

	std::string mode = "voltage";
	{
		std::istringstream conf(readTextFile("/etc/batteryplus/batteryplus.conf"));
		std::string line;
		while (std::getline(conf, line))
		{
			line = Utils::String::trim(line);
			if (line.rfind("mode=", 0) == 0)
				mode = Utils::String::trim(line.substr(5));
		}
	}

	std::string info = _("POWER") + ": " + (percent.empty() ? std::string("N/A") : percent + "%") +
		"  " + _("STATUS") + ": " + (status.empty() ? std::string("-") : status);
	if (!voltageUv.empty())
		info += "  " + std::to_string(atoi(voltageUv.c_str()) / 1000) + "mV";
	info += "  " + _("MODE") + ": " + mode;

	s->addWithLabel(_("BATTERY INFO"),
		std::make_shared<TextComponent>(mWindow, info, theme->Text.font, 0x777777FF));

	// 开关
	const bool enabled = getShOutput("systemctl is-enabled batteryplus.service 2>/dev/null") == "enabled";

	auto enableSwitch = std::make_shared<SwitchComponent>(mWindow);
	enableSwitch->setState(enabled);
	enableSwitch->setOnChangedCallback([enableSwitch]
	{
		const std::string cmd = enableSwitch->getState()
			? "sudo systemctl enable --now batteryplus.service 2>/dev/null"
			: "sudo systemctl disable --now batteryplus.service 2>/dev/null; sudo rm -f /tmp/battery.percent";
		if (system(cmd.c_str()) != 0)
			LOG(LogWarning) << "batteryplus toggle failed";
	});
	s->addWithLabel(_("ENABLE BATTERYPLUS"), enableSwitch);

	// 模式: voltage / pmic
	auto modeList = std::make_shared<OptionListComponent<std::string>>(mWindow, _("BATTERY MODE"), false);
	modeList->add(_("BatteryPlus Mode"), "voltage", mode == "voltage");
	modeList->add(_("Driver Mode"), "pmic", mode == "pmic");
	if (mode != "voltage" && mode != "pmic")
		modeList->selectFirstItem();

	modeList->setSelectedChangedCallback([](const std::string& value)
	{
		const std::string wasEnabled = getShOutput("systemctl is-enabled batteryplus.service 2>/dev/null");
		if (wasEnabled == "enabled")
			system("sudo systemctl stop batteryplus.service 2>/dev/null");

		system(("sudo sed -i 's|^mode=.*|mode=" + value + "|' /etc/batteryplus/batteryplus.conf").c_str());

		// 切换模式后丢弃旧的可视百分比/恢复状态, 否则电压模式会沿用
		// 上一个模式保存的 visible_percent (例如 pmic 的 50%) 直到下次更新.
		system("sudo sh -c 'rm -f /tmp/battery.percent /home/ark/.config/batteryplus/batteryplus-restore.state'");

		if (wasEnabled == "enabled")
			system("sudo systemctl start batteryplus.service 2>/dev/null");
	});
	s->addWithLabel(_("BATTERY MODE"), modeList);

	// 清除记录
	s->addEntry(_("DELETE USAGE RECORDS"), true, [this]
	{
		Window* window = mWindow;
		window->pushGui(new GuiMsgBox(window,
			_("DELETE ALL BATTERY USAGE RECORDS? THIS WILL RESET CALIBRATION."),
			_("YES"), [window]
			{
				std::string dataDir;
				std::istringstream conf(readTextFile("/etc/batteryplus/batteryplus.conf"));
				std::string line;
				while (std::getline(conf, line))
				{
					line = Utils::String::trim(line);
					if (line.rfind("data_dir=", 0) == 0)
						dataDir = Utils::String::trim(line.substr(9));
				}

				system("sudo rm -f /tmp/battery.percent");

				if (!dataDir.empty())
				{
					const std::string base = "sudo rm -f " + dataDir + "/";
					system((base + "batteryplus-calibrated").c_str());
					system((base + "batteryplus-voltage.map").c_str());
					system((base + "batteryplus-restore.state").c_str());
				}

				if (getShOutput("systemctl is-enabled batteryplus.service 2>/dev/null") == "enabled")
					system("sudo systemctl restart batteryplus.service 2>/dev/null");

				window->pushGui(new GuiMsgBox(window, _("RECORDS DELETED. SERVICE RESTARTED."), _("OK")));
			},
			_("NO"), nullptr));
	}, "iconAdvanced");

	mWindow->pushGui(s);
}

void GuiMenu::openPowerLedSettings()
{
	auto s = new GuiSettings(mWindow, _("POWER LED"));

	// 当前状态: /sys/devices/platform/power-leds/gpio -> "<label> <0|1>"
	std::istringstream gpioState(readTextFile("/sys/devices/platform/power-leds/gpio"));
	std::string line;
	std::vector<std::pair<std::string, bool>> leds;

	while (std::getline(gpioState, line))
	{
		std::istringstream row(line);
		std::string label;
		int value = 0;

		if (row >> label >> value)
			leds.emplace_back(label, value != 0);
	}

	static const std::pair<const char*, const char*> colorNames[] = {
		{ "red", "Red" }, { "green", "Green" }, { "blue", "Blue" },
	};

	for (const auto& led : leds)
	{
		const char* uiName = nullptr;
		for (const auto& color : colorNames)
		{
			if (led.first == color.first)
				uiName = color.second;
		}

		auto ledSwitch = std::make_shared<SwitchComponent>(mWindow);
		ledSwitch->setState(led.second);

		const std::string label = led.first;
		ledSwitch->setOnChangedCallback([ledSwitch, label]
		{
			const bool on = ledSwitch->getState();

			// 关闭时清掉阈值, 否则驱动的自动模式会忽略开关继续点亮
			if (!on)
			{
				const std::string tcmd = "sudo sh -c 'echo 0 > /sys/class/leds/" + label + "/battery_threshold'";
				system(tcmd.c_str());
				Settings::getInstance()->setInt("PowerLed." + label + ".threshold", 0);
			}

			const std::string cmd = "sudo sh -c 'echo " + label + " " +
				(on ? "1" : "0") +
				" > /sys/devices/platform/power-leds/gpio'";
			if (system(cmd.c_str()) != 0)
				LOG(LogWarning) << "power led switch failed";

			Settings::getInstance()->setBool("PowerLed." + label + ".on", on);
			Settings::getInstance()->saveFile();
		});

		s->addWithLabel(uiName ? _(uiName) : led.first, ledSwitch);

		// 每个 LED 独立电量阈值: 关闭 / 10..90
		int threshold = atoi(getShOutput(("cat /sys/class/leds/" + led.first + "/battery_threshold 2>/dev/null").c_str()).c_str());

		auto thresholdList = std::make_shared<OptionListComponent<std::string>>(mWindow, _("BATTERY THRESHOLD"), false);
		thresholdList->add(_("Off"), "0", threshold == 0);

		bool thresholdFound = threshold == 0;
		for (int t = 10; t <= 90; t += 10)
		{
			const std::string value = std::to_string(t);
			thresholdList->add(value, value, t == threshold);
			if (t == threshold)
				thresholdFound = true;
		}
		if (!thresholdFound)
			thresholdList->selectFirstItem();

		const std::string labelForPath = led.first;
		thresholdList->setSelectedChangedCallback([labelForPath](const std::string& value)
		{
			const std::string cmd = "sudo sh -c 'echo " + value +
				" > /sys/class/leds/" + labelForPath + "/battery_threshold'";
			if (system(cmd.c_str()) != 0)
				LOG(LogWarning) << "power led threshold failed";

			Settings::getInstance()->setInt("PowerLed." + labelForPath + ".threshold", atoi(value.c_str()));
			Settings::getInstance()->saveFile();
		});

		s->addWithLabel(std::string(uiName ? _(uiName) : led.first) + " " + _("BATTERY THRESHOLD"), thresholdList);
	}

	mWindow->pushGui(s);
}

void GuiMenu::openFanSettings()
{
	auto s = new GuiSettings(mWindow, _("FAN CONTROL"));

	std::string fanMode;
	int fanPwm = 0;

	const std::string fanCfgRaw = readTextFile("/home/ark/.config/fan.cfg");
	std::istringstream fanCfg(fanCfgRaw);
	std::string line;
	while (std::getline(fanCfg, line))
	{
		const auto eq = line.find('=');
		if (eq == std::string::npos)
			continue;

		const std::string key = line.substr(0, eq);
		const std::string value = line.substr(eq + 1);

		if (key == "MODE")
			fanMode = value;
		else if (key == "PWM")
			fanPwm = atoi(value.c_str());
	}

	// 档位: auto=温度自动, off=关闭, 100=低速, 200=中速, 255=高速
	static const std::pair<const char*, const char*> fanPresets[] = {
		{ "Auto", "auto" }, { "Off", "off" }, { "Low", "100" }, { "Medium", "200" }, { "High", "255" },
	};

	auto fanLevel = std::make_shared<OptionListComponent<std::string>>(mWindow, _("FAN SPEED"), false);

	bool found = false;

	for (const auto& preset : fanPresets)
	{
		const std::string value = preset.second;
		const bool selected = (fanMode == "auto" && value == "auto") ||
			(fanMode == "off" && value == "off") ||
			(fanMode == "manual" && value != "off" && value != "auto" && std::to_string(fanPwm) == value);
		fanLevel->add(_(preset.first), value, selected);
		if (selected)
			found = true;
	}
	if (!found)
		fanLevel->selectFirstItem();

	LOG(LogInfo) << "FanSettings: cfg mode='" << fanMode << "' pwm=" << fanPwm
	             << " matched=" << (found ? 1 : 0) << " selected=" << fanLevel->getSelected();

	{
		std::string flat = fanCfgRaw;
		for (auto& ch : flat)
			if (ch == '\n')
				ch = '|';

		FILE* dbg = fopen("/tmp/fan_dbg.log", "a");
		if (dbg)
		{
			fprintf(dbg, "mode='%s' pwm=%d matched=%d selected='%s' raw='%s'\n",
			        fanMode.c_str(), fanPwm, found ? 1 : 0, fanLevel->getSelected().c_str(), flat.c_str());
			fclose(dbg);
		}
	}

	fanLevel->setSelectedChangedCallback([](const std::string& value)
	{
		LOG(LogInfo) << "FanSettings: user selected '" << value << "'";
		const std::string cmd = value == "auto" ? "sudo /usr/local/bin/fanctl on"
			: value == "off" ? "sudo /usr/local/bin/fanctl off"
			: "sudo /usr/local/bin/fanctl set " + value;
		if (system(cmd.c_str()) != 0)
			LOG(LogWarning) << "fanctl failed";
	});
	s->addWithLabel(_("FAN SPEED"), fanLevel);

	mWindow->pushGui(s);
}

void GuiMenu::openDisplaySettings()
{
	// Display Brightness
	auto s = new GuiSettings(mWindow, _("DISPLAY"));

    int brighness;
    ApiSystem::getInstance()->getBrighness(brighness);
   	auto brightnessComponent = std::make_shared<SliderComponent>(mWindow, 1.0f, 100.f, 1.0f, "%");
    brightnessComponent->setValue((float) ApiSystem::getInstance()->getBrightnessLevel());
   	brightnessComponent->setOnValueChanged([](const float &newVal)
    {
    	ApiSystem::getInstance()->setBrighness((int)Math::round(newVal));
   	});
    s->addSaveFunc([this, brightnessComponent] {
         SystemConf::getInstance()->set("brightness.level", std::to_string((int)Math::round(brightnessComponent->getValue())));
    });

	s->addWithLabel(_("BRIGHTNESS"), brightnessComponent);

		auto brightnessPopup = std::make_shared<SwitchComponent>(mWindow);
		brightnessPopup->setState(Settings::getInstance()->getBool("BrightnessPopup"));
		s->addWithLabel(_("SHOW OVERLAY WHEN BRIGHTNESS CHANGES"), brightnessPopup);
		s->addSaveFunc([brightnessPopup]
			{
				bool old_value = Settings::getInstance()->getBool("BrightnessPopup");
				if (old_value != brightnessPopup->getState())
					Settings::getInstance()->setBool("BrightnessPopup", brightnessPopup->getState());
			}
		);

	// Gamma (gamma 工具, R/G/B 独立调整, 0.6-1.4, 0.1 步进)
	if (Utils::FileSystem::exists("/usr/local/bin/gamma"))
	{
		float initGamma[3] = { 1.0f, 1.0f, 1.0f };
		{
			std::istringstream gammaState(readTextFile("/dev/shm/CURRENT_GAMMA"));
			gammaState >> initGamma[0] >> initGamma[1] >> initGamma[2];
		}

		auto current = std::make_shared<std::array<std::string, 3>>();

		for (int ch = 0; ch < 3; ch++)
		{
			float value = initGamma[ch];
			if (value < 0.3f || value > 2.0f)
				value = 1.0f;

			char buf[8];
			snprintf(buf, sizeof(buf), "%.1f", value);
			(*current)[ch] = buf;
		}

		static const char* gammaLabels[3] = { "GAMMA R", "GAMMA G", "GAMMA B" };

		for (int ch = 0; ch < 3; ch++)
		{
			auto gammaList = std::make_shared<OptionListComponent<std::string>>(mWindow, _(gammaLabels[ch]), false);

			bool gammaFound = false;
			for (int step = 6; step <= 14; step++)
			{
				char value[8];
				snprintf(value, sizeof(value), "%.1f", step / 10.0);
				const bool selected = std::string(value) == (*current)[ch];
				gammaList->add(value, value, selected);
				if (selected)
					gammaFound = true;
			}
			if (!gammaFound)
				gammaList->selectFirstItem();

			gammaList->setSelectedChangedCallback([current, ch](const std::string& value)
			{
				(*current)[ch] = value;
				const std::string home = getenv("HOME") ? getenv("HOME") : "/home/ark";
				/* colortemp 负责把色温 x gamma 合成成绝对 LUT (内核参数或
				 * DRM gamma), 避免在旧 LUT 上再乘导致色彩崩掉 */
				const std::string cmd = Utils::FileSystem::exists("/usr/local/bin/colortemp")
					? "sudo -n /usr/local/bin/colortemp -r " + (*current)[0] +
					  " -g " + (*current)[1] + " -b " + (*current)[2]
					: "sudo env HOME=" + home + " /usr/local/bin/gamma -r " + (*current)[0] +
					  " -g " + (*current)[1] + " -b " + (*current)[2];
				if (system(cmd.c_str()) != 0)
					LOG(LogWarning) << "gamma apply failed";
			});

			s->addWithLabel(_(gammaLabels[ch]), gammaList);
		}
	}

	// 色温 (VOP2 gamma LUT): 6500K 中性, 越低越暖. 内核支持时写
	// /sys/module/rockchipdrm/parameters/colortemp (持久, 游戏里也生效),
	// 否则退回 /usr/local/bin/colortemp (仅当前 KMS 会话)
	{
		int curK = 6500;
		const std::string ctempSys = "/sys/module/rockchipdrm/parameters/colortemp";
		const std::string ctempConf = "/home/ark/.config/colortemp.conf";

		if (Utils::FileSystem::exists(ctempSys)) {
			int v = atoi(readTextFile(ctempSys.c_str()).c_str());
			if (v >= 3000 && v <= 9000)
				curK = v;
		} else if (Utils::FileSystem::exists(ctempConf)) {
			std::istringstream cs(readTextFile(ctempConf.c_str()));
			std::string kind;
			int v = 0;
			cs >> kind >> v;
			if (kind == "kelvin" && v >= 3000 && v <= 9000)
				curK = v;
		}

		auto ctempSlider = std::make_shared<SliderComponent>(mWindow, 3000.f, 9000.f, 100.f, "K");
		ctempSlider->setValue((float)curK);
		ctempSlider->setOnValueChanged([](const float& newVal)
		{
			const std::string v = std::to_string((int)Math::round(newVal));

			/* colortemp: 有内核参数走 sysfs (持久), 否则写 DRM LUT */
			if (Utils::FileSystem::exists("/usr/local/bin/colortemp"))
				runSystemCommand("sudo -n /usr/local/bin/colortemp -k " + v + " >/dev/null", "", nullptr);
		});
		s->addWithLabel(_("COLOR TEMPERATURE"), ctempSlider);
		s->addSaveFunc([ctempSlider]
		{
			const std::string v = std::to_string((int)Math::round(ctempSlider->getValue()));

			if (Utils::FileSystem::exists("/usr/local/bin/colortemp"))
				runSystemCommand("sudo -n /usr/local/bin/colortemp -k " + v + " >/dev/null", "", nullptr);
		});
	}

  if (Utils::FileSystem::exists("/usr/local/bin/panel_set.sh"))
  {
    // Panel Brightness
    int Dbrightness;
    ApiSystem::getInstance()->getDBrightness(Dbrightness);
   	auto DbrightnessComponent = std::make_shared<SliderComponent>(mWindow, 1.f, 100.f, 1.f, "%");
    DbrightnessComponent->setValue(Dbrightness);
   	DbrightnessComponent->setOnValueChanged([](const float &newVal)
    {
    	ApiSystem::getInstance()->setDBrightness((int)Math::round(newVal));
   	});
    s->addSaveFunc([this, DbrightnessComponent] {
         SystemConf::getInstance()->set("Dbrightness.level", std::to_string((int)Math::round(DbrightnessComponent->getValue())));
    });

	s->addWithLabel(_("PANEL BRIGHTNESS"), DbrightnessComponent);

    //Panel Contrast
    int Dcontrast;
    ApiSystem::getInstance()->getDContrast(Dcontrast);
   	auto DcontrastComponent = std::make_shared<SliderComponent>(mWindow, 1.f, 100.f, 1.f, "%");
    DcontrastComponent->setValue(Dcontrast);
   	DcontrastComponent->setOnValueChanged([](const float &newVal)
    {
    	ApiSystem::getInstance()->setDContrast((int)Math::round(newVal));
   	});
    s->addSaveFunc([this, DcontrastComponent] {
         SystemConf::getInstance()->set("Dcontrast.level", std::to_string((int)Math::round(DcontrastComponent->getValue())));
    });

	s->addWithLabel(_("PANEL CONTRAST"), DcontrastComponent);

    //Panel Saturation
    int Dsaturation;
    ApiSystem::getInstance()->getDSaturation(Dsaturation);
   	auto DsaturationComponent = std::make_shared<SliderComponent>(mWindow, 1.f, 100.f, 1.f, "%");
    DsaturationComponent->setValue(Dsaturation);
   	DsaturationComponent->setOnValueChanged([](const float &newVal)
    {
    	ApiSystem::getInstance()->setDSaturation((int)Math::round(newVal));
   	});
    s->addSaveFunc([this, DsaturationComponent] {
         SystemConf::getInstance()->set("Dsaturation.level", std::to_string((int)Math::round(DsaturationComponent->getValue())));
    });

	s->addWithLabel(_("PANEL SATURATION"), DsaturationComponent);

    //Panel Hue
    int Dhue;
    ApiSystem::getInstance()->getDHue(Dhue);
   	auto DhueComponent = std::make_shared<SliderComponent>(mWindow, 1.f, 100.f, 1.f, "%");
    DhueComponent->setValue(Dhue);
   	DhueComponent->setOnValueChanged([](const float &newVal)
    {
    	ApiSystem::getInstance()->setDHue((int)Math::round(newVal));
   	});
    s->addSaveFunc([this, DhueComponent] {
         SystemConf::getInstance()->set("Dhue.level", std::to_string((int)Math::round(DhueComponent->getValue())));
    });

	s->addWithLabel(_("PANEL HUE"), DhueComponent);

    if (!std::string(getShOutput(R"(grep 353 ~/.config/.DEVICE)")).empty())
    {
        //s->addEntry(_(""), false, [this] {  });
	    s->addEntry(_("PANEL VERSION") + ": " + std::string(getShOutput(R"(panel_id.sh version)")), false, [this] {  });
	    s->addEntry(_("PANEL ID") + ": " + std::string(getShOutput(R"(panel_id.sh id)")), false, [this] {  });
    }

  }
	mWindow->pushGui(s);
}

void GuiMenu::openScraperSettings()
{
	auto s = new GuiSettings(mWindow, _("SCRAPER"));

	std::string scraper = Settings::getInstance()->getString("Scraper");

	// scrape from
	auto scraper_list = std::make_shared< OptionListComponent< std::string > >(mWindow, _("SCRAPE FROM"), false);
	std::vector<std::string> scrapers = getScraperList();

	// Select either the first entry of the one read from the settings, just in case the scraper from settings has vanished.
	for (auto it = scrapers.cbegin(); it != scrapers.cend(); it++)
		scraper_list->add(*it, *it, *it == scraper);

	s->addWithLabel(_("SCRAPE FROM"), scraper_list);
	s->addSaveFunc([scraper_list] { Settings::getInstance()->setString("Scraper", scraper_list->getSelected()); });

	if (!scraper_list->hasSelection())
	{
		scraper_list->selectFirstItem();
		scraper = scraper_list->getSelected();
	}

	if (scraper == "ScreenScraper")
	{
		// Image source : <image> tag
		std::string imageSourceName = Settings::getInstance()->getString("ScrapperImageSrc");
		auto imageSource = std::make_shared< OptionListComponent<std::string> >(mWindow, _("IMAGE SOURCE"), false);
		imageSource->add(_("SCREENSHOT"), "ss", imageSourceName == "ss");
		imageSource->add(_("TITLE SCREENSHOT"), "sstitle", imageSourceName == "sstitle");
		imageSource->add(_("MIX V1"), "mixrbv1", imageSourceName == "mixrbv1");
		imageSource->add(_("MIX V2"), "mixrbv2", imageSourceName == "mixrbv2");
		imageSource->add(_("BOX 2D"), "box-2D", imageSourceName == "box-2D");
		imageSource->add(_("BOX 3D"), "box-3D", imageSourceName == "box-3D");
		imageSource->add(_("NONE"), "none", imageSourceName == "none");

		if (!imageSource->hasSelection())
			imageSource->selectFirstItem();

		s->addWithLabel(_("IMAGE SOURCE"), imageSource);
		s->addSaveFunc([imageSource] { Settings::getInstance()->setString("ScrapperImageSrc", imageSource->getSelected()); });

		// Box source : <thumbnail> tag
		std::string thumbSourceName = Settings::getInstance()->getString("ScrapperThumbSrc");
		auto thumbSource = std::make_shared< OptionListComponent<std::string> >(mWindow, _("BOX SOURCE"), false);
		thumbSource->add(_("NONE"), "", thumbSourceName.empty());
		thumbSource->add(_("BOX 2D"), "box-2D", thumbSourceName == "box-2D");
		thumbSource->add(_("BOX 3D"), "box-3D", thumbSourceName == "box-3D");

		if (!thumbSource->hasSelection())
			thumbSource->selectFirstItem();

		s->addWithLabel(_("BOX SOURCE"), thumbSource);
		s->addSaveFunc([thumbSource] { Settings::getInstance()->setString("ScrapperThumbSrc", thumbSource->getSelected()); });

		imageSource->setSelectedChangedCallback([this, thumbSource](std::string value)
		{
			if (value == "box-2D")
				thumbSource->remove(_("BOX 2D"));
			else
				thumbSource->add(_("BOX 2D"), "box-2D", false);

			if (value == "box-3D")
				thumbSource->remove(_("BOX 3D"));
			else
				thumbSource->add(_("BOX 3D"), "box-3D", false);
		});

		// Logo source : <marquee> tag
		std::string logoSourceName = Settings::getInstance()->getString("ScrapperLogoSrc");
		auto logoSource = std::make_shared< OptionListComponent<std::string> >(mWindow, _("LOGO SOURCE"), false);
		logoSource->add(_("NONE"), "", logoSourceName.empty());
		logoSource->add(_("WHEEL"), "wheel", logoSourceName == "wheel");
		logoSource->add(_("MARQUEE"), "marquee", logoSourceName == "marquee");

		if (!logoSource->hasSelection())
			logoSource->selectFirstItem();

		s->addWithLabel(_("LOGO SOURCE"), logoSource);
		s->addSaveFunc([logoSource] { Settings::getInstance()->setString("ScrapperLogoSrc", logoSource->getSelected()); });

		// Region source
		std::string regionName = Settings::getInstance()->getString("ScrapperRegionSrc");
		auto regionSource = std::make_shared< OptionListComponent<std::string> >(mWindow, _("REGION SOURCE"), false);
		regionSource->add(_("US"), "US", regionName == "US");
		regionSource->add(_("EU"), "EU", regionName == "EU");
		regionSource->add(_("FR"), "FR", regionName == "FR");
		regionSource->add(_("JP"), "JP", regionName == "JP");

		if (!regionSource->hasSelection())
			regionSource->selectFirstItem();

		s->addWithLabel(_("REGION SOURCE"), regionSource);
		s->addSaveFunc([regionSource] { Settings::getInstance()->setString("ScrapperRegionSrc", regionSource->getSelected()); });

		// scrape ratings
		auto scrape_ratings = std::make_shared<SwitchComponent>(mWindow);
		scrape_ratings->setState(Settings::getInstance()->getBool("ScrapeRatings"));
		s->addWithLabel(_("SCRAPE RATINGS"), scrape_ratings);
		s->addSaveFunc([scrape_ratings] { Settings::getInstance()->setBool("ScrapeRatings", scrape_ratings->getState()); });

		// scrape video
		auto scrape_video = std::make_shared<SwitchComponent>(mWindow);
		scrape_video->setState(Settings::getInstance()->getBool("ScrapeVideos"));
		s->addWithLabel(_("SCRAPE VIDEOS"), scrape_video);
		s->addSaveFunc([scrape_video] { Settings::getInstance()->setBool("ScrapeVideos", scrape_video->getState()); });

		// Account
		createInputTextRow(s, _("USERNAME"), "ScreenScraperUser", false);
		createInputTextRow(s, _("PASSWORD"), "ScreenScraperPass", true);
	}
	else
	{
		// scrape ratings
		auto scrape_ratings = std::make_shared<SwitchComponent>(mWindow);
		scrape_ratings->setState(Settings::getInstance()->getBool("ScrapeRatings"));
		s->addWithLabel(_("SCRAPE RATINGS"), scrape_ratings); // batocera
		s->addSaveFunc([scrape_ratings] { Settings::getInstance()->setBool("ScrapeRatings", scrape_ratings->getState()); });
		createInputTextRow(s, _("API KEY"), "GamesDBApiKey", false);
	}

	// scrape now
	ComponentListRow row;
	auto openScrapeNow = [this] 
	{ 
		if (ThreadedScraper::isRunning())
		{
			Window* window = mWindow;

			mWindow->pushGui(new GuiMsgBox(mWindow, _("SCRAPING IS RUNNING. DO YOU WANT TO STOP IT ?"), _("YES"), [this, window]
			{
				ThreadedScraper::stop();
			}, _("NO"), nullptr));

			return;
		}

		mWindow->pushGui(new GuiScraperStart(mWindow)); 
	};
	std::function<void()> openAndSave = openScrapeNow;
	openAndSave = [s, openAndSave] { s->save(); openAndSave(); };
	s->addEntry(_("SCRAPE NOW"), true, openAndSave, "iconScraper");

	s->updatePosition();

	scraper_list->setSelectedChangedCallback([this, s, scraper, scraper_list](std::string value)
	{
		if (value != scraper && (scraper == "ScreenScraper" || value == "ScreenScraper"))
		{
			Settings::getInstance()->setString("Scraper", value);
			delete s;
			openScraperSettings();
		}
	});

	mWindow->pushGui(s);
}

void GuiMenu::openSoundSettings()
{
	auto s = new GuiSettings(mWindow, _("SOUND SETTINGS"));
	
	// volume
	auto volume = std::make_shared<SliderComponent>(mWindow, 0.f, 100.f, 1.f, "%");
	volume->setValue((float)VolumeControl::getInstance()->getVolume());
	volume->setOnValueChanged([](const float &newVal) { VolumeControl::getInstance()->setVolume((int)Math::round(newVal)); });
	s->addWithLabel(_("SYSTEM VOLUME"), volume);
	s->addSaveFunc([volume] { VolumeControl::getInstance()->setVolume((int)Math::round(volume->getValue())); });

	if (UIModeController::getInstance()->isUIModeFull())
	{
#if defined(__linux__)
		// audio card
		auto audio_card = std::make_shared< OptionListComponent<std::string> >(mWindow, _("AUDIO CARD"), false);
		std::vector<std::string> audio_cards;
	#ifdef _RPI_
		// RPi Specific  Audio Cards
		audio_cards.push_back("local");
		audio_cards.push_back("hdmi");
		audio_cards.push_back("both");
	#endif
		audio_cards.push_back("default");
		audio_cards.push_back("sysdefault");
		audio_cards.push_back("dmix");
		audio_cards.push_back("hw");
		audio_cards.push_back("plughw");
		audio_cards.push_back("null");
		if (Settings::getInstance()->getString("AudioCard") != "") {
			if(std::find(audio_cards.begin(), audio_cards.end(), Settings::getInstance()->getString("AudioCard")) == audio_cards.end()) {
				audio_cards.push_back(Settings::getInstance()->getString("AudioCard"));
			}
		}
		for(auto ac = audio_cards.cbegin(); ac != audio_cards.cend(); ac++)
			audio_card->add(*ac, *ac, Settings::getInstance()->getString("AudioCard") == *ac);
		//s->addWithLabel("AUDIO CARD", audio_card);
		s->addSaveFunc([audio_card] {
			Settings::getInstance()->setString("AudioCard", audio_card->getSelected());
			VolumeControl::getInstance()->deinit();
			VolumeControl::getInstance()->init();
		});

		// volume control device
		auto vol_dev = std::make_shared< OptionListComponent<std::string> >(mWindow, "AUDIO DEVICE", false);
		std::vector<std::string> transitions;
		transitions.push_back("PCM");
		transitions.push_back("Speaker");
		transitions.push_back("Master");
		transitions.push_back("Digital");
		transitions.push_back("Analogue");
		if (Settings::getInstance()->getString("AudioDevice") != "") {
			if(std::find(transitions.begin(), transitions.end(), Settings::getInstance()->getString("AudioDevice")) == transitions.end()) {
				transitions.push_back(Settings::getInstance()->getString("AudioDevice"));
			}
		}
		for(auto it = transitions.cbegin(); it != transitions.cend(); it++)
			vol_dev->add(*it, *it, Settings::getInstance()->getString("AudioDevice") == *it);
		//s->addWithLabel(_("AUDIO DEVICE"), vol_dev);
		s->addSaveFunc([vol_dev] {
			Settings::getInstance()->setString("AudioDevice", vol_dev->getSelected());
			VolumeControl::getInstance()->deinit();
			VolumeControl::getInstance()->init();
		});
#endif
		auto volumePopup = std::make_shared<SwitchComponent>(mWindow);
		volumePopup->setState(Settings::getInstance()->getBool("VolumePopup"));
		s->addWithLabel(_("SHOW OVERLAY WHEN VOLUME CHANGES"), volumePopup);
		s->addSaveFunc([volumePopup]
			{
				bool old_value = Settings::getInstance()->getBool("VolumePopup");
				if (old_value != volumePopup->getState())
					Settings::getInstance()->setBool("VolumePopup", volumePopup->getState());
			}
		);

        //Flip volume button function for RGB30 and RK2023 units
        std::string isitpowkiddy=(getShOutput(R"(cat /home/ark/.config/.DEVICE)"));
        if (isitpowkiddy.compare("RGB30")==0 || isitpowkiddy.compare("RK2023")==0) {
          auto volBtns = std::make_shared<SwitchComponent>(mWindow);
          volBtns->setState(Settings::getInstance()->getBool("InvertVolBtns"));
          s->addWithLabel(_("FLIP VOLUME BUTTONS"), volBtns);
          s->addSaveFunc([this, s, volBtns]
          {
            if (Settings::getInstance()->setBool("InvertVolBtns", volBtns->getState()))
              {
                if (Settings::getInstance()->getBool("InvertVolBtns") == 1)
                  runSystemCommand("[ -z $(find /home/ark/.config/.SWAPVOLUMEBUTTONS) ] && touch /home/ark/.config/.SWAPVOLUMEBUTTONS", "", nullptr);
                else
                  runSystemCommand("[ ! -z $(find /home/ark/.config/.SWAPVOLUMEBUTTONS) ] && rm /home/ark/.config/.SWAPVOLUMEBUTTONS", "", nullptr);
              }
          });
        }

		// disable sounds
		auto music_enabled = std::make_shared<SwitchComponent>(mWindow);
		music_enabled->setState(Settings::getInstance()->getBool("audio.bgmusic"));
		s->addWithLabel(_("FRONTEND MUSIC"), music_enabled);
		s->addSaveFunc([music_enabled] {
			Settings::getInstance()->setBool("audio.bgmusic", music_enabled->getState());
			if (music_enabled->getState())
				AudioManager::getInstance()->playRandomMusic();
			else
				AudioManager::getInstance()->stopMusic();
		});

		//display music titles
		auto display_titles = std::make_shared<SwitchComponent>(mWindow);
		display_titles->setState(Settings::getInstance()->getBool("MusicTitles"));
		s->addWithLabel(_("DISPLAY SONG TITLES"), display_titles);
		s->addSaveFunc([display_titles] {
			Settings::getInstance()->setBool("MusicTitles", display_titles->getState());
		});

		// music per system
		auto music_per_system = std::make_shared<SwitchComponent>(mWindow);
		music_per_system->setState(Settings::getInstance()->getBool("audio.persystem"));
		s->addWithLabel(_("ONLY PLAY SYSTEM-SPECIFIC MUSIC FOLDER"), music_per_system);
		s->addSaveFunc([music_per_system] {
			Settings::getInstance()->setBool("audio.persystem", music_per_system->getState());
		});
		
		// batocera - music per system
		auto enableThemeMusics = std::make_shared<SwitchComponent>(mWindow);
		enableThemeMusics->setState(Settings::getInstance()->getBool("audio.thememusics"));
		s->addWithLabel(_("PLAY THEME MUSICS"), enableThemeMusics);
		s->addSaveFunc([enableThemeMusics] {
			if (Settings::getInstance()->setBool("audio.thememusics", enableThemeMusics->getState()))
				AudioManager::getInstance()->themeChanged(ViewController::get()->getState().getSystem()->getTheme(), true);
		});

		// disable sounds
		auto sounds_enabled = std::make_shared<SwitchComponent>(mWindow);
		sounds_enabled->setState(Settings::getInstance()->getBool("EnableSounds"));
		s->addWithLabel(_("ENABLE NAVIGATION SOUNDS"), sounds_enabled);
		s->addSaveFunc([sounds_enabled] {
			if (sounds_enabled->getState()
				&& !Settings::getInstance()->getBool("EnableSounds")
				&& PowerSaver::getMode() == PowerSaver::INSTANT)
			{
				Settings::getInstance()->setString("PowerSaverMode", "default");
				PowerSaver::init();
			}
			Settings::getInstance()->setBool("EnableSounds", sounds_enabled->getState());
		});

		auto video_audio = std::make_shared<SwitchComponent>(mWindow);
		video_audio->setState(Settings::getInstance()->getBool("VideoAudio"));
		s->addWithLabel(_("ENABLE VIDEO AUDIO"), video_audio);
		s->addSaveFunc([video_audio] { Settings::getInstance()->setBool("VideoAudio", video_audio->getState()); });

		auto videolowermusic = std::make_shared<SwitchComponent>(mWindow);
		videolowermusic->setState(Settings::getInstance()->getBool("VideoLowersMusic"));
		s->addWithLabel(_("LOWER MUSIC WHEN PLAYING VIDEO"), videolowermusic);
		s->addSaveFunc([videolowermusic] { Settings::getInstance()->setBool("VideoLowersMusic", videolowermusic->getState()); });

	// Verbal Battery Voices
	auto VerbalbatteryVoice = std::make_shared<OptionListComponent<std::string> >(mWindow, _("Verbal BATTERY Voice"), false);
	VerbalbatteryVoice->addRange({ { _("MALE 1"), "male1" },{ _("MALE 2"), "male2" },{ _("FEMALE"), "female" } }, Settings::getInstance()->getString("VerbalBatteryVoice"));
	s->addWithLabel(_("VERBAL BATTERY VOICE"), VerbalbatteryVoice);
	s->addSaveFunc([s, VerbalbatteryVoice]
	{
		std::string old_value = Settings::getInstance()->getString("VerbalBatteryVoice");
		if (old_value != VerbalbatteryVoice->getSelected())
           {
            if (strstr(VerbalbatteryVoice->getSelected().c_str(),"male1")) {
              runSystemCommand("[ ! -z $(find /home/ark/.config/.MBROLA_VOICE*) ] && rm /home/ark/.config/.MBROLA_VOICE*", "", nullptr);
            }
            else if (strstr(VerbalbatteryVoice->getSelected().c_str(),"male2")) {
              runSystemCommand("[ ! -z $(find /home/ark/.config/.MBROLA_VOICE*) ] && rm /home/ark/.config/.MBROLA_VOICE*", "", nullptr);
              runSystemCommand("touch /home/ark/.config/.MBROLA_VOICE_MALE3", "", nullptr);
            }
            else {
              runSystemCommand("[ ! -z $(find /home/ark/.config/.MBROLA_VOICE*) ] && rm /home/ark/.config/.MBROLA_VOICE*", "", nullptr);
              runSystemCommand("touch /home/ark/.config/.MBROLA_VOICE_FEMALE", "", nullptr);
            }
			Settings::getInstance()->setString("VerbalBatteryVoice", VerbalbatteryVoice->getSelected());
		   }
	});

	// Verbal Battery Warning indicator
	auto VerbalbatteryStatus = std::make_shared<OptionListComponent<std::string> >(mWindow, _("Verbal BATTERY Warning"), false);
	VerbalbatteryStatus->addRange({ { _("OFF"), "no" },{ _("ON"), "yes" } }, Settings::getInstance()->getString("VerbalBatteryWarning"));
	s->addWithLabel(_("VERBAL BATTERY WARNING"), VerbalbatteryStatus);
	s->addSaveFunc([s, VerbalbatteryStatus]
	{
		std::string old_value = Settings::getInstance()->getString("VerbalBatteryWarning");
		if (old_value != VerbalbatteryStatus->getSelected())
           {
            if (strstr(VerbalbatteryStatus->getSelected().c_str(),"no")) {
              runSystemCommand("[ -z $(find /home/ark/.config/.NOVERBALBATTERYWARNING) ] && touch /home/ark/.config/.NOVERBALBATTERYWARNING", "", nullptr);
            }
            else {
              runSystemCommand("[ ! -z $(find /home/ark/.config/.NOVERBALBATTERYWARNING) ] && rm /home/ark/.config/.NOVERBALBATTERYWARNING", "", nullptr);
            }
			Settings::getInstance()->setString("VerbalBatteryWarning", VerbalbatteryStatus->getSelected());
		   }
	});

	// Verbal Battery Warning threshold
	auto Thold = std::make_shared< OptionListComponent<std::string> >(mWindow, _("Verbal BATTERY Warning Threshold (%)"), false);
	std::vector<std::string> athreshold;
	athreshold.push_back("Default");
	athreshold.push_back("5");
	athreshold.push_back("10");
	athreshold.push_back("15");
	athreshold.push_back("20");
	athreshold.push_back("25");
	athreshold.push_back("30");
	athreshold.push_back("35");
	athreshold.push_back("40");
	athreshold.push_back("45");
	athreshold.push_back("50");

	auto threshold = Settings::getInstance()->getString("VerbalBatteryThreshold");
	if (threshold.empty())
		threshold = "Default";

	for (auto it = athreshold.cbegin(); it != athreshold.cend(); it++)
		Thold->add(_(it->c_str()), *it, threshold == *it);

	s->addWithLabel(_("Verbal BATTERY Warning Threshold (%)"), Thold);
	s->addSaveFunc([this, Thold] { Settings::getInstance()->setString("VerbalBatteryThreshold", Thold->getSelected());
		if (Thold->changed()) {
		    if (strstr(Thold->getSelected().c_str(),"Default")) {
		      runSystemCommand("[ ! -z $(find /home/ark/.config/.CUSTOM_BATT_LIFE_WARNING) ] && rm -f /home/ark/.config/.CUSTOM_BATT_LIFE_WARNING", "", nullptr);
		    }
		    else {
		      runSystemCommand("echo " + Settings::getInstance()->getString("VerbalBatteryThreshold") + " > /home/ark/.config/.CUSTOM_BATT_LIFE_WARNING", "", nullptr);
		    }
		}
	});

#ifdef _RPI_
		// OMX player Audio Device
		auto omx_audio_dev = std::make_shared< OptionListComponent<std::string> >(mWindow, "OMX PLAYER AUDIO DEVICE", false);
		std::vector<std::string> omx_cards;
		// RPi Specific  Audio Cards
		omx_cards.push_back("local");
		omx_cards.push_back("hdmi");
		omx_cards.push_back("both");
		omx_cards.push_back("alsa:hw:0,0");
		omx_cards.push_back("alsa:hw:1,0");
		if (Settings::getInstance()->getString("OMXAudioDev") != "") {
			if (std::find(omx_cards.begin(), omx_cards.end(), Settings::getInstance()->getString("OMXAudioDev")) == omx_cards.end()) {
				omx_cards.push_back(Settings::getInstance()->getString("OMXAudioDev"));
			}
		}
		for (auto it = omx_cards.cbegin(); it != omx_cards.cend(); it++)
			omx_audio_dev->add(*it, *it, Settings::getInstance()->getString("OMXAudioDev") == *it);
		s->addWithLabel("OMX PLAYER AUDIO DEVICE", omx_audio_dev);
		s->addSaveFunc([omx_audio_dev] {
			if (Settings::getInstance()->getString("OMXAudioDev") != omx_audio_dev->getSelected())
				Settings::getInstance()->setString("OMXAudioDev", omx_audio_dev->getSelected());
		});
#endif
	}

	s->updatePosition();
	mWindow->pushGui(s);

}

struct ThemeConfigOption
{
	std::string defaultSettingName;
	std::string subset;
	std::shared_ptr<OptionListComponent<std::string>> component;
};

void GuiMenu::openThemeConfiguration(Window* mWindow, GuiComponent* s, std::shared_ptr<OptionListComponent<std::string>> theme_set,const std::string systemTheme)
{
	if (theme_set != nullptr && Settings::getInstance()->getString("ThemeSet") != theme_set->getSelected())
	{
		mWindow->pushGui(new GuiMsgBox(mWindow, _("YOU MUST APPLY THE THEME BEFORE EDIT CONFIGURATION"), _("OK")));
		return;
	}

	Window* window = mWindow;

	auto system = ViewController::get()->getState().getSystem();
	auto theme = system->getTheme();

	auto themeconfig = new GuiSettings(mWindow, (systemTheme.empty() ? _("THEME CONFIGURATION") : _("VIEW CUSTOMIZATION")).c_str());

	auto themeSubSets = theme->getSubSets();

	std::string viewName;
	bool showGridFeatures = true;
	if (!systemTheme.empty())
	{
		auto glv = ViewController::get()->getGameListView(system);
		viewName = glv->getName();
		std::string baseType = theme->getCustomViewBaseType(viewName);

		showGridFeatures = (viewName == "grid" || baseType == "grid");
	}


	// gamelist_style
	std::shared_ptr<OptionListComponent<std::string>> gamelist_style = nullptr;

	if (systemTheme.empty())
	{
		gamelist_style = std::make_shared< OptionListComponent<std::string> >(mWindow, _("GAMELIST VIEW STYLE"), false);

		std::vector<std::pair<std::string, std::string>> styles;
		styles.push_back(std::pair<std::string, std::string>("automatic", _("automatic")));

		if (system != NULL)
		{
			auto mViews = theme->getViewsOfTheme();
			for (auto it = mViews.cbegin(); it != mViews.cend(); ++it)
			{
				if (it->first == "basic" || it->first == "detailed" || it->first == "grid" || it->first == "video")
					styles.push_back(std::pair<std::string, std::string>(it->first, _(it->first.c_str())));
				else
					styles.push_back(*it);
			}
		}
		else
		{
			styles.push_back(std::pair<std::string, std::string>("basic", _("basic")));
			styles.push_back(std::pair<std::string, std::string>("detailed", _("detailed")));
		}

		auto viewPreference = systemTheme.empty() ? Settings::getInstance()->getString("GamelistViewStyle") : system->getSystemViewMode();
		if (!theme->hasView(viewPreference))
			viewPreference = "automatic";

		for (auto it = styles.cbegin(); it != styles.cend(); it++)
			gamelist_style->add(it->second, it->first, viewPreference == it->first);

		if (!gamelist_style->hasSelection())
			gamelist_style->selectFirstItem();

		themeconfig->addWithLabel(_("GAMELIST VIEW STYLE"), gamelist_style);
	}

	// Default grid size
	std::shared_ptr<OptionListComponent<std::string>> mGridSize = nullptr;
	if (showGridFeatures && system != NULL && theme->hasView("grid"))
	{
		Vector2f gridOverride =
			systemTheme.empty() ? Vector2f::parseString(Settings::getInstance()->getString("DefaultGridSize")) :
			system->getGridSizeOverride();

		auto ovv = std::to_string((int)gridOverride.x()) + "x" + std::to_string((int)gridOverride.y());

		mGridSize = std::make_shared<OptionListComponent<std::string>>(mWindow, _("DEFAULT GRID SIZE"), false);

		bool found = false;
		for (auto it = GuiGamelistOptions::gridSizes.cbegin(); it != GuiGamelistOptions::gridSizes.cend(); it++)
		{
			bool sel = (gridOverride == Vector2f(0, 0) && *it == "automatic") || ovv == *it;
			if (sel)
				found = true;

			mGridSize->add(_(it->c_str()), *it, sel);
		}

		if (!found)
			mGridSize->selectFirstItem();

		themeconfig->addWithLabel(_("DEFAULT GRID SIZE"), mGridSize);
	}

	std::map<std::string, ThemeConfigOption> options;

	for (std::string subset : theme->getSubSetNames(viewName))
	{
		std::string settingName = "subset." + subset;
		std::string perSystemSettingName = systemTheme.empty() ? "" : "subset." + systemTheme + "." + subset;

		if (subset == "colorset") settingName = "ThemeColorSet";
		else if (subset == "iconset") settingName = "ThemeIconSet";
		else if (subset == "menu") settingName = "ThemeMenu";
		else if (subset == "systemview") settingName = "ThemeSystemView";
		else if (subset == "gamelistview") settingName = "ThemeGamelistView";
		else if (subset == "region") settingName = "ThemeRegionName";

		auto themeColorSets = ThemeData::getSubSet(themeSubSets, subset);
			
		if (themeColorSets.size() > 0)
		{
			auto selectedColorSet = themeColorSets.end();
			auto selectedName = !perSystemSettingName.empty() ? Settings::getInstance()->getString(perSystemSettingName) : Settings::getInstance()->getString(settingName);

			if (!perSystemSettingName.empty() && selectedName.empty())
				selectedName = Settings::getInstance()->getString(settingName);

			for (auto it = themeColorSets.begin(); it != themeColorSets.end() && selectedColorSet == themeColorSets.end(); it++)
				if (it->name == selectedName)
					selectedColorSet = it;

			std::shared_ptr<OptionListComponent<std::string>> item = std::make_shared<OptionListComponent<std::string> >(mWindow, _(("THEME " + Utils::String::toUpper(subset)).c_str()), false);
			item->setTag(!perSystemSettingName.empty()? perSystemSettingName : settingName);

			for (auto it = themeColorSets.begin(); it != themeColorSets.end(); it++)
			{
				std::string displayName = it->displayName;

				if (!systemTheme.empty())
				{
					std::string defaultValue = Settings::getInstance()->getString(settingName);
					if (defaultValue.empty())
						defaultValue = system->getTheme()->getDefaultSubSetValue(subset);

					if (it->name == defaultValue)
						displayName = displayName + " (" + _("DEFAULT") +")";
				}

				item->add(displayName, it->name, it == selectedColorSet);
			}

			if (selectedColorSet == themeColorSets.end())
				item->selectFirstItem();

			if (!themeColorSets.empty())
			{						
				std::string displayName = themeColorSets.cbegin()->subSetDisplayName;
				if (!displayName.empty())
				{
					std::string prefix;

					if (systemTheme.empty())
					{
						for (auto subsetName : themeColorSets.cbegin()->appliesTo)
						{
							std::string pfx = theme->getViewDisplayName(subsetName);
							if (!pfx.empty())
							{
								if (prefix.empty())
									prefix = pfx;
								else
									prefix = prefix + ", " + pfx;								
							}
						}

						if (!prefix.empty())
							prefix = " ("+ prefix+")";

					}					

					themeconfig->addWithLabel(displayName + prefix, item);
				}
				else
					themeconfig->addWithLabel(_(("THEME " + Utils::String::toUpper(subset)).c_str()), item);
			}

			ThemeConfigOption opt;
			opt.component = item;
			opt.subset = subset;			
			opt.defaultSettingName = settingName;
			options[!perSystemSettingName.empty() ? perSystemSettingName : settingName] = opt;
		}
		else
		{
			ThemeConfigOption opt;
			opt.component = nullptr;
			options[!perSystemSettingName.empty() ? perSystemSettingName : settingName] = opt;
		}
	}


	if (systemTheme.empty())
	{
		themeconfig->addEntry(_("RESET GAMELIST CUSTOMIZATIONS"), false, [s, themeconfig, window]
		{
			Settings::getInstance()->setString("GamelistViewStyle", "");
			Settings::getInstance()->setString("DefaultGridSize", "");

			for (auto sysIt = SystemData::sSystemVector.cbegin(); sysIt != SystemData::sSystemVector.cend(); sysIt++)
				(*sysIt)->setSystemViewMode("automatic", Vector2f(0, 0));

			themeconfig->setVariable("reloadAll", true);
			themeconfig->close();
		});


		// File extensions
		if (!system->isCollection() && !system->isGroupSystem())
		{
			auto hiddenExts = Utils::String::split(Settings::getInstance()->getString(system->getName() + ".HiddenExt"), ';');

			auto hiddenCtrl = std::make_shared<OptionListComponent<std::string>>(mWindow, _("FILE EXTENSIONS"), true);

			for (auto ext : system->getExtensions())
			{
				std::string extid = Utils::String::toLower(Utils::String::replace(ext, ".", ""));
				hiddenCtrl->add(ext, extid, std::find(hiddenExts.cbegin(), hiddenExts.cend(), extid) == hiddenExts.cend());
			}

			themeconfig->addWithLabel(_("FILE EXTENSIONS"), hiddenCtrl);
			themeconfig->addSaveFunc([themeconfig, system, hiddenCtrl]
			{
				std::string hiddenSystems;

				std::vector<std::string> sel = hiddenCtrl->getSelectedObjects();

				for (auto ext : system->getExtensions())
				{
					std::string extid = Utils::String::toLower(Utils::String::replace(ext, ".", ""));
					if (std::find(sel.cbegin(), sel.cend(), extid) == sel.cend())
					{
						if (hiddenSystems.empty())
							hiddenSystems = extid;
						else
							hiddenSystems = hiddenSystems + ";" + extid;
					}
				}

				if ((Settings::getInstance()->getString(system->getName() + ".HiddenExt") != hiddenSystems))
				{
					if (Settings::getInstance()->setString(system->getName() + ".HiddenExt", hiddenSystems))
					{
						Settings::getInstance()->saveFile();
						themeconfig->setVariable("reloadAll", true);
						//themeconfig->setVariable("forceReloadGames", true);
						themeconfig->setVariable("reloadGuiMenu", true);
					}
				}
			});
		}
	}

	//  theme_colorset, theme_iconset, theme_menu, theme_systemview, theme_gamelistview, theme_region,
	themeconfig->addSaveFunc([systemTheme, system, themeconfig, theme_set, options, gamelist_style, mGridSize, window]
	{
		bool reloadAll = systemTheme.empty() ? Settings::getInstance()->setString("ThemeSet", theme_set == nullptr ? "" : theme_set->getSelected()) : false;

		for (auto option : options)
		{
			ThemeConfigOption& opt = option.second;

			std::string value;

			if (opt.component != nullptr)
			{
				value = opt.component->getSelected();

				if (!systemTheme.empty() && !value.empty())
				{
					std::string defaultValue = Settings::getInstance()->getString(opt.defaultSettingName);
					if (defaultValue.empty())
						defaultValue = system->getTheme()->getDefaultSubSetValue(opt.subset);

					if (value == defaultValue)
						value = "";
				}
				else if (systemTheme.empty() && value == system->getTheme()->getDefaultSubSetValue(opt.subset))
					value = "";
			}

			if (value != Settings::getInstance()->getString(option.first))
				reloadAll |= Settings::getInstance()->setString(option.first, value);
		}
	
		Vector2f gridSizeOverride(0, 0);

		if (mGridSize != nullptr)
		{
			std::string str = mGridSize->getSelected();
			std::string value = "";

			size_t divider = str.find('x');
			if (divider != std::string::npos)
			{
				std::string first = str.substr(0, divider);
				std::string second = str.substr(divider + 1, std::string::npos);

				gridSizeOverride = Vector2f((float)atof(first.c_str()), (float)atof(second.c_str()));
				value = Utils::String::replace(Utils::String::replace(gridSizeOverride.toString(), ".000000", ""), "0 0", "");
			}

			if (systemTheme.empty())
				reloadAll |= Settings::getInstance()->setString("DefaultGridSize", value);
		}
		else if (systemTheme.empty())
			reloadAll |= Settings::getInstance()->setString("DefaultGridSize", "");
		
		if (systemTheme.empty())
			reloadAll |= Settings::getInstance()->setString("GamelistViewStyle", gamelist_style == nullptr ? "" : gamelist_style->getSelected());
		else
		{
			std::string viewMode = gamelist_style == nullptr ? system->getSystemViewMode() : gamelist_style->getSelected();
			reloadAll |= system->setSystemViewMode(viewMode, gridSizeOverride);
		}

		if (reloadAll || themeconfig->getVariable("reloadAll"))
		{	
			if (themeconfig->getVariable("forceReloadGames"))
			{
				reloadAllGames(window, false);
			}
			else if (systemTheme.empty())
			{				
				CollectionSystemManager::get()->updateSystemsList();
				ViewController::get()->reloadAll(window);
				window->endRenderLoadingScreen();

				if (theme_set != nullptr)
				{
					std::string oldTheme = Settings::getInstance()->getString("ThemeSet");
					Scripting::fireEvent("theme-changed", theme_set->getSelected(), oldTheme);
				}
			}
			else
			{
				system->loadTheme();
				system->resetFilters();
				ViewController::get()->reloadGameListView(system);
			}
		}
	});

	mWindow->pushGui(themeconfig);
}

void GuiMenu::reloadAllGames(Window* window, bool deleteCurrentGui)
{
	window->renderLoadingScreen(_("Loading..."));

	if (!deleteCurrentGui)
	{
		GuiComponent* topGui = window->peekGui();
		window->removeGui(topGui);
	}

	GuiComponent *gui;
	while ((gui = window->peekGui()) != NULL)
	{
		window->removeGui(gui);
		delete gui;
	}

	ViewController::init(window);
	CollectionSystemManager::deinit();
	CollectionSystemManager::init(window);
	SystemData::loadConfig(window);

	ViewController::get()->reloadAll(window);
	//ViewController::get()->reloadAll(nullptr, false); // Avoid reloading themes a second time

	window->pushGui(ViewController::get());
}

void GuiMenu::openUISettings()
{	
	auto pthis = this;
	Window* window = mWindow;

	auto s = new GuiSettings(mWindow, _("UI SETTINGS"));
	
	// theme set
	auto theme = ThemeData::getMenuTheme();
	auto themeSets = ThemeData::getThemeSets();
	auto system = ViewController::get()->getState().getSystem();

	if (!themeSets.empty())
	{
		std::map<std::string, ThemeSet>::const_iterator selectedSet = themeSets.find(Settings::getInstance()->getString("ThemeSet"));
		if (selectedSet == themeSets.cend())
			selectedSet = themeSets.cbegin();

		auto theme_set = std::make_shared< OptionListComponent<std::string> >(mWindow, _("THEME"), false);
		for (auto it = themeSets.cbegin(); it != themeSets.cend(); it++)
			theme_set->add(it->first, it->first, it == selectedSet);

		s->addWithLabel(_("THEME"), theme_set);
		s->addSaveFunc([s, theme_set, window]
		{
			std::string oldTheme = Settings::getInstance()->getString("ThemeSet");
			if (oldTheme != theme_set->getSelected())
			{
				Settings::getInstance()->setString("ThemeSet", theme_set->getSelected());

				// theme changed without setting options, forcing options to avoid crash/blank theme
				Settings::getInstance()->setString("ThemeRegionName", "");
				Settings::getInstance()->setString("ThemeColorSet", "");
				Settings::getInstance()->setString("ThemeIconSet", "");
				Settings::getInstance()->setString("ThemeMenu", "");
				Settings::getInstance()->setString("ThemeSystemView", "");
				Settings::getInstance()->setString("ThemeGamelistView", "");
				Settings::getInstance()->setString("GamelistViewStyle", "");
				Settings::getInstance()->setString("DefaultGridSize", "");

				for(auto sm : Settings::getInstance()->getStringMap())
					if (Utils::String::startsWith(sm.first, "subset."))
						Settings::getInstance()->setString(sm.first, "");

				for (auto sysIt = SystemData::sSystemVector.cbegin(); sysIt != SystemData::sSystemVector.cend(); sysIt++)
					(*sysIt)->setSystemViewMode("automatic", Vector2f(0, 0));

				s->setVariable("reloadCollections", true);
				s->setVariable("reloadAll", true);
				s->setVariable("reloadGuiMenu", true);

				Scripting::fireEvent("theme-changed", theme_set->getSelected(), oldTheme);
			}
		});
	
		bool showThemeConfiguration = system->getTheme()->hasSubsets() || system->getTheme()->hasView("grid");
		if (showThemeConfiguration)
		{
			s->addSubMenu(_("THEME CONFIGURATION"), [this, s, theme_set]() { openThemeConfiguration(mWindow, s, theme_set); });
		}
		else // GameList view style only, acts like Retropie for simple themes
		{
			auto gamelist_style = std::make_shared< OptionListComponent<std::string> >(mWindow, _("GAMELIST VIEW STYLE"), false);
			std::vector<std::pair<std::string, std::string>> styles;
			styles.push_back(std::pair<std::string, std::string>("automatic", _("automatic")));

			auto system = ViewController::get()->getState().getSystem();
			if (system != NULL)
			{
				auto mViews = system->getTheme()->getViewsOfTheme();
				for (auto it = mViews.cbegin(); it != mViews.cend(); ++it)
					styles.push_back(*it);
			}
			else
			{
				styles.push_back(std::pair<std::string, std::string>("basic", _("basic")));
				styles.push_back(std::pair<std::string, std::string>("detailed", _("detailed")));
				styles.push_back(std::pair<std::string, std::string>("video", _("video")));
				styles.push_back(std::pair<std::string, std::string>("grid", _("grid")));
			}

			auto viewPreference = Settings::getInstance()->getString("GamelistViewStyle");
			if (!system->getTheme()->hasView(viewPreference))
				viewPreference = "automatic";

			for (auto it = styles.cbegin(); it != styles.cend(); it++)
				gamelist_style->add(it->second, it->first, viewPreference == it->first);

			s->addWithLabel(_("GAMELIST VIEW STYLE"), gamelist_style);
			s->addSaveFunc([s, gamelist_style, window] 
			{
				if (Settings::getInstance()->setString("GamelistViewStyle", gamelist_style->getSelected()))
				{
					s->setVariable("reloadAll", true);
					s->setVariable("reloadGuiMenu", true);
				}
			});
		}
	}

	// screensaver
	ComponentListRow screensaver_row;
	screensaver_row.elements.clear();
	screensaver_row.addElement(std::make_shared<TextComponent>(mWindow, _("SCREENSAVER SETTINGS"), theme->Text.font, theme->Text.color), true);
	screensaver_row.addElement(makeArrow(mWindow), false);
	screensaver_row.makeAcceptInputHandler(std::bind(&GuiMenu::openScreensaverOptions, this));
	s->addRow(screensaver_row);

	//#ifndef WIN32
		//UI mode
	auto UImodeSelection = std::make_shared< OptionListComponent<std::string> >(mWindow, _("UI MODE"), false);
	std::vector<std::string> UImodes = UIModeController::getInstance()->getUIModes();
	for (auto it = UImodes.cbegin(); it != UImodes.cend(); it++)
		UImodeSelection->add(_(*it), *it, Settings::getInstance()->getString("UIMode") == *it);
	s->addWithLabel(_("UI MODE"), UImodeSelection);

	s->addSaveFunc([UImodeSelection, window]
	{
		std::string selectedMode = UImodeSelection->getSelected();
		if (selectedMode != "Full")
		{
			std::string msg = _("You are changing the UI to a restricted mode:") + "\n" + selectedMode + "\n";
			msg += _("This will hide most menu-options to prevent changes to the system.") + "\n";
			msg += _("To unlock and return to the full UI, enter this code:") + "\n";
			msg += "\"" + UIModeController::getInstance()->getFormattedPassKeyStr() + "\"\n\n";
			msg += _("Do you want to proceed?");
			window->pushGui(new GuiMsgBox(window, msg,
				_("YES"), [selectedMode] {
				LOG(LogDebug) << "Setting UI mode to " << selectedMode;
				Settings::getInstance()->setString("UIMode", selectedMode);
				Settings::getInstance()->saveFile();
			}, _("NO"), nullptr));
		}
	});
	//#endif

	// LANGUAGE
	/*
	std::vector<std::string> langues;
	langues.push_back("en");

	std::string xmlpath = ResourceManager::getInstance()->getResourcePath(":/splash.svg");
	if (xmlpath.length() > 0)
	{
		xmlpath = Utils::FileSystem::getParent(xmlpath) + "/locale/";

		Utils::FileSystem::stringList dirContent = Utils::FileSystem::getDirContent(xmlpath, true);
		for (Utils::FileSystem::stringList::const_iterator it = dirContent.cbegin(); it != dirContent.cend(); ++it)
		{
			if (Utils::FileSystem::isDirectory(*it))
				continue;

			std::string name = *it;

			if (name.rfind("emulationstation2.po") == std::string::npos)
				continue;

			name = Utils::FileSystem::getParent(name);
			name = Utils::FileSystem::getFileName(name);

			if (name != "en")
				langues.push_back(name);
		}

		if (langues.size() > 1)
		{
			auto language = std::make_shared< OptionListComponent<std::string> >(mWindow, _("LANGUAGE"), false);

			for (auto it = langues.cbegin(); it != langues.cend(); it++)
				language->add(*it, *it, Settings::getInstance()->getString("Language") == *it);

			s->addWithLabel(_("LANGUAGE"), language);
			s->addSaveFunc([language, window, pthis, s] {
				
				if (language->getSelected() != Settings::getInstance()->getString("Language"))
				{
					if (Settings::getInstance()->setString("Language", language->getSelected()))
						s->setVariable("reloadGuiMenu", true);
				}
			});
		}
	}
	*/
	// transition style
	auto transition_style = std::make_shared< OptionListComponent<std::string> >(mWindow, _("TRANSITION STYLE"), false);
	std::vector<std::string> transitions;
	transitions.push_back("auto");
	transitions.push_back("fade");
	transitions.push_back("slide");
	transitions.push_back("instant");
	
	for (auto it = transitions.cbegin(); it != transitions.cend(); it++)
		transition_style->add(_(*it), *it, Settings::getInstance()->getString("TransitionStyle") == *it);

	if (!transition_style->hasSelection())
		transition_style->selectFirstItem();

	s->addWithLabel(_("TRANSITION STYLE"), transition_style);
	s->addSaveFunc([transition_style] {
		if (Settings::getInstance()->getString("TransitionStyle") == "instant"
			&& transition_style->getSelected() != "instant"
			&& PowerSaver::getMode() == PowerSaver::INSTANT)
		{
			Settings::getInstance()->setString("PowerSaverMode", "default");
			PowerSaver::init();
		}
		Settings::getInstance()->setString("TransitionStyle", transition_style->getSelected());
		GuiComponent::ALLOWANIMATIONS = Settings::getInstance()->getString("TransitionStyle") != "instant";
	});
	
	auto transitionOfGames_style = std::make_shared< OptionListComponent<std::string> >(mWindow, _("GAME LAUNCH TRANSITION"), false);
	std::vector<std::string> gameTransitions;
	gameTransitions.push_back("fade");
	gameTransitions.push_back("slide");
	gameTransitions.push_back("instant");
	for (auto it = gameTransitions.cbegin(); it != gameTransitions.cend(); it++)
		transitionOfGames_style->add(_(*it), *it, Settings::getInstance()->getString("GameTransitionStyle") == *it);

	s->addWithLabel(_("GAME LAUNCH TRANSITION"), transitionOfGames_style);
	s->addSaveFunc([transitionOfGames_style] {
		if (Settings::getInstance()->getString("GameTransitionStyle") == "instant"
			&& transitionOfGames_style->getSelected() != "instant"
			&& PowerSaver::getMode() == PowerSaver::INSTANT)
		{
			Settings::getInstance()->setString("PowerSaverMode", "default");
			PowerSaver::init();
		}
		Settings::getInstance()->setString("GameTransitionStyle", transitionOfGames_style->getSelected());
	});


	// Optionally start in selected system
	auto systemfocus_list = std::make_shared< OptionListComponent<std::string> >(mWindow, _("START ON SYSTEM"), false);
	systemfocus_list->add(_("NONE"), "", Settings::getInstance()->getString("StartupSystem") == "");

	for (auto it = SystemData::sSystemVector.cbegin(); it != SystemData::sSystemVector.cend(); it++)
		if ("retropie" != (*it)->getName() && (*it)->isVisible())
			systemfocus_list->add((*it)->getName(), (*it)->getName(), Settings::getInstance()->getString("StartupSystem") == (*it)->getName());

	if (!systemfocus_list->hasSelection())
		systemfocus_list->selectFirstItem();

	s->addWithLabel(_("START ON SYSTEM"), systemfocus_list);
	s->addSaveFunc([systemfocus_list] {
		Settings::getInstance()->setString("StartupSystem", systemfocus_list->getSelected());
	});



	// Select systems to hide
	auto hiddenSystems = Utils::String::split(Settings::getInstance()->getString("HiddenSystems"), ';');

	auto displayedSystems = std::make_shared<OptionListComponent<SystemData*>>(mWindow, _("VISIBLE SYSTEMS"), true);

	for (auto system : SystemData::sSystemVector)
		if(!system->isCollection() && !system->isGroupChildSystem())
			displayedSystems->add(system->getFullName(), system, std::find(hiddenSystems.cbegin(), hiddenSystems.cend(), system->getName()) == hiddenSystems.cend());

	s->addWithLabel(_("VISIBLE SYSTEMS"), displayedSystems);
	s->addSaveFunc([s, displayedSystems]
	{
		std::string hiddenSystems;

		std::vector<SystemData*> sys = displayedSystems->getSelectedObjects();

		for (auto system : SystemData::sSystemVector)
		{
			if (system->isCollection() || system->isGroupChildSystem())
				continue;

			if (std::find(sys.cbegin(), sys.cend(), system) == sys.cend())
			{
				if (hiddenSystems.empty())
					hiddenSystems = system->getName();
				else
					hiddenSystems = hiddenSystems + ";" + system->getName();
			}
		}

		if (Settings::getInstance()->setString("HiddenSystems", hiddenSystems))
		{
			Settings::getInstance()->saveFile();
			s->setVariable("reloadAll", true);
		}		
	});

	
	// Open gamelist at start
	auto bootOnGamelist = std::make_shared<SwitchComponent>(mWindow);
	bootOnGamelist->setState(Settings::getInstance()->getBool("StartupOnGameList"));
	s->addWithLabel(_("BOOT ON GAMELIST"), bootOnGamelist);
	s->addSaveFunc([bootOnGamelist] { Settings::getInstance()->setBool("StartupOnGameList", bootOnGamelist->getState()); });

	// Hide system view
	auto hideSystemView = std::make_shared<SwitchComponent>(mWindow);
	hideSystemView->setState(Settings::getInstance()->getBool("HideSystemView"));
	s->addWithLabel(_("HIDE SYSTEM VIEW"), hideSystemView);
	s->addSaveFunc([hideSystemView] 
	{ 
		bool hideSysView = Settings::getInstance()->getBool("HideSystemView");
		Settings::getInstance()->setBool("HideSystemView", hideSystemView->getState());

		if (!hideSysView && hideSystemView->getState())
			ViewController::get()->goToStart(true);
	});


#if defined(_WIN32)
	// quick system select (left/right in game list view)
	auto hideWindowScreen = std::make_shared<SwitchComponent>(mWindow);
	hideWindowScreen->setState(Settings::getInstance()->getBool("HideWindow"));
	s->addWithLabel(_("HIDE WHEN RUNNING GAME"), hideWindowScreen);
	s->addSaveFunc([hideWindowScreen] { Settings::getInstance()->setBool("HideWindow", hideWindowScreen->getState()); });
#endif

	// quick system select (left/right in game list view)
	auto quick_sys_select = std::make_shared<SwitchComponent>(mWindow);
	quick_sys_select->setState(Settings::getInstance()->getBool("QuickSystemSelect"));
	s->addWithLabel(_("QUICK SYSTEM SELECT"), quick_sys_select);
	s->addSaveFunc([quick_sys_select] { Settings::getInstance()->setBool("QuickSystemSelect", quick_sys_select->getState()); });

	// carousel transition option
	auto move_carousel = std::make_shared<SwitchComponent>(mWindow);
	move_carousel->setState(Settings::getInstance()->getBool("MoveCarousel"));
	s->addWithLabel(_("CAROUSEL TRANSITIONS"), move_carousel);
	s->addSaveFunc([move_carousel] {
		if (move_carousel->getState()
			&& !Settings::getInstance()->getBool("MoveCarousel")
			&& PowerSaver::getMode() == PowerSaver::INSTANT)
		{
			Settings::getInstance()->setString("PowerSaverMode", "default");
			PowerSaver::init();
		}
		Settings::getInstance()->setBool("MoveCarousel", move_carousel->getState());
	});

	// clock
	auto clock = std::make_shared<SwitchComponent>(mWindow);
	clock->setState(Settings::getInstance()->getBool("DrawClock"));
	s->addWithLabel(_("SHOW CLOCK"), clock);
	s->addSaveFunc(
		[clock] { Settings::getInstance()->setBool("DrawClock", clock->getState()); });

	// show help
	auto show_help = std::make_shared<SwitchComponent>(mWindow);
	show_help->setState(Settings::getInstance()->getBool("ShowHelpPrompts"));
	s->addWithLabel(_("ON-SCREEN HELP"), show_help);
	s->addSaveFunc([s, show_help]
	{
		if (Settings::getInstance()->setBool("ShowHelpPrompts", show_help->getState()))
			s->setVariable("reloadAll", true);
	});

	// Battery indicator
	if (ApiSystem::getInstance()->getBatteryInformation().hasBattery)
	{
		auto batteryStatus = std::make_shared<OptionListComponent<std::string> >(mWindow, _("SHOW BATTERY STATUS"), false);
		batteryStatus->addRange({ { _("NO"), "" },{ _("ICON"), "icon" },{ _("ICON AND TEXT"), "text" } }, Settings::getInstance()->getString("ShowBattery"));
		s->addWithLabel(_("SHOW BATTERY STATUS"), batteryStatus);
		s->addSaveFunc([s, batteryStatus]
		{
			std::string old_value = Settings::getInstance()->getString("ShowBattery");
			if (old_value != batteryStatus->getSelected())
            {
				Settings::getInstance()->setString("ShowBattery", batteryStatus->getSelected());
				s->setVariable("reloadAll", true);
			}
		});
	}

	// Network indicator
	auto networkIndicator = std::make_shared<SwitchComponent>(mWindow);
	networkIndicator->setState(Settings::getInstance()->getBool("ShowNetworkIndicator"));
	s->addWithLabel(_("SHOW NETWORK ICON"), networkIndicator);
	s->addSaveFunc([s, networkIndicator]
	{
    		if (Settings::getInstance()->setBool("ShowNetworkIndicator", networkIndicator->getState()))
        	s->setVariable("reloadAll", true);
	});

	// filenames
	auto hidden_files = std::make_shared<SwitchComponent>(mWindow);
	hidden_files->setState(Settings::getInstance()->getBool("ShowFilenames"));
	s->addWithLabel(_("SHOW FILENAMES IN LISTS"), hidden_files);
	s->addSaveFunc([hidden_files, s] 
	{ 
		if (Settings::getInstance()->setBool("ShowFilenames", hidden_files->getState()))
		{
			FileData::resetSettings();
			s->setVariable("reloadCollections", true);
			s->setVariable("reloadAll", true);
		}
	});

	auto ignoreArticles = std::make_shared<SwitchComponent>(mWindow);
	ignoreArticles->setState(Settings::getInstance()->getBool("IgnoreLeadingArticles"));
	s->addWithLabel(_("IGNORE LEADING ARTICLES WHEN SORTING"), ignoreArticles);
	s->addSaveFunc([s, ignoreArticles]
	{
		if (Settings::getInstance()->setBool("IgnoreLeadingArticles", ignoreArticles->getState()))
		{
			s->setVariable("reloadAll", true);
		}
	});

	// enable filters (ForceDisableFilters)
	auto enable_filter = std::make_shared<SwitchComponent>(mWindow);
	enable_filter->setState(!Settings::getInstance()->getBool("ForceDisableFilters"));
	s->addWithLabel(_("ENABLE FILTERS"), enable_filter);
	s->addSaveFunc([enable_filter, s] { 
		bool filter_is_enabled = !Settings::getInstance()->getBool("ForceDisableFilters");
		if (Settings::getInstance()->setBool("ForceDisableFilters", !enable_filter->getState()))
			s->setVariable("reloadAll", true);		
	});

    // scan ports
	auto scanPorts = std::make_shared<SwitchComponent>(mWindow);
	scanPorts->setState(Settings::getInstance()->getBool("ScanPorts"));
	s->addWithLabel(_("Scan Ports folder on boot"), scanPorts);
	s->addSaveFunc([scanPorts, s]
	{
		if (Settings::getInstance()->setBool("ScanPorts", scanPorts->getState()))
			s->setVariable("reloadAll", true);
			s->setVariable("reloadGuiMenu", true);
			//mWindow->displayNotificationMessage("Please restart Emulationstation for your changes to take affect");
	});

    // disable kodi
    if (Utils::FileSystem::exists("/usr/local/bin/Kodi.sh")){
	  auto showKodi = std::make_shared<SwitchComponent>(mWindow);
	  showKodi->setState(Settings::getInstance()->getBool("EnableKodi"));
	  s->addWithLabel(_("Show Kodi in Start Menu"), showKodi);
	  s->addSaveFunc([s, showKodi]
	  {
        Settings::getInstance()->setBool("EnableKodi", showKodi->getState());
	  });
    }
	  s->onFinalize([s, pthis, window]
	  {
		if (s->getVariable("reloadCollections"))
			CollectionSystemManager::get()->updateSystemsList();

		if (s->getVariable("reloadAll"))
		{
			ViewController::get()->reloadAll(window);
			window->endRenderLoadingScreen();
		}

		if (s->getVariable("reloadGuiMenu"))
		{
			delete pthis;
			window->pushGui(new GuiMenu(window, false));
		}
	  });
	// Game Loading Image Mode
auto GameLoadingImageMode = std::make_shared<OptionListComponent<std::string>>(mWindow, "Game Loading Image Mode", false);
GameLoadingImageMode->addRange({ {_("PIC"), "pic"},{_("ASCII"), "ascii"},{_("GIF"), "gif"},{_("VID"), "vid"},{_("NONE"), "none"} }, Settings::getInstance()->getString("GameLoadingIMode"));
s->addWithLabel(_("GAME LOADING IMAGE MODE"), GameLoadingImageMode);
s->addSaveFunc([s, GameLoadingImageMode] {
  std::string oldvalue = Settings::getInstance()->getString("GameLoadingIMode");
  if (oldvalue != GameLoadingImageMode->getSelected()) {
    if (strstr(GameLoadingImageMode->getSelected().c_str(),"ascii")) {
      runSystemCommand("rm -f /home/ark/.config/.GameLoadingIMode*", "", nullptr);
      runSystemCommand("touch /home/ark/.config/.GameLoadingIModeASCII", "", nullptr);
    }
    else if (strstr(GameLoadingImageMode->getSelected().c_str(),"pic")) {
      runSystemCommand("rm -f /home/ark/.config/.GameLoadingIMode*", "", nullptr);
      runSystemCommand("touch /home/ark/.config/.GameLoadingIModePIC", "", nullptr);
    }
    else if (strstr(GameLoadingImageMode->getSelected().c_str(),"gif")) {
      runSystemCommand("rm -f /home/ark/.config/.GameLoadingIMode*", "", nullptr);
      runSystemCommand("touch /home/ark/.config/.GameLoadingIModeGIF", "", nullptr);
    }
    else if (strstr(GameLoadingImageMode->getSelected().c_str(),"vid")) {
      runSystemCommand("rm -f /home/ark/.config/.GameLoadingIMode*", "", nullptr);
      runSystemCommand("touch /home/ark/.config/.GameLoadingIModeVID", "", nullptr);
    }
    else {
      runSystemCommand("rm -f /home/ark/.config/.GameLoadingIMode*", "", nullptr);
      runSystemCommand("touch /home/ark/.config/.GameLoadingIModeNO", "", nullptr);
    }
    Settings::getInstance()->setString("GameLoadingIMode", GameLoadingImageMode->getSelected());
  }
});

	// Game Loading Image
    if (strstr(GameLoadingImageMode->getSelected().c_str(),"pic") || strstr(GameLoadingImageMode->getSelected().c_str(),"gif") || strstr(GameLoadingImageMode->getSelected().c_str(),"vid")){
     if (strstr(GameLoadingImageMode->getSelected().c_str(),"pic")){
	 auto GameLoadingImage = std::make_shared<OptionListComponent<std::string> >(mWindow, _("Game Loading Image"), false);
	 GameLoadingImage->addRange({ { _("DEFAULT"), "default" },{ _("MARQUEE"), "marquee" },{ _("IMAGE"), "image" },{ _("THUMB"), "thumb" } }, Settings::getInstance()->getString("GameLoadingImage"));
	 s->addWithLabel(_("  GAME LOADING IMAGE"), GameLoadingImage);
	 s->addSaveFunc([s, GameLoadingImage]
	 {
		std::string old_value = Settings::getInstance()->getString("GameLoadingImage");
		if (old_value != GameLoadingImage->getSelected())
           {
            if (strstr(GameLoadingImage->getSelected().c_str(),"default")) {
              runSystemCommand("[ ! -z $(find /home/ark/.config/.LOADING_IMAGE*) ] && rm /home/ark/.config/.LOADING_IMAGE*", "", nullptr);
            }
            else if (strstr(GameLoadingImage->getSelected().c_str(),"marquee")) {
              runSystemCommand("[ ! -z $(find /home/ark/.config/.LOADING_IMAGE*) ] && rm /home/ark/.config/.LOADING_IMAGE*", "", nullptr);
              runSystemCommand("touch /home/ark/.config/.LOADING_IMAGE_MARQUEE", "", nullptr);
            }
            else if (strstr(GameLoadingImage->getSelected().c_str(),"image")) {
              runSystemCommand("[ ! -z $(find /home/ark/.config/.LOADING_IMAGE*) ] && rm /home/ark/.config/.LOADING_IMAGE*", "", nullptr);
              runSystemCommand("touch /home/ark/.config/.LOADING_IMAGE_IMAGE", "", nullptr);
            }
            else {
              runSystemCommand("[ ! -z $(find /home/ark/.config/.LOADING_IMAGE*) ] && rm /home/ark/.config/.LOADING_IMAGE*", "", nullptr);
              runSystemCommand("touch /home/ark/.config/.LOADING_IMAGE_THUMB", "", nullptr);
            }
			Settings::getInstance()->setString("GameLoadingImage", GameLoadingImage->getSelected());
		   }
	 });
	 }
	// Game Loading Image delay
	 auto ITime = std::make_shared< OptionListComponent<std::string> >(mWindow, _("Game Loading Image Delay (secs)"), false);
	 std::vector<std::string> adelay;
	 adelay.push_back("1.5");
	 adelay.push_back("2");
	 adelay.push_back("2.5");
	 adelay.push_back("3");
	 adelay.push_back("3.5");
	 adelay.push_back("4");
	 adelay.push_back("4.5");
	 adelay.push_back("5");

	 auto delay = Settings::getInstance()->getString("ImagedelayTime");
	 if (delay.empty())
		delay = "1.5";

	 for (auto it = adelay.cbegin(); it != adelay.cend(); it++)
		ITime->add(_(it->c_str()), *it, delay == *it);

	 s->addWithLabel(_("  Game Loading Image delay (secs)"), ITime);
	 s->addSaveFunc([this, ITime] { Settings::getInstance()->setString("ImagedelayTime", ITime->getSelected()); 
		if (ITime->changed()) {
		    runSystemCommand("echo " + Settings::getInstance()->getString("ImagedelayTime") + " > /home/ark/.config/.IMAGEDELAYTIME", "", nullptr);
		}
	 });
	}
	s->updatePosition();
	mWindow->pushGui(s);
}

void GuiMenu::openSystemEmulatorSettings(SystemData* system)
{
	auto theme = ThemeData::getMenuTheme();

	GuiSettings* s = new GuiSettings(mWindow, system->getFullName().c_str());

	auto emul_choice = std::make_shared<OptionListComponent<std::string>>(mWindow, _("EMULATOR"), false);
	auto core_choice = std::make_shared<OptionListComponent<std::string>>(mWindow, _("CORE"), false);

	std::string currentEmul = Settings::getInstance()->getString(system->getName() + ".emulator");
	std::string defaultEmul = (system->getSystemEnvData()->mEmulators.size() == 0 ? "" : system->getSystemEnvData()->mEmulators[0].mName);

//	if (defaultEmul.length() == 0)
		emul_choice->add(_("AUTO"), "", false);
//	else
//		emul_choice->add(_("AUTO") + " (" + defaultEmul + ")", "", currentEmul.length() == 0);

	bool found = false;
	for (auto core : system->getSystemEnvData()->mEmulators)
	{
		if (core.mName == currentEmul)
			found = true;

		emul_choice->add(core.mName, core.mName, core.mName == currentEmul);
	}

	if (!found)
		emul_choice->selectFirstItem();
	
	ComponentListRow row;
	row.addElement(std::make_shared<TextComponent>(mWindow, _("EMULATOR"), theme->Text.font, theme->Text.color), true);
	row.addElement(emul_choice, false);

	s->addRow(row);

	emul_choice->setSelectedChangedCallback([this, system, core_choice](std::string emulatorName)
	{
		std::string currentCore = Settings::getInstance()->getString(system->getName() + ".core");
		std::string defaultCore;
		
		for (auto& emulator : system->getSystemEnvData()->mEmulators)
		{
			if (emulatorName == emulator.mName)
			{
				for (auto core : emulator.mCores)
				{
					defaultCore = core;
					break;
				}
			}
		}

		core_choice->clear();

	//	if (defaultCore.length() == 0)
			core_choice->add(_("AUTO"), "", false);
	//	else
	//		core_choice->add(_("AUTO") + " (" + defaultCore + ")", "", false);

		std::vector<std::string> cores = system->getSystemEnvData()->getCores(emulatorName);

		bool found = false;

		for (auto it = cores.begin(); it != cores.end(); it++)
		{
			std::string core = *it;
			core_choice->add(core, core, currentCore == core);
			if (currentCore == core)
				found = true;
		}

		if (!found)
			core_choice->selectFirstItem();
		else
			core_choice->invalidate();
	});

	row.elements.clear();
	row.addElement(std::make_shared<TextComponent>(mWindow, _("CORE"), theme->Text.font, theme->Text.color), true);
	row.addElement(core_choice, false);
	s->addRow(row);

	// force change event to load core list
	emul_choice->invalidate();

	// set governor
	auto gov_choice = std::make_shared<OptionListComponent<std::string>>(mWindow, _("GOVERNOR"), false);

	gov_choice->clear();

	gov_choice->add(_("AUTO"), "", false);

	std::vector<std::string> governors = system->getSystemEnvData()->allGovernors();
	std::string currentGovernor = Settings::getInstance()->getString(system->getName() + ".governor");

	bool foundgov = false;

	for (auto it = governors.begin(); it != governors.end(); it++)
	{
		std::string govena = *it;
		gov_choice->add(govena, govena, currentGovernor == govena);
		if (currentGovernor == govena)
			foundgov = true;
	}

	if (!foundgov)
		gov_choice->selectFirstItem();
	else
		gov_choice->invalidate();

	s->addWithLabel(_("GOVERNOR"), gov_choice);

	s->addSaveFunc([system, emul_choice, core_choice, gov_choice]
	{		
		Settings::getInstance()->setString(system->getName() + ".emulator", emul_choice->getSelected());
		Settings::getInstance()->setString(system->getName() + ".core", core_choice->getSelected());
		Settings::getInstance()->setString(system->getName() + ".governor", gov_choice->getSelected());
	});

	mWindow->pushGui(s);
}

void GuiMenu::openEmulatorSettings()
{
	GuiSettings* configuration = new GuiSettings(mWindow, _("EMULATOR SETTINGS").c_str());	

	Window* window = mWindow;
	
	// For each activated system
	for (auto system : SystemData::sSystemVector)
	{
		if (system->isCollection())
			continue;

		if (system->getName() == "options")
			continue;

		/*if (system->getSystemEnvData()->mEmulators.size() == 0)
			continue;*/

		/*if (system->getSystemEnvData()->mEmulators.size() == 1 && system->getSystemEnvData()->mEmulators[0].mCores.size() <= 1)
			continue;*/
		
		configuration->addEntry(system->getFullName(), true, [this, system] { openSystemEmulatorSettings(system); });
	}

	window->pushGui(configuration);
}

void GuiMenu::openUpdateSettings()
{
	Window* window = mWindow;
	auto s = new GuiSettings(mWindow, _("DOWNLOADS AND UPDATES"));

	// themes installer/browser
	s->addEntry(_("THEME INSTALLER"), true, [this]
	{
		mWindow->pushGui(new GuiThemeInstall(mWindow));
	});

	// Enable updates
	auto updates_enabled = std::make_shared<SwitchComponent>(mWindow);
	updates_enabled->setState(Settings::getInstance()->getBool("updates.enabled"));
	s->addWithLabel(_("AUTO UPDATES"), updates_enabled);
	s->addSaveFunc([updates_enabled]
	{
		Settings::getInstance()->setBool("updates.enabled", updates_enabled->getState());
	});

	// Start update
	s->addEntry(ApiSystem::state == UpdateState::State::UPDATE_READY ? _("APPLY UPDATE") : _("START UPDATE"), true, [this, s]
	{
		if (ApiSystem::checkUpdateVersion().empty())
		{
			mWindow->pushGui(new GuiMsgBox(mWindow, _("NO UPDATE AVAILABLE")));
			return;
		}

		if (ApiSystem::state == UpdateState::State::UPDATE_READY)
		{
			if (quitES(QuitMode::QUIT))
				LOG(LogWarning) << "Reboot terminated with non-zero result!";
		}
		else if (ApiSystem::state == UpdateState::State::UPDATER_RUNNING)
			mWindow->pushGui(new GuiMsgBox(mWindow, _("UPDATE IS ALREADY RUNNING")));
		else
		{
			ApiSystem::startUpdate(mWindow);

			s->setVariable("closeGuiMenu", true);
			s->close();			
		}
	});

	s->updatePosition();

	auto pthis = this;

	s->onFinalize([s, pthis, window]
	{
		if (s->getVariable("closeGuiMenu"))
			delete pthis;
	});

	mWindow->pushGui(s);

}
	

void GuiMenu::openOtherSettings()
{
	Window* window = mWindow;
	auto s = new GuiSettings(mWindow, _("ADVANCED SETTINGS"));

	/*
	// Emulator settings 
	for (auto system : SystemData::sSystemVector)
	{
		if (system->isCollection() || system->getSystemEnvData()->mEmulators.size() == 0 || (system->getSystemEnvData()->mEmulators.size() == 1 && system->getSystemEnvData()->mEmulators[0].mCores.size() <= 1))
			continue;

		s->addEntry(_("EMULATOR SETTINGS"), true, [this] { openEmulatorSettings(); }, "iconGames");
		break;
	}
	*/

	//Timezone - Adapted from emuelec

	auto es_timezones = std::make_shared<OptionListComponent<std::string> >(mWindow, _("TIMEZONE"), false);

	std::string currentTimezone = SystemConf::getInstance()->get("system.timezone");

	if (currentTimezone.empty())
		currentTimezone = std::string(getShOutput(R"(/usr/local/bin/timezones current)"));
	std::string a;
	for(std::stringstream ss(getShOutput(R"(/usr/local/bin/timezones available)")); getline(ss, a, ','); ) {
		es_timezones->add(a, a, currentTimezone == a);
	}
	s->addWithLabel(_("TIMEZONE"), es_timezones);
	s->addSaveFunc([es_timezones, this] {
		if (es_timezones->changed()) {
			std::string selectedTimezone = es_timezones->getSelected();
			runSystemCommand("sudo ln -sf /usr/share/zoneinfo/" + selectedTimezone + " /etc/localtime", "", nullptr);
			std::string setRepo = std::string(getShOutput(R"(/usr/local/bin/timezones setrepo)"));
			if (!setRepo.empty())
			  mWindow->displayNotificationMessage(setRepo);
		}
		SystemConf::getInstance()->set("system.timezone", es_timezones->getSelected());
	});

	// Clock time format (14:42 or 2:42 pm)
	auto tmFormat = std::make_shared<SwitchComponent>(mWindow);
	tmFormat->setState(Settings::getInstance()->getBool("ClockMode12"));
	s->addWithLabel(_("SHOW CLOCK IN 12-HOUR FORMAT"), tmFormat);
	s->addSaveFunc([tmFormat] { Settings::getInstance()->setBool("ClockMode12", tmFormat->getState()); });

    //Switch A and B buttons
    
	auto invertJoy = std::make_shared<SwitchComponent>(mWindow);
	invertJoy->setState(Settings::getInstance()->getBool("InvertButtons"));
	s->addWithLabel(_("SWITCH A/B BUTTONS IN EMULATIONSTATION"), invertJoy);
	s->addSaveFunc([this, s, invertJoy]
	{
		if (Settings::getInstance()->setBool("InvertButtons", invertJoy->getState()))
		{
			InputConfig::AssignActionButtons();
			ViewController::get()->reloadAll(mWindow);
		}
	});

    //Flip power button suspend/poweroff function
    
	auto powerBtn = std::make_shared<SwitchComponent>(mWindow);
	powerBtn->setState(Settings::getInstance()->getBool("InvertPwrBtn"));
	s->addWithLabel(_("SWITCH POWER BUTTON TAP TO OFF"), powerBtn);
	s->addSaveFunc([this, s, powerBtn]
	{
		if (Settings::getInstance()->setBool("InvertPwrBtn", powerBtn->getState()))
		{
          if (Settings::getInstance()->getBool("InvertPwrBtn") == 1)
            runSystemCommand("[ -z $(find /home/ark/.config/.SWAPPOWERANDSUSPEND) ] && touch /home/ark/.config/.SWAPPOWERANDSUSPEND", "", nullptr);
		  else
            runSystemCommand("[ ! -z $(find /home/ark/.config/.SWAPPOWERANDSUSPEND) ] && rm /home/ark/.config/.SWAPPOWERANDSUSPEND", "", nullptr);
		}
	});

	// power saver
	/*auto power_saver = std::make_shared< OptionListComponent<std::string> >(mWindow, _("POWER SAVER MODES"), false);
	std::vector<std::string> modes;
	modes.push_back("disabled");
	modes.push_back("default");
	modes.push_back("enhanced");
	modes.push_back("instant");
	for (auto it = modes.cbegin(); it != modes.cend(); it++)
		power_saver->add(_(it->c_str()), *it, Settings::getInstance()->getString("PowerSaverMode") == *it);

	s->addWithLabel(_("POWER SAVER MODES"), power_saver);
	s->addSaveFunc([this, power_saver] {
		if (Settings::getInstance()->getString("PowerSaverMode") != "instant" && power_saver->getSelected() == "instant") {
			Settings::getInstance()->setString("TransitionStyle", "instant");
			Settings::getInstance()->setString("GameTransitionStyle", "instant");
			Settings::getInstance()->setBool("MoveCarousel", false);
			Settings::getInstance()->setBool("EnableSounds", false);
		}

		GuiComponent::ALLOWANIMATIONS = Settings::getInstance()->getString("TransitionStyle") != "instant";

		Settings::getInstance()->setString("PowerSaverMode", power_saver->getSelected());
		PowerSaver::init();
	});*/

    //Power LED Status during sleep
    std::string isitpowkiddy=(getShOutput(R"(cat /home/ark/.config/.DEVICE)"));
    if (isitpowkiddy.compare("RGB20PRO")==0) {
	auto pwrLEDsleep = std::make_shared<OptionListComponent<std::string> >(mWindow, _("Power LED Status during Sleep"), false);
	pwrLEDsleep->addRange({ { _("Red"), "Red" },{ _("Dim Red"), "low_power" },{ _("Green"), "Green" },{ _("Off"), "Off" } }, Settings::getInstance()->getString("PowerLEDSleep"));
	s->addWithLabel(_("POWER LED STATUS DURING SLEEP"), pwrLEDsleep);
	s->addSaveFunc([s, pwrLEDsleep]
	{
		std::string old_value = Settings::getInstance()->getString("PowerLEDSleep");
		if (old_value != pwrLEDsleep->getSelected())
           {
            if (strstr(pwrLEDsleep->getSelected().c_str(),"Red")) {
              runSystemCommand("echo Red > /home/ark/.config/.PowerLEDSleep", "", nullptr);
            }
            else if (strstr(pwrLEDsleep->getSelected().c_str(),"low_power")) {
              runSystemCommand("echo low_power > /home/ark/.config/.PowerLEDSleep", "", nullptr);
            }
            else if (strstr(pwrLEDsleep->getSelected().c_str(),"Green")) {
              runSystemCommand("echo Green > /home/ark/.config/.PowerLEDSleep", "", nullptr);
			}
            else {
              runSystemCommand("rm -f /home/ark/.config/.PowerLEDSleep", "", nullptr);
            }
			Settings::getInstance()->setString("PowerLEDSleep", pwrLEDsleep->getSelected());
		   }
	});
    }
	    
	// LANGUAGE

	std::vector<std::string> langues;
	langues.push_back("en");

	std::string xmlpath = ResourceManager::getInstance()->getResourcePath(":/splash.svg");
	if (xmlpath.length() > 0)
	{
		xmlpath = Utils::FileSystem::getParent(xmlpath) + "/locale/";

		Utils::FileSystem::stringList dirContent = Utils::FileSystem::getDirContent(xmlpath, true);
		for (Utils::FileSystem::stringList::const_iterator it = dirContent.cbegin(); it != dirContent.cend(); ++it)
		{
			if (Utils::FileSystem::isDirectory(*it))
				continue;

			std::string name = *it;

			if (name.rfind("emulationstation2.po") == std::string::npos)
				continue;

			name = Utils::FileSystem::createRelativePath(name, xmlpath, false);
			if (Utils::String::startsWith(name, "./"))
			{
				name = name.substr(2);

				while (name.find("/") != std::string::npos)
					name = Utils::FileSystem::getParent(name);
			}
			else
				name = Utils::FileSystem::getParent(name);

			name = Utils::FileSystem::getFileName(name);

			if (name != "en")
				langues.push_back(name);
		}

		if (langues.size() > 1)
		{
			auto language = std::make_shared< OptionListComponent<std::string> >(mWindow, _("LANGUAGE"), false);

			for (auto it = langues.cbegin(); it != langues.cend(); it++)
				language->add(*it, *it, Settings::getInstance()->getString("Language") == *it);

			s->addWithLabel(_("LANGUAGE"), language);
			s->addSaveFunc([language, window, s] {

				if (language->getSelected() != Settings::getInstance()->getString("Language"))
				{
					if (Settings::getInstance()->setString("Language", language->getSelected()))
						s->setVariable("reloadGuiMenu", true);
				}
			});
		}
	}


	// maximum vram
	auto max_vram = std::make_shared<SliderComponent>(mWindow, 40.f, 1000.f, 10.f, "Mb");
	max_vram->setValue((float)(Settings::getInstance()->getInt("MaxVRAM")));
	s->addWithLabel(_("VRAM LIMIT"), max_vram);
	s->addSaveFunc([max_vram] { Settings::getInstance()->setInt("MaxVRAM", (int)Math::round(max_vram->getValue())); });


	/*
#if WIN32

	// Enable updates
	auto updates_enabled = std::make_shared<SwitchComponent>(mWindow);
	updates_enabled->setState(Settings::getInstance()->getBool("updates.enabled"));
	s->addWithLabel(_("AUTO UPDATES"), updates_enabled);
	s->addSaveFunc([updates_enabled]
	{
		Settings::getInstance()->setBool("updates.enabled", updates_enabled->getState());
	});

	// Start update
	s->addEntry(ApiSystem::state == UpdateState::State::UPDATE_READY ? _("APPLY UPDATE") : _("START UPDATE"), true, [this]
	{
		if (ApiSystem::checkUpdateVersion().empty())
		{
			mWindow->pushGui(new GuiMsgBox(mWindow, _("NO UPDATE AVAILABLE")));
			return;
		}

		if (ApiSystem::state == UpdateState::State::UPDATE_READY)
		{
			if (quitES(QuitMode::QUIT))
				LOG(LogWarning) << "Reboot terminated with non-zero result!";
		}
		else if (ApiSystem::state == UpdateState::State::UPDATER_RUNNING)
			mWindow->pushGui(new GuiMsgBox(mWindow, _("UPDATE IS ALREADY RUNNING")));
		else
			ApiSystem::startUpdate(mWindow);
	});
#endif
*/




	// gamelists
	auto save_gamelists = std::make_shared<SwitchComponent>(mWindow);
	save_gamelists->setState(Settings::getInstance()->getBool("SaveGamelistsOnExit"));
	s->addWithLabel(_("SAVE METADATA ON EXIT"), save_gamelists);
	s->addSaveFunc([save_gamelists] { Settings::getInstance()->setBool("SaveGamelistsOnExit", save_gamelists->getState()); });

	auto parse_gamelists = std::make_shared<SwitchComponent>(mWindow);
	parse_gamelists->setState(Settings::getInstance()->getBool("ParseGamelistOnly"));
	s->addWithLabel(_("PARSE GAMESLISTS ONLY"), parse_gamelists);
	s->addSaveFunc([parse_gamelists] { Settings::getInstance()->setBool("ParseGamelistOnly", parse_gamelists->getState()); });
	
#ifndef WIN32
	auto local_art = std::make_shared<SwitchComponent>(mWindow);
	local_art->setState(Settings::getInstance()->getBool("LocalArt"));
	s->addWithLabel(_("SEARCH FOR LOCAL ART"), local_art);
	s->addSaveFunc([local_art] { Settings::getInstance()->setBool("LocalArt", local_art->getState()); });
#endif

	s->addEntry(_("RESET FILE EXTENSIONS"), false, [this, s]
	{
		for (auto system : SystemData::sSystemVector)
			Settings::getInstance()->setString(system->getName() + ".HiddenExt", "");

		Settings::getInstance()->saveFile();
		reloadAllGames(mWindow, false);		
	});

#ifdef _RPI_
	// Video Player - VideoOmxPlayer
	auto omx_player = std::make_shared<SwitchComponent>(mWindow);
	omx_player->setState(Settings::getInstance()->getBool("VideoOmxPlayer"));
	s->addWithLabel("USE OMX PLAYER (HW ACCELERATED)", omx_player);
	s->addSaveFunc([omx_player]
	{
		// need to reload all views to re-create the right video components
		bool needReload = false;
		if(Settings::getInstance()->getBool("VideoOmxPlayer") != omx_player->getState())
			needReload = true;

		Settings::getInstance()->setBool("VideoOmxPlayer", omx_player->getState());

		if(needReload)
			ViewController::get()->reloadAll();
	});

#endif

	// preload UI
	auto preloadUI = std::make_shared<SwitchComponent>(mWindow);
	preloadUI->setState(Settings::getInstance()->getBool("PreloadUI"));
	s->addWithLabel(_("PRELOAD UI"), preloadUI);
	s->addSaveFunc([preloadUI] { Settings::getInstance()->setBool("PreloadUI", preloadUI->getState()); });
	
	// optimizeVram
	auto optimizeVram = std::make_shared<SwitchComponent>(mWindow);
	optimizeVram->setState(Settings::getInstance()->getBool("OptimizeVRAM"));
	s->addWithLabel(_("OPTIMIZE IMAGES VRAM USE"), optimizeVram);
	s->addSaveFunc([optimizeVram]
	{
		TextureData::OPTIMIZEVRAM = optimizeVram->getState();
		Settings::getInstance()->setBool("OptimizeVRAM", optimizeVram->getState());
	});

#ifdef WIN32
	// vsync
	auto vsync = std::make_shared<SwitchComponent>(mWindow);
	vsync->setState(Settings::getInstance()->getBool("VSync"));
	s->addWithLabel(_("VSYNC"), vsync);
	s->addSaveFunc([vsync] 
	{ 
		Settings::getInstance()->setBool("VSync", vsync->getState()); 
		Renderer::setSwapInterval();
	});
#endif

	// framerate	
	auto framerate = std::make_shared<SwitchComponent>(mWindow);
	framerate->setState(Settings::getInstance()->getBool("DrawFramerate"));
	s->addWithLabel(_("SHOW FRAMERATE"), framerate);
	s->addSaveFunc([framerate] { Settings::getInstance()->setBool("DrawFramerate", framerate->getState()); });

	// threaded loading
	auto threadedLoading = std::make_shared<SwitchComponent>(mWindow);
	threadedLoading->setState(Settings::getInstance()->getBool("ThreadedLoading"));
	s->addWithLabel(_("THREADED LOADING"), threadedLoading);
	s->addSaveFunc([threadedLoading] { Settings::getInstance()->setBool("ThreadedLoading", threadedLoading->getState()); });

	// global default emualtor performance governor
	auto gdepg = std::make_shared< OptionListComponent<std::string> >(mWindow, _("Default Emulator Governor"), false);

	EmulatorData GOVs;
	std::vector<std::string> ggovs = GOVs.mGovernors;

	auto ggov = Settings::getInstance()->getString("GlobalPerformanceGovernor");
	if (ggov.empty())
		ggov = "performance";

	for (auto it = ggovs.cbegin(); it != ggovs.cend(); it++)
		gdepg->add(_(it->c_str()), *it, ggov == *it);

	s->addWithLabel(_("Default Emulator Governor"), gdepg);
	s->addSaveFunc([this, gdepg] { Settings::getInstance()->setString("GlobalPerformanceGovernor", gdepg->getSelected()); });

	// Auto Suspend Timeout
	auto Tout = std::make_shared< OptionListComponent<std::string> >(mWindow, _("Auto Suspend Timeout (mins)"), false);
	std::vector<std::string> asuspend;
	asuspend.push_back("Off");
	asuspend.push_back("5");
	asuspend.push_back("10");
	asuspend.push_back("15");
	asuspend.push_back("20");
	asuspend.push_back("25");
	asuspend.push_back("30");
	asuspend.push_back("35");
	asuspend.push_back("40");
	asuspend.push_back("45");
	asuspend.push_back("50");
	asuspend.push_back("55");
	asuspend.push_back("60");

	auto suspend = Settings::getInstance()->getString("AutoSuspendTimeout");
	if (suspend.empty())
		suspend = "Off";

	for (auto it = asuspend.cbegin(); it != asuspend.cend(); it++)
		Tout->add(_(it->c_str()), *it, suspend == *it);

	s->addWithLabel(_("Auto Suspend Timeout (mins)"), Tout);
	s->addSaveFunc([this, Tout] { Settings::getInstance()->setString("AutoSuspendTimeout", Tout->getSelected()); 
		if (Tout->changed()) {
		    runSystemCommand("echo " + Settings::getInstance()->getString("AutoSuspendTimeout") + " > /home/ark/.config/.TIMEOUT", "", nullptr);
		    runSystemCommand("/usr/local/bin/auto_suspend_update.sh", "", nullptr);
		}
	});

#ifndef _RPI_
	// full exit
	auto fullExitMenu = std::make_shared<SwitchComponent>(mWindow);
	fullExitMenu->setState(!Settings::getInstance()->getBool("ShowOnlyExit"));
	s->addWithLabel(_("COMPLETE QUIT MENU"), fullExitMenu);
	s->addSaveFunc([fullExitMenu] { Settings::getInstance()->setBool("ShowOnlyExit", !fullExitMenu->getState()); });
#endif

	// log level
	auto logLevel = std::make_shared< OptionListComponent<std::string> >(mWindow, _("LOG LEVEL"), false);
	std::vector<std::string> levels;
	levels.push_back("default");
	levels.push_back("disabled");
	levels.push_back("warning");
	levels.push_back("error");
	levels.push_back("debug");

	auto level = Settings::getInstance()->getString("LogLevel");
	if (level.empty())
		level = "default";

	for (auto it = levels.cbegin(); it != levels.cend(); it++)
		logLevel->add(_(it->c_str()), *it, level == *it);

	s->addWithLabel(_("LOG LEVEL"), logLevel);
	s->addSaveFunc([this, logLevel]
	{
		if (Settings::getInstance()->setString("LogLevel", logLevel->getSelected() == "default" ? "" : logLevel->getSelected()))
		{
			Log::setupReportingLevel();
			Log::init();
		}
	});

	// rumble
    if (isitpowkiddy.compare("RGB30")==0 || isitpowkiddy.compare("RK2023")==0) {
	  auto RumbleStatus = std::make_shared<SwitchComponent>(mWindow);
	  RumbleStatus->setState(Settings::getInstance()->getBool("EnableRumble"));
	  s->addWithLabel(_("OPTIONAL RUMBLE MOTOR"), RumbleStatus);
	  s->addSaveFunc([this, s, RumbleStatus]
	  {
		  if (Settings::getInstance()->setBool("EnableRumble", RumbleStatus->getState()))
		  {
            if (Settings::getInstance()->getBool("EnableRumble") == 1)
              runSystemCommand("[ -z $(sudo grep enable_vibration /var/spool/cron/crontabs/root) ] && /usr/local/bin/enable_vibration.sh && echo '@reboot /usr/local/bin/enable_vibration.sh &' | sudo tee -a /var/spool/cron/crontabs/root && sleep 0.5 && echo 0 | sudo tee /sys/class/pwm/pwmchip1/pwm0/enable && sleep 0.1 && echo 1 | sudo tee /sys/class/pwm/pwmchip1/pwm0/enable && echo 0 | sudo tee /sys/class/pwm/pwmchip1/pwm0/enable && sleep 0.1 && echo 1 | sudo tee /sys/class/pwm/pwmchip1/pwm0/enable && echo 0 | sudo tee /sys/class/pwm/pwmchip1/pwm0/enable && sleep 0.2 && echo 1 | sudo tee /sys/class/pwm/pwmchip1/pwm0/enable &", "", nullptr);
		    else
              runSystemCommand("sudo sed -i '/@reboot \\/usr\\/local\\/bin\\/enable_vibration.sh/d' /var/spool/cron/crontabs/root", "", nullptr);
		  }
	  }); }

	s->updatePosition();

	auto pthis = this;

	s->onFinalize([s, pthis, window]
	{
		if (s->getVariable("reloadGuiMenu"))
		{
			delete pthis;
			window->pushGui(new GuiMenu(window, false));
		}
	});

	mWindow->pushGui(s);

}

void GuiMenu::openConfigInput()
{
	Window* window = mWindow;
	//window->pushGui(new GuiDetectDevice(window, false, nullptr));
		
	window->pushGui(new GuiMsgBox(window, _("ARE YOU SURE YOU WANT TO CONFIGURE INPUT?"), _("YES"),
		[window] {
		window->pushGui(new GuiDetectDevice(window, false, nullptr));
	}, _("NO"), nullptr)
	);

}

void GuiMenu::openQuitMenu()
{
	if (Settings::getInstance()->getBool("ShowOnlyExit"))
	{
		Scripting::fireEvent("quit");
		quitES();
		return;
	}

	auto s = new GuiSettings(mWindow, _("QUIT"));

	Window* window = mWindow;

	ComponentListRow row;
	if (UIModeController::getInstance()->isUIModeFull())
	{
#ifndef WIN32
		// Restart does not work on Windows
		row.makeAcceptInputHandler([window] {
			window->pushGui(new GuiMsgBox(window, _("REALLY RESTART?"), _("YES"),
				[] {
				Scripting::fireEvent("quit");
				if(quitES(QuitMode::RESTART) != 0)
					LOG(LogWarning) << "Restart terminated with non-zero result!";
			}, _("NO"), nullptr));
		});
		row.addElement(std::make_shared<TextComponent>(window, _("RESTART EMULATIONSTATION"), ThemeData::getMenuTheme()->Text.font, ThemeData::getMenuTheme()->Text.color), true);
		s->addRow(row);
#endif

		if(Settings::getInstance()->getBool("ShowExit"))
		{
			row.elements.clear();
			row.makeAcceptInputHandler([window] {
				window->pushGui(new GuiMsgBox(window, _("REALLY QUIT?"), _("YES"),
					[] {
					Scripting::fireEvent("quit");
					quitES();
				}, _("NO"), nullptr));
			});
			row.addElement(std::make_shared<TextComponent>(window, _("QUIT EMULATIONSTATION"), ThemeData::getMenuTheme()->Text.font, ThemeData::getMenuTheme()->Text.color), true);
			s->addRow(row);
		}
	}
	row.elements.clear();
	row.makeAcceptInputHandler([window] {
		window->pushGui(new GuiMsgBox(window, _("REALLY RESTART?"), _("YES"),
			[] {
			Scripting::fireEvent("quit", "reboot");
			Scripting::fireEvent("reboot");
			if (quitES(QuitMode::REBOOT) != 0)
				LOG(LogWarning) << "Restart terminated with non-zero result!";
		}, _("NO"), nullptr));
	});
	row.addElement(std::make_shared<TextComponent>(window, _("RESTART SYSTEM"), ThemeData::getMenuTheme()->Text.font, ThemeData::getMenuTheme()->Text.color), true);
	s->addRow(row);

	row.elements.clear();
	row.makeAcceptInputHandler([window] {
		window->pushGui(new GuiMsgBox(window, _("REALLY SHUTDOWN?"), _("YES"),
			[] {
			Scripting::fireEvent("quit", "shutdown");
			Scripting::fireEvent("shutdown");
			if (quitES(QuitMode::SHUTDOWN) != 0)
				LOG(LogWarning) << "Shutdown terminated with non-zero result!";
		}, _("NO"), nullptr));
	});
	row.addElement(std::make_shared<TextComponent>(window, _("SHUTDOWN SYSTEM"), ThemeData::getMenuTheme()->Text.font, ThemeData::getMenuTheme()->Text.color), true);
	s->addRow(row);

	s->updatePosition();
	mWindow->pushGui(s);
}

std::string getBuildTime()
{
	std::string datestr = __DATE__;
	std::string timestr = __TIME__;

	std::istringstream iss_date(datestr);
	std::string str_month;
	int day;
	int year;
	iss_date >> str_month >> day >> year;

	int month;
	if (str_month == "Jan") month = 1;
	else if (str_month == "Feb") month = 2;
	else if (str_month == "Mar") month = 3;
	else if (str_month == "Apr") month = 4;
	else if (str_month == "May") month = 5;
	else if (str_month == "Jun") month = 6;
	else if (str_month == "Jul") month = 7;
	else if (str_month == "Aug") month = 8;
	else if (str_month == "Sep") month = 9;
	else if (str_month == "Oct") month = 10;
	else if (str_month == "Nov") month = 11;
	else if (str_month == "Dec") month = 12;
	else exit(-1);

	for (std::string::size_type pos = timestr.find(':'); pos != std::string::npos; pos = timestr.find(':', pos))
		timestr[pos] = ' ';

	std::istringstream iss_time(timestr);
	int hour, min, sec;
	iss_time >> hour >> min >> sec;

	char buffer[100];
	
	sprintf(buffer, "%4d%.2d%.2d%.2d%.2d%.2d\n", year, month, day, hour, min, sec);

	return buffer;
}

void GuiMenu::addVersionInfo()
{
	std::string  buildDate = getBuildTime();
	//	(Settings::getInstance()->getBool("Debug") ? std::string( "   (" + Utils::String::toUpper(PROGRAM_BUILT_STRING) + ")") : (""));

	auto theme = ThemeData::getMenuTheme();
//	mVersion.setFont(Font::get(FONT_SIZE_SMALL));
//	mVersion.setColor(0x5E5E5EFF);

	mVersion.setFont(theme->Footer.font);
	mVersion.setColor(theme->Footer.color);

	mVersion.setLineSpacing(0);

#if WIN32
	std::string localVersion;
	std::string localVersionFile = Utils::FileSystem::getExePath() + "/version.info";
	if (Utils::FileSystem::exists(localVersionFile))
	{
		localVersion = Utils::FileSystem::readAllText(localVersionFile);
		localVersion = Utils::String::replace(Utils::String::replace(localVersion, "\r", ""), "\n", "");	
		mVersion.setText("EMULATIONSTATION V" + localVersion+" FCAMOD");	
	}
	else
#endif
	if (!std::string(getShOutput(R"(blkid | grep mmcblk0)")).empty())
		mVersion.setText("EMMC | EMULATIONSTATION V" + Utils::String::toUpper(PROGRAM_VERSION_STRING) + " BUILD " + buildDate);
    else
		mVersion.setText("EMULATIONSTATION V" + Utils::String::toUpper(PROGRAM_VERSION_STRING) + " BUILD " + buildDate);

	mVersion.setHorizontalAlignment(ALIGN_CENTER);	
	mVersion.setVerticalAlignment(ALIGN_CENTER);
	addChild(&mVersion);
}

void GuiMenu::openScreensaverOptions() {
	mWindow->pushGui(new GuiGeneralScreensaverOptions(mWindow, _("SCREENSAVER SETTINGS")));
}

void GuiMenu::openCollectionSystemSettings() 
{
	if (ThreadedScraper::isRunning())
	{
		mWindow->pushGui(new GuiMsgBox(mWindow, _("THIS FUNCTION IS DISABLED WHEN SCRAPING IS RUNNING")));
		return;
	}

	mWindow->pushGui(new GuiCollectionSystemsOptions(mWindow));
}

void GuiMenu::onSizeChanged()
{
	float h = mMenu.getButtonGridHeight();

	mVersion.setSize(mSize.x(), h);
	mVersion.setPosition(0, mSize.y() - h); //  mVersion.getSize().y()
}

void GuiMenu::addEntry(std::string name, bool add_arrow, const std::function<void()>& func, const std::string iconName)
{
	auto theme = ThemeData::getMenuTheme();
	std::shared_ptr<Font> font = theme->Text.font;
	unsigned int color = theme->Text.color;

	// populate the list
	ComponentListRow row;

	if (!iconName.empty())
	{
		std::string iconPath = theme->getMenuIcon(iconName);
		if (!iconPath.empty())
		{
			// icon
			auto icon = std::make_shared<ImageComponent>(mWindow);
			icon->setImage(iconPath);
			icon->setColorShift(theme->Text.color);
			icon->setResize(0, theme->Text.font->getLetterHeight() * 1.25f);
			row.addElement(icon, false);

			// spacer between icon and text
			auto spacer = std::make_shared<GuiComponent>(mWindow);
			spacer->setSize(10, 0);
			row.addElement(spacer, false);
		}
	}

	row.addElement(std::make_shared<TextComponent>(mWindow, name, font, color), true);

	if (add_arrow)
	{
		std::shared_ptr<ImageComponent> bracket = makeArrow(mWindow);
		row.addElement(bracket, false);
	}

	row.makeAcceptInputHandler(func);
	mMenu.addRow(row);
}

bool GuiMenu::input(InputConfig* config, Input input)
{
	if(GuiComponent::input(config, input))
		return true;

	if((config->isMappedTo(BUTTON_BACK, input) || config->isMappedTo("start", input)) && input.value != 0)
	{
		delete this;
		return true;
	}

	return false;
}

HelpStyle GuiMenu::getHelpStyle()
{
	HelpStyle style = HelpStyle();

	if (ThemeData::getDefaultTheme() != nullptr)
	{
		std::shared_ptr<ThemeData> theme = std::shared_ptr<ThemeData>(ThemeData::getDefaultTheme(), [](ThemeData*) {});
		style.applyTheme(theme, "system");
	}
	else 
		style.applyTheme(ViewController::get()->getState().getSystem()->getTheme(), "system");

	return style;
}

std::vector<HelpPrompt> GuiMenu::getHelpPrompts()
{
	std::vector<HelpPrompt> prompts;
	prompts.push_back(HelpPrompt("up/down", _("CHOOSE")));
	prompts.push_back(HelpPrompt(BUTTON_OK, _("SELECT")));
	prompts.push_back(HelpPrompt("start", _("CLOSE")));
	return prompts;
}

void GuiMenu::createInputTextRow(GuiSettings *gui, std::string title, const char *settingsID, bool password)
{
	auto theme = ThemeData::getMenuTheme();
	std::shared_ptr<Font> font = theme->Text.font;
	unsigned int color = theme->Text.color;

	// LABEL
	Window *window = mWindow;
	ComponentListRow row;

	auto lbl = std::make_shared<TextComponent>(window, title, font, color);
	row.addElement(lbl, true); // label

	std::shared_ptr<GuiComponent> ed;

	std::string value = Settings::getInstance()->getString(settingsID);

	ed = std::make_shared<TextComponent>(window, ((password && value != "") ? "*********" : value), font, color, ALIGN_RIGHT); // Font::get(FONT_SIZE_MEDIUM, FONT_PATH_LIGHT)
	row.addElement(ed, true);

	auto spacer = std::make_shared<GuiComponent>(mWindow);
	spacer->setSize(Renderer::getScreenWidth() * 0.005f, 0);
	row.addElement(spacer, false);

	auto bracket = std::make_shared<ImageComponent>(mWindow);
	bracket->setImage(theme->Icons.arrow);
	bracket->setResize(Vector2f(0, lbl->getFont()->getLetterHeight()));
	row.addElement(bracket, false);

	auto updateVal = [ed, settingsID, password](const std::string &newVal) {
		if (!password)
			ed->setValue(newVal);
		else {
			ed->setValue("*********");
		}

		Settings::getInstance()->setString(settingsID, newVal);
	}; // ok callback (apply new value to ed)

	row.makeAcceptInputHandler([this, title, updateVal, settingsID]
	{
		std::string data = Settings::getInstance()->getString(settingsID);
		mWindow->pushGui(new GuiTextEditPopupKeyboard(mWindow, title, data, updateVal, false));
	});

	gui->addRow(row);
}
