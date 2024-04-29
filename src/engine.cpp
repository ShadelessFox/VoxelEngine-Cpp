#include "engine.h"

#define GLEW_STATIC

#include "debug/Logger.hpp"
#include "assets/AssetsLoader.h"
#include "audio/audio.h"
#include "coders/GLSLExtension.h"
#include "coders/imageio.hpp"
#include "coders/json.h"
#include "content/ContentLoader.h"
#include "core_defs.h"
#include "files/files.h"
#include "files/settings_io.hpp"
#include "frontend/locale/langs.h"
#include "frontend/menu.hpp"
#include "frontend/screens/Screen.hpp"
#include "frontend/screens/MenuScreen.hpp"
#include "graphics/core/Batch2D.hpp"
#include "graphics/core/DrawContext.hpp"
#include "graphics/core/ImageData.hpp"
#include "graphics/core/Shader.hpp"
#include "graphics/ui/GUI.hpp"
#include "logic/EngineController.hpp"
#include "logic/scripting/scripting.h"
#include "util/listutil.h"
#include "util/platform.h"
#include "voxels/DefaultWorldGenerator.h"
#include "voxels/FlatWorldGenerator.h"
#include "window/Camera.h"
#include "window/Events.h"
#include "window/input.h"
#include "window/Window.h"
#include "world/WorldGenerators.h"

#include <iostream>
#include <assert.h>
#include <glm/glm.hpp>
#include <unordered_set>
#include <functional>

static debug::Logger logger("engine");

namespace fs = std::filesystem;

void addWorldGenerators() {
    WorldGenerators::addGenerator<DefaultWorldGenerator>("core:default");
    WorldGenerators::addGenerator<FlatWorldGenerator>("core:flat");
}

inline void create_channel(Engine* engine, std::string name, NumberSetting& setting) {
    if (name != "master") {
        audio::create_channel(name);
    }
    engine->keepAlive(setting.observe([=](auto value) {
        audio::get_channel(name)->setVolume(value*value);
    }));
}

Engine::Engine(EngineSettings& settings, SettingsHandler& settingsHandler, EnginePaths* paths) 
    : settings(settings), settingsHandler(settingsHandler), paths(paths) 
{
    controller = std::make_unique<EngineController>(this);
    if (Window::initialize(&this->settings.display)){
        throw initialize_error("could not initialize window");
    }
    audio::initialize(settings.audio.enabled.get());
    create_channel(this, "master", settings.audio.volumeMaster);
    create_channel(this, "regular", settings.audio.volumeRegular);
    create_channel(this, "music", settings.audio.volumeMusic);
    create_channel(this, "ambient", settings.audio.volumeAmbient);
    create_channel(this, "ui", settings.audio.volumeUI);

    gui = std::make_unique<gui::GUI>();
    if (settings.ui.language.get() == "auto") {
        settings.ui.language.set(langs::locale_by_envlocale(
            platform::detect_locale(),
            paths->getResources()
        ));
    }
    if (ENGINE_VERSION_INDEV) {
        menus::create_version_label(this);
    }
    keepAlive(settings.ui.language.observe([=](auto lang) {
        setLanguage(lang);
    }, true));
    addWorldGenerators();
    
    scripting::initialize(this);
}

void Engine::onAssetsLoaded() {
    gui->onAssetsLoad(assets.get());
}

void Engine::updateTimers() {
    frame++;
    double currentTime = Window::time();
    delta = currentTime - lastTime;
    lastTime = currentTime;
}

void Engine::updateHotkeys() {
    if (Events::jpressed(keycode::F2)) {
        saveScreenshot();
    }
    if (Events::jpressed(keycode::F11)) {
        settings.display.fullscreen.toggle();
    }
}

void Engine::saveScreenshot() {
    auto image = Window::takeScreenshot();
    image->flipY();
    fs::path filename = paths->getScreenshotFile("png");
    imageio::write(filename.string(), image.get());
    logger.info() << "saved screenshot as " << filename.u8string();
}

void Engine::mainloop() {
    logger.info() << "starting menu screen";
    setScreen(std::make_shared<MenuScreen>(this));

    Batch2D batch(1024);
    lastTime = Window::time();
    
    logger.info() << "engine started";
    while (!Window::isShouldClose()){
        assert(screen != nullptr);
        updateTimers();
        updateHotkeys();
        audio::update(delta);

        gui->act(delta, Viewport(Window::width, Window::height));
        screen->update(delta);

        if (!Window::isIconified()) {
            renderFrame(batch);
        }
        Window::swapInterval(Window::isIconified() ? 1 : settings.display.vsync.get());

        processPostRunnables();

        Window::swapBuffers();
        Events::pollEvents();
    }
}

