#include "guis/arkos4clone/SdCardControl.h"
#include "guis/arkos4clone/ArkOSUtil.h"
#include "Settings.h"
#include "SystemData.h"
#include "Log.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <set>
#include <sstream>
#include <sys/stat.h>

#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"

namespace SdCardControl
{
    static const char* SETTINGS_KEY = "RomsSdMode";
    static const char* ROMS2_MOUNT = "/roms2";
    static const char* CFG_BASE = "/etc/emulationstation/es_systems.cfg";
    static const std::string roms2 = ROMS2_MOUNT;

    // ------------------------------------------------------------------
    // helpers
    // ------------------------------------------------------------------

    // Utils::String::trim() only strips " \t" - popen output keeps its
    // trailing newline, which breaks exact matches (whitelist lookups) and
    // command splicing. Always pass command output through this before use.
    static std::string sanitizeOneLine(const std::string& raw)
    {
        std::string s = Utils::String::trim(raw);
        size_t cut = s.find_first_of("\r\n");
        if (cut != std::string::npos)
            s.resize(cut);
        return Utils::String::trim(s);
    }

    // ------------------------------------------------------------------
    // probing
    // ------------------------------------------------------------------

    bool isSd2CardPresent()
    {
        return Utils::FileSystem::exists("/dev/mmcblk1p1") || Utils::FileSystem::exists("/dev/mmcblk1");
    }

    bool isMountPoint(const std::string& mountPoint)
    {
        std::ifstream mounts("/proc/mounts");
        if (!mounts.is_open())
            return false;

        std::string line;
        while (std::getline(mounts, line))
        {
            std::istringstream stream(line);
            std::string dev, mp;
            if ((stream >> dev) && (stream >> mp) && mp == mountPoint)
                return true;
        }
        return false;
    }

    bool hasRoms2Subdirs()
    {
        DIR* dir = opendir(ROMS2_MOUNT);
        if (dir == nullptr)
            return false;

        bool found = false;
        struct dirent* entry;
        while (!found && (entry = readdir(dir)) != nullptr)
        {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;

            bool isDir = false;
            if (entry->d_type == DT_DIR)
                isDir = true;
            else if (entry->d_type == DT_UNKNOWN)
            {
                struct stat st;
                std::string full = std::string(ROMS2_MOUNT) + "/" + entry->d_name;
                if (stat(full.c_str(), &st) == 0)
                    isDir = S_ISDIR(st.st_mode);
            }

            if (isDir)
                found = true;
        }

        closedir(dir);
        return found;
    }

    bool isRoms2Usable()
    {
        return isMountPoint(roms2) && hasRoms2Subdirs();
    }

    // ------------------------------------------------------------------
    // persistence
    // ------------------------------------------------------------------

    std::string modeToString(RomsMode mode)
    {
        switch (mode)
        {
            case RomsMode::Sd2:  return "sd2";
            case RomsMode::Dual: return "dual";
            case RomsMode::Sd1:
            default:             return "sd1";
        }
    }

    RomsMode modeFromString(const std::string& value)
    {
        if (value == "sd2")  return RomsMode::Sd2;
        if (value == "dual") return RomsMode::Dual;
        return RomsMode::Sd1;
    }

    RomsMode getSavedMode()
    {
        return modeFromString(Settings::getInstance()->getString(SETTINGS_KEY));
    }

    void setSavedMode(RomsMode mode)
    {
        Settings::getInstance()->setString(SETTINGS_KEY, modeToString(mode));
        Settings::getInstance()->saveFile();
    }

    RomsMode resolveEffectiveMode()
    {
        RomsMode saved = getSavedMode();
        if (saved == RomsMode::Sd1)
            return RomsMode::Sd1;

        if (isRoms2Usable())
            return saved;

        if (!isSd2CardPresent())
            LOG(LogWarning) << "SdCardControl: SD2 card not present, falling back to sd1 (saved: " << modeToString(saved) << ")";
        else if (!isMountPoint(roms2))
            LOG(LogWarning) << "SdCardControl: " << roms2 << " is not mounted, falling back to sd1 (saved: " << modeToString(saved) << ")";
        else
            LOG(LogWarning) << "SdCardControl: " << roms2 << " is empty, falling back to sd1 (saved: " << modeToString(saved) << ")";

        return RomsMode::Sd1;
    }

    // ------------------------------------------------------------------
    // cfg handling
    // ------------------------------------------------------------------

