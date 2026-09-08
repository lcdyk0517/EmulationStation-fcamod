#include "guis/GuiVolumeKeyCalibration.h"
#include "guis/GuiMsgBox.h"
#include "guis/arkos4clone/ArkOSUtil.h"
#include "guis/arkos4clone/SystemSettings.h"
#include "Settings.h"
#include "Window.h"
#include "Log.h"
#include "renderers/Renderer.h"
#include "EsLocale.h"
#include "ThemeData.h"
#include "resources/Font.h"

#include <fstream>
#include <sstream>
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <vector>

static const std::string ADC_RAW_PATH = "/sys/bus/iio/devices/iio:device0/in_voltage2_raw";
static const int VOLUME_ADC_TOLERANCE = 10;
static const int SAMPLE_DURATION_MS = 5000;

static int readAdcRaw()
{
    std::ifstream file(ADC_RAW_PATH);
    int value = -1;
    if (file.is_open() && (file >> value))
        return value;
    return -1;
}

// Median of the samples (robust against single-sample glitches)
static int medianOf(std::vector<int> samples)
{
    if (samples.empty())
        return -1;

    std::sort(samples.begin(), samples.end());
    size_t n = samples.size();
    if (n % 2 == 1)
        return samples[n / 2];
    return (int)std::lround((samples[n / 2 - 1] + samples[n / 2]) / 2.0);
}

// Median of the samples that differ from the idle value, dropping samples
// still at idle level (e.g. taken before the key was pressed or after release)
static int pressedMedian(const std::vector<int>& samples, int idleValue)
{
    std::vector<int> pressed;
    for (int s : samples) {
        if (idleValue >= 0 && std::abs(s - idleValue) <= VOLUME_ADC_TOLERANCE)
            continue;
        pressed.push_back(s);
    }
    return medianOf(pressed);
}

GuiVolumeKeyCalibration::GuiVolumeKeyCalibration(Window* window)
    : GuiComponent(window), mPhase(Phase::SAMPLE_IDLE), mTimer(-1),
      mIdleValue(-1), mVolDownValue(-1), mVolUpValue(-1)
{
}

GuiVolumeKeyCalibration::~GuiVolumeKeyCalibration()
{
}

void GuiVolumeKeyCalibration::startPhase(Phase phase)
{
    mPhase = phase;
    mSamples.clear();
    mTimer = SAMPLE_DURATION_MS;
}

void GuiVolumeKeyCalibration::restartCalibration()
{
    mIdleValue = -1;
    mVolDownValue = -1;
    mVolUpValue = -1;
    mSamples.clear();
    mTimer = -1; // re-show intro on next update
}

void GuiVolumeKeyCalibration::finishCalibration()
{
    bool valid = (mIdleValue >= 0 && mVolDownValue >= 0 && mVolUpValue >= 0) &&
                 std::abs(mVolDownValue - mIdleValue) > VOLUME_ADC_TOLERANCE &&
                 std::abs(mVolUpValue - mIdleValue) > VOLUME_ADC_TOLERANCE;

    if (!valid) {
        LOG(LogWarning) << "GuiVolumeKeyCalibration: idle=" << mIdleValue
                        << " volDown=" << mVolDownValue
                        << " volUp=" << mVolUpValue;
        mWindow->pushGui(new GuiMsgBox(mWindow,
            std::string(_("NO VOLTAGE CHANGE DETECTED")) + "\n\n" + _("DO YOU WANT TO CONTINUE?"),
            _("YES"), [this] { restartCalibration(); },
            _("NO"), [this] { delete this; }));
        return;
    }

    // Write sysfs nodes and persist values
    SystemSettings::setVolumeAdcKeyValues(mVolDownValue, mVolUpValue);
    Settings::getInstance()->setInt("VolumeAdcCalibration.idle", mIdleValue);
    Settings::getInstance()->setInt("VolumeAdcCalibration.down", mVolDownValue);
    Settings::getInstance()->setInt("VolumeAdcCalibration.up", mVolUpValue);
    Settings::getInstance()->setBool("VolumeAdcCalibration.hasData", true);
    Settings::getInstance()->saveFile();

    std::stringstream ss;
    ss << _("CALIBRATION COMPLETE!") << "\n\n";
    ss << "IDLE: " << mIdleValue << "\n";
    ss << "VOL-: " << mVolDownValue << "\n";
    ss << "VOL+: " << mVolUpValue;

    mWindow->pushGui(new GuiMsgBox(mWindow, ss.str(), _("OK"), [this] { delete this; }));
}

