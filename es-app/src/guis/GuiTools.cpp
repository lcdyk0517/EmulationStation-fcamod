#include <string>
#include <vector>
#include <algorithm>
#include "AudioManager.h"
#include "VolumeControl.h"
#include "guis/GuiTools.h"
#include "components/MenuComponent.h"
#include "Window.h"
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"
#include "platform.h"

// ------------------- SubMenuWrapper -------------------
class SubMenuWrapper : public GuiComponent
{
public:
    SubMenuWrapper(Window* window, MenuComponent* menu)
        : GuiComponent(window), mMenu(menu)
    {
        addChild(menu);
    }

    void close()
    {
        mWindow->removeGui(this);
    }

    bool input(InputConfig* config, Input input) override
    {
        if (input.value != 0 && config->isMappedTo(BUTTON_BACK, input))
        {
            close();
            return true;
        }
        return GuiComponent::input(config, input);
    }

private:
    MenuComponent* mMenu;
};

// ------------------- GuiTools -------------------
GuiTools::GuiTools(Window* window)
    : GuiComponent(window)
    , mMenu(window, _("OPTIONS"))
    , mResolver(new NameResolver("/opt/system"))
{
    addChild(&mMenu);

    addScriptsToMenu(mMenu, "/opt/system");

    mMenu.addButton(_("BACK"), "back", [this] { delete this; });

    Vector2f screenSize((float)Renderer::getScreenWidth(), (float)Renderer::getScreenHeight());
    Vector2f menuSize = mMenu.getSize();

    if (screenSize.x() >= 1024 && screenSize.y() >= 600)
    {
        Vector3f pos;
        pos[0] = (screenSize.x() - menuSize.x()) / 2.0f;
        pos[1] = (screenSize.y() - menuSize.y()) / 2.0f;
        pos[2] = 0;
        mMenu.setPosition(pos);
    }
}

GuiTools::~GuiTools()
{
}

bool GuiTools::addScriptsToMenu(MenuComponent& menu, const std::string& folderPath)
{
    auto items = Utils::FileSystem::getDirContent(folderPath);
    std::vector<std::pair<std::string, SubMenuWrapper*> > folders;
    std::vector<std::pair<std::string, std::string> > scripts;

    for (auto& item : items)
    {
        std::string fileName = Utils::FileSystem::getFileName(item);

        if (Utils::FileSystem::isDirectory(item))
        {
            MenuComponent* subMenu = new MenuComponent(mWindow, fileName);
            bool hasItems = addScriptsToMenu(*subMenu, item);

            if (hasItems)
            {
                SubMenuWrapper* wrapper = new SubMenuWrapper(mWindow, subMenu);

                Vector2f screenSize((float)Renderer::getScreenWidth(), (float)Renderer::getScreenHeight());
                Vector2f menuSize = subMenu->getSize();

                if (screenSize.x() >= 1024 && screenSize.y() >= 600)
                {
                    Vector3f pos;
                    pos[0] = (screenSize.x() - menuSize.x()) / 2.0f;
                    pos[1] = (screenSize.y() - menuSize.y()) / 2.0f;
                    pos[2] = 0;
                    subMenu->setPosition(pos);
                }

                subMenu->addButton(_("BACK"), "back", [wrapper] {
                    wrapper->close();
                });

                std::string displayName = _U("\uF07B ") + mResolver->resolve(item);
                folders.push_back(std::make_pair(displayName, wrapper));
            }
            else
            {
                delete subMenu;
            }
        }
        else if (Utils::String::toLower(Utils::FileSystem::getExtension(fileName)) == ".sh")
        {
            std::string displayName = _U("\uF013 ") + mResolver->resolve(item);
            scripts.push_back(std::make_pair(displayName, item));
        }
    }

    // Sort case-insensitive
    std::sort(folders.begin(), folders.end(),
        [](const std::pair<std::string, SubMenuWrapper*>& a,
           const std::pair<std::string, SubMenuWrapper*>& b) {
            return Utils::String::toLower(a.first) < Utils::String::toLower(b.first);
        });

    std::sort(scripts.begin(), scripts.end(),
        [](const std::pair<std::string, std::string>& a,
           const std::pair<std::string, std::string>& b) {
            return Utils::String::toLower(a.first) < Utils::String::toLower(b.first);
        });

    // Add folder entries - capture only the pointer, not the whole vector
    for (size_t i = 0; i < folders.size(); ++i)
    {
        SubMenuWrapper* wrapperPtr = folders[i].second;
        std::string entryName = folders[i].first;
        menu.addEntry(entryName, true, [this, wrapperPtr] {
            mWindow->pushGui(wrapperPtr);
        }, "");
    }

    // Add script entries - capture only the script path, not the whole vector
    for (size_t i = 0; i < scripts.size(); ++i)
    {
        std::string scriptPath = scripts[i].second;
        std::string entryName = scripts[i].first;
        menu.addEntry(entryName, false, [this, scriptPath] {
            launchTool(scriptPath);
        }, "");
    }

    return (!folders.empty() || !scripts.empty());
}

void GuiTools::launchTool(const std::string& script)
{
    AudioManager::getInstance()->deinit();
    VolumeControl::getInstance()->deinit();
    mWindow->deinit(true);

    system("sudo chmod 666 /dev/tty1");
    std::string cmd = "/bin/bash \"" + script + "\" 2>&1 > /dev/tty1";
    int ret = system(cmd.c_str());
    (void)ret;

    system("setterm -clear all > /dev/tty1");

    mWindow->init(true);
    VolumeControl::getInstance()->init();
    AudioManager::getInstance()->init();
}

bool GuiTools::input(InputConfig* config, Input input)
{
    if (input.value != 0 && config->isMappedTo(BUTTON_BACK, input))
    {
        GuiComponent* top = mWindow->peekGui();
        if (top != this)
        {
            mWindow->removeGui(top);
            return true;
        }
        else
        {
            delete this;
            return true;
        }
    }
    return GuiComponent::input(config, input);
}

void GuiTools::render(const Transform4x4f& parentTrans)
{
    GuiComponent::render(parentTrans);
}
