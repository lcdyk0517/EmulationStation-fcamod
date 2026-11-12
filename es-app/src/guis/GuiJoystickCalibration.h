#pragma once
#ifndef ES_APP_GUIJOYSTICKCALIBRATION_H
#define ES_APP_GUIJOYSTICKCALIBRATION_H

#include "GuiComponent.h"
#include "Settings.h"

#include <map>
#include <string>

// 摇杆校准 (适配 rk3562-joystick 驱动)
//   节点: /sys/devices/platform/rk3562-joystick/joypad_tuning
//   读:   "x: raw=<uv> cal=<uv> min=<uv> max=<uv>"
//   写:   "x <center_uv> <min_uv> <max_uv>"
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
	enum class Phase { SAMPLE_CENTER, SAMPLE_RANGE, SHOW_RESULTS };

	void startRangeSampling();
	void finishCalibration();
	void readTuningNode();
	void applyCalibrationValues();
	void saveCalibrationToSettings();
	static void loadCalibrationFromSettings();

	Phase mPhase;
	int mTimer;

	std::map<std::string, long> mSumRaw;
	std::map<std::string, int> mCount;
	std::map<std::string, int> mRangeMin;
	std::map<std::string, int> mRangeMax;

	std::map<std::string, int> mCenter;
	std::map<std::string, int> mMin;
	std::map<std::string, int> mMax;
};

#endif
