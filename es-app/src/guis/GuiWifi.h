#pragma once
#ifndef ES_APP_GUIS_GUI_WIFI_H
#define ES_APP_GUIS_GUI_WIFI_H

#include <string>

#include "guis/GuiSettings.h"

// Wi-Fi 列表页 (nmcli 后端), 布局与 ES 其他设置页一致。
// 有多个无线网口 (如内置 wlan0 + USB 网卡) 时, 列表顶部多一项当前网口,
// 按下循环切换, 切换后重新扫描并列出该网口的网络。
class GuiWifi : public GuiSettings
{
public:
	explicit GuiWifi(Window* window);
	GuiWifi(Window* window, std::string iface);

private:
	void load();
	void refresh();
	void connectTo(const std::string& ssid, const std::string& password);
	void nextInterface();

	std::string iface_;
};

#endif
