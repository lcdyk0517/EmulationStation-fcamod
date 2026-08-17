#pragma once
#ifndef ES_APP_GUIS_ARKOS4CLONE_WIFIMANAGER_H
#define ES_APP_GUIS_ARKOS4CLONE_WIFIMANAGER_H

#include <string>

namespace WifiManager
{
    std::string getActiveWifiInterface();
    std::string getCurrentWifiSSID();
    bool isWifiRfkillBlocked();
    bool isWifiEnabled();
    void toggleWifi(bool enable);

    bool isRemoteServicesEnabled();
    std::string getIpAddress();
    void toggleRemoteServices(bool enable);
    bool isRemoteServicesAutoStart();
    void toggleRemoteServicesAutoStart(bool enable);
}

#endif // ES_APP_GUIS_ARKOS4CLONE_WIFIMANAGER_H
