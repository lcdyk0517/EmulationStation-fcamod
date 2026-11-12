#pragma once
#ifndef ES_APP_GUIS_ARKOS4CLONE_ARKOSUTIL_H
#define ES_APP_GUIS_ARKOS4CLONE_ARKOSUTIL_H

#include <string>

namespace ArkOSUtil
{
    std::string executeCommand(const std::string& cmd);
    std::string shellQuote(const std::string& arg);
    std::string getSystemName();
}

#endif // ES_APP_GUIS_ARKOS4CLONE_ARKOSUTIL_H
