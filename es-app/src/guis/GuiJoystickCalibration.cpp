#include "guis/GuiJoystickCalibration.h"
#include "guis/GuiMsgBox.h"
#include "guis/arkos4clone/ArkOSUtil.h"
#include "Settings.h"
#include "Window.h"
#include "Log.h"
#include "renderers/Renderer.h"
#include "EsLocale.h"
#include "ThemeData.h"
#include "resources/Font.h"
#include "utils/StringUtil.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdlib>

const std::string GuiJoystickCalibration::TUNING_NODE = "/sys/devices/platform/odroidgo3-joypad/joypad_tuning";

static void writeTuningNode(const std::string& axis, int value)
{
    ArkOSUtil::executeCommand("sudo sh -c 'echo " + axis + " " + std::to_string(value) + " > " + GuiJoystickCalibration::TUNING_NODE + "'");
}

GuiJoystickCalibration::GuiJoystickCalibration(Window* window)
    : GuiComponent(window), mPhase(Phase::SAMPLE_CENTER), mTimer(-1)
{
    std::vector<std::string> axes = {"x", "y", "rx", "ry"};
    for (const auto& axis : axes) {
        mCal[axis] = 0;
        mRangeMin[axis] = 99999;
        mRangeMax[axis] = -99999;
    }
}

GuiJoystickCalibration::~GuiJoystickCalibration()
{
}

void GuiJoystickCalibration::startRangeSampling()
{
    std::vector<std::string> axes = {"x", "y", "rx", "ry"};
    for (const auto& axis : axes) {
        mRangeMin[axis] = 99999;
        mRangeMax[axis] = -99999;
    }

    mPhase = Phase::SAMPLE_RANGE;
    mWindow->pushGui(new GuiMsgBox(mWindow,
        std::string(_("STEP 2: ROTATE BOTH STICKS FULL RANGE SLOWLY")) + "\n\n" +
        _("PRESS OK TO START") + " (5s)",
        _("OK"), [this] {
            mTimer = 5000;
        }));
}

void GuiJoystickCalibration::finishCalibration()
{
    mPhase = Phase::SHOW_RESULTS;

    std::vector<std::string> axes = {"x", "y", "rx", "ry"};
    for (const auto& axis : axes) {
        if (mCal[axis] == 0 || mRangeMin[axis] == 99999 || mRangeMax[axis] == -99999)
            continue;

        int center = mCal[axis];
        int posRange = mRangeMax[axis] - center;
        int negRange = center - mRangeMin[axis];

        if (posRange <= 0 || negRange <= 0)
            continue;

        mResult.tuningP[axis] = 180000 / posRange;
        mResult.tuningN[axis] = 180000 / negRange;
    }

    saveCalibrationToSettings();
    applyCalibrationValues();

    std::stringstream ss;
    ss << _("CALIBRATION COMPLETE!") << "\n\n";
    std::vector<std::string> showAxes = {"x", "y", "rx", "ry"};
    for (const auto& axis : showAxes) {
        if (mResult.tuningP.count(axis) && mResult.tuningN.count(axis)) {
            ss << axis << "_p=" << mResult.tuningP[axis]
               << "  " << axis << "_n=" << mResult.tuningN[axis] << "\n";
        }
    }

    mWindow->pushGui(new GuiMsgBox(mWindow, ss.str(), _("OK"), [this] { delete this; }));
}

bool GuiJoystickCalibration::readTuningNode()
{
    std::ifstream file(TUNING_NODE);
    if (!file.is_open())
        return false;

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string axis;
        std::getline(iss, axis, ':');
        axis.erase(std::remove_if(axis.begin(), axis.end(), ::isspace), axis.end());

        if (axis.empty() || mCal.find(axis) == mCal.end())
            continue;

        std::string pair;
        while (std::getline(iss, pair, ' ')) {
            size_t eqPos = pair.find('=');
            if (eqPos == std::string::npos) continue;

            std::string key = pair.substr(0, eqPos);
            std::string value = pair.substr(eqPos + 1);

            try {
                if (key.find("raw") != std::string::npos) {
                    int raw = std::stoi(value);
                    if (raw < mRangeMin[axis]) mRangeMin[axis] = raw;
                    if (raw > mRangeMax[axis]) mRangeMax[axis] = raw;
                } else if (key.find("cal") != std::string::npos) {
                    mCal[axis] = std::stoi(value);
                }
            } catch (...) {}
        }
    }
    return true;
}

