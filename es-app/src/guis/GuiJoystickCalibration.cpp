#include "guis/GuiJoystickCalibration.h"
#include "guis/GuiMsgBox.h"
#include "Window.h"
#include "EsLocale.h"
#include "ThemeData.h"
#include "renderers/Renderer.h"
#include "resources/Font.h"
#include "Log.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>
#include <unistd.h>

const std::string GuiJoystickCalibration::TUNING_NODE =
	access("/sys/devices/platform/play_joystick", F_OK) == 0
		? "/sys/devices/platform/play_joystick/joypad_tuning"
		: "/sys/devices/platform/rk3562-joystick/joypad_tuning";

static const char* const kAxes[] = { "x", "y", "rx", "ry" };

static void execCmd(const std::string& cmd)
{
	if (system(cmd.c_str()) != 0)
		LOG(LogWarning) << "Calibration: command failed: " << cmd;
}

static void writeTuningAxis(const std::string& axis, int center, int min, int max)
{
	execCmd("sudo sh -c 'echo " + axis + " " + std::to_string(center) + " " +
		std::to_string(min) + " " + std::to_string(max) + " > " +
		GuiJoystickCalibration::TUNING_NODE + "'");
}

GuiJoystickCalibration::GuiJoystickCalibration(Window* window)
	: GuiComponent(window), mPhase(Phase::SAMPLE_CENTER), mTimer(-1)
{
}

GuiJoystickCalibration::~GuiJoystickCalibration() = default;

void GuiJoystickCalibration::startRangeSampling()
{
	for (const char* axis : kAxes)
	{
		mRangeMin[axis] = 99999999;
		mRangeMax[axis] = -99999999;
	}

	mPhase = Phase::SAMPLE_RANGE;
	mWindow->pushGui(new GuiMsgBox(mWindow,
		std::string(_("STEP 2: ROTATE BOTH STICKS FULL RANGE SLOWLY")) + "\n\n" +
		_("PRESS OK TO START") + " (5s)",
		_("OK"), [this] { mTimer = 5000; }));
}

void GuiJoystickCalibration::finishCalibration()
{
	mPhase = Phase::SHOW_RESULTS;

	std::stringstream ss;
	ss << _("CALIBRATION COMPLETE!") << "\n\n";

	for (const char* axis : kAxes)
	{
		if (mCount[axis] == 0 || mRangeMin[axis] == 99999999 ||
		    mRangeMax[axis] == -99999999)
			continue;

		const int center = (int)(mSumRaw[axis] / mCount[axis]);
		const int minv = mRangeMin[axis];
		const int maxv = mRangeMax[axis];

		if (minv >= center || center >= maxv)
			continue;

		mCenter[axis] = center;
		mMin[axis] = minv;
		mMax[axis] = maxv;

		writeTuningAxis(axis, center, minv, maxv);
		ss << axis << ": center=" << center << " min=" << minv
		   << " max=" << maxv << "\n";
	}

	saveCalibrationToSettings();
	mWindow->pushGui(new GuiMsgBox(mWindow, ss.str(), _("OK"), [this] { delete this; }));
}

void GuiJoystickCalibration::readTuningNode()
{
	std::ifstream file(TUNING_NODE);
	if (!file.is_open())
		return;

	std::string line;
	while (std::getline(file, line))
	{
		const auto colon = line.find(':');
		if (colon == std::string::npos)
			continue;

		std::string axis = line.substr(0, colon);
		axis.erase(std::remove_if(axis.begin(), axis.end(), ::isspace), axis.end());

		if (std::find_if(std::begin(kAxes), std::end(kAxes),
				 [&](const char* a) { return axis == a; }) == std::end(kAxes))
			continue;

		std::istringstream iss(line.substr(colon + 1));
		std::string pair;
		int raw = -1;

		while (iss >> pair)
		{
			const auto eq = pair.find('=');
			if (eq == std::string::npos)
				continue;
			if (pair.substr(0, eq) == "raw")
				raw = atoi(pair.substr(eq + 1).c_str());
		}

		if (raw < 0)
			continue;

		if (mPhase == Phase::SAMPLE_CENTER)
		{
			mSumRaw[axis] += raw;
			mCount[axis]++;
		}
		else if (mPhase == Phase::SAMPLE_RANGE)
		{
			mRangeMin[axis] = std::min(mRangeMin[axis], raw);
			mRangeMax[axis] = std::max(mRangeMax[axis], raw);
		}
	}
}

