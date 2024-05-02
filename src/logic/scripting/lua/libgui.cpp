#include "lua_commons.h"
#include "api_lua.h"

#include <iostream>
#include "../scripting.h"
#include "lua_util.h"
#include "LuaState.h"

#include "../../../engine.h"
#include "../../../assets/Assets.h"
#include "../../../items/Inventories.h"
#include "../../../graphics/ui/gui_util.hpp"
#include "../../../graphics/ui/elements/UINode.hpp"
#include "../../../graphics/ui/elements/Button.hpp"
#include "../../../graphics/ui/elements/CheckBox.hpp"
#include "../../../graphics/ui/elements/TextBox.hpp"
#include "../../../graphics/ui/elements/TrackBar.hpp"
#include "../../../graphics/ui/elements/Panel.hpp"
#include "../../../graphics/ui/elements/Menu.hpp"
#include "../../../graphics/ui/elements/InventoryView.hpp"
#include "../../../frontend/UiDocument.h"
#include "../../../frontend/locale/langs.h"
#include "../../../util/stringutil.h"
#include "../../../world/Level.h"

using namespace gui;

struct DocumentNode {
    UiDocument* document;
    std::shared_ptr<UINode> node;
};

static DocumentNode getDocumentNode(lua_State* L, const std::string& name, const std::string& nodeName) {
    auto doc = scripting::engine->getAssets()->getLayout(name);
    if (doc == nullptr) {
        luaL_error(L, "document '%s' not found", name.c_str());
    }
    auto node = doc->get(nodeName);
    if (node == nullptr) {
        luaL_error(L, "document '%s' has no element with id '%s'", name.c_str(), nodeName.c_str());
    }
    return {doc, node};
}

static bool getattr(lua_State* L, TrackBar* bar, const std::string& attr) {
    if (bar == nullptr)
        return false;
    if (attr == "value") {
        lua_pushnumber(L, bar->getValue()); return true;
    } else if (attr == "min") {
        lua_pushnumber(L, bar->getMin()); return true;
    } else if (attr == "max") {
        lua_pushnumber(L, bar->getMax()); 
        return true;
    } else if (attr == "step") {
        lua_pushnumber(L, bar->getStep()); 
        return true;
    } else if (attr == "trackWidth") {
        lua_pushnumber(L, bar->getTrackWidth()); 
        return true;
    } else if (attr == "trackColor") {
        return lua::pushcolor_arr(L, bar->getTrackColor());
    }
    return false;
}

static bool setattr(lua_State* L, TrackBar* bar, const std::string& attr) {
    if (bar == nullptr)
        return false;
    if (attr == "value") {
        bar->setValue(lua_tonumber(L, 4));
        return true;
    } else if (attr == "min") {
        bar->setMin(lua_tonumber(L, 4));
        return true;
    } else if (attr == "max") {
        bar->setMax(lua_tonumber(L, 4));
        return true;
    } else if (attr == "step") {
        bar->setStep(lua_tonumber(L, 4));
        return true;
    } else if (attr == "trackWidth") {
        bar->setTrackWidth(lua_tonumber(L, 4));
        return true;
    } else if (attr == "trackColor") {
        bar->setTrackColor(lua::tocolor(L, 4));
        return true;
    }
    return false;
}

static bool getattr(lua_State* L, Button* button, const std::string& attr) {
    if (button == nullptr)
        return false;
    if (attr == "text") {
        lua_pushstring(L, util::wstr2str_utf8(button->getText()).c_str());
        return true;
    } else if (attr == "pressedColor") {
        return lua::pushcolor_arr(L, button->getPressedColor());
    }
    return false;
}

static bool getattr(lua_State* L, Label* label, const std::string& attr) {
    if (label == nullptr)
        return false;
    if (attr == "text") {
        lua_pushstring(L, util::wstr2str_utf8(label->getText()).c_str());
        return true;
    }
    return false;
}

static bool getattr(lua_State* L, FullCheckBox* box, const std::string& attr) {
    if (box == nullptr)
        return false;
    if (attr == "checked") {
        lua_pushboolean(L, box->isChecked());
        return true;
    }
    return false;
}

static bool getattr(lua_State* L, TextBox* box, const std::string& attr) {
    if (box == nullptr)
        return false;
    if (attr == "text") {
        lua_pushstring(L, util::wstr2str_utf8(box->getText()).c_str());
        return true;
    } else if (attr == "placeholder") {
        lua_pushstring(L, util::wstr2str_utf8(box->getPlaceholder()).c_str());
        return true;
    } else if (attr == "valid") {
        lua_pushboolean(L, box->validate());
        return true;
    }
    return false;
}

static DocumentNode getDocumentNode(lua_State* L, int idx=1) {
    lua_getfield(L, idx, "docname");
    lua_getfield(L, idx, "name");
    auto docname = lua_tostring(L, -2);
    auto name = lua_tostring(L, -1);
    auto node = getDocumentNode(L, docname, name);
    lua_pop(L, 2);
    return node;
}

