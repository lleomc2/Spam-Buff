#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
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
};

inline RuntimeState g_state;

bool isEnabled() {
    return Mod::get()->getSettingValue<bool>("enabled");
}

bool tapSyncEnabled() {
    return Mod::get()->getSettingValue<bool>("tap-sync-enabled");
}

bool holdSpamEnabled() {
    return Mod::get()->getSettingValue<bool>("hold-spam-enabled");
}

int holdInterval() {
    return std::clamp(
        Mod::get()->getSettingValue<int>("hold-spam-interval"), 1, 8
    );
}

PlayerObject* getPlayer(GJBaseGameLayer* layer, bool player1) {
    return player1 ? layer->m_player1 : layer->m_player2;
}

void reset() {
    g_state = RuntimeState{};
}
}

class $modify(SpamBuffBaseGameLayer, GJBaseGameLayer) {
    void sendSyntheticInput(bool down, int button, bool player1) {
        auto& state = spambuff::g_state;
        state.forwardingInput = true;
        GJBaseGameLayer::handleButton(down, button, player1);
        state.forwardingInput = false;
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        auto& state = spambuff::g_state;

        if (state.forwardingInput) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
            return;
        }

        auto* player = spambuff::getPlayer(this, isPlayer1);
        const bool isWave = player && player->m_isDart && !player->m_isDead;

        if (!spambuff::isEnabled() || button != 1 || !isWave) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
            return;
        }

        // Hold mode: one physical press starts the automatic, evenly-spaced
        // Wave toggles. Releasing the screen always stops the sequence.
        if (spambuff::holdSpamEnabled()) {
            if (down) {
                if (!state.holdActive) {
                    state.holdActive = true;
                    state.holdButtonDown = true;
                    state.holdPlayer1 = isPlayer1;
                    state.holdTicks = 0;
                    sendSyntheticInput(true, button, isPlayer1);
                }
            } else if (state.holdActive) {
                state.holdActive = false;
                state.holdTicks = 0;
                if (state.holdButtonDown) {
                    sendSyntheticInput(false, button, isPlayer1);
                    state.holdButtonDown = false;
                }
            }
            return;
        }

        // Tap precision mode: preserve every real down/up event, but apply
        // it on the next physics tick rather than adding a multi-tick delay.
        if (spambuff::tapSyncEnabled()) {
            const std::uint64_t nextTick = state.physicsTick + 1;
            const std::uint64_t targetTick = state.queue.empty()
                ? nextTick
                : std::max(nextTick, state.queue.back().applyAtTick);

            state.queue.push_back({
                targetTick, down, button, isPlayer1
            });
            return;
        }

        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }

    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        auto& state = spambuff::g_state;
        ++state.physicsTick;

        const bool enabled = spambuff::isEnabled();
        const bool tapEnabled = enabled && spambuff::tapSyncEnabled();

        while (!state.queue.empty() &&
               (!tapEnabled ||
                state.queue.front().applyAtTick <= state.physicsTick)) {
            const auto input = state.queue.front();
            state.queue.pop_front();

            auto* player = spambuff::getPlayer(this, input.player1);
            if (!player || !player->m_isDart || player->m_isDead) {
                continue;
            }

            sendSyntheticInput(input.down, input.button, input.player1);
        }

        // If the setting is switched off or the player leaves Wave while
        // the finger is held, release the synthetic button cleanly.
        if (state.holdActive) {
            auto* player = spambuff::getPlayer(this, state.holdPlayer1);
            const bool canContinue = enabled && spambuff::holdSpamEnabled()
                && player && player->m_isDart && !player->m_isDead;

            if (!canContinue) {
                state.holdActive = false;
                state.holdTicks = 0;
                if (state.holdButtonDown) {
                    sendSyntheticInput(false, 1, state.holdPlayer1);
                    state.holdButtonDown = false;
                }
            } else {
                ++state.holdTicks;
                if (state.holdTicks >= spambuff::holdInterval()) {
                    state.holdTicks = 0;
                    state.holdButtonDown = !state.holdButtonDown;
                    sendSyntheticInput(state.holdButtonDown, 1, state.holdPlayer1);
                }
            }
        }

        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
    }
};

class $modify(SpamBuffPlayLayer, PlayLayer) {
    void resetLevel() {
        spambuff::reset();
        PlayLayer::resetLevel();
    }

    void onQuit() {
        spambuff::reset();
        PlayLayer::onQuit();
    }
};

$on_mod(Loaded) {
    log::info("Spam Buff v1.1.0 loaded");
    log::info("Tap precision: {}", spambuff::tapSyncEnabled());
    log::info("Hold spam: {}", spambuff::holdSpamEnabled());
    log::info("Hold interval: {} physics ticks", spambuff::holdInterval());
}
