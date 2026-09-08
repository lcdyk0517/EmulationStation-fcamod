#pragma once
#ifndef ES_APP_GUIVOLUMEKEYCALIBRATION_H
#define ES_APP_GUIVOLUMEKEYCALIBRATION_H

#include "GuiComponent.h"
#include <vector>

class GuiVolumeKeyCalibration : public GuiComponent
{
public:
    GuiVolumeKeyCalibration(Window* window);
    ~GuiVolumeKeyCalibration() override;

    bool input(InputConfig* config, Input input) override;
    void update(int deltaTime) override;
    void render(const Transform4x4f& parentTrans) override;
    std::vector<HelpPrompt> getHelpPrompts() override;

private:
    enum class Phase {
        SAMPLE_IDLE,
        SAMPLE_VOLDOWN,
        SAMPLE_VOLUP
    };

    void startPhase(Phase phase);
    void finishCalibration();
    void restartCalibration();
    std::string getStepText() const;

    Phase mPhase;
    int mTimer;

    std::vector<int> mSamples;

    int mIdleValue;
    int mVolDownValue;
    int mVolUpValue;
};

#endif
