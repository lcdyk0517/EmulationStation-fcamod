#pragma once
#ifndef ES_APP_GUIS_ARKOS4CLONE_FREQPROFILE_H
#define ES_APP_GUIS_ARKOS4CLONE_FREQPROFILE_H

#include <string>

// Per-system CPU/GPU/DMC max frequency profiles.
//
// Storage (Settings / es_settings.cfg, persisted):
//   <system>.cpufreq / <system>.gpufreq / <system>.dmcfreq / <system>.cpucores
// Frequency values are raw sysfs frequency strings (CPU: kHz, GPU/DMC: Hz);
// core count is a decimal string. An empty value means AUTO (leave untouched).
//
// The global TOOLS menus are NOT persisted: they read/write sysfs directly.
//
// Policy:
//   - Game launch : apply the system profile if set, otherwise touch nothing
//   - Game exit   : restore the values captured before launch
namespace FreqProfile
{
    // Settings key name (single source of truth)
    std::string systemKey(const std::string& system, char which);  // which: 'c'/'g'/'d'

    // Raw value access (empty string = AUTO)
    std::string getSystemFreq(const std::string& system, char which);
    void setSystemFreq(const std::string& system, char which, const std::string& freq);

    // CPU core count per system (empty string = AUTO)
    std::string getSystemCores(const std::string& system);
    void setSystemCores(const std::string& system, const std::string& cores);

    // Resolve the effective frequency for a system: validated against the
    // available frequency list, dropped (with a log line) if invalid.
    std::string resolveFreq(const std::string& system, char which);

    // Resolve the effective core count: validated against the core count,
    // dropped (with a log line) if invalid.
    int resolveCores(const std::string& system);

    // Apply resolved frequencies and core count for a system (skips absent hardware).
    void applyForSystem(const std::string& system);

    // Restore the values captured before launch (skips absent hardware).
    void restoreValues(const std::string& cpu, const std::string& gpu, const std::string& dmc, int cores);

    // RAII: captures current values, applies the system profile at launch and
    // restores the captured values when the game exits (any exit path).
    struct GameFreqGuard
    {
        GameFreqGuard(const std::string& systemName);
        ~GameFreqGuard();

    private:
        std::string mSystem;
        std::string mCpu;
        std::string mGpu;
        std::string mDmc;
        int mCores;
        bool mApplied;
    };
}

#endif // ES_APP_GUIS_ARKOS4CLONE_FREQPROFILE_H
