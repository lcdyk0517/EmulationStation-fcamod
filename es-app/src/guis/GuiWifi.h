#pragma once
#ifndef ES_APP_GUIS_GUI_WIFI_H
#define ES_APP_GUIS_GUI_WIFI_H

#include "guis/GuiSettings.h"

// Wi-Fi 列表页 (nmcli 后端), 布局与 ES 其他设置页一致
class GuiWifi : public GuiSettings
{
public:
	explicit GuiWifi(Window* window);

private:
	void load();
	void refresh();
	void connectTo(const std::string& ssid, const std::string& password);
};

#endif