void Engine::renderFrame(Batch2D& batch) {
    screen->draw(delta);

    Viewport viewport(Window::width, Window::height);
    DrawContext ctx(nullptr, viewport, &batch);
    gui->draw(&ctx, assets.get());
}

void Engine::processPostRunnables() {
    std::lock_guard<std::recursive_mutex> lock(postRunnablesMutex);
    while (!postRunnables.empty()) {
        postRunnables.front()();
        postRunnables.pop();
    }
    scripting::process_post_runnables();
}

Engine::~Engine() {
    logger.info() << "shutting down";
    if (screen) {
        screen->onEngineShutdown();
        screen.reset();
    }
    content.reset();
    assets.reset();
    gui.reset();
    audio::close();
    scripting::close();
    Window::terminate();
    logger.info() << "engine finished";
}

EngineController* Engine::getController() {
    return controller.get();
}

PacksManager Engine::createPacksManager(const fs::path& worldFolder) {
    PacksManager manager;
    manager.setSources({
        worldFolder/fs::path("content"),
        paths->getUserfiles()/fs::path("content"),
        paths->getResources()/fs::path("content")
    });
    return manager;
}

void Engine::loadAssets() {
    logger.info() << "loading assets";
    Shader::preprocessor->setPaths(resPaths.get());

    auto new_assets = std::make_unique<Assets>();
    AssetsLoader loader(new_assets.get(), resPaths.get());
    AssetsLoader::addDefaults(loader, content.get());

    bool threading = false;
    if (threading) {
        auto task = loader.startTask([=](){});
        task->waitForEnd();
    } else {
        while (loader.hasNext()) {
            if (!loader.loadNext()) {
                new_assets.reset();
                throw std::runtime_error("could not to load assets");
            }
        }
    }
    assets.reset(new_assets.release());
}

void Engine::loadContent() {
    auto resdir = paths->getResources();
    ContentBuilder contentBuilder;
    corecontent::setup(&contentBuilder);
    paths->setContentPacks(&contentPacks);

    std::vector<std::string> names;
    for (auto& pack : contentPacks) {
        names.push_back(pack.id);
    }
    PacksManager manager = createPacksManager(paths->getWorldFolder());
    manager.scan();
    names = manager.assembly(names);
    contentPacks = manager.getAll(names);

    std::vector<std::pair<std::string, fs::path>> resRoots;
    for (auto& pack : contentPacks) {
        resRoots.push_back({pack.id, pack.folder});

        ContentLoader loader(&pack);
        loader.load(contentBuilder);
    }
    content.reset(contentBuilder.build());
    resPaths = std::make_unique<ResPaths>(resdir, resRoots);

    langs::setup(resdir, langs::current->getId(), contentPacks);
    loadAssets();
    onAssetsLoaded();
}

void Engine::loadWorldContent(const fs::path& folder) {
    contentPacks.clear();
    auto packNames = ContentPack::worldPacksList(folder);
    PacksManager manager;
    manager.setSources({
        folder/fs::path("content"),
        paths->getUserfiles()/fs::path("content"),
        paths->getResources()/fs::path("content")
    });
    manager.scan();
    contentPacks = manager.getAll(manager.assembly(packNames));
    paths->setWorldFolder(folder);
    loadContent();
}

void Engine::loadAllPacks() {
    PacksManager manager = createPacksManager(paths->getWorldFolder());
    manager.scan();
    auto allnames = manager.getAllNames();
    contentPacks = manager.getAll(manager.assembly(allnames));
}

double Engine::getDelta() const {
    return delta;
}

void Engine::setScreen(std::shared_ptr<Screen> screen) {
    audio::reset_channel(audio::get_channel_index("regular"));
    audio::reset_channel(audio::get_channel_index("ambient"));
    this->screen = screen;
}

void Engine::setLanguage(std::string locale) {
    langs::setup(paths->getResources(), locale, contentPacks);
    menus::create_menus(this);
}

gui::GUI* Engine::getGUI() {
    return gui.get();
}

EngineSettings& Engine::getSettings() {
    return settings;
}

Assets* Engine::getAssets() {
    return assets.get();
}

const Content* Engine::getContent() const {
    return content.get();
}

std::vector<ContentPack>& Engine::getContentPacks() {
    return contentPacks;
}

EnginePaths* Engine::getPaths() {
    return paths;
}

ResPaths* Engine::getResPaths() {
    return resPaths.get();
}

std::shared_ptr<Screen> Engine::getScreen() {
    return screen;
}

void Engine::postRunnable(runnable callback) {
    std::lock_guard<std::recursive_mutex> lock(postRunnablesMutex);
    postRunnables.push(callback);
}

SettingsHandler& Engine::getSettingsHandler() {
    return settingsHandler;
}
