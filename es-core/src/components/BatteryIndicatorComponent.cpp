#include <string>
#include "components/BatteryIndicatorComponent.h"

#include "resources/TextureResource.h"
#include "ThemeData.h"
#include "InputManager.h"
#include "Settings.h"
#include <SDL_power.h>
#include "utils/StringUtil.h"

BatteryIndicatorComponent::BatteryIndicatorComponent(Window* window) : ControllerActivityComponent(window) { }

void BatteryIndicatorComponent::init()
{
	ControllerActivityComponent::init();

	mHorizontalAlignment = ALIGN_LEFT;

	if (Renderer::isSmallScreen())
	{
		setPosition(Renderer::getScreenWidth() * 0.010, Renderer::getScreenHeight() * 0);
		setSize(Renderer::getScreenWidth() * 0.320, Renderer::getScreenHeight() * 0.065);
	}
	else
	{
		setPosition(Renderer::getScreenWidth() * 0.955, Renderer::getScreenHeight() *0.0125);
		setSize(Renderer::getScreenWidth() * 0.033, Renderer::getScreenHeight() *0.033);
	}

	mView = ActivityView::BATTERY;

	if (ResourceManager::getInstance()->fileExists(":/battery/incharge.svg"))
		mIncharge = ResourceManager::getInstance()->getResourcePath(":/battery/incharge.svg");

	if (ResourceManager::getInstance()->fileExists(":/battery/full.svg"))
		mFull = ResourceManager::getInstance()->getResourcePath(":/battery/full.svg");

	if (ResourceManager::getInstance()->fileExists(":/battery/75.svg"))
		mAt75 = ResourceManager::getInstance()->getResourcePath(":/battery/75.svg");

	if (ResourceManager::getInstance()->fileExists(":/battery/50.svg"))
		mAt50 = ResourceManager::getInstance()->getResourcePath(":/battery/50.svg");

	if (ResourceManager::getInstance()->fileExists(":/battery/25.svg"))
		mAt25 = ResourceManager::getInstance()->getResourcePath(":/battery/25.svg");

	if (ResourceManager::getInstance()->fileExists(":/battery/empty.svg"))
		mEmpty = ResourceManager::getInstance()->getResourcePath(":/battery/empty.svg");	

	if (ResourceManager::getInstance()->fileExists(":/battery/empty.svg"))
		mEmpty = ResourceManager::getInstance()->getResourcePath(":/battery/empty.svg");

	if (Settings::getInstance()->getBool("networkIcon") && ResourceManager::getInstance()->fileExists(":/network.svg"))
	{
		mView |= ActivityView::NETWORK;
		mNetworkImage = TextureResource::get(ResourceManager::getInstance()->getResourcePath(":/network.svg"), false, true);
	}
	if (ResourceManager::getInstance()->fileExists(":/network_active.svg"))
		mNetworkActiveImage = TextureResource::get(ResourceManager::getInstance()->getResourcePath(":/network_active.svg"), false, true);
	if (ResourceManager::getInstance()->fileExists(":/network_off.svg"))
		mNetworkOffImage = TextureResource::get(ResourceManager::getInstance()->getResourcePath(":/network_off.svg"), false, true);
	if (Settings::getInstance()->getBool("bluetoothIcon") && ResourceManager::getInstance()->fileExists(":/bluetooth.svg"))
	{
		mView |= ActivityView::BLUETOOTH;
		mBluetoothImage = TextureResource::get(ResourceManager::getInstance()->getResourcePath(":/bluetooth.svg"), false, true);
	}
	if (ResourceManager::getInstance()->fileExists(":/bluetooth_active.svg"))
		mBluetoothActiveImage = TextureResource::get(ResourceManager::getInstance()->getResourcePath(":/bluetooth_active.svg"), false, true);
	if (ResourceManager::getInstance()->fileExists(":/bluetooth_off.svg"))
		mBluetoothOffImage = TextureResource::get(ResourceManager::getInstance()->getResourcePath(":/bluetooth_off.svg"), false, true);
	if (ResourceManager::getInstance()->fileExists(":/network_share.svg"))
		mNetworkShareImage = TextureResource::get(ResourceManager::getInstance()->getResourcePath(":/network_share.svg"), false, true);
	if (ResourceManager::getInstance()->fileExists(":/network_service.svg"))
		mNetworkServiceImage = TextureResource::get(ResourceManager::getInstance()->getResourcePath(":/network_service.svg"), false, true);

	updateNetworkInfo();
	updateBatteryInfo();
}
