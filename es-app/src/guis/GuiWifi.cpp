#include "guis/GuiWifi.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "guis/GuiMsgBox.h"
#include "guis/GuiTextEditPopupKeyboard.h"
#include "EsLocale.h"
#include "Log.h"
#include "Window.h"

namespace
{
std::vector<std::string> runLines(const std::string& command)
{
	std::vector<std::string> lines;

	FILE* pipe = popen(command.c_str(), "r");
	if (!pipe)
		return lines;

	char buffer[512];
	while (fgets(buffer, sizeof(buffer), pipe) != nullptr)
	{
		std::string line(buffer);
		while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
			line.pop_back();
		if (!line.empty())
			lines.push_back(line);
	}

	pclose(pipe);
	return lines;
}

std::string quote(const std::string& value)
{
	std::string out = "\"";
	for (const char c : value)
	{
		if (c == '"' || c == '\\')
			out += '\\';
		out += c;
	}
	out += '"';
	return out;
}
}  // namespace

GuiWifi::GuiWifi(Window* window)
	: GuiSettings(window, _("WIFI LIST"))
{
	load();
}

void GuiWifi::load()
{
	const auto active = runLines("nmcli -t -f ACTIVE,SSID device wifi 2>/dev/null | sed -n 's/^yes://p'");
	const std::string current = active.empty() ? std::string(_("NOT CONNECTED")) : active[0];

	addEntry(_("CURRENT") + ": " + current, false, nullptr);

	std::vector<std::pair<std::string, bool>> networks;

	for (const auto& entry : runLines("nmcli -t -f SSID,SECURITY device wifi list 2>/dev/null"))
	{
		const auto pos = entry.rfind(':');
		if (pos == std::string::npos)
			continue;

		const std::string ssid = entry.substr(0, pos);
		const std::string security = entry.substr(pos + 1);

		if (ssid.empty())
			continue;

		bool duplicate = false;
		for (const auto& network : networks)
		{
			if (network.first == ssid)
			{
				duplicate = true;
				break;
			}
		}
		if (duplicate)
			continue;

		const bool secure = !security.empty() && security != "--";
		networks.emplace_back(ssid, secure);
	}

	if (networks.empty())
	{
		addEntry(_("NO WI-FI NETWORKS FOUND"), false, [this] { refresh(); });
	}
	else
	{
		for (const auto& network : networks)
		{
			const std::string ssid = network.first;
			const bool secure = network.second;

			addEntry(ssid, true, [this, ssid, secure]
			{
				if (!secure)
				{
					connectTo(ssid, "");
					return;
				}

				mWindow->pushGui(new GuiTextEditPopupKeyboard(mWindow, ssid, "",
					[this, ssid](const std::string& password)
					{
						connectTo(ssid, password);
					}, false));
			}, "iconNetwork");
		}
	}

	addEntry(_("REFRESH"), true, [this] { refresh(); }, "iconRefresh");
	addEntry(_("INPUT MANUALLY"), true, [this]
	{
		mWindow->pushGui(new GuiTextEditPopupKeyboard(mWindow, _("WIFI SSID"), "",
			[this](const std::string& ssid)
			{
				if (!ssid.empty())
					connectTo(ssid, "");
			}, false));
	}, "iconNetwork");
}

void GuiWifi::refresh()
{
	system("sudo nmcli device wifi rescan >/dev/null 2>&1");

	Window* window = mWindow;
	window->pushGui(new GuiWifi(window));
	close();
}

void GuiWifi::connectTo(const std::string& ssid, const std::string& password)
{
	std::string command = "sudo nmcli device wifi connect " + quote(ssid);
	if (!password.empty())
		command += " password " + quote(password);

	LOG(LogInfo) << "Wifi: connecting to " << ssid;

	const int result = system((command + " >/tmp/es_wifi_connect.log 2>&1").c_str());
	if (result == 0)
	{
		mWindow->pushGui(new GuiMsgBox(mWindow, _("CONNECTED"), _("OK"), [this] { refresh(); }));
	}
	else
	{
		mWindow->pushGui(new GuiMsgBox(mWindow, _("CONNECTION FAILED"), _("OK"), nullptr, ICON_ERROR));
	}
}
