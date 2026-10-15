# SNC MMORPG 传奇版 — 项目说明

## 概述

本目录为基于 **snc C++17 框架** 的 **传奇 (Legend of Mir) 风格 MMORPG 服务器** 实现。
完整设计依据请参考：

- [mmorpg-legend-server-design.md](../../docs/design/mmorpg-legend-server-design.md)（整体方案）
- [mmorpg-legend-implementation-spec.md](../../docs/design/mmorpg-legend-implementation-spec.md)（实施规格）

## 版本

| 组件 | 版本 |
|------|------|
| 框架 snc | 1.0.1 |
| MMORPG 模块 | 1.0.0 |
| 传奇版扩展 | 1.0.0 |

## 模块清单

| 模块 | 文件 | 状态 |
|------|------|------|
| PlayerActor（增强版） | `include/snc/mmorpg/PlayerActor.h` + `src/PlayerActor.cpp` | Phase 1 |
| MapManager（增强版） | `include/snc/mmorpg/MapManager.h`（覆盖原版） | Phase 2 |
| NPCManager | `include/snc/mmorpg/NPCManager.h` + `src/NPCManager.cpp` | Phase 3 |
| MonsterManager | `include/snc/mmorpg/MonsterManager.h` + `src/MonsterManager.cpp` | Phase 4 |
| CombatSystem | `include/snc/mmorpg/CombatSystem.h` + `src/CombatSystem.cpp` | Phase 5 |
| DropManager | `include/snc/mmorpg/DropManager.h` + `src/DropManager.cpp` | Phase 6 |
| GuildManager | `include/snc/mmorpg/GuildManager.h` + `src/GuildManager.cpp` | Phase 7 |
| WorldChannel | `include/snc/mmorpg/WorldChannel.h` + `src/WorldChannel.cpp` | Phase 8 |
| GameService（增强版） | `include/snc/mmorpg/GameService.h`（覆盖原版）+ `src/GameService.cpp` | Phase 9 |
| 示例程序 | `examples/example_game.cpp`, `examples/example_full.cpp` | Phase 10 |
| 单元测试 | `tests/test_player_actor.cpp`, `tests/test_combat.cpp` 等 | Phase 10 |

## 依赖

- snc 框架（本体 `libs/snc_mmorpg/` 已提供基础接口，本目录独立于 libs 层）
- CMake 3.20+
- C++17 编译器（MSVC / GCC / Clang）

## 构建

```bash
cmake -B build -S .
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```