    static bool readFile(const std::string& path, std::string& out)
    {
        std::ifstream in(path, std::ios::in | std::ios::binary);
        if (!in.is_open())
            return false;

        std::ostringstream ss;
        ss << in.rdbuf();
        out = ss.str();
        return true;
    }

    static bool writeFile(const std::string& path, const std::string& content)
    {
        std::ofstream out(path, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!out.is_open())
            return false;

        out << content;
        return out.good();
    }

    ApplyResult applyCfg(RomsMode mode)
    {
        const std::string variant = std::string(CFG_BASE) + "." + modeToString(mode);
        const std::string target = SystemData::getConfigPath(false);

        std::string content;
        if (!readFile(variant, content) || content.empty())
        {
            LOG(LogWarning) << "SdCardControl: variant file missing or empty: " << variant;
            return ApplyResult::Failed;
        }

        if (target == variant)
            return ApplyResult::Unchanged; // already the live config

        // Content compare, but skip reading the target entirely when its
        // size already differs (common case after a real mode switch).
        // mtime-only checks cannot prove equality, so sizes equal -> read once.
        std::string current;
        struct stat st;
        bool sizeMatches = (stat(target.c_str(), &st) == 0 && (size_t)st.st_size == content.size());
        if (sizeMatches && readFile(target, current) && current == content)
        {
            LOG(LogInfo) << "SdCardControl: " << target << " already matches " << modeToString(mode);
            return ApplyResult::Unchanged;
        }

        if (writeFile(target, content))
        {
            LOG(LogInfo) << "SdCardControl: applied " << variant << " -> " << target;
            return ApplyResult::Applied;
        }

        // direct write failed (e.g. target missing/permissions), fall back to sudo
        LOG(LogWarning) << "SdCardControl: direct write failed, using sudo cp";
        std::string cmd = "sudo cp -f " + ArkOSUtil::shellQuote(variant) + " " + ArkOSUtil::shellQuote(target);
        int ret = std::system(cmd.c_str());
        std::string after;
        if (ret == 0 && readFile(target, after) && after == content)
        {
            std::system(("sudo chown ark:ark " + ArkOSUtil::shellQuote(target)).c_str());
            std::system(("sudo chmod 777 " + ArkOSUtil::shellQuote(target)).c_str());
            LOG(LogInfo) << "SdCardControl: applied via sudo " << variant << " -> " << target;
            return ApplyResult::Applied;
        }

        LOG(LogError) << "SdCardControl: failed to apply " << variant;
        return ApplyResult::Failed;
    }

    // ------------------------------------------------------------------
    // mounting
    // ------------------------------------------------------------------

    bool mountRoms2()
    {
        if (isMountPoint(roms2))
            return true;

        if (!isSd2CardPresent())
        {
            LOG(LogWarning) << "SdCardControl: cannot mount " << roms2 << ", no SD2 card";
            return false;
        }

        std::string fsType = sanitizeOneLine(ArkOSUtil::executeCommand("lsblk -no FSTYPE /dev/mmcblk1p1 2>/dev/null"));
        std::string device = "/dev/mmcblk1p1";
        if (fsType.empty())
        {
            fsType = sanitizeOneLine(ArkOSUtil::executeCommand("lsblk -no FSTYPE /dev/mmcblk1 2>/dev/null"));
            device = "/dev/mmcblk1";
        }

        if (fsType == "ntfs")
            fsType = "ntfs-3g";

        // whitelist: never splice unverified lsblk output into a shell command
        static const std::set<std::string> allowedFs = { "ext4", "f2fs", "vfat", "exfat", "ntfs-3g" };
        if (fsType.empty())
        {
            LOG(LogError) << "SdCardControl: no filesystem detected on SD2 card";
            return false;
        }
        if (allowedFs.find(fsType) == allowedFs.end())
        {
            LOG(LogError) << "SdCardControl: unsupported filesystem on SD2 card: '" << fsType << "'";
            return false;
        }

        int mkdirRet = std::system(("sudo mkdir -p " + ArkOSUtil::shellQuote(roms2)).c_str());
        if (mkdirRet != 0)
            LOG(LogWarning) << "SdCardControl: mkdir -p " << roms2 << " failed, exit code " << mkdirRet;

        // linux-native filesystems have no uid/umask mount options;
        // fat/exfat/ntfs need them so the ark user can write
        static const std::set<std::string> linuxNativeFs = { "ext4", "f2fs" };

        std::string mountCmd;
        if (linuxNativeFs.find(fsType) != linuxNativeFs.end())
        {
            mountCmd = "sudo mount -t " + fsType + " " + device + " " + roms2 + " 2>&1";
        }
        else
        {
            std::string uid = sanitizeOneLine(ArkOSUtil::executeCommand("id -u ark 2>/dev/null"));
            std::string gid = sanitizeOneLine(ArkOSUtil::executeCommand("id -g ark 2>/dev/null"));
            if (uid.empty() || uid.find_first_not_of("0123456789") != std::string::npos) uid = "1002";
            if (gid.empty() || gid.find_first_not_of("0123456789") != std::string::npos) gid = uid;

            mountCmd = "sudo mount -t " + fsType + " " + device + " " + roms2
                     + " -o umask=0000,iocharset=utf8,uid=" + uid + ",gid=" + gid + " 2>&1";
        }

        std::string mountOut = ArkOSUtil::executeCommand(mountCmd);

        if (!isMountPoint(roms2))
        {
            LOG(LogError) << "SdCardControl: failed to mount " << device << " (" << fsType << ") on " << roms2
                          << (mountOut.empty() ? "" : (": " + mountOut));
            return false;
        }

        LOG(LogInfo) << "SdCardControl: mounted " << device << " (" << fsType << ") on " << roms2;
        return true;
    }