bool GuiVolumeKeyCalibration::input(InputConfig* config, Input input)
{
    // Allow cancelling while sampling
    if (mTimer > 0 && input.value != 0 && config->isMappedTo(BUTTON_BACK, input)) {
        delete this;
        return true;
    }
    return false;
}

void GuiVolumeKeyCalibration::update(int deltaTime)
{
    if (mTimer == -1) {
        mTimer = -2;
        mWindow->pushGui(new GuiMsgBox(mWindow,
            std::string(_("VOLUME KEY ADC CALIBRATION")) + "\n\n" +
            _("STEP 1: KEEP THE DEVICE STILL") + "\n\n" +
            _("PRESS OK TO START"),
            _("OK"), [this] { startPhase(Phase::SAMPLE_IDLE); }));
        return;
    }

    if (mTimer <= 0)
        return;

    int raw = readAdcRaw();
    if (raw >= 0)
        mSamples.push_back(raw);

    mTimer -= deltaTime;

    if (mTimer <= 0) {
        LOG(LogDebug) << "GuiVolumeKeyCalibration: phase " << (int)mPhase
                      << " samples=" << mSamples.size();

        switch (mPhase) {
            case Phase::SAMPLE_IDLE:
                mIdleValue = medianOf(mSamples);
                mWindow->pushGui(new GuiMsgBox(mWindow,
                    std::string(_("STEP 2: HOLD VOLUME DOWN")) + "\n\n" + _("PRESS OK TO START"),
                    _("OK"), [this] { startPhase(Phase::SAMPLE_VOLDOWN); }));
                break;

            case Phase::SAMPLE_VOLDOWN:
                mVolDownValue = pressedMedian(mSamples, mIdleValue);
                mWindow->pushGui(new GuiMsgBox(mWindow,
                    std::string(_("STEP 3: HOLD VOLUME UP")) + "\n\n" + _("PRESS OK TO START"),
                    _("OK"), [this] { startPhase(Phase::SAMPLE_VOLUP); }));
                break;

            case Phase::SAMPLE_VOLUP:
                mVolUpValue = pressedMedian(mSamples, mIdleValue);
                finishCalibration();
                break;
        }
    }
}

std::string GuiVolumeKeyCalibration::getStepText() const
{
    switch (mPhase) {
        case Phase::SAMPLE_IDLE:   return _("STEP 1: KEEP THE DEVICE STILL");
        case Phase::SAMPLE_VOLDOWN: return _("STEP 2: HOLD VOLUME DOWN");
        case Phase::SAMPLE_VOLUP:   return _("STEP 3: HOLD VOLUME UP");
    }
    return "";
}

void GuiVolumeKeyCalibration::render(const Transform4x4f& parentTrans)
{
    GuiComponent::render(parentTrans);

    if (mTimer > 0) {
        Renderer::setMatrix(Transform4x4f::Identity());
        Renderer::drawRect(0.0f, 0.0f, Renderer::getScreenWidth(), Renderer::getScreenHeight(), 0x000000CC);

        auto theme = ThemeData::getMenuTheme();
        auto font = theme->TextSmall.font;
        int seconds = (mTimer + 999) / 1000;

        std::string text = getStepText();
        text += "\n" + std::to_string(seconds) + "s\n\n";
        text += _("PRESS BACK TO CANCEL");

        auto size = font->sizeText(text);
        float x = (Renderer::getScreenWidth() - size.x()) / 2;
        float y = (Renderer::getScreenHeight() - size.y()) / 2;
        auto cache = font->buildTextCache(text, Vector2f(x, y), 0xFFFFFFFF, size.x());
        font->renderTextCache(cache);
        delete cache;
    }
}

std::vector<HelpPrompt> GuiVolumeKeyCalibration::getHelpPrompts()
{
    return std::vector<HelpPrompt>();
}
