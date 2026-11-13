#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <algorithm>
#include <cstdint>
#include <deque>

using namespace geode::prelude;

namespace spambuff {
struct QueuedInput {
    std::uint64_t applyAtTick;
    bool down;
    int button;
    bool player1;
};

struct RuntimeState {
    std::deque<QueuedInput> queue;
    std::uint64_t physicsTick = 0;
    bool forwardingInput = false;
    bool holdActive = false;
    bool holdButtonDown = false;
    bool holdPlayer1 = true;
    int holdTicks = 0;
    bool autoSpamEnabled = false;
    bool lastPlayer1 = true;
};

inline RuntimeState g_state;
inline CCMenuItemSpriteExtra* g_autoButton = nullptr;

bool isEnabled() {
    return Mod::get()->getSettingValue<bool>("enabled");
}

bool tapSyncEnabled() {
    return Mod::get()->getSettingValue<bool>("tap-sync-enabled");
}

int holdInterval() {
    return std::clamp(
        Mod::get()->getSettingValue<int>("hold-spam-interval"), 1, 8
    );
}

void refreshAutoButton() {
    if (!g_autoButton) return;
    auto text = g_state.autoSpamEnabled ? "AUTO ON" : "AUTO OFF";
    auto sprite = ButtonSprite::create(text, "goldFont.fnt", "GJ_button_01.png", 0.7f);
    if (sprite) g_autoButton->setNormalImage(sprite);
}

PlayerObject* getPlayer(GJBaseGameLayer* layer, bool player1) {
    return player1 ? layer->m_player1 : layer->m_player2;
}

void reset() {
    g_state = RuntimeState{};
    refreshAutoButton();
}
}

class $modify(SpamBuffBaseGameLayer, GJBaseGameLayer) {
    void sendSyntheticInput(bool down, int button, bool player1) {
        auto& state = spambuff::g_state;
        state.forwardingInput = true;
        GJBaseGameLayer::handleButton(down, button, player1);
        state.forwardingInput = false;
    }

    void stopHold(int button = 1) {
        auto& state = spambuff::g_state;
        if (state.holdButtonDown) {
            sendSyntheticInput(false, button, state.holdPlayer1);
        }
        state.holdActive = false;
        state.holdButtonDown = false;
        state.holdTicks = 0;
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        auto& state = spambuff::g_state;

        if (state.forwardingInput) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
            return;
        }

        if (button == 1) state.lastPlayer1 = isPlayer1;

        auto* player = spambuff::getPlayer(this, isPlayer1);
        const bool isWave = player && player->m_isDart && !player->m_isDead;

        // When automatic mode is enabled, physical taps must not fight
        // the generated Wave input. The overlay button remains independent.
        if (spambuff::isEnabled() && state.autoSpamEnabled &&
            button == 1 && isWave) {
            return;
        }

        if (!spambuff::isEnabled() || button != 1 || !isWave) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
            return;
        }

        if (spambuff::tapSyncEnabled()) {
            const std::uint64_t nextTick = state.physicsTick + 1;
            const std::uint64_t afterPrevious = state.queue.empty()
                ? nextTick
                : state.queue.back().applyAtTick + 1;
            const std::uint64_t targetTick = std::max(nextTick, afterPrevious);

            state.queue.push_back({ targetTick, down, button, isPlayer1 });
            return;
        }

        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }

    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        auto& state = spambuff::g_state;
        ++state.physicsTick;

        const bool enabled = spambuff::isEnabled();
        const bool tapEnabled = enabled && spambuff::tapSyncEnabled();

        if (!tapEnabled || state.autoSpamEnabled) {
            state.queue.clear();
        } else {
            while (!state.queue.empty() &&
                   state.queue.front().applyAtTick <= state.physicsTick) {
                const auto input = state.queue.front();
                state.queue.pop_front();

                auto* player = spambuff::getPlayer(this, input.player1);
                if (!player || !player->m_isDart || player->m_isDead) {
                    state.queue.clear();
                    break;
                }
                sendSyntheticInput(input.down, input.button, input.player1);
            }
        }

        if (state.holdActive) {
            auto* player = spambuff::getPlayer(this, state.holdPlayer1);
            const bool canContinue = enabled && state.autoSpamEnabled &&
                player && player->m_isDart && !player->m_isDead;

            if (!canContinue) {
                const bool died = !player || player->m_isDead;
                stopHold();
                if (died && state.autoSpamEnabled) {
                    state.autoSpamEnabled = false;
                    spambuff::refreshAutoButton();
                }
            } else {
                ++state.holdTicks;
                if (state.holdTicks >= spambuff::holdInterval()) {
                    state.holdTicks = 0;
                    state.holdButtonDown = !state.holdButtonDown;
                    sendSyntheticInput(state.holdButtonDown, 1, state.holdPlayer1);
                }
            }
        } else if (enabled && state.autoSpamEnabled) {
            auto* player = spambuff::getPlayer(this, state.lastPlayer1);
            if (player && player->m_isDart && !player->m_isDead) {
                state.queue.clear();
                state.holdActive = true;
                state.holdButtonDown = true;
                state.holdPlayer1 = state.lastPlayer1;
                state.holdTicks = 0;
                sendSyntheticInput(true, 1, state.holdPlayer1);
            }
        }

        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
    }
};

class $modify(SpamBuffPlayLayer, PlayLayer) {
    void onEnter() {
        PlayLayer::onEnter();

        auto winSize = CCDirector::sharedDirector()->getWinSize();
        auto menu = CCMenu::create();
        if (!menu) return;
        menu->setPosition({ 0.f, 0.f });

        auto sprite = ButtonSprite::create("AUTO OFF", "goldFont.fnt", "GJ_button_01.png", 0.7f);
        if (!sprite) return;

        spambuff::g_autoButton = CCMenuItemSpriteExtra::create(
            sprite, this, menu_selector(SpamBuffPlayLayer::onAutoSpam)
        );
        if (!spambuff::g_autoButton) return;

        spambuff::g_autoButton->setPosition({ winSize.width - 78.f, 48.f });
        menu->addChild(spambuff::g_autoButton);
        this->addChild(menu, 1000);
    }

    void onAutoSpam(CCObject*) {
        auto& state = spambuff::g_state;
        if (!spambuff::isEnabled()) return;
        state.autoSpamEnabled = !state.autoSpamEnabled;
        if (!state.autoSpamEnabled) {
            // The next physics step releases any synthetic hold safely.
            state.queue.clear();
        }
        spambuff::refreshAutoButton();
    }

    void resetLevel() {
        spambuff::reset();
        PlayLayer::resetLevel();
    }

    void onQuit() {
        spambuff::reset();
        spambuff::g_autoButton = nullptr;
        PlayLayer::onQuit();
    }
};

$on_mod(Loaded) {
    log::info("Spam Buff v1.4.0 loaded");
    log::info("Tap precision: {}", spambuff::tapSyncEnabled());
    log::info("Auto spam interval: {} physics ticks", spambuff::holdInterval());
}
