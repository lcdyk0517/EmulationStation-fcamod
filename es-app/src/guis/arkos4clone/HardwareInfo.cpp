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
    std::string result = ArkOSUtil::executeCommand("ls -d /sys/class/devfreq/*gpu* 2>/dev/null | head -1");
    return !Utils::String::trim(result).empty();
}

std::string getGpuDevPath()
{
    std::string result = ArkOSUtil::executeCommand("ls -d /sys/class/devfreq/*gpu* 2>/dev/null | head -1");
    // executeCommand() 不剥换行, 直接拼路径会变成 "...gpu\n/available_frequencies"
    result.erase(std::remove_if(result.begin(), result.end(), ::isspace), result.end());
    if (result.empty())
        return "";
    return result + "/";
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

static std::string dmcDevPath()
{
    std::string result = ArkOSUtil::executeCommand("ls -d /sys/class/devfreq/*dmc* 2>/dev/null | head -1");
    result.erase(std::remove_if(result.begin(), result.end(), ::isspace), result.end());
    if (result.empty())
        return "";
    return result + "/";
}

bool hasDmcFreqControl()
{
    std::string result = ArkOSUtil::executeCommand("ls -d /sys/class/devfreq/*dmc* 2>/dev/null | head -1");
    return !Utils::String::trim(result).empty();
}

std::string getDmcMaxFreq()
{
    std::string result = ArkOSUtil::executeCommand("cat " + dmcDevPath() + "max_freq 2>/dev/null");
    result.erase(std::remove_if(result.begin(), result.end(), ::isspace), result.end());
    return result;
}

void setDmcMaxFreq(const std::string& freq)
{
    ArkOSUtil::executeCommand("echo " + freq + " | sudo tee " + dmcDevPath() + "max_freq >/dev/null 2>&1");
}

std::vector<std::string> getDmcAvailableFreqs()
{
    std::string result = ArkOSUtil::executeCommand("cat " + dmcDevPath() + "available_frequencies 2>/dev/null");
    std::vector<std::string> freqs;
    std::istringstream stream(result);
    std::string freq;
    while (stream >> freq) {
        freq.erase(std::remove_if(freq.begin(), freq.end(), ::isspace), freq.end());
        if (!freq.empty()) freqs.push_back(freq);
    }
    return freqs;
}

// A value from /etc/zram.conf, the settings zram-setup.sh applies at boot.
// Older builds wrote the file with "echo -e" through dash, leaving "-e " on
// the first line.
static std::string getZramConfValue(const std::string& key)
{
    std::string result = ArkOSUtil::executeCommand(
        "sed -n 's/^\\(-e \\)\\{0,1\\}" + key + "=//p' /etc/zram.conf 2>/dev/null | head -n 1");
    result.erase(std::remove_if(result.begin(), result.end(), ::isspace), result.end());
    return result;
}

std::string getZramSize()
{
    std::string result = ArkOSUtil::executeCommand("cat /sys/block/zram0/disksize 2>/dev/null");
    result.erase(std::remove_if(result.begin(), result.end(), ::isspace), result.end());
    // zram is off: show the saved size, not the default
    if (result.empty() || result == "0")
        result = getZramConfValue("SIZE");
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
    // zram is off: show the saved algorithm if this kernel has it
    if (!isZramEnabled()) {
        std::string saved = getZramConfValue("ALGORITHM");
        std::vector<std::string> algos = getAvailableZramAlgorithms();
        if (std::find(algos.begin(), algos.end(), saved) != algos.end())
            return saved;
    }

    std::string result = ArkOSUtil::executeCommand("cat /sys/block/zram0/comp_algorithm 2>/dev/null");
    // Parse current algorithm from format like "[lzo] lz4 lz4hc zstd"
    size_t start = result.find('[');
    size_t end = result.find(']');
    if (start != std::string::npos && end != std::string::npos && end > start) {
        return result.substr(start + 1, end - start - 1);
    }
    return getDefaultZramAlgorithm();
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

// The preferred algorithm this kernel has: 5.10 has zstd, the 4.4 kernel
// only lzo.
std::string getDefaultZramAlgorithm()
{
    std::vector<std::string> algos = getAvailableZramAlgorithms();
    for (const char* a : {"lz4", "zstd", "lzo-rle", "lzo"}) {
        if (std::find(algos.begin(), algos.end(), a) != algos.end())
            return a;
    }
    return algos.empty() ? "lzo" : algos.front();
}

// Parse a menu size ("512M", "2G", or raw bytes) into bytes. The old code
// only listed 128/256/512/1024M and silently fell back to 512M for 2048M.
static long zramSizeToBytes(const std::string& size)
{
    if (size.empty())
        return 512L * 1024 * 1024;
    char* end = nullptr;
    long v = std::strtol(size.c_str(), &end, 10);
    if (v <= 0)
        return 512L * 1024 * 1024;
    if (end && (*end == 'M' || *end == 'm'))
        return v * 1024L * 1024L;
    if (end && (*end == 'G' || *end == 'g'))
        return v * 1024L * 1024L * 1024L;
    if (size.find_first_of("MmGg") == std::string::npos && v > 1024L * 1024L)
        return v; // already bytes
    return v * 1024L * 1024L;
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

        // Convert size string (e.g., "512M", "2G") to bytes
        long bytes = zramSizeToBytes(size);

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

void saveZramConfig(const std::string& size, const std::string& compAlgo, bool enabled)
{
    long bytes = zramSizeToBytes(size);

    // printf, not "echo -e": the command runs through /bin/sh (dash), whose
    // echo has no -e and writes it into the file.
    std::string cmd = "printf 'ENABLED=" + std::string(enabled ? "1" : "0") +
                      "\\nALGORITHM=" + compAlgo +
                      "\\nSIZE=" + std::to_string(bytes) +
                      "\\n' | sudo tee /etc/zram.conf >/dev/null 2>&1";
    ArkOSUtil::executeCommand(cmd);
}

bool isZramAutoStart()
{
    std::string result = ArkOSUtil::executeCommand("systemctl is-enabled zram-swap.service 2>/dev/null");
    result.erase(std::remove_if(result.begin(), result.end(), ::isspace), result.end());
    return result == "enabled";
}

void toggleZramAutoStart(bool enable, const std::string& size, const std::string& compAlgo, bool zramEnabled)
{
    if (enable) {
        saveZramConfig(size, compAlgo, zramEnabled);
        ArkOSUtil::executeCommand("sudo systemctl enable zram-swap.service 2>/dev/null || true");
    } else {
        ArkOSUtil::executeCommand("sudo systemctl disable zram-swap.service 2>/dev/null || true");
    }
}

std::string getSdCardName(const std::string& device)
{
    // Manufacturer name from sysfs, falling back to the CID prefix on
    // controllers that do not expose device/name.
    std::string name = ArkOSUtil::executeCommand("cat /sys/block/" + device + "/device/name 2>/dev/null");
    name.erase(std::remove_if(name.begin(), name.end(), ::isspace), name.end());
    if (name.empty()) {
        name = ArkOSUtil::executeCommand("cat /sys/block/" + device + "/device/cid 2>/dev/null | cut -c1-8");
        name.erase(std::remove_if(name.begin(), name.end(), ::isspace), name.end());
    }
    return name.empty() ? _("UNKNOWN") : name;
}

}