static int menu_back(lua_State* L) {
    auto node = getDocumentNode(L);
    auto menu = dynamic_cast<Menu*>(node.node.get());
    menu->back();
    return 0;
}

static int menu_reset(lua_State* L) {
    auto node = getDocumentNode(L);
    auto menu = dynamic_cast<Menu*>(node.node.get());
    menu->reset();
    return 0;
}

static bool getattr(lua_State* L, Menu* menu, const std::string& attr) {
    if (menu == nullptr)
        return false;
    if (attr == "page") {
        lua_pushstring(L, menu->getCurrent().name.c_str());
        return true;
    } else if (attr == "back") {
        lua_pushcfunction(L, menu_back);
        return true;
    } else if (attr == "reset") {
        lua_pushcfunction(L, menu_reset);
        return true;
    }
    return false;
}

static bool setattr(lua_State* L, FullCheckBox* box, const std::string& attr) {
    if (box == nullptr)
        return false;
    if (attr == "checked") {
        box->setChecked(lua_toboolean(L, 4));
        return true;
    }
    return false;
}

static bool setattr(lua_State* L, Button* button, const std::string& attr) {
    if (button == nullptr)
        return false;
    if (attr == "text") {
        button->setText(util::str2wstr_utf8(lua_tostring(L, 4)));
        return true;
    } else if (attr == "pressedColor") {
        button->setPressedColor(lua::tocolor(L, 4));
    }
    return false;
}

static bool setattr(lua_State* L, TextBox* box, const std::string& attr) {
    if (box == nullptr)
        return false;
    if (attr == "text") {
        box->setText(util::str2wstr_utf8(lua_tostring(L, 4)));
        return true;
    } else if (attr == "placeholder") {
        box->setPlaceholder(util::str2wstr_utf8(lua_tostring(L, 4)));
        return true;
    }
    return false;
}

static bool setattr(lua_State* L, Label* label, const std::string& attr) {
    if (label == nullptr)
        return false;
    if (attr == "text") {
        label->setText(util::str2wstr_utf8(lua_tostring(L, 4)));
        return true;
    }
    return false;
}

static bool setattr(lua_State* L, Menu* menu, const std::string& attr) {
    if (menu == nullptr)
        return false;
    if (attr == "page") {
        menu->setPage(lua_tostring(L, 4));
        return true;
    }
    return false;
}

static bool setattr(lua_State* L, InventoryView* view, const std::string& attr) {
    if (view == nullptr)
        return false;
    if (attr == "inventory") {
        auto inventory = scripting::level->inventories->get(lua_tointeger(L, 4));
        if (inventory == nullptr) {
            view->unbind();
        } else {
            view->bind(inventory, scripting::content);
        }
        return true;
    }
    return false;
}

static int container_add(lua_State* L) {
    auto docnode = getDocumentNode(L);
    auto node = dynamic_cast<Container*>(docnode.node.get());
    auto xmlsrc = lua_tostring(L, 2);
    try {
        auto subnode = guiutil::create(xmlsrc, docnode.document->getEnvironment());
        node->add(subnode);
        UINode::getIndices(subnode, docnode.document->getMapWriteable());
    } catch (const std::exception& err) {
        luaL_error(L, err.what());
    }
    return 0;
}

static int container_clear(lua_State* L) {
    auto node = getDocumentNode(L, 1);
    if (auto container = std::dynamic_pointer_cast<Container>(node.node)) {
        container->clear();
    }
    return 0;
}

static bool getattr(lua_State* L, Container* container, const std::string& attr) {
    if (container == nullptr)
        return false;
    
    if (attr == "add") {
        lua_pushcfunction(L, container_add);
        return true;
    } else if (attr == "clear") {
        lua_pushcfunction(L, container_clear);
        return true;
    }
    return false;
}

static bool getattr(lua_State* L, InventoryView* inventory, const std::string& attr) {
    if (inventory == nullptr)
        return false;
    if (attr == "inventory") {
        auto inv = inventory->getInventory();
        lua_pushinteger(L, inv ? inv->getId() : 0);
        return true;
    }
    return false;
}

static int uinode_move_into(lua_State* L) {
    auto node = getDocumentNode(L, 1);
    auto dest = getDocumentNode(L, 2);
    UINode::moveInto(node.node, std::dynamic_pointer_cast<Container>(dest.node));
    return 0;
}

