#include "guis/arkos4clone/GammaControl.h"
#include "guis/arkos4clone/ArkOSUtil.h"
#include "utils/FileSystemUtil.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cmath>

#define DEFAULT_GAMMA 1.0f
#define CONFIG_FILE ".config/gamma/GAMMA.VALUE"

namespace GammaControl
{

bool isAvailable()
{
    return Utils::FileSystem::exists("/usr/local/bin/gamma");
}

static std::string getConfigPath()
{
    const char* home = getenv("HOME");
    if (!home) return "";
    return std::string(home) + "/" + CONFIG_FILE;
}

static float readGammaValue(int index)
{
    std::string path = getConfigPath();
    if (path.empty()) return DEFAULT_GAMMA;

    FILE *fp = fopen(path.c_str(), "r");
    if (!fp) return DEFAULT_GAMMA;

    float r = DEFAULT_GAMMA, g = DEFAULT_GAMMA, b = DEFAULT_GAMMA;
    int n = fscanf(fp, "%f %f %f", &r, &g, &b);
    fclose(fp);

    if (n < 1) return DEFAULT_GAMMA;

    switch (index) {
        case 0: return r;
        case 1: return g;
        case 2: return b;
        default: return DEFAULT_GAMMA;
    }
}

float getGammaR()
{
    return readGammaValue(0);
}

float getGammaG()
{
    return readGammaValue(1);
}

float getGammaB()
{
    return readGammaValue(2);
}

void setGamma(float r, float g, float b)
{
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "gamma -r %.2f -g %.2f -b %.2f", r, g, b);
    ArkOSUtil::executeCommand(cmd);
}

void resetGamma()
{
    ArkOSUtil::executeCommand("gamma -R");
}

void applyGammaOnStartup()
{
    if (!isAvailable()) return;

    std::string path = getConfigPath();
    if (path.empty()) return;

    FILE *fp = fopen(path.c_str(), "r");
    if (!fp) return;

    float r = DEFAULT_GAMMA, g = DEFAULT_GAMMA, b = DEFAULT_GAMMA;
    int n = fscanf(fp, "%f %f %f", &r, &g, &b);
    fclose(fp);

    if (n == 3 && r >= 0.3f && r <= 2.0f && g >= 0.3f && g <= 2.0f && b >= 0.3f && b <= 2.0f) {
        setGamma(r, g, b);
    }
}

}
