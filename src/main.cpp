// AutoBot - bot de macros para Geometry Dash (Geode)
// Graba los clicks por frame de fisica y los reproduce para pasar el nivel.
#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>

using namespace geode::prelude;

enum class BotMode { Idle, Recording, Replaying };

struct Click {
    int frame;
    bool down;
    int button;
    bool p1;
};

$execute {
    log::info("AutoBot v1.0.1 cargado: hooks de PauseLayer/PlayLayer/GJBaseGameLayer registrados");
}

namespace Bot {
    inline BotMode mode = BotMode::Idle;
    inline std::vector<Click> clicks;
    inline size_t index = 0;
    inline bool injecting = false;

    inline void note(std::string const& text, NotificationIcon icon = NotificationIcon::Success) {
        Notification::create(text, icon)->show();
    }

    inline std::string levelKey() {
        auto pl = PlayLayer::get();
        if (!pl || !pl->m_level) return "unknown";
        std::string name = pl->m_level->m_levelName;
        std::string out;
        for (char c : name) {
            if (std::isalnum(static_cast<unsigned char>(c))) out += c;
            else out += '_';
        }
        if (out.empty()) out = "unnamed";
        return out;
    }

    inline std::filesystem::path macroPath() {
        return Mod::get()->getSaveDir() / ("macro_" + levelKey() + ".txt");
    }

    inline bool save() {
        std::ofstream f(macroPath());
        if (!f.is_open()) return false;
        for (auto const& c : clicks) {
            f << c.frame << ' ' << (c.down ? 1 : 0) << ' ' << c.button << ' ' << (c.p1 ? 1 : 0) << '\n';
        }
        return true;
    }

    inline bool load() {
        std::ifstream f(macroPath());
        if (!f.is_open()) return false;
        std::vector<Click> loaded;
        int frame, down, button, p1;
        while (f >> frame >> down >> button >> p1) {
            loaded.push_back({frame, down != 0, button, p1 != 0});
        }
        if (loaded.empty()) return false;
        clicks = std::move(loaded);
        return true;
    }
}

class $modify(BotGJBaseGameLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        if (Bot::mode == BotMode::Recording && !Bot::injecting && PlayLayer::get()) {
            Bot::clicks.push_back({m_gameState.m_currentProgress, down, button, isPlayer1});
        }
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }

    void processCommands(float dt) {
        if (Bot::mode == BotMode::Replaying && PlayLayer::get() && m_player1 && !m_player1->m_isDead) {
            int frame = m_gameState.m_currentProgress;
            Bot::injecting = true;
            while (Bot::index < Bot::clicks.size() && Bot::clicks[Bot::index].frame <= frame) {
                auto const& c = Bot::clicks[Bot::index++];
                this->handleButton(c.down, c.button, c.p1);
            }
            Bot::injecting = false;
        }
        GJBaseGameLayer::processCommands(dt);
    }
};

class $modify(BotPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        Bot::mode = BotMode::Idle;
        Bot::index = 0;
        if (Mod::get()->getSettingValue<bool>("auto-replay") && Bot::load()) {
            Bot::mode = BotMode::Replaying;
            Bot::note("AutoBot: reproduciendo macro");
        }
        return true;
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        if (Bot::mode == BotMode::Recording) Bot::clicks.clear();
        if (Bot::mode == BotMode::Replaying) Bot::index = 0;
    }

    void levelComplete() {
        if (Bot::mode == BotMode::Recording) {
            if (Bot::save()) Bot::note("AutoBot: macro guardada");
            else Bot::note("AutoBot: no se pudo guardar", NotificationIcon::Error);
        }
        Bot::mode = BotMode::Idle;
        PlayLayer::levelComplete();
    }

    void onQuit() {
        Bot::mode = BotMode::Idle;
        PlayLayer::onQuit();
    }
};

class $modify(BotPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        auto winSize = CCDirector::get()->getWinSize();
        auto menu = CCMenu::create();
        menu->setContentSize({120.f, 220.f});
        menu->setPosition({winSize.width - 70.f, winSize.height / 2.f});
        menu->setLayout(ColumnLayout::create()->setGap(6.f));

        auto add = [&](char const* text, SEL_MenuHandler cb) {
            auto spr = ButtonSprite::create(text);
            spr->setScale(0.8f);
            menu->addChild(CCMenuItemSpriteExtra::create(spr, this, cb));
        };
        add("GRABAR", menu_selector(BotPauseLayer::onBotRecord));
        add("REPRODUCIR", menu_selector(BotPauseLayer::onBotPlay));
        add("DETENER", menu_selector(BotPauseLayer::onBotStop));
        add("COMPLETAR", menu_selector(BotPauseLayer::onBotFinish));
        menu->updateLayout();
        this->addChild(menu);
    }

    void onBotRecord(CCObject*) {
        Bot::mode = BotMode::Recording;
        Bot::clicks.clear();
        Bot::note("AutoBot: grabando desde el inicio. Pasa el nivel.");
        this->onRestart(nullptr);
    }

    void onBotPlay(CCObject*) {
        if (Bot::clicks.empty() && !Bot::load()) {
            Bot::note("AutoBot: no hay macro para este nivel", NotificationIcon::Error);
            return;
        }
        Bot::mode = BotMode::Replaying;
        Bot::index = 0;
        Bot::note("AutoBot: reproduciendo");
        this->onRestart(nullptr);
    }

    void onBotStop(CCObject*) {
        if (Bot::mode == BotMode::Recording && !Bot::clicks.empty()) {
            Bot::save();
            Bot::note("AutoBot: grabacion guardada");
        } else {
            Bot::note("AutoBot: detenido", NotificationIcon::Info);
        }
        Bot::mode = BotMode::Idle;
    }

    // Completa el nivel al instante (sin jugarlo). Usalo solo offline / sin enviar records.
    void onBotFinish(CCObject*) {
        auto pl = PlayLayer::get();
        if (!pl) return;
        this->onResume(nullptr);
        pl->levelComplete();
    }
};
