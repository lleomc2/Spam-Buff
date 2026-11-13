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

        if (button == 1 && state.holdActive &&
            state.holdPlayer1 == isPlayer1 && !down) {
            // Always catch the physical release, even if the player has just
            // died or is no longer in Wave. This prevents a stuck input.
            stopHold(button);
            return;
        }

        auto* player = spambuff::getPlayer(this, isPlayer1);
        const bool isWave = player && player->m_isDart && !player->m_isDead;

        if (!spambuff::isEnabled() || button != 1 || !isWave) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
            return;
        }

        if (spambuff::holdSpamEnabled()) {
            if (down && !state.holdActive) {
                // Discard pending tap events before starting the other mode.
                state.queue.clear();
                state.holdActive = true;
                state.holdButtonDown = true;
                state.holdPlayer1 = isPlayer1;
                state.holdTicks = 0;
                sendSyntheticInput(true, button, isPlayer1);
            }
            return;
        }

        if (spambuff::tapSyncEnabled()) {
            // Apply each state transition on its own physics tick. In
            // particular, a very fast down/up pair cannot collapse into
            // two transitions in the same tick and disappear.
            const std::uint64_t nextTick = state.physicsTick + 1;
            const std::uint64_t afterPrevious = state.queue.empty()
                ? nextTick
                : state.queue.back().applyAtTick + 1;
            const std::uint64_t targetTick = std::max(nextTick, afterPrevious);

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

        if (!tapEnabled) {
            // Never replay stale queued taps after the user disables the mode.
            state.queue.clear();
        } else {
            while (!state.queue.empty() &&
                   state.queue.front().applyAtTick <= state.physicsTick) {
                const auto input = state.queue.front();
                state.queue.pop_front();

                auto* player = spambuff::getPlayer(this, input.player1);
                if (!player || !player->m_isDart || player->m_isDead) {
                    // A death or mode change invalidates the remaining
                    // buffered sequence; do not replay it after a respawn.
                    state.queue.clear();
                    break;
                }

                sendSyntheticInput(input.down, input.button, input.player1);
            }
        }

        if (state.holdActive) {
            auto* player = spambuff::getPlayer(this, state.holdPlayer1);
            const bool canContinue = enabled && spambuff::holdSpamEnabled()
                && player && player->m_isDart && !player->m_isDead;

            if (!canContinue) {
                stopHold();
            } else {
                ++state.holdTicks;
                if (state.holdTicks >= spambuff::holdInterval()) {
                    state.holdTicks = 0;
                    state.holdButtonDown = !state.holdButtonDown;
                    sendSyntheticInput(
                        state.holdButtonDown, 1, state.holdPlayer1
                    );
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
    log::info("Spam Buff v1.2.0 loaded");
    log::info("Tap precision: {}", spambuff::tapSyncEnabled());
    log::info("Hold spam: {}", spambuff::holdSpamEnabled());
    log::info("Hold interval: {} physics ticks", spambuff::holdInterval());
}
