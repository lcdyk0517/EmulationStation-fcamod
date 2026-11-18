#include "guis/GuiWifi.h"

#include <algorithm>
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

// One nmcli device line: DEVICE:TYPE:STATE. P2P and unmanaged devices are not
// station interfaces, so neither takes part here.
bool parseDevice(const std::string& line, std::string& device, std::string& type, std::string& state)
{
	const auto p = line.find(':');
	if (p == std::string::npos)
		return false;
	const auto q = line.find(':', p + 1);
	if (q == std::string::npos)
		return false;
	device = line.substr(0, p);
	type = line.substr(p + 1, q - p - 1);
	state = line.substr(q + 1);
	return true;
}

std::vector<std::string> wifiDevices()
{
	std::vector<std::string> devices;
	for (const auto& line : runLines("nmcli -t -f DEVICE,TYPE,STATE device 2>/dev/null"))
	{
		std::string device, type, state;
		if (parseDevice(line, device, type, state) && type == "wifi" && state != "unmanaged")
			devices.push_back(device);
	}
	return devices;
}

std::string connectedWifi()
{
	for (const auto& line : runLines("nmcli -t -f DEVICE,TYPE,STATE device 2>/dev/null"))
	{
		std::string device, type, state;
		if (parseDevice(line, device, type, state) && type == "wifi" && state == "connected")
			return device;
	}
	return {};
}
}  // namespace

GuiWifi::GuiWifi(Window* window)
	: GuiWifi(window, std::string())
{
}

GuiWifi::GuiWifi(Window* window, std::string iface)
	: GuiSettings(window, _("WIFI LIST")), iface_(std::move(iface))
{
	load();
}

void GuiWifi::load()
{
	// With one wireless interface there is nothing to pick and the commands
	// stay interface-less, exactly as before. A chosen interface that has gone
	// away (USB adapter unplugged) falls back to the connected one.
	const std::vector<std::string> ifaces = wifiDevices();
	if (iface_.empty() || std::find(ifaces.begin(), ifaces.end(), iface_) == ifaces.end())
	{
		const std::string connected = connectedWifi();
		if (!connected.empty() && std::find(ifaces.begin(), ifaces.end(), connected) != ifaces.end())
			iface_ = connected;
		else if (std::find(ifaces.begin(), ifaces.end(), "wlan0") != ifaces.end())
			iface_ = "wlan0";
		else if (!ifaces.empty())
			iface_ = ifaces.front();
	}

	const std::string ifname = iface_.empty() ? std::string() : " ifname " + quote(iface_);

	// Entering the page rescans, so the list is not whatever the last scan left.
	if (!iface_.empty())
		system(("sudo nmcli device wifi rescan" + ifname + " >/dev/null 2>&1").c_str());

	const auto active = runLines("nmcli -t -f ACTIVE,SSID device wifi list" + ifname + " 2>/dev/null | sed -n 's/^yes://p'");
	const std::string current = active.empty() ? std::string(_("NOT CONNECTED")) : active[0];

	addEntry(_("CURRENT") + ": " + current, false, nullptr);

	if (ifaces.size() > 1)
		addEntry(_("WIFI INTERFACE") + ": " + iface_, true, [this] { nextInterface(); }, "iconNetwork");

	std::vector<std::pair<std::string, bool>> networks;

	for (const auto& entry : runLines("nmcli -t -f SSID,SECURITY device wifi list" + ifname + " 2>/dev/null"))
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

void GuiWifi::nextInterface()
{
	const std::vector<std::string> ifaces = wifiDevices();
	if (ifaces.size() < 2)
		return;

	auto it = std::find(ifaces.begin(), ifaces.end(), iface_);
	if (it == ifaces.end() || ++it == ifaces.end())
		it = ifaces.begin();

	Window* window = mWindow;
	window->pushGui(new GuiWifi(window, *it));
	close();
}

void GuiWifi::refresh()
{
	if (!iface_.empty())
		system(("sudo nmcli device wifi rescan ifname " + quote(iface_) + " >/dev/null 2>&1").c_str());

	Window* window = mWindow;
	window->pushGui(new GuiWifi(window, iface_));
	close();
}

void GuiWifi::connectTo(const std::string& ssid, const std::string& password)
{
	std::string command = "sudo nmcli device wifi connect " + quote(ssid);
	if (!iface_.empty())
		command += " ifname " + quote(iface_);
	if (!password.empty())
		command += " password " + quote(password);

	LOG(LogInfo) << "Wifi: connecting to " << ssid << (iface_.empty() ? "" : " on " + iface_);

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