static int l_gui_getattr(lua_State* L) {
    auto docname = lua_tostring(L, 1);
    auto element = lua_tostring(L, 2);
    const std::string attr = lua_tostring(L, 3);
    auto docnode = getDocumentNode(L, docname, element);
    auto node = docnode.node;

    if (attr == "color") {
        return lua::pushcolor_arr(L, node->getColor());
    } else if (attr == "pos") {
        return lua::pushvec2_arr(L, node->getPos());
    } else if (attr == "size") {
        return lua::pushvec2_arr(L, node->getSize());
    } else if (attr == "hoverColor") {
        return lua::pushcolor_arr(L, node->getHoverColor());
    } else if (attr == "interactive") {
        lua_pushboolean(L, node->isInteractive());
        return 1;
    } else if (attr == "visible") {
        lua_pushboolean(L, node->isVisible());
        return 1;
    } else if (attr == "enabled") {
        lua_pushboolean(L, node->isEnabled());
        return 1;
    } else if (attr == "move_into") {
        lua_pushcfunction(L, uinode_move_into);
        return 1;
    }

    if (getattr(L, dynamic_cast<Container*>(node.get()), attr))
        return 1;
    if (getattr(L, dynamic_cast<Button*>(node.get()), attr))
        return 1;
    if (getattr(L, dynamic_cast<Label*>(node.get()), attr))
        return 1;
    if (getattr(L, dynamic_cast<TextBox*>(node.get()), attr))
        return 1;
    if (getattr(L, dynamic_cast<TrackBar*>(node.get()), attr))
        return 1;
    if (getattr(L, dynamic_cast<FullCheckBox*>(node.get()), attr))
        return 1;
    if (getattr(L, dynamic_cast<Menu*>(node.get()), attr))
        return 1;
    if (getattr(L, dynamic_cast<InventoryView*>(node.get()), attr))
        return 1;

    return 0;
}

static int l_gui_getviewport(lua_State* L) {
    lua::pushvec2_arr(L, scripting::engine->getGUI()->getContainer()->getSize());
    return 1;
}

static int l_gui_setattr(lua_State* L) {
    auto docname = lua_tostring(L, 1);
    auto element = lua_tostring(L, 2);
    const std::string attr = lua_tostring(L, 3);

    auto docnode = getDocumentNode(L, docname, element);
    auto node = docnode.node;
    if (attr == "pos") {
        node->setPos(lua::tovec2(L, 4));
    } else if (attr == "size") {
        node->setSize(lua::tovec2(L, 4));
    } else if (attr == "color") {
        node->setColor(lua::tocolor(L, 4));
    } else if (attr == "hoverColor") {
        node->setHoverColor(lua::tocolor(L, 4));
    } else if (attr == "interactive") {
        node->setInteractive(lua_toboolean(L, 4));
    } else if (attr == "visible") {
        node->setVisible(lua_toboolean(L, 4));
    } else if (attr == "enabled") {
        node->setEnabled(lua_toboolean(L, 4));
    } else {
        if (setattr(L, dynamic_cast<Button*>(node.get()), attr))
            return 0;
        if (setattr(L, dynamic_cast<Label*>(node.get()), attr))
            return 0;
        if (setattr(L, dynamic_cast<TextBox*>(node.get()), attr))
            return 0;
        if (setattr(L, dynamic_cast<TrackBar*>(node.get()), attr))
            return 0;
        if (setattr(L, dynamic_cast<FullCheckBox*>(node.get()), attr))
            return 0;
        if (setattr(L, dynamic_cast<Menu*>(node.get()), attr))
            return 0;
        if (setattr(L, dynamic_cast<InventoryView*>(node.get()), attr))
            return 0;
    }
    return 0;
}

static int l_gui_get_env(lua_State* L) {
    auto name = lua_tostring(L, 1);
    auto doc = scripting::engine->getAssets()->getLayout(name);
    if (doc == nullptr) {
        luaL_error(L, "document '%s' not found", name);
    }
    lua_getglobal(L, lua::LuaState::envName(*doc->getEnvironment()).c_str());
    return 1;
}

static int l_gui_str(lua_State* L) {
    auto text = util::str2wstr_utf8(lua_tostring(L, 1));
    if (!lua_isnoneornil(L, 2)) {
        auto context = util::str2wstr_utf8(lua_tostring(L, 2));
        lua_pushstring(L, util::wstr2str_utf8(langs::get(text, context)).c_str());
    } else {
        lua_pushstring(L, util::wstr2str_utf8(langs::get(text)).c_str());
    }
    return 1;
}

static int l_gui_reindex(lua_State* L) {
    auto name = lua_tostring(L, 1);
    auto doc = scripting::engine->getAssets()->getLayout(name);
    if (doc == nullptr) {
        luaL_error(L, "document '%s' not found", name);
    }
    doc->rebuildIndices();
    return 0;
}

/// @brief gui.get_locales_info() -> table of tables 
static int l_gui_get_locales_info(lua_State* L) {
    auto& locales = langs::locales_info;
    lua_createtable(L, 0, locales.size());
    for (auto& entry : locales) {
        lua_createtable(L, 0, 1);
        lua_pushstring(L, entry.second.name.c_str());
        lua_setfield(L, -2, "name");
        lua_setfield(L, -2, entry.first.c_str());
    }
    return 1;
}

const luaL_Reg guilib [] = {
    {"get_viewport", lua_wrap_errors<l_gui_getviewport>},
    {"getattr", lua_wrap_errors<l_gui_getattr>},
    {"setattr", lua_wrap_errors<l_gui_setattr>},
    {"get_env", lua_wrap_errors<l_gui_get_env>},
    {"str", lua_wrap_errors<l_gui_str>},
    {"reindex", lua_wrap_errors<l_gui_reindex>},
    {"get_locales_info", lua_wrap_errors<l_gui_get_locales_info>},
    {NULL, NULL}
};