    // ------------------------------------------------------------------
    // fstab marker
    // ------------------------------------------------------------------

    // The single "# roms2" marker line we manage in /etc/fstab. Switching
    // to sd2/dual appends it, switching back to sd1 deletes it again.
    // Purely a marker - mounting itself stays with mountRoms2().
    static const char* FSTAB = "/etc/fstab";
    static const char* FSTAB_MARKER = "# roms2";

    static bool fstabHasMarker()
    {
        std::ifstream in(FSTAB);
        if (!in.is_open())
            return false;

        std::string line;
        while (std::getline(in, line))
        {
            size_t first = line.find_first_not_of(" \t");
            if (first == std::string::npos)
                continue;

            size_t last = line.find_last_not_of(" \t\r");
            if (line.compare(first, last - first + 1, FSTAB_MARKER) == 0)
                return true;
        }
        return false;
    }

    bool updateFstabEntry(RomsMode mode)
    {
        if (mode == RomsMode::Sd1)
        {
            if (!fstabHasMarker())
                return true;

            // delete the exact "# roms2" line (comments and other lines kept)
            int ret = std::system("sudo sed -i '\\%^[[:space:]]*#[[:space:]]*roms2[[:space:]]*$%d' /etc/fstab");
            if (ret == 0 && !fstabHasMarker())
            {
                LOG(LogInfo) << "SdCardControl: removed " << FSTAB_MARKER << " from " << FSTAB;
                return true;
            }
            LOG(LogError) << "SdCardControl: failed to remove " << FSTAB_MARKER << " from " << FSTAB;
            return false;
        }

        if (fstabHasMarker())
            return true;

        // appending must not glue onto a last line that lacks a newline
        std::ifstream tail(FSTAB, std::ios::binary);
        bool needNewline = false;
        if (tail.is_open() && tail.seekg(-1, std::ios::end))
        {
            char last = '\n';
            tail.get(last);
            needNewline = (last != '\n');
        }

        std::string payload = std::string(needNewline ? "\n" : "") + FSTAB_MARKER + "\n";
        int ret = std::system(("printf '%s' " + ArkOSUtil::shellQuote(payload) + " | sudo tee -a /etc/fstab > /dev/null").c_str());
        if (ret == 0 && fstabHasMarker())
        {
            LOG(LogInfo) << "SdCardControl: added " << FSTAB_MARKER << " to " << FSTAB;
            return true;
        }
        LOG(LogError) << "SdCardControl: failed to add " << FSTAB_MARKER << " to " << FSTAB;
        return false;
    }

    // ------------------------------------------------------------------
    // startup hook
    // ------------------------------------------------------------------

    void applyRomsModeOnStartup()
    {
        RomsMode saved = getSavedMode();
        RomsMode effective;

        if (saved == RomsMode::Sd1)
        {
            effective = RomsMode::Sd1;
        }
        else
        {
            // guarantee the /roms2 mount before judging usability
            if (!isMountPoint(roms2) && isSd2CardPresent())
                mountRoms2();

            effective = isRoms2Usable() ? saved : RomsMode::Sd1;
        }

        LOG(LogInfo) << "SdCardControl: saved mode '" << modeToString(saved) << "', effective mode '" << modeToString(effective) << "'";
        applyCfg(effective); // best effort; result logged by applyCfg
    }
}
