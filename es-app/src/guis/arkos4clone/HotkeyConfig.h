#pragma once
#ifndef ES_APP_GUIS_ARKOS4CLONE_HOTKEYCONFIG_H
#define ES_APP_GUIS_ARKOS4CLONE_HOTKEYCONFIG_H

#include <map>
#include <string>

namespace HotkeyConfig
{
    // /home/ark/ogage.conf as KEY=value. Unknown keys/comments are preserved
    // by writeValues(); only the keys passed in are replaced or appended.
    std::map<std::string, std::string> read();
    bool writeValues(const std::map<std::string, std::string>& values);
    void restartOgAge();
}

#endif // ES_APP_GUIS_ARKOS4CLONE_HOTKEYCONFIG_H
