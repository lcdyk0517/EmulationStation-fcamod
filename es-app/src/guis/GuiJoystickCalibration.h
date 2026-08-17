#pragma once
#ifndef ES_APP_GUIJOYSTICKCALIBRATION_H
#define ES_APP_GUIJOYSTICKCALIBRATION_H

#include "GuiComponent.h"
#include <string>
#include <map>

class GuiJoystickCalibration : public GuiComponent
{
public:
    GuiJoystickCalibration(Window* window);
    ~GuiJoystickCalibration() override;

    bool input(InputConfig* config, Input input) override;
    void update(int deltaTime) override;
    void render(const Transform4x4f& parentTrans) override;
    std::vector<HelpPrompt> getHelpPrompts() override;

    static void applyCalibrationOnStartup();
    static bool hasCalibrationData();
    static const std::string TUNING_NODE;

private:
    enum class Phase {
        SAMPLE_CENTER,
        SAMPLE_RANGE,
        SHOW_RESULTS
    };

    void startRangeSampling();
    void finishCalibration();
    bool readTuningNode();
    void applyCalibrationValues();
    void saveCalibrationToSettings();
    static void loadCalibrationFromSettings();

    Phase mPhase;
    int mTimer;

    std::map<std::string, int> mCal;
    std::map<std::string, int> mRangeMin;
    std::map<std::string, int> mRangeMax;

    struct Result {
        std::map<std::string, int> tuningP;
        std::map<std::string, int> tuningN;
    };
    Result mResult;
};

#endif
