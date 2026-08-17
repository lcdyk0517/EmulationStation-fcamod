#include "guis/arkos4clone/WifiManager.h"
#include "guis/arkos4clone/ArkOSUtil.h"
#include "utils/StringUtil.h"

#include <sstream>
#include <regex>
#include <algorithm>
#include <cctype>
#include <vector>

namespace WifiManager
{

// Check if WiFi is enabled (not blocked by rfkill)
bool isWifiRfkillBlocked()
{
    std::string result = ArkOSUtil::executeCommand("rfkill list wifi 2>/dev/null | grep -i 'Soft blocked' | head -1");
    return result.find("yes") != std::string::npos;
}

// Get active WiFi interface (wlan0, p2p0, etc.)
std::string getActiveWifiInterface()
{
    // Check for connected wifi devices via nmcli
    std::string result = ArkOSUtil::executeCommand("nmcli -t -f DEVICE,TYPE,STATE dev 2>/dev/null");
    if (!result.empty()) {
        std::istringstream stream(result);
        std::string line;
        while (std::getline(stream, line)) {
            // Format: device:type:state
            if (line.find(":wifi:") != std::string::npos && line.find(":connected") != std::string::npos) {
                size_t colonPos = line.find(':');
                if (colonPos != std::string::npos) {
                    std::string iface = line.substr(0, colonPos);
                    if (!iface.empty()) return iface;
                }
            }
        }
    }

    // Fallback: check operstate of common wifi interfaces
    std::vector<std::string> wifiInterfaces = {"p2p0", "wlan0", "wlan1"};
    for (const auto& iface : wifiInterfaces) {
        std::string operstate = ArkOSUtil::executeCommand("cat /sys/class/net/" + iface + "/operstate 2>/dev/null");
        if (operstate == "up") return iface;
    }

    return "wlan0"; // Default fallback
}

std::string getCurrentWifiSSID()
{
    // Method 1: nmcli active connection - check all wifi interfaces
    std::string result = ArkOSUtil::executeCommand("nmcli -t -f NAME,DEVICE connection show --active 2>/dev/null");
    if (!result.empty()) {
        std::istringstream stream(result);
        std::string line;
        while (std::getline(stream, line)) {
            // Check for wlan, p2p, or any wifi interface
            if (line.find(":wlan") != std::string::npos ||
                line.find(":p2p") != std::string::npos ||
                line.find("wlan") != std::string::npos ||
                line.find("p2p") != std::string::npos) {
                size_t colonPos = line.find(':');
                if (colonPos != std::string::npos) {
                    std::string connName = line.substr(0, colonPos);
                    if (!connName.empty() && connName != "lo") {
                        return connName;
                    }
                }
            }
        }
    }

    // Method 2: iw dev - check active interface
    std::string iface = getActiveWifiInterface();
    std::string ssid = ArkOSUtil::executeCommand("iw dev " + iface + " info 2>/dev/null | grep ssid");
    if (!ssid.empty()) {
        size_t pos = ssid.find("ssid ");
        if (pos != std::string::npos) {
            ssid = ssid.substr(pos + 5);
            ssid.erase(std::remove_if(ssid.begin(), ssid.end(), ::isspace), ssid.end());
            if (!ssid.empty() && ssid != "off/any") {
                return ssid;
            }
        }
    }

    return "";
}

bool isWifiEnabled()
{
    return !isWifiRfkillBlocked();
}

void toggleWifi(bool enable)
{
    if (enable) {
        ArkOSUtil::executeCommand("sudo rfkill unblock wifi 2>/dev/null");
    } else {
        ArkOSUtil::executeCommand("sudo rfkill block wifi 2>/dev/null");
    }
}

bool isRemoteServicesEnabled()
{
    // Check if sshd process is running as indicator
    std::string result = ArkOSUtil::executeCommand("pgrep -x sshd 2>/dev/null");
    return !result.empty();
}

std::string getIpAddress()
{
    std::string ip = ArkOSUtil::executeCommand("ip route | awk '/src/ { print $9; exit }' 2>/dev/null");
    ip = Utils::String::trim(ip);

    // Extract only IP address pattern (xxx.xxx.xxx.xxx) using regex
    std::regex ipPattern("(\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3})");
    std::smatch match;
    if (std::regex_search(ip, match, ipPattern)) {
        return match[1].str();
    }
    return ip;
}

void toggleRemoteServices(bool enable)
{
    if (enable) {
        // Check if network is available
        std::string gateway = ArkOSUtil::executeCommand("ip route | awk '/default/ { print $3; exit }' 2>/dev/null");
        if (Utils::String::trim(gateway).empty()) {
            return; // No network connection
        }

        // Enable NetworkManager-wait-online
        ArkOSUtil::executeCommand("sudo systemctl enable NetworkManager-wait-online 2>/dev/null");
        ArkOSUtil::executeCommand("sudo systemctl start NetworkManager-wait-online 2>/dev/null");

        // Enable NTP time sync
        ArkOSUtil::executeCommand("sudo timedatectl set-ntp 1 2>/dev/null");

        // Start Samba services
        ArkOSUtil::executeCommand("sudo systemctl start smbd 2>/dev/null");
        ArkOSUtil::executeCommand("sudo systemctl start nmbd 2>/dev/null");

        // Start SSH service
        ArkOSUtil::executeCommand("sudo systemctl start ssh.service 2>/dev/null");

        // Start FileBrowser
        ArkOSUtil::executeCommand("sudo pkill -f filebrowser 2>/dev/null || true");
        ArkOSUtil::executeCommand("sudo filebrowser -a 0.0.0.0 -p 80 -d /home/ark/.config/filebrowser.db -r / >/dev/null 2>&1 &");
    } else {
        // Disable NetworkManager-wait-online
        ArkOSUtil::executeCommand("sudo systemctl disable NetworkManager-wait-online 2>/dev/null");
        ArkOSUtil::executeCommand("sudo systemctl stop NetworkManager-wait-online 2>/dev/null");

        // Disable NTP time sync
        ArkOSUtil::executeCommand("sudo timedatectl set-ntp 0 2>/dev/null");

        // Stop Samba services
        ArkOSUtil::executeCommand("sudo systemctl stop smbd 2>/dev/null");
        ArkOSUtil::executeCommand("sudo systemctl stop nmbd 2>/dev/null");

        // Stop SSH service
        ArkOSUtil::executeCommand("sudo systemctl stop ssh.service 2>/dev/null");

        // Stop FileBrowser
        ArkOSUtil::executeCommand("sudo pkill -f filebrowser 2>/dev/null || true");
    }
}

bool isRemoteServicesAutoStart()
{
    std::string result = ArkOSUtil::executeCommand("systemctl is-enabled ssh.service 2>/dev/null");
    result.erase(std::remove_if(result.begin(), result.end(), ::isspace), result.end());
    return result.find("enabled") != std::string::npos;
}

void toggleRemoteServicesAutoStart(bool enable)
{
    if (enable) {
        ArkOSUtil::executeCommand("sudo systemctl enable ssh.service 2>/dev/null || true");
        ArkOSUtil::executeCommand("sudo systemctl enable smbd 2>/dev/null || true");
        ArkOSUtil::executeCommand("sudo systemctl enable nmbd 2>/dev/null || true");
    } else {
        ArkOSUtil::executeCommand("sudo systemctl disable ssh.service 2>/dev/null || true");
        ArkOSUtil::executeCommand("sudo systemctl disable smbd 2>/dev/null || true");
        ArkOSUtil::executeCommand("sudo systemctl disable nmbd 2>/dev/null || true");
    }
}

}
