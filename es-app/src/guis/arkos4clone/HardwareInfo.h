#pragma once
#ifndef ES_APP_GUIS_ARKOS4CLONE_HARDWAREINFO_H
#define ES_APP_GUIS_ARKOS4CLONE_HARDWAREINFO_H

#include <string>
#include <vector>

namespace HardwareInfo
{
    // CPU
    int getCpuCoreCount();
    int getOnlineCpuCount();
    std::string getCpuGovernor();
    void setCpuGovernor(const std::string& governor);
    std::string getCpuMaxFreq();
    void setCpuMaxFreq(const std::string& freq);
    std::vector<std::string> getCpuAvailableFreqs();
    std::vector<std::string> getAvailableGovernors();
    void setCpuCores(int count);

    // GPU
    bool hasGpuFreqControl();
    std::string getGpuDevPath();
    std::string getGpuMaxFreq();
    void setGpuMaxFreq(const std::string& freq);
    std::vector<std::string> getGpuAvailableFreqs();

    // DMC
    bool hasDmcFreqControl();
    std::string getDmcMaxFreq();
    void setDmcMaxFreq(const std::string& freq);
    std::vector<std::string> getDmcAvailableFreqs();

    // ZRAM
    std::string getZramSize();
    bool isZramEnabled();
    std::string getZramCompAlgorithm();
    std::vector<std::string> getAvailableZramAlgorithms();
    void toggleZram(bool enable, const std::string& size = "512M", const std::string& compAlgo = "lz4");
    bool isZramAutoStart();
    void toggleZramAutoStart(bool enable, const std::string& size = "512M", const std::string& compAlgo = "lz4");
    void saveZramConfig(const std::string& size, const std::string& compAlgo);

    // View Info
    std::string getSdCardSpeed(const std::string& device);
    std::string getSdCardName(const std::string& device);
    std::string getCpuBinning();
    std::string getCpuTemp();
}

#endif // ES_APP_GUIS_ARKOS4CLONE_HARDWAREINFO_H
