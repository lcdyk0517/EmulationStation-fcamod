#include "guis/arkos4clone/SdCardControl.h"
#include "guis/arkos4clone/ArkOSUtil.h"
#include "Settings.h"
#include "SystemData.h"
#include "Log.h"
#include "EsLocale.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <set>
#include <sstream>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <vector>

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

    // Block device backing "/" (mmcblk1p7 -> mmcblk1), empty when root is
    // not on an mmc device.
    static std::string rootBlockDevice()
    {
        std::ifstream mounts("/proc/mounts");
        if (!mounts.is_open())
            return "";

        std::string line;
        while (std::getline(mounts, line))
        {
            std::istringstream stream(line);
            std::string dev, mp;
            if (!((stream >> dev) && (stream >> mp)) || mp != "/")
                continue;

            const std::string prefix = "/dev/";
            if (dev.compare(0, prefix.size(), prefix) != 0)
                return "";

            std::string name = dev.substr(prefix.size());
            for (size_t i = name.size(); i > 0; --i)
            {
                if (name[i - 1] == 'p' && i < name.size() && isdigit((unsigned char)name[i]))
                {
                    name.resize(i - 1);
                    break;
                }
            }
            return name;
        }
        return "";
    }

    std::string findSd2Device()
    {
        // RK3562 enumerates eMMC as mmcblk0, the boot TF card as mmcblk1
        // and a real second SD slot as mmcblk2 - so the slot must be
        // detected, never assumed.
        const std::string rootDev = rootBlockDevice();

        DIR* dir = opendir("/sys/block");
        if (dir == nullptr)
            return "";

        std::vector<std::string> candidates;
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr)
        {
            std::string name = entry->d_name;
            if (name.compare(0, 6, "mmcblk") == 0 && name != rootDev)
                candidates.push_back(name);
        }
        closedir(dir);
        std::sort(candidates.begin(), candidates.end());

        for (const std::string& name : candidates)
        {
            // SD cards report device/type "SD"; eMMC reports "MMC"; the
            // Wi-Fi SDIO controller has no block device at all.
            std::ifstream typeFile("/sys/block/" + name + "/device/type");
            std::string type;
            if (typeFile.is_open())
                typeFile >> type;

            if (!type.empty() && type != "SD")
                continue;

            if (type.empty())
            {
                std::ifstream removableFile("/sys/block/" + name + "/removable");
                std::string removable;
                if (!removableFile.is_open() || !(removableFile >> removable) || removable != "1")
                    continue;
            }

            if (Utils::FileSystem::exists("/dev/" + name))
                return "/dev/" + name;
        }
        return "";
    }

    bool isSd2CardPresent()
    {
        return !findSd2Device().empty();
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

    // A freshly formatted card only carries filesystem housekeeping dirs;
    // counting those as "ROM directories" would make an empty card look usable.
    static bool isHousekeepingEntry(const char* name)
    {
        if (name[0] == '.')
            return true;

        return strcmp(name, "lost+found") == 0 ||
               strcmp(name, "System Volume Information") == 0 ||
               strcmp(name, "$RECYCLE.BIN") == 0;
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
            if (isHousekeepingEntry(entry->d_name))
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

    // Write to a temp file in the same directory and rename() over the
    // target, so a crash or power loss never leaves a half-written cfg and
    // readers only ever see the old or the new file.
    static bool writeFileAtomic(const std::string& path, const std::string& content)
    {
        const std::string tmp = path + ".tmp";

        int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0)
            return false;

        size_t written = 0;
        bool ok = true;
        while (written < content.size())
        {
            ssize_t n = ::write(fd, content.data() + written, content.size() - written);
            if (n < 0)
            {
                if (errno == EINTR)
                    continue;
                ok = false;
                break;
            }
            if (n == 0)
            {
                ok = false;
                break;
            }
            written += static_cast<size_t>(n);
        }

        if (ok && ::fsync(fd) != 0)
            ok = false;
        ::close(fd);

        if (!ok || ::rename(tmp.c_str(), path.c_str()) != 0)
        {
            ::unlink(tmp.c_str());
            return false;
        }
        return true;
    }

    // Same atomic replace, but staged under /tmp and installed via sudo for
    // targets the ES user cannot write (e.g. a root-owned /etc config).
    static bool writeFileAtomicSudo(const std::string& path, const std::string& content)
    {
        char staged[] = "/tmp/es_systems.XXXXXX";
        int fd = ::mkstemp(staged);
        if (fd < 0)
            return false;

        size_t written = 0;
        while (written < content.size())
        {
            ssize_t n = ::write(fd, content.data() + written, content.size() - written);
            if (n < 0)
            {
                if (errno == EINTR)
                    continue;
                break;
            }
            if (n == 0)
                break;
            written += static_cast<size_t>(n);
        }
        ::close(fd);

        if (written != content.size())
        {
            ::unlink(staged);
            return false;
        }

        const std::string tmp = path + ".tmp";
        std::string cmd = "sudo cp -f " + ArkOSUtil::shellQuote(staged) + " " + ArkOSUtil::shellQuote(tmp)
                        + " && sudo chown ark:ark " + ArkOSUtil::shellQuote(tmp)
                        + " && sudo chmod 777 " + ArkOSUtil::shellQuote(tmp)
                        + " && sudo mv -f " + ArkOSUtil::shellQuote(tmp) + " " + ArkOSUtil::shellQuote(path);
        int ret = std::system(cmd.c_str());
        ::unlink(staged);

        if (ret != 0)
        {
            std::system(("sudo rm -f " + ArkOSUtil::shellQuote(tmp)).c_str());
            return false;
        }
        return true;
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

        if (writeFileAtomic(target, content))
        {
            LOG(LogInfo) << "SdCardControl: applied " << variant << " -> " << target;
            return ApplyResult::Applied;
        }

        // direct write failed (e.g. target missing/permissions), stage via sudo
        LOG(LogWarning) << "SdCardControl: direct write failed, staging via sudo";
        std::string after;
        if (writeFileAtomicSudo(target, content) && readFile(target, after) && after == content)
        {
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

        const std::string deviceBase = findSd2Device();
        if (deviceBase.empty())
        {
            LOG(LogWarning) << "SdCardControl: cannot mount " << roms2 << ", no SD2 card";
            return false;
        }

        // Prefer the first partition; a whole-disk filesystem also works.
        std::string device = deviceBase;
        if (Utils::FileSystem::exists(deviceBase + "p1"))
            device = deviceBase + "p1";

        std::string fsType = sanitizeOneLine(ArkOSUtil::executeCommand("lsblk -no FSTYPE " + device + " 2>/dev/null"));
        if (fsType.empty() && device != deviceBase)
        {
            device = deviceBase;
            fsType = sanitizeOneLine(ArkOSUtil::executeCommand("lsblk -no FSTYPE " + device + " 2>/dev/null"));
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
    // directory sync
    // ------------------------------------------------------------------

    static bool isMountReadOnly(const std::string& path)
    {
        std::ifstream in("/proc/mounts");
        std::string line, dev, mnt, type, opts;
        while (std::getline(in, line))
        {
            std::istringstream ss(line);
            if (!(ss >> dev >> mnt >> type >> opts))
                continue;
            if (mnt != path)
                continue;

            std::istringstream o(opts);
            std::string opt;
            while (std::getline(o, opt, ','))
            {
                if (opt == "ro")
                    return true;
            }
            return false;
        }
        return false;
    }

    static unsigned long long freeBytes(const std::string& path)
    {
        struct statvfs st;
        if (statvfs(path.c_str(), &st) != 0)
            return 0;
        return static_cast<unsigned long long>(st.f_bavail) * st.f_frsize;
    }

    static std::string errnoText(int err)
    {
        switch (err)
        {
            case EROFS:        return _("read-only filesystem");
            case ENOSPC:       return _("no space left");
            case EACCES:
            case EPERM:        return _("permission denied");
            case EEXIST:       return _("a file with this name already exists");
            case EINVAL:       return _("name not allowed on this filesystem");
            case ENAMETOOLONG: return _("name too long");
            case EIO:          return _("I/O error");
            default:           return _("unknown error");
        }
    }

    SyncResult syncRoms2Dirs()
    {
        SyncResult r;

        if (!isMountPoint(roms2))
        {
            r.ok = false;
            r.detail = _("SD2 card is not mounted");
            return r;
        }

        // A card can be mounted read-only (remount-ro on I/O error, manual
        // mount option). Try one remount before giving up.
        if (isMountReadOnly(roms2))
        {
            ArkOSUtil::executeCommand("sudo mount -o remount,rw " + roms2 + " 2>/dev/null");
            if (isMountReadOnly(roms2))
            {
                r.ok = false;
                r.detail = _("SD2 card is mounted read-only");
                return r;
            }
        }

        // Empty directories need almost no space; below this the card is full
        // or its filesystem is broken, which directory creation cannot fix.
        if (freeBytes(roms2) < 8ULL * 1024 * 1024)
        {
            r.ok = false;
            r.detail = _("Not enough free space on SD2 card");
            return r;
        }

        // Write probe: catch a card that mounted rw but still rejects writes.
        {
            const std::string probe = roms2 + "/.sd2-write-test";
            int rc = mkdir(probe.c_str(), 0777);
            int err = errno;
            if (rc != 0 && (err == EACCES || err == EPERM))
            {
                if (std::system(("sudo mkdir -p " + ArkOSUtil::shellQuote(probe)).c_str()) == 0)
                    rc = 0;
                else
                    err = errno;
            }
            if (rc != 0)
            {
                r.ok = false;
                r.detail = _("SD2 card is not writable") + "\n" + errnoText(err);
                return r;
            }
            rmdir(probe.c_str());
        }

        // Mirror the /roms layout as empty directories. Best effort: a name
        // the card's filesystem rejects (or a colliding file) is skipped and
        // reported, the remaining directories are still created.
        DIR* dir = opendir("/roms");
        if (!dir)
        {
            r.ok = false;
            r.detail = _("Internal /roms is missing");
            return r;
        }

        std::string first_fail;
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr)
        {
            if (entry->d_name[0] == '.')
                continue;

            const std::string src = std::string("/roms/") + entry->d_name;
            struct stat st;
            if (stat(src.c_str(), &st) != 0 || !S_ISDIR(st.st_mode))
                continue;

            const std::string dst = roms2 + "/" + entry->d_name;
            if (mkdir(dst.c_str(), 0777) == 0)
            {
                ++r.created;
                continue;
            }

            int err = errno;
            if (err == EEXIST)
            {
                if (stat(dst.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
                {
                    ++r.existing;
                    continue;
                }
            }
            else if (err == EACCES || err == EPERM)
            {
                // ext4/f2fs mounts are root-owned: fall back to sudo once.
                if (std::system(("sudo mkdir -p " + ArkOSUtil::shellQuote(dst)).c_str()) == 0)
                {
                    ++r.created;
                    continue;
                }
            }

            ++r.failed;
            const std::string why = errnoText(err);
            LOG(LogWarning) << "SdCardControl: mkdir " << dst << " failed: " << why;
            if (first_fail.empty())
                first_fail = entry->d_name + std::string(": ") + why;
        }
        closedir(dir);

        if (r.failed > 0 && r.created == 0 && r.existing == 0)
        {
            r.ok = false;
            r.detail = _("Nothing could be created on SD2 card") + "\n" + first_fail;
        }

        LOG(LogInfo) << "SdCardControl: roms2 prepare: created " << r.created
                     << ", existing " << r.existing << ", failed " << r.failed;
        return r;
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

            // A freshly formatted card has no system directories yet: mirror
            // the /roms layout before declaring it empty and falling back.
            if (isMountPoint(roms2) && !hasRoms2Subdirs())
                syncRoms2Dirs();

            effective = isRoms2Usable() ? saved : RomsMode::Sd1;
        }

        LOG(LogInfo) << "SdCardControl: saved mode '" << modeToString(saved) << "', effective mode '" << modeToString(effective) << "'";

        ApplyResult result = applyCfg(effective);
        if (result == ApplyResult::Failed && effective != RomsMode::Sd1)
        {
            LOG(LogError) << "SdCardControl: cannot apply '" << modeToString(effective)
                          << "' config, falling back to sd1 config for this session";
            result = applyCfg(RomsMode::Sd1);
        }

        if (result == ApplyResult::Failed)
            LOG(LogError) << "SdCardControl: no applicable config variant, keeping the existing es_systems.cfg";
    }
}