void GuiJoystickCalibration::applyCalibrationValues()
{
    std::vector<std::string> axes = {"x", "y", "rx", "ry"};
    for (const auto& axis : axes) {
        if (mResult.tuningP.count(axis))
            writeTuningNode(axis + "_p", mResult.tuningP[axis]);
        if (mResult.tuningN.count(axis))
            writeTuningNode(axis + "_n", mResult.tuningN[axis]);
    }
}

void GuiJoystickCalibration::saveCalibrationToSettings()
{
    std::vector<std::string> axes = {"x", "y", "rx", "ry"};
    for (const auto& axis : axes) {
        if (mResult.tuningP.count(axis))
            Settings::getInstance()->setInt("JoystickCalibration." + axis + "_p", mResult.tuningP[axis]);
        if (mResult.tuningN.count(axis))
            Settings::getInstance()->setInt("JoystickCalibration." + axis + "_n", mResult.tuningN[axis]);
    }
    Settings::getInstance()->setBool("JoystickCalibration.hasData", true);
    Settings::getInstance()->saveFile();
}

void GuiJoystickCalibration::loadCalibrationFromSettings()
{
    if (!Settings::getInstance()->getBool("JoystickCalibration.hasData"))
        return;

    std::vector<std::string> axes = {"x", "y", "rx", "ry"};
    for (const auto& axis : axes) {
        int pVal = Settings::getInstance()->getInt("JoystickCalibration." + axis + "_p");
        int nVal = Settings::getInstance()->getInt("JoystickCalibration." + axis + "_n");

        if (pVal <= 0) {
            std::string pStr = Settings::getInstance()->getString("JoystickCalibration." + axis + "_p");
            if (!pStr.empty()) pVal = std::atoi(pStr.c_str());
        }
        if (nVal <= 0) {
            std::string nStr = Settings::getInstance()->getString("JoystickCalibration." + axis + "_n");
            if (!nStr.empty()) nVal = std::atoi(nStr.c_str());
        }

        if (pVal > 0) writeTuningNode(axis + "_p", pVal);
        if (nVal > 0) writeTuningNode(axis + "_n", nVal);
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

bool GuiJoystickCalibration::input(InputConfig* config, Input input)
{
    return false;
}

void GuiJoystickCalibration::update(int deltaTime)
{
    if (mTimer == -1) {
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

    if (mTimer <= 0) {
        if (mPhase == Phase::SAMPLE_CENTER) {
            mWindow->pushGui(new GuiMsgBox(mWindow,
                std::string(_("CENTER SAMPLING COMPLETE!")) + "\n\n" +
                _("PRESS OK TO START RANGE CALIBRATION"),
                _("OK"), [this] { startRangeSampling(); }));
        } else if (mPhase == Phase::SAMPLE_RANGE) {
            finishCalibration();
        }
    }
}

void GuiJoystickCalibration::render(const Transform4x4f& parentTrans)
{
    GuiComponent::render(parentTrans);

    if (mTimer > 0) {
        Renderer::setMatrix(Transform4x4f::Identity());
        Renderer::drawRect(0.0f, 0.0f, Renderer::getScreenWidth(), Renderer::getScreenHeight(), 0x000000CC);

        auto theme = ThemeData::getMenuTheme();
        auto font = theme->TextSmall.font;
        int seconds = (mTimer + 999) / 1000;

        std::string text;
        if (mPhase == Phase::SAMPLE_CENTER) {
            text = _("STEP 1: KEEP BOTH STICKS COMPLETELY STILL");
            text += "\n" + std::to_string(seconds) + "s";
        } else if (mPhase == Phase::SAMPLE_RANGE) {
            text = _("STEP 2: ROTATE BOTH STICKS FULL RANGE SLOWLY");
            text += "\n" + std::to_string(seconds) + "s";
        }

        auto size = font->sizeText(text);
        float x = (Renderer::getScreenWidth() - size.x()) / 2;
        float y = (Renderer::getScreenHeight() - size.y()) / 2;
        auto cache = font->buildTextCache(text, Vector2f(x, y), 0xFFFFFFFF, size.x());
        font->renderTextCache(cache);
        delete cache;
    }
}

std::vector<HelpPrompt> GuiJoystickCalibration::getHelpPrompts()
{
    return std::vector<HelpPrompt>();
}
