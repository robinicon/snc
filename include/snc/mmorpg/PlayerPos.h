#pragma once

/// @file PlayerPos.h
/// @brief 玩家/实体位置数据结构，供框架层与传奇层共享。

#include <cstdint>
#include <cmath>
#include <string>

namespace snc {
namespace mmorpg {

struct PlayerPos {
    int32_t mapId = 0;
    int32_t x     = 0;
    int32_t y     = 0;
    bool IsValid() const { return mapId > 0 && x >= 0 && y >= 0; }
    int32_t ManhattanDistTo(const PlayerPos& o) const {
        return std::abs(x - o.x) + std::abs(y - o.y);
    }
    std::string ToString() const {
        return "(" + std::to_string(mapId) + "," + std::to_string(x) + "," + std::to_string(y) + ")";
    }
};

} // namespace mmorpg
} // namespace snc
