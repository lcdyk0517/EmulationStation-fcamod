#include "guis/arkos4clone/FreqProfile.h"
#include "guis/arkos4clone/HardwareInfo.h"
#include "Settings.h"
#include "Log.h"

#include <vector>
#include <cstdlib>

namespace
{
    bool containsFreq(const std::vector<std::string>& freqs, const std::string& value)
    {
        if (value.empty()) return false;
        for (const auto& f : freqs) {
            if (f == value) return true;
        }
        return false;
    }
}

namespace FreqProfile
{

std::string systemKey(const std::string& system, char which)
{
    switch (which) {
        case 'c': return system + ".cpufreq";
        case 'g': return system + ".gpufreq";
        case 'd': return system + ".dmcfreq";
    }
    return "";
}

std::string getSystemFreq(const std::string& system, char which)
{
    return Settings::getInstance()->getString(systemKey(system, which));
}

void setSystemFreq(const std::string& system, char which, const std::string& freq)
{
    Settings::getInstance()->setString(systemKey(system, which), freq);
}

std::string resolveFreq(const std::string& system, char which)
{
    std::string value = getSystemFreq(system, which);
    if (value.empty())
        return "";

    std::vector<std::string> freqs;
    switch (which) {
        case 'c': freqs = HardwareInfo::getCpuAvailableFreqs(); break;
        case 'g': freqs = HardwareInfo::getGpuAvailableFreqs(); break;
        case 'd': freqs = HardwareInfo::getDmcAvailableFreqs(); break;
    }

    if (freqs.empty())
        return "";  // no frequency control on this hardware

    if (containsFreq(freqs, value))
        return value;

    LOG(LogWarning) << "FreqProfile: value '" << value << "' not in available frequencies, ignoring";
    return "";
}

std::string getSystemCores(const std::string& system)
{
    return Settings::getInstance()->getString(system + ".cpucores");
}

void setSystemCores(const std::string& system, const std::string& cores)
{
    Settings::getInstance()->setString(system + ".cpucores", cores);
}

int resolveCores(const std::string& system)
{
    std::string value = getSystemCores(system);
    if (value.empty())
        return 0;

    int cores = atoi(value.c_str());
    int total = HardwareInfo::getCpuCoreCount();

    if (cores < 1 || cores > total) {
        LOG(LogWarning) << "FreqProfile: core count '" << value << "' invalid (1-" << total << "), ignoring";
        return 0;
    }
    return cores;
}

void applyForSystem(const std::string& system)
{
    std::string cpu = resolveFreq(system, 'c');
    if (!cpu.empty())
        HardwareInfo::setCpuMaxFreq(cpu);

    int cores = resolveCores(system);
    if (cores > 0)
        HardwareInfo::setCpuCores(cores);

    if (HardwareInfo::hasGpuFreqControl()) {
        std::string gpu = resolveFreq(system, 'g');
        if (!gpu.empty())
            HardwareInfo::setGpuMaxFreq(gpu);
    }

    if (HardwareInfo::hasDmcFreqControl()) {
        std::string dmc = resolveFreq(system, 'd');
        if (!dmc.empty())
            HardwareInfo::setDmcMaxFreq(dmc);
    }
}

void restoreValues(const std::string& cpu, const std::string& gpu, const std::string& dmc, int cores)
{
    if (!cpu.empty())
        HardwareInfo::setCpuMaxFreq(cpu);

    if (cores > 0)
        HardwareInfo::setCpuCores(cores);

    if (!gpu.empty() && HardwareInfo::hasGpuFreqControl())
        HardwareInfo::setGpuMaxFreq(gpu);

    if (!dmc.empty() && HardwareInfo::hasDmcFreqControl())
        HardwareInfo::setDmcMaxFreq(dmc);
}

GameFreqGuard::GameFreqGuard(const std::string& systemName)
    : mSystem(systemName), mCores(HardwareInfo::getOnlineCpuCount()), mApplied(false)
{
    mCpu = HardwareInfo::getCpuMaxFreq();
    mGpu = HardwareInfo::hasGpuFreqControl() ? HardwareInfo::getGpuMaxFreq() : "";
    mDmc = HardwareInfo::hasDmcFreqControl() ? HardwareInfo::getDmcMaxFreq() : "";

    applyForSystem(mSystem);
    mApplied = true;
}

GameFreqGuard::~GameFreqGuard()
{
    if (mApplied)
        restoreValues(mCpu, mGpu, mDmc, mCores);
}

}
