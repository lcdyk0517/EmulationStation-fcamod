#pragma once
#ifndef ES_APP_GUIS_GUITOOLS_H
#define ES_APP_GUIS_GUITOOLS_H

#include "GuiComponent.h"
#include "components/MenuComponent.h"
#include "utils/NameResolver.h"

#include <memory>

class GuiTools : public GuiComponent
{
public:
    GuiTools(Window* window);
    ~GuiTools() override;

    bool input(InputConfig* config, Input input) override;
    void render(const Transform4x4f& parentTrans) override;

private:
    bool addScriptsToMenu(MenuComponent& menu, const std::string& folderPath);
    void launchTool(const std::string& script);

    MenuComponent mMenu;
    std::unique_ptr<NameResolver> mResolver;
};

#endif // ES_APP_GUIS_GUITOOLS_H
