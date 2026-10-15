/// @file PetInstance.cpp
/// @brief 宠物实体实现 — 跟随主人 + 攻击逻辑

#include "snc/mmorpg/legend/PetInstance.h"
#include <sstream>

namespace snc {
namespace mmorpg {
namespace legend {

void PetInstance::Tick(long long elapsedMs) {
    if (!IsAlive()) return;
    if (IsExpired()) {
        // 寿命到期，清除自身（由父级 Scene 处理移除）
        current_hp_ = 0;
        return;
    }

    // 跟随主人逻辑
    if (owner_) {
        auto ownerPos = owner_->Pos();
        int dx = ownerPos.x - pos_.x;
        int dy = ownerPos.y - pos_.y;
        int dist = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);

        if (dist > def_.atkRange) {
            // 向主人移动
            int steps = std::min(def_.speed, dist);
            if (dx != 0) pos_.x += (dx > 0 ? 1 : -1) * std::min(steps, std::abs(dx));
            if (dy != 0) pos_.y += (dy > 0 ? 1 : -1) * std::min(steps, std::abs(dy));
        }
        // 同步地图
        pos_.mapId = ownerPos.mapId;
    }
}

bool PetInstance::IsExpired() const {
    if (def_.lifetimeMs <= 0) return false;
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - summoned_at_).count();
    return elapsed >= def_.lifetimeMs;
}

std::string PetInstance::DumpState() const {
    std::ostringstream oss;
    oss << "{\"pet_id\":" << EntityId()
        << ",\"owner\":" << OwnerId()
        << ",\"hp\":" << current_hp_ << "/" << def_.hp
        << ",\"state\":\"" << PetStateName(state_) << "\""
        << ",\"pos\":{\"map\":" << pos_.mapId << ",\"x\":" << pos_.x << ",\"y\":" << pos_.y << "}}"
        << "}";
    return oss.str();
}

} // namespace legend
} // namespace mmorpg
} // namespace snc
