#include "guis/arkos4clone/BatteryPlus.h"
#include "guis/arkos4clone/ArkOSUtil.h"
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace BatteryPlus
{

bool isAvailable()
{
    return Utils::FileSystem::exists("/usr/local/bin/batteryplus");
}

bool isEnabled()
{
    std::string result = ArkOSUtil::executeCommand("systemctl is-enabled batteryplus.service 2>/dev/null");
    result.erase(std::remove_if(result.begin(), result.end(), ::isspace), result.end());
    return result == "enabled";
}

void setEnabled(bool enable)
{
    if (enable) {
        ArkOSUtil::executeCommand("sudo systemctl enable batteryplus.service 2>/dev/null || true");
        ArkOSUtil::executeCommand("sudo systemctl start batteryplus.service 2>/dev/null || true");
    } else {
        ArkOSUtil::executeCommand("sudo systemctl stop batteryplus.service 2>/dev/null || true");
        ArkOSUtil::executeCommand("sudo systemctl disable batteryplus.service 2>/dev/null || true");
        ArkOSUtil::executeCommand("sudo rm -f /tmp/battery.percent");
    }
}

std::string getMode()
{
    std::string result = ArkOSUtil::executeCommand("grep -oP '(?<=^mode=).*' /etc/batteryplus/batteryplus.conf 2>/dev/null");
    result.erase(std::remove_if(result.begin(), result.end(), ::isspace), result.end());
    if (result.empty()) return "voltage";
    return result;
}

void setMode(const std::string& mode)
{
    bool wasEnabled = isEnabled();
    if (wasEnabled) {
        ArkOSUtil::executeCommand("sudo systemctl stop batteryplus.service 2>/dev/null || true");
    }
    ArkOSUtil::executeCommand("sudo sed -i 's|^mode=.*|mode=" + mode + "|' /etc/batteryplus/batteryplus.conf 2>/dev/null");
    if (wasEnabled) {
        ArkOSUtil::executeCommand("sudo systemctl start batteryplus.service 2>/dev/null || true");
    }
}

std::string getPercent()
{
    std::string result = ArkOSUtil::executeCommand("cat /tmp/battery.percent 2>/dev/null");
    result.erase(std::remove_if(result.begin(), result.end(), ::isspace), result.end());
    if (result.empty()) return "N/A";
    return result + "%";
}

std::string getChargeStatus()
{
    // Find battery status path
    std::string statusPath = ArkOSUtil::executeCommand(
        "ls /sys/class/power_supply/*/status 2>/dev/null | head -1");
    statusPath.erase(std::remove_if(statusPath.begin(), statusPath.end(), ::isspace), statusPath.end());
    if (statusPath.empty()) return "Unknown";

    std::string result = ArkOSUtil::executeCommand("cat " + statusPath + " 2>/dev/null");
    result.erase(std::remove_if(result.begin(), result.end(), ::isspace), result.end());
    return result;
}

int getVoltageMv()
{
    std::string voltPath = ArkOSUtil::executeCommand(
        "ls /sys/class/power_supply/*/voltage_now 2>/dev/null | head -1");
    voltPath.erase(std::remove_if(voltPath.begin(), voltPath.end(), ::isspace), voltPath.end());
    if (voltPath.empty()) return 0;

    std::string result = ArkOSUtil::executeCommand("cat " + voltPath + " 2>/dev/null");
    result.erase(std::remove_if(result.begin(), result.end(), ::isspace), result.end());
    try {
        int uv = std::stoi(result);
        return uv / 1000; // uV -> mV
    } catch (...) {
        return 0;
    }
}

void deleteRecords()
{
    ArkOSUtil::executeCommand("sudo rm -f /tmp/battery.percent");
    std::string dataDir = ArkOSUtil::executeCommand(
        "grep -oP '(?<=^data_dir=).*' /etc/batteryplus/batteryplus.conf 2>/dev/null");
    dataDir = Utils::String::trim(dataDir);
    if (!dataDir.empty()) {
        ArkOSUtil::executeCommand("sudo rm -f " + dataDir + "/batteryplus-calibrated");
        ArkOSUtil::executeCommand("sudo rm -f " + dataDir + "/batteryplus-voltage.map");
        ArkOSUtil::executeCommand("sudo rm -f " + dataDir + "/batteryplus-restore.state");
    }
    // Restart to re-learn
    if (isEnabled()) {
        ArkOSUtil::executeCommand("sudo systemctl restart batteryplus.service 2>/dev/null || true");
    }
}

}