void GuiJoystickCalibration::update(int deltaTime)
{
	if (mTimer == -1)
	{
		mTimer = -2;
		mWindow->pushGui(new GuiMsgBox(mWindow,
			std::string(_("JOYSTICK CALIBRATION")) + "\n\n" +
			_("STEP 1: KEEP BOTH STICKS COMPLETELY STILL") + "\n\n" +
			_("PRESS OK TO START"),
			_("OK"), [this] { mTimer = 3000; }));
		return;
	}

	if (mTimer <= 0)
		return;

	readTuningNode();
	mTimer -= deltaTime;

	if (mTimer <= 0)
	{
		if (mPhase == Phase::SAMPLE_CENTER)
		{
			mWindow->pushGui(new GuiMsgBox(mWindow,
				std::string(_("CENTER SAMPLING COMPLETE!")) + "\n\n" +
				_("PRESS OK TO START RANGE CALIBRATION"),
				_("OK"), [this] { startRangeSampling(); }));
		}
		else if (mPhase == Phase::SAMPLE_RANGE)
		{
			finishCalibration();
		}
	}
}

void GuiJoystickCalibration::render(const Transform4x4f& parentTrans)
{
	GuiComponent::render(parentTrans);

	if (mTimer <= 0)
		return;

	Renderer::setMatrix(Transform4x4f::Identity());
	Renderer::drawRect(0.0f, 0.0f, Renderer::getScreenWidth(), Renderer::getScreenHeight(), 0x000000CC);

	auto theme = ThemeData::getMenuTheme();
	auto font = theme->TextSmall.font;
	const int seconds = (mTimer + 999) / 1000;

	std::string text = (mPhase == Phase::SAMPLE_CENTER) ?
		_("STEP 1: KEEP BOTH STICKS COMPLETELY STILL") :
		_("STEP 2: ROTATE BOTH STICKS FULL RANGE SLOWLY");
	text += "\n" + std::to_string(seconds) + "s";

	auto size = font->sizeText(text);
	float x = (Renderer::getScreenWidth() - size.x()) / 2;
	float y = (Renderer::getScreenHeight() - size.y()) / 2;
	auto cache = font->buildTextCache(text, Vector2f(x, y), 0xFFFFFFFF, size.x());
	font->renderTextCache(cache);
	delete cache;
}

std::vector<HelpPrompt> GuiJoystickCalibration::getHelpPrompts()
{
	return {};
}

bool GuiJoystickCalibration::input(InputConfig*, Input)
{
	return false;
}

void GuiJoystickCalibration::saveCalibrationToSettings()
{
	for (const char* axis : kAxes)
	{
		if (!mCenter.count(axis))
			continue;
		Settings::getInstance()->setInt(std::string("JoystickCalibration.") + axis + "_center", mCenter[axis]);
		Settings::getInstance()->setInt(std::string("JoystickCalibration.") + axis + "_min", mMin[axis]);
		Settings::getInstance()->setInt(std::string("JoystickCalibration.") + axis + "_max", mMax[axis]);
	}
	Settings::getInstance()->setBool("JoystickCalibration.hasData", true);
	Settings::getInstance()->saveFile();
}

void GuiJoystickCalibration::loadCalibrationFromSettings()
{
	if (!Settings::getInstance()->getBool("JoystickCalibration.hasData"))
		return;

	for (const char* axis : kAxes)
	{
		const int c = Settings::getInstance()->getInt(std::string("JoystickCalibration.") + axis + "_center");
		const int mn = Settings::getInstance()->getInt(std::string("JoystickCalibration.") + axis + "_min");
		const int mx = Settings::getInstance()->getInt(std::string("JoystickCalibration.") + axis + "_max");

		if (c > 0 && mn > 0 && mx > mn)
			writeTuningAxis(axis, c, mn, mx);
	}
}

void GuiJoystickCalibration::applyCalibrationOnStartup()
{
	loadCalibrationFromSettings();
}

bool GuiJoystickCalibration::hasCalibrationData()
{
	return Settings::getInstance()->getBool("JoystickCalibration.hasData");
}
