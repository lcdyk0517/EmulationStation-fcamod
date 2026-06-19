#include <string>
#include <functional>
#pragma once
#ifndef ES_APP_COMPONENTS_CONTROLLERACTIVITY_COMPONENT_H
#define ES_APP_COMPONENTS_CONTROLLERACTIVITY_COMPONENT_H

#include "renderers/Renderer.h"
#include "GuiComponent.h"
#include "resources/Font.h"
#include "platform.h"

class TextureResource;

// Battery state change callback type: level (0-100), charging (true/false)
using BatteryStateCallback = std::function<void(int level, bool charging)>;

class ControllerActivityComponent : public GuiComponent
{
public:
	enum ActivityView : unsigned int
	{
		CONTROLLERS = 1,
		BATTERY = 2,
		NETWORK = 4,
			BLUETOOTH = 8
	};


	ControllerActivityComponent(Window* window);

	void onSizeChanged() override;
	void onPositionChanged() override;

	// Multiply all pixels in the image by this color when rendering.
	void setColorShift(unsigned int color);

	void update(int deltaTime) override;
	void render(const Transform4x4f& parentTrans) override;
	bool input(InputConfig* config, Input input) override;

	virtual void applyTheme(const std::shared_ptr<ThemeData>& theme, const std::string& view, const std::string& element, unsigned int properties) override;

	void setSpacing(float spacing) { mSpacing = spacing; }
	void setHorizontalAlignment(Alignment align) { mHorizontalAlignment = align; }
	
	void setActivityColor(unsigned int color) { mActivityColor = color; }
	void setHotkeyColor(unsigned int color) { mHotkeyColor = color; }
	
	bool hasBattery() { return mBatteryInfo.hasBattery; }

	void setBatteryStateCallback(BatteryStateCallback callback) { mBatteryStateCallback = callback; }
	
	// Force refresh network state (call after WiFi toggle)
	void refreshNetworkState() { updateNetworkInfo(); }
	
	// Start fast network checking (after WiFi enabled, check every second until connected)
	void startFastNetworkCheck();

protected:
	virtual void	init();

	unsigned int	mView;

protected:
	Vector2f	getTextureSize(std::shared_ptr<TextureResource> texture);
	int			renderTexture(float x, float w, std::shared_ptr<TextureResource> texture, unsigned int color);

	float mSpacing;
	Alignment mHorizontalAlignment;

	unsigned int mColorShift;
	unsigned int mActivityColor;
	unsigned int mHotkeyColor;

protected:
	// Pads
	std::shared_ptr<TextureResource> mPadTexture;	

	class PlayerPad
	{
	public:
		PlayerPad()
		{
			reset();
		}
	
		int  index;
		int  batteryLevel;
		int  keyState;
		int  timeOut;

		std::shared_ptr<TextCache>	batteryText;

		std::string batteryTextValue;
		int batteryTextSize;

		void reset()
		{
			index = -1;
			batteryLevel = -1;
			keyState = 0;
			timeOut = 0;
		}
	};

	PlayerPad mPads[MAX_PLAYERS];

protected:
	// Network info
	void updateNetworkInfo();
	std::shared_ptr<TextureResource> mNetworkImage;
	std::shared_ptr<TextureResource> mNetworkActiveImage;
	std::shared_ptr<TextureResource> mNetworkOffImage;
	std::shared_ptr<TextureResource> mNetworkShareImage;
	std::shared_ptr<TextureResource> mNetworkServiceImage;
	bool mNetworkConnected;
	int mNetworkState; // 0=off 1=active 2=connected 3=sharing 4=service
	int mNetworkCheckTime;
	void updateBluetoothInfo();
	std::shared_ptr<TextureResource> mBluetoothImage;
	std::shared_ptr<TextureResource> mBluetoothActiveImage;
	std::shared_ptr<TextureResource> mBluetoothOffImage;
	int mBluetoothState;
	int mBluetoothCheckTime;

protected:
	// Battery info
	int mBatteryCheckTime;
	int mBatteryTextX;

	BatteryInformation mBatteryInfo;

	std::shared_ptr<TextureResource> mBatteryImage;
	std::shared_ptr<Font>            mBatteryFont;
	std::shared_ptr<TextCache>       mBatteryText;

	std::string mCurrentBatteryTexture;
	
	void updateBatteryInfo();

	std::string mIncharge;
	std::string mFull;
	std::string mAt75;
	std::string mAt50;
	std::string mAt25;
	std::string mEmpty;

	BatteryStateCallback mBatteryStateCallback;
};

#endif // ES_APP_COMPONENTS_RATING_COMPONENT_H
