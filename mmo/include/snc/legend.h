// SPDX-License-Identifier: MIT
// Licensed under the MIT License — see LICENSE for details.
//
// snc/legend.h — Legend MMO business module unified header.
//
// This header aggregates all Legend-specific business-layer headers.
// It is intentionally separate from snc.h (which only contains the
// core/base library). Business logic must never be mixed into the
// foundation library.
#pragma once

#include "snc/mmorpg/legend/CombatSystem.h"
#include "snc/mmorpg/legend/DropManager.h"
#include "snc/mmorpg/legend/Entity.h"
#include "snc/mmorpg/legend/EntityMgr.h"
#include "snc/mmorpg/legend/GuildManager.h"
#include "snc/mmorpg/legend/Item.h"
#include "snc/mmorpg/legend/ItemMgr.h"
#include "snc/mmorpg/legend/MonsterManager.h"
#include "snc/mmorpg/legend/NPCManager.h"
#include "snc/mmorpg/legend/PlayerActor.h"
#include "snc/mmorpg/legend/Scene.h"
#include "snc/mmorpg/legend/SceneMgr.h"
#include "snc/mmorpg/legend/WorldChannel.h"
