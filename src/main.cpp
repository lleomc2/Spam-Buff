#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <algorithm>
#include <cstdint>
#include <deque>
using namespace geode::prelude;
namespace spambuff {
struct QueuedInput { std::uint64_t applyAtTick; bool down; int button; bool player1; };
struct RuntimeState { std::deque<QueuedInput> queue; std::uint64_t physicsTick = 0; bool forwardingQueuedInput = false; };
inline RuntimeState g_state;
bool isEnabled() { return Mod::get()->getSettingValue<bool>("enabled"); }
int delayTicks() { return std::clamp(Mod::get()->getSettingValue<int>("wave-delay-ticks"), 1, 4); }
PlayerObject* getPlayer(GJBaseGameLayer* layer, bool player1) { return player1 ? layer->m_player1 : layer->m_player2; }
void reset() { g_state.queue.clear(); g_state.physicsTick = 0; g_state.forwardingQueuedInput = false; }
}
class $modify(SpamBuffBaseGameLayer, GJBaseGameLayer) {
void handleButton(bool down, int button, bool isPlayer1) {
auto& state = spambuff::g_state;
if (state.forwardingQueuedInput) { GJBaseGameLayer::handleButton(down, button, isPlayer1); return; }
auto* player = spambuff::getPlayer(this, isPlayer1);
const bool isWave = player && player->m_isDart && !player->m_isDead;
if (!spambuff::isEnabled() || button != 1 || !isWave) { GJBaseGameLayer::handleButton(down, button, isPlayer1); return; }
std::uint64_t targetTick = state.physicsTick + static_cast<std::uint64_t>(spambuff::delayTicks());
if (!state.queue.empty() && targetTick <= state.queue.back().applyAtTick) targetTick = state.queue.back().applyAtTick + 1;
state.queue.push_back({targetTick, down, button, isPlayer1});
}
void processCommands(float dt, bool isHalfTick, bool isLastTick) {
auto& state = spambuff::g_state;
++state.physicsTick;
const bool enabled = spambuff::isEnabled();
while (!state.queue.empty() && (!enabled || state.queue.front().applyAtTick <= state.physicsTick)) {
const auto input = state.queue.front(); state.queue.pop_front();
auto* player = spambuff::getPlayer(this, input.player1);
if (!player || !player->m_isDart || player->m_isDead) continue;
state.forwardingQueuedInput = true;
GJBaseGameLayer::handleButton(input.down, input.button, input.player1);
state.forwardingQueuedInput = false;
}
GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
}
};
class $modify(SpamBuffPlayLayer, PlayLayer) {
void resetLevel() { spambuff::reset(); PlayLayer::resetLevel(); }
void onQuit() { spambuff::reset(); PlayLayer::onQuit(); }
};
$on_mod(Loaded) {
log::info("Spam Buff v1.0.0 loaded");
log::info("Wave input tick alignment: {}", spambuff::delayTicks());
}