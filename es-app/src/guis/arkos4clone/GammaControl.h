#pragma once
#ifndef ES_APP_GUIS_ARKOS4CLONE_GAMMACONTROL_H
#define ES_APP_GUIS_ARKOS4CLONE_GAMMACONTROL_H

#include <string>

namespace GammaControl
{
    bool isAvailable();
    float getGammaR();
    float getGammaG();
    float getGammaB();
    void setGamma(float r, float g, float b);
    void resetGamma();
    void applyGammaOnStartup();
}

#endif // ES_APP_GUIS_ARKOS4CLONE_GAMMACONTROL_H
