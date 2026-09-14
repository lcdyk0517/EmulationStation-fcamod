#pragma once
#ifndef ES_APP_GUIS_ARKOS4CLONE_SDCARDCONTROL_H
#define ES_APP_GUIS_ARKOS4CLONE_SDCARDCONTROL_H

#include <string>

namespace SdCardControl
{
    enum class RomsMode
    {
        Sd1,   // main SD only:  /roms
        Sd2,   // SD2 only:      /roms2
        Dual   // SD1 + SD2:     /roms + /roms2 ([SD2] systems)
    };

    enum class ApplyResult
    {
        Applied,    // live config content was changed
        Unchanged,  // live config already matches the requested mode
        Failed      // variant file missing or could not be written
    };

    // ---- probing ----
    bool isSd2CardPresent();                            // /dev/mmcblk1 or /dev/mmcblk1p1 exists
    bool isMountPoint(const std::string& mountPoint);   // listed in /proc/mounts
    bool isRoms2Usable();                               // mounted on /roms2 and has >= 1 subdirectory
    bool hasRoms2Subdirs();                             // /roms2 directory itself has >= 1 subdirectory,
                                                        // mount state NOT checked - pair with isMountPoint()

    // ---- persistence (Settings key "RomsSdMode": sd1 / sd2 / dual) ----
    RomsMode getSavedMode();
    void setSavedMode(RomsMode mode);
    std::string modeToString(RomsMode mode);
    RomsMode modeFromString(const std::string& value);

    // Effective mode: falls back to Sd1 for this session when the saved mode
    // needs /roms2 but the card is missing, not mounted or empty. The saved
    // preference is kept so it recovers automatically on the next boot.
    RomsMode resolveEffectiveMode();

    // Copy /etc/emulationstation/es_systems.cfg.<sd1|sd2|dual> over the live
    // config (target follows SystemData::getConfigPath()). Missing variant
    // file keeps the current config untouched.
    ApplyResult applyCfg(RomsMode mode);

    // Mount /roms2 (fs-type detection via lsblk, uid/gid of the ark user).
    // Returns true when /roms2 is mounted afterwards.
    bool mountRoms2();

    // Startup hook: resolve mode, guarantee /roms2 mount for sd2/dual,
    // then apply the matching cfg before SystemData::loadConfig() runs.
    void applyRomsModeOnStartup();
}

#endif // ES_APP_GUIS_ARKOS4CLONE_SDCARDCONTROL_H
