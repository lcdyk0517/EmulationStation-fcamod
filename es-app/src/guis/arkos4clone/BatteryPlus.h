#pragma once
#ifndef ES_APP_GUIS_ARKOS4CLONE_BATTERYPLUS_H
#define ES_APP_GUIS_ARKOS4CLONE_BATTERYPLUS_H

#include <string>

namespace BatteryPlus
{
    bool isAvailable();
    bool isEnabled();
    void setEnabled(bool enable);
    std::string getMode();
    void setMode(const std::string& mode);
    std::string getPercent();
    std::string getChargeStatus();
    int getVoltageMv();
    void deleteRecords();
}

#endif // ES_APP_GUIS_ARKOS4CLONE_BATTERYPLUS_H
