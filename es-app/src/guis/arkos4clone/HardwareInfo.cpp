#include "guis/arkos4clone/HardwareInfo.h"
#include "guis/arkos4clone/ArkOSUtil.h"
#include "utils/StringUtil.h"
#include "EsLocale.h"

#include <sstream>
#include <cstdlib>
#include <algorithm>
#include <cctype>

namespace HardwareInfo
{

int getCpuCoreCount()
{
    std::string result = ArkOSUtil::executeCommand("ls -d /sys/devices/system/cpu/cpu[0-9]* 2>/dev/null | wc -l");
    return atoi(result.c_str());
}

int getOnlineCpuCount()
{
    int count = 0;
    int total = getCpuCoreCount();
    for (int i = 0; i < total; i++) {
        std::string result = ArkOSUtil::executeCommand("cat /sys/devices/system/cpu/cpu" + std::to_string(i) + "/online 2>/dev/null");
        // Remove all whitespace including newlines
        result.erase(std::remove_if(result.begin(), result.end(), ::isspace), result.end());
        if (result == "1" || result.empty()) {  // cpu0 has no online file but is always on
            count++;
        }
    }
    return count;
}

std::string getCpuGovernor()
{
    std::string result = ArkOSUtil::executeCommand("cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null");
    // Remove all whitespace including newlines
    result.erase(std::remove_if(result.begin(), result.end(), ::isspace), result.end());
    return result;
}

void setCpuGovernor(const std::string& governor)
{
    int cores = getCpuCoreCount();
    for (int i = 0; i < cores; i++) {
        ArkOSUtil::executeCommand("echo " + governor + " | sudo tee /sys/devices/system/cpu/cpu" + std::to_string(i) + "/cpufreq/scaling_governor >/dev/null 2>&1");
    }
}

std::string getCpuMaxFreq()
{
    std::string result = ArkOSUtil::executeCommand("cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq 2>/dev/null");
    result.erase(std::remove_if(result.begin(), result.end(), ::isspace), result.end());
    return result;
}

void setCpuMaxFreq(const std::string& freq)
{
    int cores = getCpuCoreCount();
    for (int i = 0; i < cores; i++) {
        ArkOSUtil::executeCommand("echo " + freq + " | sudo tee /sys/devices/system/cpu/cpu" + std::to_string(i) + "/cpufreq/scaling_max_freq >/dev/null 2>&1");
    }
}

std::vector<std::string> getCpuAvailableFreqs()
{
    std::string result = ArkOSUtil::executeCommand("cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_available_frequencies 2>/dev/null");
    std::vector<std::string> freqs;
    std::istringstream stream(result);
    std::string freq;
    while (stream >> freq) {
        freq.erase(std::remove_if(freq.begin(), freq.end(), ::isspace), freq.end());
        if (!freq.empty()) freqs.push_back(freq);
    }
    return freqs;
}

std::vector<std::string> getAvailableGovernors()
{
    std::string result = ArkOSUtil::executeCommand("cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_available_governors 2>/dev/null");
    std::vector<std::string> governors;
    std::istringstream stream(result);
    std::string gov;
    while (stream >> gov) {
        gov.erase(std::remove_if(gov.begin(), gov.end(), ::isspace), gov.end());
        if (!gov.empty()) governors.push_back(gov);
    }
    return governors;
}

void setCpuCores(int count)
{
    int totalCores = getCpuCoreCount();
    // Enable all cores first
    for (int i = 0; i < totalCores; i++) {
        ArkOSUtil::executeCommand("echo 1 | sudo tee /sys/devices/system/cpu/cpu" + std::to_string(i) + "/online >/dev/null 2>&1");
    }
    // Disable cores beyond count (keep cpu0 always on)
    for (int i = count; i < totalCores; i++) {
        ArkOSUtil::executeCommand("echo 0 | sudo tee /sys/devices/system/cpu/cpu" + std::to_string(i) + "/online >/dev/null 2>&1");
    }
}

bool hasGpuFreqControl()
{
    std::string result = ArkOSUtil::executeCommand("ls -d /sys/class/devfreq/ff400000.gpu 2>/dev/null");
    return !Utils::String::trim(result).empty();
}

std::string getGpuDevPath()
{
    return "/sys/class/devfreq/ff400000.gpu/";
}

std::string getGpuMaxFreq()
{
    std::string gpuPath = getGpuDevPath();
    if (gpuPath.empty()) return "";
    std::string result = ArkOSUtil::executeCommand("cat " + gpuPath + "max_freq 2>/dev/null");
    result.erase(std::remove_if(result.begin(), result.end(), ::isspace), result.end());
    return result;
}

void setGpuMaxFreq(const std::string& freq)
{
    std::string gpuPath = getGpuDevPath();
    if (gpuPath.empty()) return;
    ArkOSUtil::executeCommand("echo " + freq + " | sudo tee " + gpuPath + "max_freq >/dev/null 2>&1");
}

std::vector<std::string> getGpuAvailableFreqs()
{
    std::string gpuPath = getGpuDevPath();
    if (gpuPath.empty()) return {};
    std::string result = ArkOSUtil::executeCommand("cat " + gpuPath + "available_frequencies 2>/dev/null");
    std::vector<std::string> freqs;
    std::istringstream stream(result);
    std::string freq;
    while (stream >> freq) {
        freq.erase(std::remove_if(freq.begin(), freq.end(), ::isspace), freq.end());
        if (!freq.empty()) freqs.push_back(freq);
    }
    return freqs;
}

bool hasDmcFreqControl()
{
    std::string result = ArkOSUtil::executeCommand("ls /sys/class/devfreq/dmc/available_frequencies 2>/dev/null");
    return !Utils::String::trim(result).empty();
}

std::string getDmcMaxFreq()
{
    std::string result = ArkOSUtil::executeCommand("cat /sys/class/devfreq/dmc/max_freq 2>/dev/null");
    result.erase(std::remove_if(result.begin(), result.end(), ::isspace), result.end());
    return result;
}

void setDmcMaxFreq(const std::string& freq)
{
    ArkOSUtil::executeCommand("echo " + freq + " | sudo tee /sys/class/devfreq/dmc/max_freq >/dev/null 2>&1");
}

std::vector<std::string> getDmcAvailableFreqs()
{
    std::string result = ArkOSUtil::executeCommand("cat /sys/class/devfreq/dmc/available_frequencies 2>/dev/null");
    std::vector<std::string> freqs;
    std::istringstream stream(result);
    std::string freq;
    while (stream >> freq) {
        freq.erase(std::remove_if(freq.begin(), freq.end(), ::isspace), freq.end());
        if (!freq.empty()) freqs.push_back(freq);
    }
    return freqs;
}

std::string getZramSize()
{
    std::string result = ArkOSUtil::executeCommand("cat /sys/block/zram0/disksize 2>/dev/null");
    result.erase(std::remove_if(result.begin(), result.end(), ::isspace), result.end());
    if (result.empty()) return "0";
    long size = atol(result.c_str());
    // Convert to MB
    long mb = size / (1024 * 1024);
    return std::to_string(mb) + "M";
}

bool isZramEnabled()
{
    std::string result = ArkOSUtil::executeCommand("grep -q '^/dev/zram0' /proc/swaps 2>/dev/null && echo yes || echo no");
    result.erase(std::remove_if(result.begin(), result.end(), ::isspace), result.end());
    return result == "yes";
}

std::string getZramCompAlgorithm()
{
    std::string result = ArkOSUtil::executeCommand("cat /sys/block/zram0/comp_algorithm 2>/dev/null");
    // Parse current algorithm from format like "[lzo] lz4 lz4hc zstd"
    size_t start = result.find('[');
    size_t end = result.find(']');
    if (start != std::string::npos && end != std::string::npos && end > start) {
        return result.substr(start + 1, end - start - 1);
    }
    return "lz4";
}

std::vector<std::string> getAvailableZramAlgorithms()
{
    std::vector<std::string> algos;
    std::string result = ArkOSUtil::executeCommand("cat /sys/block/zram0/comp_algorithm 2>/dev/null");
    // Parse format like "[lzo] lz4 lz4hc zstd"
    std::string current;
    for (size_t i = 0; i < result.size(); i++) {
        char c = result[i];
        if (c == '[' || c == ']') continue;
        if (c == ' ' || c == '\n' || c == '\t') {
            if (!current.empty()) {
                algos.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        algos.push_back(current);
    }
    return algos;
}

void toggleZram(bool enable, const std::string& size, const std::string& compAlgo)
{
    if (enable) {
        // Disable first if already enabled
        ArkOSUtil::executeCommand("sudo swapoff /dev/zram0 2>/dev/null || true");
        // Reset zram
        ArkOSUtil::executeCommand("echo 1 | sudo tee /sys/block/zram0/reset >/dev/null 2>&1");

        // Set compression algorithm (must be set after reset, before disksize)
        ArkOSUtil::executeCommand("echo " + compAlgo + " | sudo tee /sys/block/zram0/comp_algorithm >/dev/null 2>&1");

        // Convert size string (e.g., "512M") to bytes
        long bytes = 536870912; // default 512M
        if (size == "128M") bytes = 134217728;
        else if (size == "256M") bytes = 268435456;
        else if (size == "512M") bytes = 536870912;
        else if (size == "1024M") bytes = 1073741824;

        // Set size in bytes
        ArkOSUtil::executeCommand("echo " + std::to_string(bytes) + " | sudo tee /sys/block/zram0/disksize >/dev/null 2>&1");
        // Create swap and enable
        ArkOSUtil::executeCommand("sudo mkswap /dev/zram0 >/dev/null 2>&1");
        ArkOSUtil::executeCommand("sudo swapon -p 5 /dev/zram0 >/dev/null 2>&1");
    } else {
        ArkOSUtil::executeCommand("sudo swapoff /dev/zram0 2>/dev/null || true");
        ArkOSUtil::executeCommand("echo 1 | sudo tee /sys/block/zram0/reset >/dev/null 2>&1 || true");
    }
}

void saveZramConfig(const std::string& size, const std::string& compAlgo)
{
    long bytes = 536870912;
    if (size == "128M") bytes = 134217728;
    else if (size == "256M") bytes = 268435456;
    else if (size == "512M") bytes = 536870912;
    else if (size == "1024M") bytes = 1073741824;

    std::string cmd = "echo -e 'ENABLED=1\\nALGORITHM=" + compAlgo +
                      "\\nSIZE=" + std::to_string(bytes) +
                      "' | sudo tee /etc/zram.conf >/dev/null 2>&1";
    ArkOSUtil::executeCommand(cmd);
}

bool isZramAutoStart()
{
    std::string result = ArkOSUtil::executeCommand("systemctl is-enabled zram-swap.service 2>/dev/null");
    result.erase(std::remove_if(result.begin(), result.end(), ::isspace), result.end());
    return result == "enabled";
}

void toggleZramAutoStart(bool enable, const std::string& size, const std::string& compAlgo)
{
    if (enable) {
        saveZramConfig(size, compAlgo);
        ArkOSUtil::executeCommand("sudo systemctl enable zram-swap.service 2>/dev/null || true");
    } else {
        ArkOSUtil::executeCommand("sudo systemctl disable zram-swap.service 2>/dev/null || true");
    }
}

std::string getSdCardName(const std::string& device)
{
    std::string name = ArkOSUtil::executeCommand("cat /sys/block/" + device + "/device/name 2>/dev/null");
    name.erase(std::remove_if(name.begin(), name.end(), ::isspace), name.end());
    if (name.empty()) {
        name = ArkOSUtil::executeCommand("cat /sys/block/" + device + "/device/cid 2>/dev/null | cut -c1-8");
        name.erase(std::remove_if(name.begin(), name.end(), ::isspace), name.end());
    }
    return name.empty() ? _("UNKNOWN") : name;
}

std::string getSdCardSpeed(const std::string& device)
{
    // Map device name to dmesg host name (mmcblk0 -> mmc0, mmcblk1 -> mmc1)
    std::string hostNum = (device == "mmcblk0") ? "mmc0" : "mmc1";

    // Parse existing kernel dmesg output
    // Format: "mmc0: new high speed SDXC card at address 0001"
    // Format: "mmc0: new ultra high speed SDR104 SDHC card at address 0001"
    std::string dmesgLine = ArkOSUtil::executeCommand("dmesg | grep '" + hostNum + ": new' | tail -1");

    if (!dmesgLine.empty()) {
        // Check for UHS (ultra high speed)
        if (dmesgLine.find("ultra high speed") != std::string::npos) {
            // UHS Speed Grade: U3 (SDR104), U1 (SDR50/DDR50), Class 10 (SDR25/SDR12)
            if (dmesgLine.find("SDR104") != std::string::npos) return "UHS-I U3 (104MB/s)";
            if (dmesgLine.find("SDR50") != std::string::npos) return "UHS-I U1 (50MB/s)";
            if (dmesgLine.find("DDR50") != std::string::npos) return "UHS-I U1 DDR (50MB/s)";
            if (dmesgLine.find("SDR25") != std::string::npos) return "UHS-I Class 10 (25MB/s)";
            if (dmesgLine.find("SDR12") != std::string::npos) return "UHS-I Class 4 (12MB/s)";
            return "UHS-I";
        }
        // Check for High Speed
        if (dmesgLine.find("high speed") != std::string::npos) {
            return "Class 10 (25MB/s)";
        }
    }

    return _("N/A");
}

std::string getCpuBinning()
{
    // Try to get CPU binning info from dmesg (added by rockchip-cpufreq.c)
    // Format: es_info: cpu_bin=X process=X scale=X volt_sel=X
    // volt_sel is the actual quality grade based on CPU leakage:
    // - lower volt_sel = higher leakage = worse quality = needs higher voltage
    // - higher volt_sel = lower leakage = better quality = can run at lower voltage
    std::string dmesgBin = ArkOSUtil::executeCommand("dmesg | grep 'es_info: cpu_bin=' | tail -1");

    if (!dmesgBin.empty()) {
        // Parse the volt_sel value (actual quality indicator)
        std::string voltSel = ArkOSUtil::executeCommand("echo '" + dmesgBin + "' | sed 's/.*volt_sel=\\(-*[0-9]*\\).*/\\1/'");
        voltSel.erase(std::remove_if(voltSel.begin(), voltSel.end(), ::isspace), voltSel.end());

        int voltVal = atoi(voltSel.c_str());

        // Rockchip CPU quality grades based on volt_sel:
        // volt_sel=0: L0 一般体质 - higher leakage, needs more voltage
        // volt_sel=1: L1 较差体质 - slightly better than L0
        // volt_sel=2: L2 标准体质 - standard quality
        // volt_sel=3: L3 最佳体质 - lowest leakage, can run at lowest voltage
        // negative value: N/A - not detected

        if (voltVal < 0) return "N/A";
        if (voltVal == 0) return "L0 (" + std::string(_("AVERAGE")) + ")";
        if (voltVal == 1) return "L1 (" + std::string(_("POOR")) + ")";
        if (voltVal == 2) return "L2 (" + std::string(_("STANDARD")) + ")";
        if (voltVal == 3) return "L3 (" + std::string(_("BEST")) + ")";

        return "L" + std::to_string(voltVal) + " (" + std::string(_("AVERAGE")) + ")";
    }

    return "N/A";
}

std::string getCpuTemp()
{
    std::string temp = ArkOSUtil::executeCommand("cat /sys/class/thermal/thermal_zone0/temp 2>/dev/null");
    temp.erase(std::remove_if(temp.begin(), temp.end(), ::isspace), temp.end());

    if (!temp.empty()) {
        // Convert millidegree to degree
        int tempVal = atoi(temp.c_str());
        if (tempVal > 1000) {
            tempVal = tempVal / 1000;
        }
        return std::to_string(tempVal) + "°C";
    }

    return _("N/A");
}

}
