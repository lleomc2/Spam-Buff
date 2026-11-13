#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <algorithm>
#include <cstdint>
#include <chrono>
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
    bool rapidSpamActive = false;
    int rapidTapCount = 0;
    std::chrono::steady_clock::time_point lastRapidTapAt{};
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

bool rapidSpamEnabled() {
    return Mod::get()->getSettingValue<bool>("rapid-spam-enabled");
}

int rapidSpamWindowMs() {
    return std::clamp(
        Mod::get()->getSettingValue<int>("rapid-spam-window-ms"), 80, 400
    );
}

int rapidSpamReleaseMs() {
    return std::clamp(
        Mod::get()->getSettingValue<int>("rapid-spam-release-ms"), 150, 700
    );
}

void refreshAutoButton() {
    if (!g_autoButton) return;
    auto text = g_state.rapidSpamActive
        ? "RAPID"
        : (g_state.autoSpamEnabled ? "SPAM ON" : "SPAM OFF");
    auto sprite = ButtonSprite::create(text, "goldFont.fnt", "GJ_button_01.png", 0.85f);
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
        const bool enabled = spambuff::isEnabled();

        // Once rapid spam is detected, physical taps become a heartbeat:
        // they keep the automatic hold-pattern alive, but do not inject
        // extra down/up edges that could fight the synthetic input.
        if (enabled && button == 1 && state.rapidSpamActive) {
            if (down) {
                state.lastRapidTapAt = std::chrono::steady_clock::now();
                state.holdPlayer1 = isPlayer1;
            }
            return;
        }

        if (enabled && state.autoSpamEnabled && button == 1 && isWave) {
            return;
        }

        if (!enabled || button != 1 || !isWave) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
            return;
        }

        // Three quick press edges activate the same alternating hold engine
        // used by the floating SPAM toggle. A single long press never triggers it.
        if (spambuff::rapidSpamEnabled() && down && !state.autoSpamEnabled) {
            const auto now = std::chrono::steady_clock::now();
            const auto gap = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - state.lastRapidTapAt
            ).count();

            if (state.rapidTapCount > 0 &&
                gap >= 0 && gap <= spambuff::rapidSpamWindowMs()) {
                ++state.rapidTapCount;
            } else {
                state.rapidTapCount = 1;
            }
            state.lastRapidTapAt = now;

            if (state.rapidTapCount >= 3) {
                state.rapidSpamActive = true;
                state.queue.clear();
                state.holdActive = true;
                state.holdButtonDown = true;
                state.holdPlayer1 = isPlayer1;
                state.holdTicks = 0;
                sendSyntheticInput(true, 1, isPlayer1);
                spambuff::refreshAutoButton();
                return;
            }
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

        // Stop the rapid mode only after a short pause, so tiny gaps between
        // real taps do not cause the synthetic hold to flicker off.
        if (state.rapidSpamActive &&
            std::chrono::steady_clock::now() - state.lastRapidTapAt >=
                std::chrono::milliseconds(spambuff::rapidSpamReleaseMs())) {
            state.rapidSpamActive = false;
            state.rapidTapCount = 0;
            if (!state.autoSpamEnabled) stopHold();
            spambuff::refreshAutoButton();
        }

        if (!tapEnabled || state.autoSpamEnabled || state.rapidSpamActive) {
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
            const bool holdRequested =
                state.autoSpamEnabled || state.rapidSpamActive;
            const bool canContinue = enabled && holdRequested &&
                player && player->m_isDart && !player->m_isDead;

            if (!canContinue) {
                const bool died = !player || player->m_isDead;
                stopHold();
                if (state.rapidSpamActive) {
                    state.rapidSpamActive = false;
                    state.rapidTapCount = 0;
                }
                if (died && state.autoSpamEnabled) {
                    state.autoSpamEnabled = false;
                }
                spambuff::refreshAutoButton();
            } else {
                ++state.holdTicks;
                if (state.holdTicks >= spambuff::holdInterval()) {
                    state.holdTicks = 0;
                    state.holdButtonDown = !state.holdButtonDown;
                    sendSyntheticInput(state.holdButtonDown, 1, state.holdPlayer1);
                }
            }
        } else if (enabled &&
                   (state.autoSpamEnabled || state.rapidSpamActive)) {
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
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        // Build the floating toggle after the level UI has been initialized.
        auto winSize = CCDirector::sharedDirector()->getWinSize();
        auto menu = CCMenu::create();
        if (!menu) return true;
        menu->setPosition({ 0.f, 0.f });
        menu->setID("spambuff-floating-menu"_spr);

        auto sprite = ButtonSprite::create(
            "SPAM OFF", "goldFont.fnt", "GJ_button_01.png", 0.85f
        );
        if (!sprite) return true;

        spambuff::g_autoButton = CCMenuItemSpriteExtra::create(
            sprite, this, menu_selector(SpamBuffPlayLayer::onAutoSpam)
        );
        if (!spambuff::g_autoButton) return true;

        // Keep it away from the normal jump area: a visible floating control
        // at the right side of the play screen.
        spambuff::g_autoButton->setPosition({
            winSize.width - 72.f,
            winSize.height * 0.52f
        });
        spambuff::g_autoButton->setID("spambuff-floating-toggle"_spr);
        menu->addChild(spambuff::g_autoButton);
        menu->setZOrder(100000);
        this->addChild(menu, 100000);

        log::info("Spam Buff floating toggle created");
        return true;
    }

    void onAutoSpam(CCObject*) {
        auto& state = spambuff::g_state;
        if (!spambuff::isEnabled()) return;
        state.autoSpamEnabled = !state.autoSpamEnabled;
        if (!state.autoSpamEnabled) {
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
    log::info("Spam Buff v1.5.0 loaded");
    log::info("Tap precision: {}", spambuff::tapSyncEnabled());
    log::info("Auto spam interval: {} physics ticks", spambuff::holdInterval());
    log::info("Rapid spam detection: {}", spambuff::rapidSpamEnabled());
}
