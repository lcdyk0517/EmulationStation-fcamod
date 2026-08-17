#include "guis/arkos4clone/ArkOSUtil.h"
#include "utils/StringUtil.h"

#include <cstdio>

namespace ArkOSUtil
{

std::string executeCommand(const std::string& cmd)
{
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";

    char buffer[256];
    std::string result;
    while (fgets(buffer, sizeof(buffer), pipe)) {
        result += buffer;
    }
    pclose(pipe);
    return Utils::String::trim(result);
}

// Wrap a value as a single shell-quoted argument, safe against spaces,
// quotes and shell metacharacters. Use for any user-supplied input that is
// embedded into a command line (e.g. WiFi SSID / password).
std::string shellQuote(const std::string& arg)
{
    std::string out = "'";
    for (char c : arg) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}

std::string getSystemName()
{
    static std::string cached;
    if (cached.empty()) {
        cached = executeCommand("/usr/local/bin/console_detect -V 2>/dev/null");
        if (cached.empty()) cached = "ArkOS4Clone";
        cached = Utils::String::trim(cached);
    }
    return cached;
}

}
