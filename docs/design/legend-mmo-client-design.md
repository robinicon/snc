# 传奇类 MMORPG 客户端设计 — Unity / UE5 技术选型与落地方案

**日期**：2026-10-15
**状态**：评估草稿
**配套服务器**：SNC C++ 框架（传奇版 v2.1）

---

## 一、执行摘要

| 维度 | Unity 2022 LTS | Unreal Engine 5.4 |
|------|---------------|-------------------|
| **推荐指数** | ⭐⭐⭐⭐ | ⭐⭐⭐⭐ |
| **开发效率** | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ |
| **性能上限** | ⭐⭐⭐ | ⭐⭐⭐⭐⭐ |
| **传奇风格适配** | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐ |
| **团队门槛** | 低（C# 普及） | 高（需 C++ 基础） |
| **移动端支持** | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ |
| **服务器协议对齐** | 需自研网络层 | 需自定义 NetChannel |

**结论**：传奇类游戏体量小、风格怀旧、目标用户广泛，**Unity 是更务实的选择**。UE5 适合追求高清重制、PC/主机首发的高端项目。

---

## 二、现有服务端协议分析

### 2.1 协议格式（SNC 定义）

**文件**：[Protocol.h](file:///c:/Users/10422/Documents/code/snc/include/snc/base/Protocol.h)

```
Wire Format:
┌──────────────┬──────────────┬─────────────────────┐
│  msgId       │  payloadLen  │  payload            │
│  int32 (4B)  │  int32 (4B)  │  [0 ~ 16MB]         │
└──────────────┴──────────────┴─────────────────────┘
   固定 8 字节头部          变长 payload
```

**当前问题**：GameService.cpp 使用 **JSON 字符串 + 手工查找** 解析 payload（[GameService.cpp:274-306](file:///c:/Users/10422/Documents/code/snc/mmo/src/GameService.cpp#L274-L306)），这是过渡期实现，需要替换为结构化二进制。

### 2.2 消息 ID 规划

**文件**：[Protocol.h](file:///c:/Users/10422/Documents/code/snc/include/snc/base/Protocol.h) + [GameService.h](file:///c:/Users/10422/Documents/code/snc/mmo/include/snc/mmorpg/legend/GameService.h)

```
消息 ID 空间：
  0x0001        心跳 Ping
  0x0002        心跳 Pong
  0xFEED        RPC 控制消息
  0x1000-0x1FFF 客户端 → Gate（业务输入）
  0x1100-0x11FF 传奇业务消息（已定义 7 个）
  0x2000-0x2FFF Gate  → Game 服（RPC 转发）
  0x2000        GameFromGate
  0x2100        GameToGate
  0x3000-0x3FFF 服务器 ↔ 服务器
  0x4000-0x4FFF 广播消息
```

**已定义的传奇业务消息**：

| msgId | 常量名 | 方向 | 用途 |
|-------|--------|------|------|
| 0x1100 | kMsgClientMove | C→S | 移动请求 |
| 0x1101 | kMsgClientAttack | C→S | 攻击请求 |
| 0x1102 | kMsgClientNPCDialog | C→S | NPC 交互 |
| 0x1103 | kMsgClientChat | C→S | 聊天 |
| 0x1104 | kMsgClientPickup | C→S | 拾取物品 |
| 0x1105 | kMsgClientSkill | C→S | 技能释放 |
| 0x1106 | kMsgClientGuildReq | C→S | 行会请求 |

> ⚠️ **客户端需要补全服务器→客户端的消息 ID**（当前只有 C→S 定义，缺少 S→C 响应消息）。

---

## 三、技术选型分析

### 3.1 网络层方案对比

#### 方案 A：Unity + 自研 TCP 客户端（推荐）

```
Unity C# Client
    │
    ▼
TcpClient (System.Net.Sockets)
    ├── 自定义 8 字节头部解析器
    ├── 粘包/拆包处理（LengthFieldBasedFrameDecoder）
    └── 消息分发（按 msgId 路由）
```

| 维度 | 评估 |
|------|------|
| 与 SNC 协议对齐 | ✅ 完全一致（8 字节头部） |
| 开发工作量 | 🟡 中（约 500 行 C# 网络层代码） |
| 调试友好度 | ✅ 高（可直接抓包 Wireshark） |
| 性能 | ✅ 足够（传奇类游戏消息量不大） |
| 推荐度 | ⭐⭐⭐⭐⭐ |

#### 方案 B：Unity + Photon Fusion

| 维度 | 评估 |
|------|------|
| 与 SNC 协议对齐 | ❌ 不兼容（Photon 自有协议） |
| 开发工作量 | 🟢 低（网络层全托管） |
| 适合场景 | 轻量联机（<50 人），不适合 MMORPG |
| 推荐度 | ⭐⭐ |

#### 方案 C：Unity + Mirror / Netcode for GameObjects

| 维度 | 评估 |
|------|------|
| 与 SNC 协议对齐 | ❌ 不兼容 |
| 定位 | 小型多人游戏，非 MMO |
| 推荐度 | ⭐⭐ |

#### 方案 D：UE5 + 自定义 NetChannel

```
UE5 Client
    │
    ▼
UNetConnection (自定义)
    ├── 重写 SerializeNewOutgoingMessages
    ├── 自定义包头解析
    └── 消息路由到 GameInstance
```

| 维度 | 评估 |
|------|------|
| 与 SNC 协议对齐 | ✅ 可行（需重写 NetChannel） |
| 开发工作量 | 🔴 高（UE 网络层深入修改） |
| 调试友好度 | 🟡 中（需借助 NetVisualizer） |
| 推荐度 | ⭐⭐⭐ |

#### 方案 E：UE5 + Replication（不推荐）

UE 内置 Replication 与 SNC 的 8 字节头部协议**完全不兼容**，强行使用需要放弃 SNC 服务器或写完整桥接层，成本过高。

### 3.2 序列化方案选择

SNC 服务端已有三种序列化后端：Protobuf、FlatBuffers、ISerializer 抽象接口。但传奇业务层目前仍用 **JSON 字符串**（[GameService.cpp:274](file:///c:/Users/10422/Documents/code/snc/mmo/src/GameService.cpp#L274)）。

**客户端侧序列化推荐**：

| 方案 | 优势 | 劣势 | 推荐度 |
|------|------|------|--------|
| **FlatBuffers** | 零拷贝反序列化，移动消息极快 | Schema 需单独维护，向前兼容弱 | ⭐⭐⭐⭐⭐ |
| **Protobuf** | Schema 向后兼容，生态成熟 | 反序列化有临时分配 | ⭐⭐⭐⭐ |
| **手写 Binary** | 最快，完全可控 | 字段变更需手动维护 | ⭐⭐⭐ |

**推荐**：传奇类游戏消息量不大（高频仅移动坐标 ~20B），**FlatBuffers 最合适**。移动消息可零拷贝直接访问字段， CombatSystem 计算结果也可高效传递。

### 3.3 客户端架构模式对比

#### 模式 A：MVC（Model-View-Controller）

```
View (Unity UI) ←→ Controller (C# 逻辑) ←→ Model (数据)
```

| 评估 | 说明 |
|------|------|
| 适合场景 | 小型 RPG，UI 交互为主 |
| 传奇适配 | ⭐⭐⭐⭐ 良好，UI 与逻辑分离清晰 |
| 缺点 | 大量 UI 回调时 Controller 膨胀 |

#### 模式 B：ECS（Entity-Component-System）

```
Entities → Components → Systems（纯数据驱动）
```

| 评估 | 说明 |
|------|------|
| 适合场景 | 大规模实体模拟（万人同屏） |
| 传奇适配 | ⭐⭐ 过度设计，传奇同时在线少 |
| 缺点 | 学习曲线陡，Unity DOTS 成熟度不足 |

#### 模式 C：FSM + 事件总线（推荐）

```
EventBus
  ├── NetworkEvent（收到消息）
  ├── GameStateEvent（状态切换）
  └── UIM event（UI 刷新）

State Machine
  ├── LobbyState
  ├── BattleState
  ├── ChatState
  └── GuildState
```

| 评估 | 说明 |
|------|------|
| 适合场景 | 传奇类状态明确的 MMORPG |
| 传奇适配 | ⭐⭐⭐⭐⭐ 完美匹配（登录→选服→进入游戏→战斗） |
| 优点 | 状态清晰、易于调试、热更新友好 |

### 3.4 渲染风格与技术栈

传奇类游戏的视觉定位决定技术选型：

| 风格 | Unity 方案 | UE5 方案 | 推荐 |
|------|-----------|---------|------|
| **经典低多边形**（还原 2001 年） | Standard Shader + 简单烘焙光 | 禁用 Nanite/Lumen，Lightmass 烘焙 | Unity 更轻 |
| **HD 重制版**（现代 PC） | URP + 烘焙光 + 简单 LOD | Lumen + Nanite + Niagara | UE5 更强 |
| **移动端优先** | URP Mobile 渲染路径 | 不适用（UE 移动端性能差） | Unity 胜出 |

**推荐**：经典低多边形 + URP（Unity）或 烘焙光照（UE5），性价比最高。

---

## 四、Unity 客户端详细设计

### 4.1 项目结构

```
LegendClient/
├── Assets/
│   ├── Scripts/
│   │   ├── Core/                    # 核心框架
│   │   │   ├── Network/
│   │   │   │   ├── TcpClient.cs           # TCP 连接管理
│   │   │   │   ├── MsgParser.cs           # 8 字节头部解析
│   │   │   │   ├── MsgRouter.cs           # 按 msgId 分发
│   │   │   │   ├── FrameDecoder.cs        # 粘包/拆包处理
│   │   │   │   └── Heartbeat.cs           # 心跳保活
│   │   │   ├── StateMachine/
│   │   │   │   ├── GameStateMachine.cs    # 全局状态机
│   │   │   │   ├── States/
│   │   │   │   │   ├── LobbyState.cs
│   │   │   │   │   ├── GameState.cs
│   │   │   │   │   └── DeadState.cs
│   │   │   │   └── EventBus.cs            # 事件总线
│   │   │   └── managers/
│   │   │       ├── SceneManager.cs        # 场景流式加载
│   │   │       ├── EntityMgr.cs           # 实体管理器
│   │   │       └── ObjectPool.cs          # 对象池
│   │   ├── Game/                    # 游戏逻辑
│   │   │   ├── Player/
│   │   │   │   ├── PlayerController.cs    # 本地玩家控制
│   │   │   │   ├── PlayerInput.cs         # 格子移动输入
│   │   │   │   └── PlayerStats.cs         # 本地属性缓存
│   │   │   ├── Combat/
│   │   │   │   ├── CombatSystem.cs        # 客户端战斗预测
│   │   │   │   ├── DamagePopup.cs         # 伤害数字
│   │   │   │   └── SkillManager.cs        # 技能 CD/UI
│   │   │   ├── Entity/
│   │   │   │   ├── EntityBase.cs          # 实体基类（玩家/怪物/NPC）
│   │   │   │   ├── MonsterAI.cs           # 本地怪物动画驱动
│   │   │   │   └── GroundDrop.cs          # 地面掉落物
│   │   │   ├── UI/
│   │   │   │   ├── HUD/
│   │   │   │   │   ├── HealthBar.cs
│   │   │   │   │   ├── MpBar.cs
│   │   │   │   │   ├── ExpBar.cs
│   │   │   │   │   └── Minimap.cs
│   │   │   │   ├── Chat/
│   │   │   │   │   └── ChatWindow.cs
│   │   │   │   ├── Inventory/
│   │   │   │   │   ├── InventoryUI.cs
│   │   │   │   │   └── EquipmentUI.cs
│   │   │   │   ├── Guild/
│   │   │   │   │   └── GuildWindow.cs
│   │   │   │   └── Login/
│   │   │   │       ├── LoginUI.cs
│   │   │   │       └── SelectServerUI.cs
│   │   │   └── Data/
│   │   │       ├── PlayerData.cs          # 本地玩家数据
│   │   │       ├── WorldChannelData.cs    # 世界频道缓存
│   │       └── └── DropData.cs            # 掉落缓存
│   ├── Prefabs/
│   │   ├── Entities/
│   │   │   ├── Player.prefab
│   │   │   ├── Monster_*.prefab
│   │   │   └── NPC.prefab
│   │   ├── UI/
│   │   │   ├── HUD.prefab
│   │   │   ├── ChatWindow.prefab
│   │   │   └── Inventory.prefab
│   │   └── VFX/
│   │       ├── Skill_*.prefab
│   │       └── DamageNumber.prefab
│   ├── Materials/
│   ├── Shaders/
│   ├── Animations/
│   └── Scripts.Generated/           # FlatBuffers 生成代码
│       ├── LegendMsg.cs              # 消息结构体
│       └── *._generated.cs
├── Packages/
│   └── manifest.json
├── ProjectSettings/
└── README.md
```

### 4.2 网络层设计（核心）

#### 4.2.1 TCP 连接管理

```csharp
// Core/Network/TcpClient.cs
public class TcpClient : MonoBehaviour
{
    private TcpListener _listener;
    private TcpClient _socket;
    private NetworkStream _stream;
    private byte[] _readBuffer = new byte[4096];
    private List<byte> _pendingData = new List<byte>();

    // 连接服务器
    public void Connect(string host, int port, string jwtToken)
    {
        _socket = new TcpClient(host, port);
        _stream = _socket.GetStream();
        // 发送认证消息（msgId=0x0003, payload=JWT）
        SendMessage(Protocol.kMsgAuthLogin, Encoding.UTF8.GetBytes(jwtToken));
    }

    // 异步读取循环
    private void ReadLoop()
    {
        while (_socket.Connected)
        {
            int bytes = _stream.Read(_readBuffer, 0, _readBuffer.Length);
            if (bytes == 0) break; // 连接断开
            _pendingData.AddRange(_readBuffer.AsSpan(0, bytes).ToArray());
            ProcessAvailableData();
        }
    }

    // 处理可用数据（粘包处理）
    private void ProcessAvailableData()
    {
        while (_pendingData.Count >= Protocol.HeaderSize)
        {
            // 读取头部
            int msgId = BitConverter.ToInt32(_pendingData.ToArray(), 0);
            int payloadLen = BitConverter.ToInt32(_pendingData.ToArray(), 4);

            if (_pendingData.Count < Protocol.HeaderSize + payloadLen)
                break; // 数据不完整，等待下一批

            // 提取消息
            byte[] payload = _pendingData.Slice(Protocol.HeaderSize, payloadLen).ToArray();
            _pendingData.RemoveRange(0, Protocol.HeaderSize + payloadLen);

            // 分发到路由
            MsgRouter.Instance.Dispatch(msgId, payload);
        }
    }

    public void SendMessage(int msgId, byte[] payload)
    {
        byte[] header = new byte[Protocol.HeaderSize];
        Buffer.BlockCopy(BitConverter.GetBytes(msgId), 0, header, 0, 4);
        Buffer.BlockCopy(BitConverter.GetBytes(payload.Length), 0, header, 4, 4);

        byte[] message = new byte[Protocol.HeaderSize + payload.Length];
        Buffer.BlockCopy(header, 0, message, 0, Protocol.HeaderSize);
        Buffer.BlockCopy(payload, 0, message, Protocol.HeaderSize, payload.Length);

        _stream.Write(message, 0, message.Length);
    }
}
```

#### 4.2.2 消息路由

```csharp
// Core/Network/MsgRouter.cs
public class MsgRouter
{
    public static MsgRouter Instance { get; private set; }

    private Dictionary<int, Action<byte[]>> _handlers = new();

    public void Register(int msgId, Action<byte[]> handler)
    {
        _handlers[msgId] = handler;
    }

    public void Dispatch(int msgId, byte[] payload)
    {
        if (_handlers.TryGetValue(msgId, out var handler))
            handler(payload);
        else
            Debug.LogWarning($"[MsgRouter] Unknown msgId: 0x{msgId:X4}");
    }
}
```

#### 4.2.3 与 SNC 协议的完整映射

```csharp
// Core/Network/Protocol.cs
public static class Protocol
{
    public const int HeaderSize = 8;
    public const int MaxPayload = 16 * 1024 * 1024; // 16MB

    // 控制消息
    public const int MsgHeartbeatPing  = 0x0001;
    public const int MsgHeartbeatPong  = 0x0002;
    public const int MsgAuthLogin      = 0x0003;  // 客户端→服务端：登录认证
    public const int MsgAuthResponse   = 0x0004;  // 服务端→客户端：登录结果

    // 客户端→服务端业务消息（对应服务端 0x1100~0x11FF）
    public const int MsgMove           = 0x1100;
    public const int MsgAttack         = 0x1101;
    public const int MsgNPCDialog      = 0x1102;
    public const int MsgChat           = 0x1103;
    public const int MsgPickup         = 0x1104;
    public const int MsgSkill          = 0x1105;
    public const int MsgGuildReq       = 0x1106;

    // 服务端→客户端响应消息（需要补充定义）
    public const int MsgPlayerSync     = 0x1200;  // 玩家属性同步
    public const int MsgPositionSync   = 0x1201;  // 位置同步（其他玩家/怪物）
    public const int MsgDamageResult   = 0x1202;  // 伤害结果
    public const int MsgDeathNotify    = 0x1203;  // 死亡通知
    public const int MsgDropNotify     = 0x1204;  // 掉落通知
    public const int MsgLevelUp        = 0x1205;  // 升级通知
    public const int MsgChatBroadcast  = 0x1206;  // 聊天广播
    public const int MsgGuildNotice    = 0x1207;  // 行会通知
    public const int MsgSceneEnter     = 0x1208;  // 进入场景
    public const int MsgSceneLeave     = 0x1209;  // 离开场景
}
```

### 4.3 核心系统设计

#### 4.3.1 状态机（GameStateMachine）

```csharp
// Core/StateMachine/GameStateMachine.cs
public class GameStateMachine : MonoBehaviour
{
    private IGameState _currentState;
    private Dictionary<Type, IGameState> _states = new();

    public void Register<T>() where T : IGameState, new()
    {
        _states[typeof(T)] = new T();
    }

    public void TransitionTo<T>() where T : IGameState
    {
        _currentState?.Exit();
        _currentState = _states[typeof(T)];
        _currentState.Enter();
    }

    public void Update()
    {
        _currentState?.Update();
    }
}

// 状态实现
public class LobbyState : IGameState
{
    public void Enter() { /* 显示登录 UI */ }
    public void Exit()  { /* 清理登录 UI */ }
    public void Update(){ /* 处理登录逻辑 */ }
}

public class GameState : IGameState
{
    public void Enter() { /* 加载场景、创建玩家 */ }
    public void Exit()  { /* 卸载场景 */ }
    public void Update(){ /* 处理游戏逻辑 */ }
}
```

#### 4.3.2 实体管理（EntityMgr）

```csharp
// Core/managers/EntityMgr.cs
public class EntityMgr : MonoBehaviour
{
    private Dictionary<int, EntityBase> _entities = new();
    private Dictionary<int, Queue<EntityBase>> _pool = new(); // 对象池

    // 创建实体（从服务器同步数据）
    public EntityBase CreateEntity(int entityId, EntityType type, Vector3 pos)
    {
        if (_entities.TryGetValue(entityId, out var existing))
            return existing;

        EntityBase entity = GetOrCreateFromPool(type);
        entity.Initialize(entityId, type, pos);
        _entities[entityId] = entity;
        return entity;
    }

    // 更新实体位置（服务器推送）
    public void UpdatePosition(int entityId, float x, float y)
    {
        if (_entities.TryGetValue(entityId, out var entity))
            entity.SetPosition(new Vector3(x, 0, y));
    }

    // 销毁实体
    public void DestroyEntity(int entityId)
    {
        if (_entities.TryGetValue(entityId, out var entity))
        {
            ReturnToPool(entity);
            _entities.Remove(entityId);
        }
    }

    // 清理不在视野内的实体（LOD 优化）
    public void CleanupOutOfView(Vector3 playerPos, float viewRange)
    {
        var toRemove = _entities.Values
            .Where(e => Vector3.Distance(e.Position, playerPos) > viewRange)
            .ToList();
        foreach (var e in toRemove)
            DestroyEntity(e.Id);
    }
}
```

#### 4.3.3 本地战斗预测（Client-Side Prediction）

传奇类游戏的战斗是**回合制/格子战斗**，不是实时 FPS，预测需求较低。但仍需处理服务器延迟感：

```csharp
// Game/Combat/CombatSystem.cs
public class CombatSystem : MonoBehaviour
{
    private Dictionary<int, float> _skillCooldowns = new();
    private List<DamagePrediction> _pendingPredictions = new();

    // 客户端发起攻击（本地预测）
    public void Attack(int targetId, int skillId)
    {
        // 1. 本地立即播放攻击动画（预测）
        PlayAttackAnimation(skillId);

        // 2. 发送请求到服务器
        var payload = BuildAttackPayload(targetId, skillId);
        TcpClient.Instance.SendMessage(Protocol.MsgAttack, payload);

        // 3. 记录预测（等待服务器确认）
        _pendingPredictions.Add(new DamagePrediction
        {
            TargetId = targetId,
            SkillId = skillId,
            PredictedTime = Time.time
        });
    }

    // 服务器确认伤害结果
    public void OnDamageResult(int targetId, int damage, bool isCritical)
    {
        // 移除预测
        _pendingPredictions.RemoveAll(p => p.TargetId == targetId);

        // 显示伤害数字
        ShowDamageNumber(targetId, damage, isCritical);

        // 更新本地属性（服务器权威）
        UpdateEntityStats(targetId, damage);
    }

    // 技能冷却管理
    public bool CanUseSkill(int skillId)
    {
        if (_skillCooldowns.TryGetValue(skillId, out var cd))
            return Time.time >= cd;
        return true;
    }

    public void StartCooldown(int skillId, float durationMs)
    {
        _skillCooldowns[skillId] = Time.time + durationMs / 1000f;
    }
}
```

### 4.4 UI 系统设计

#### 4.4.1 HUD 布局（传奇经典布局）

```
┌─────────────────────────────────────────────────────────┐
│  [ minimap ]              [ HP Bar ] [ MP Bar ] [ EXP ] │
│  (左上角)                 (右上角，经典布局)              │
│                                                          │
│                                                          │
│         [ 游戏主画面 - 角色/怪物/NPC ]                    │
│                                                          │
│                                                          │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────┐ │
│  │ 快捷技能栏  │  │  聊天窗口   │  │  任务/背包      │ │
│  │ (底部中央)  │  │  (左下角)   │  │  (右侧可折叠)   │ │
│  └─────────────┘  └─────────────┘  └─────────────────┘ │
└─────────────────────────────────────────────────────────┘
```

#### 4.4.2 关键 UI 组件

```csharp
// Game/UI/HUD/HealthBar.cs
public class HealthBar : MonoBehaviour
{
    private Slider _slider;
    private Text _text;

    public void UpdateStats(int currentHp, int maxHp, int currentMp, int maxMp)
    {
        _slider.value = (float)currentHp / maxHp;
        _text.text = $"{currentHp}/{maxHp}";

        // MP Bar 同步更新
        GetComponent<MpBar>().UpdateStats(currentMp, maxMp);
    }
}

// Game/UI/HUD/Minimap.cs
public class Minimap : MonoBehaviour
{
    private Renderer _renderer;
    private Texture2D _minimapTex;

    // 服务器推送地图数据时更新
    public void UpdateMap(int sceneId, List<EntityPos> entities)
    {
        // 绘制迷你地图（简化版：只显示附近实体）
        var sprite = RenderEntitiesToTexture(entities, _minimapTex);
        _renderer.material.mainTexture = sprite;
    }
}
```

### 4.5 输入系统（格子移动）

传奇的核心操作是**鼠标点击格子移动 + 快捷键技能**：

```csharp
// Game/Player/PlayerInput.cs
public class PlayerInput : MonoBehaviour
{
    [SerializeField] private float moveGridSize = 2.0f;

    private void Update()
    {
        // 左键点击移动
        if (Input.GetMouseButtonDown(0))
        {
            var hit = GetGridHit();
            if (hit.HasValue)
                SendMoveRequest(hit.Value.x, hit.Value.z);
        }

        // 右键攻击（对准鼠标指向的实体）
        if (Input.GetMouseButtonDown(1))
        {
            var target = GetTargetUnderCursor();
            if (target.HasValue)
                SendAttackRequest(target.Value.id, GetEquippedWeaponSkill());
        }

        // 数字键 1-9 释放技能
        for (int i = 1; i <= 9; i++)
        {
            if (Input.GetKeyDown(KeyCode.Alpha9 + i)) // 注意：Unity Alpha1=49
                SendSkillRequest(i, GetTargetForSkill(i));
        }
    }

    private void SendMoveRequest(float x, float z)
    {
        var payload = new BinaryWriter(new MemoryStream());
        payload.Write(x);
        payload.Write(z);
        TcpClient.Instance.SendMessage(Protocol.MsgMove, payload.ToArray());
    }

    private void SendAttackRequest(int targetId, int skillId)
    {
        var payload = new BinaryWriter(new MemoryStream());
        payload.Write(targetId);
        payload.Write(skillId);
        TcpClient.Instance.SendMessage(Protocol.MsgAttack, payload.ToArray());
    }
}
```

---

## 五、UE5 客户端设计（备选方案）

### 5.1 网络层架构

```unrealscript
// 自定义 NetChannel 替代默认 Replication
class UNetChannel_Legend : public UNetChannel
{
    virtual void SerializeNewOutgoingMessages(
        UNetConnection* Connection,
        FArchive& Ar) override;

    virtual void SerializeOptions(
        UNetConnection* Connection,
        FArchive& Ar) override;
};

// 传奇协议包头
struct FLegendPacketHeader
{
    uint32 MsgId;       // 消息 ID（与 SNC 0x1100 等对齐）
    uint32 PayloadLen;  // payload 长度
    // 总共 8 字节，与 SNC 协议完全一致
};
```

### 5.2 GAS 技能系统

```unrealscript
// 传奇技能映射到 GAS Ability
UCLASS()
class UAbility_MeleeAttack : public UGameplayAbility
{
    GENERATED_BODY()

protected:
    virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                 const FGameplayAbilityActorInfo* ActorInfo,
                                 const FGameplayAbilityActivationInfo ActivationInfo,
                                 const FGameplayEventData* TriggerEventData) override;

    // 攻击参数
    UPROPERTY(EditDefaultsOnly, Category = "Legend")
    int32 TargetEntityId = -1;

    UPROPERTY(EditDefaultsOnly, Category = "Legend")
    float AttackRange = 2.0f;

    UPROPERTY(EditDefaultsOnly, Category = "Legend")
    TSubclassOf<UGameplayEffect> DamageEffect;
};

// 属性集（对应玩家属性）
UCLASS()
class ULegendAttributeSet : public UAttributeSet
{
    GENERATED_BODY()

public:
    // HP/MP
    UPROPERTY(BlueprintReadOnly, Replicated, Category = "Legend|Stats")
    float Health;
    UPROPERTY(BlueprintReadOnly, Replicated, Category = "Legend|Stats")
    float MaxHealth;
    UPROPERTY(BlueprintReadOnly, Replicated, Category = "Legend|Stats")
    float Mana;
    UPROPERTY(BlueprintReadOnly, Replicated, Category = "Legend|Stats")
    float MaxMana;

    // 战斗属性
    UPROPERTY(BlueprintReadOnly, Replicated, Category = "Legend|Combat")
    float PhysicalAttack;
    UPROPERTY(BlueprintReadOnly, Replicated, Category = "Legend|Combat")
    float MagicAttack;
    UPROPERTY(BlueprintReadOnly, Replicated, Category = "Legend|Combat")
    float Defense;
    UPROPERTY(BlueprintReadOnly, Replicated, Category = "Legend|Combat")
    float MagicDefense;

    // 暴击
    UPROPERTY(BlueprintReadOnly, Replicated, Category = "Legend|Crit")
    float CriticalChance;
    UPROPERTY(BlueprintReadOnly, Replicated, Category = "Legend|Crit")
    float CriticalMultiplier;
};
```

### 5.3 实体管理（Actor 体系）

```unrealscript
// 传奇实体基类
UCLASS()
class ALegendEntity : public AActor
{
    GENERATED_BODY()

public:
    UPROPERTY(Replicated)
    int32 EntityId;

    UPROPERTY(Replicated)
    EEntityType EntityType; // PLAYER, MONSTER, NPC, PET

    UPROPERTY(ReplicatedUsing = OnRep_Position)
    FVector Location;

    UPROPERTY(ReplicatedUsing = OnRep_Hp)
    float CurrentHp;

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;

protected:
    UFUNCTION()
    void OnRep_Position();

    UFUNCTION()
    void OnRep_Hp();
};

// 玩家角色
UCLASS()
class ALegendPlayerCharacter : public ALegendEntity
{
    GENERATED_BODY()

public:
    UPROPERTY(VisibleAnywhere)
    USkeletalMeshComponent* Mesh;

    UPROPERTY(VisibleAnywhere)
    ULegendAbilitySystemComponent* ASC;

    UPROPERTY(VisibleAnywhere)
    UStateMachineComponent* StateComp;

    // 传奇特有：背包/装备槽
    UPROPERTY(Replicated)
    TArray<FItemSlot> EquipmentSlots; // 10 槽

    UPROPERTY(Replicated)
    TArray<FItemSlot> BagSlots;       // 30 槽

    // 格子移动输入
    UFUNCTION()
    void MoveToGrid FVector NewPos);

    UFUNCTION()
    void AttackTarget int32 TargetId, int32 SkillId);
};
```

### 5.4 UE5 客户端目录结构

```
LegendUE5/
├── Source/
│   ├── LegendClient/
│   │   ├── LegendClient.cpp        # 模块入口
│   │   ├── LegendClient.h
│   │   ├── Private/
│   │   │   ├── Network/
│   │   │   │   ├── LegendNetChannel.cpp
│   │   │   │   ├── LegendTcpConnection.cpp
│   │   │   │   └── LegendMsgRouter.cpp
│   │   │   ├── Game/
│   │   │   │   ├── LegendGameMode.cpp
│   │   │   │   ├── LegendPlayerController.cpp
│   │   │   │   ├── LegendPlayerCharacter.cpp
│   │   │   │   ├── LegendGameState.cpp
│   │   │   │   └── Abilities/
│   │   │   │       ├── LegendAbility.cpp
│   │   │   │       └── LegendAttributeSet.cpp
│   │   │   ├── UI/
│   │   │   │   ├── LegendHUD.cpp
│   │   │   │   ├── LegendHealthBar.cpp
│   │   │   │   └── LegendChatWindow.cpp
│   │   │   └── Managers/
│   │   │       ├── LegendEntityManager.cpp
│   │   │       └── LegendSceneManager.cpp
│   │   └── Public/
│   │       ├── Network/
│   │       ├── Game/
│   │       ├── UI/
│   │       └── Managers/
│   └── LegendClient.Target.cs
├── Content/
│   ├── Maps/
│   ├── Characters/
│   ├── UI/
│   └── Materials/
└── Config/
    ├── DefaultEngine.ini
    └── DefaultGame.ini
```

---

## 六、FlatBuffers Schema 设计

为客户端和服务端定义统一的 FlatBuffers schema，替代当前的 JSON 解析。

```flatbuffers
// legend.fbs — 传奇 MMO 消息 Schema

namespace LegendProto;

// ── 消息头（8 字节，与 SNC Protocol.h 对齐）────────────────
table MessageHeader {
    msg_id: int;      // int32，如 0x1100
    payload_len: int; // int32
}

// ── 移动消息（0x1100 C→S）─────────────────────────────────
table MoveRequest {
    player_id: int;   // int32
    target_x: float;  // 目标格子 X
    target_y: float;  // 目标格子 Z（传奇是 2D 格子）
    timestamp: long;  // 客户端时间戳（防重放）
}

// ── 移动响应（S→C，0x1201）────────────────────────────────
table PositionSync {
    entities: [EntityPos];
}

table EntityPos {
    entity_id: int;
    entity_type: EntityType;
    x: float;
    y: float;
    direction: FaceDirection;
    is_moving: bool;
}

enum EntityType : byte {
    Player = 0,
    Monster = 1,
    NPC = 2,
    Pet = 3,
    GroundDrop = 4,
}

enum FaceDirection : byte {
    South = 0,
    West = 1,
    North = 2,
    East = 3,
}

// ── 攻击消息（0x1101 C→S）─────────────────────────────────
table AttackRequest {
    attacker_id: int;
    target_id: int;
    skill_id: int;    // 0 = 普通攻击
    timestamp: long;
}

// ── 伤害结果（S→C，0x1202）────────────────────────────────
table DamageResult {
    target_id: int;
    damage: int;
    is_critical: bool;
    is_miss: bool;
    skill_id: int;
}

// ── 玩家属性同步（0x1200）─────────────────────────────────
table PlayerSync {
    player_id: int;
    name: string;
    class_type: ClassType;
    level: int;
    exp: long;
    hp: int;
    max_hp: int;
    mp: int;
    max_mp: int;
    pos_x: float;
    pos_y: float;
    equipment: [ItemSlot];
    skills: [SkillEntry];
}

enum ClassType : byte {
    Warrior = 0,
    Magician = 1,
    Taoist = 2,
}

table ItemSlot {
    slot_index: int;    // 0-9（装备槽）或 10-39（背包）
    item_id: int;
    count: int;
    is_equipped: bool;
}

table SkillEntry {
    skill_id: int;
    level: int;
    cooldown_remaining: long;  // ms
}

// ── 聊天消息（0x1103 C→S / 0x1206 S→C）───────────────────
table ChatRequest {
    player_id: int;
    channel: ChatChannel;  // WORLD=0, GUILD=1, WHISPER=2
    target_id: int;        // 私聊目标
    message: string;
}

enum ChatChannel : byte {
    World = 0,
    Guild = 1,
    Whisper = 2,
}

table ChatBroadcast {
    sender_id: int;
    sender_name: string;
    channel: ChatChannel;
    message: string;
    timestamp: long;
}

// ── 掉落消息（0x1104 C→S / 0x1204 S→C）───────────────────
table PickupRequest {
    player_id: int;
    drop_id: long;
}

table DropNotify {
    drop_id: long;
    entity_id: int;      // 在地面的实体 ID
    x: float;
    y: float;
    item_id: int;
    count: int;
    lifetime_ms: long;   // 剩余存活时间
}

// ── 死亡通知（0x1203）────────────────────────────────────
table DeathNotify {
    entity_id: int;
    killer_id: int;
    is_player: bool;
    respawn_delay_ms: long;
}

// ── 升级通知（0x1205）────────────────────────────────────
table LevelUpNotify {
    player_id: int;
    new_level: int;
    hp_restore: int;
    mp_restore: int;
    stats_changed: [StatChange];
}

table StatChange {
    stat_name: string;
    old_value: int;
    new_value: int;
}

// ── 根表（消息入口）──────────────────────────────────────
table Message {
    header: MessageHeader;
    // 根据 msg_id  union 选择 payload
    move_req: MoveRequest;
    move_resp: PositionSync;
    attack_req: AttackRequest;
    damage: DamageResult;
    player_sync: PlayerSync;
    chat_req: ChatRequest;
    chat_broadcast: ChatBroadcast;
    pickup_req: PickupRequest;
    drop_notify: DropNotify;
    death_notify: DeathNotify;
    level_up: LevelUpNotify;
}

root_type Message;
```

**编译命令**：
```bash
flatc --csharp --gen-object-api -o Assets/Scripts.Generated legend.fbs
```

---

## 七、落地方案与实施计划

### 7.1 分阶段实施

```
Phase 1（4周）→ 基础框架 + 网络层
Phase 2（4周）→ 核心玩法（移动/战斗/技能）
Phase 3（4周）→ 社交系统（聊天/行会/掉落）
Phase 4（4周）→ UI/UX 打磨 + 性能优化
Phase 5（2周）→ 联调测试 + 发布准备
```

### 7.2 Phase 1 详细任务（基础框架 + 网络层）

| 周次 | 任务 | 交付物 |
|------|------|--------|
| W1 | Unity 项目初始化 + FlatBuffers 工具链 | 空项目 + flatc 集成 |
| W1 | 实现 TcpClient + 粘包处理 | TcpClient.cs, FrameDecoder.cs |
| W2 | 实现 MsgRouter + 消息注册 | MsgRouter.cs, Protocol.cs |
| W2 | 实现 GameStateMachine + LobbyState | 状态机框架 + 登录界面 |
| W3 | JWT 登录流程对接 SNC AccountService | 登录 API + 登录 UI |
| W3 | 实现 PlayerInput（格子移动） | PlayerInput.cs |
| W4 | 实现 EntityMgr + 基础实体预制体 | EntityBase.prefab, EntityMgr.cs |
| W4 | 实现 PositionSync 处理（其他玩家/怪物移动） | 至少 2 个实体同步 |

### 7.3 Phase 2 详细任务（核心玩法）

| 周次 | 任务 | 交付物 |
|------|------|--------|
| W5 | 实现 CombatSystem（伤害计算 + 本地预测） | CombatSystem.cs, DamagePopup.cs |
| W5 | 实现技能系统（CD + 技能栏 UI） | SkillManager.cs, 技能快捷栏 |
| W6 | 实现怪物 AI 本地驱动（动画状态机） | MonsterAI.cs |
| W6 | 实现 HP/MP/EXP 条 UI | HealthBar, MpBar, ExpBar |
| W7 | 场景管理（比奇城/沃玛森林切换） | SceneManager.cs |
| W7 | 落地 FlatBuffers Schema 完整定义 | legend.fbs + 生成代码 |
| W8 | 服务端协议对齐（替换 JSON 为 FlatBuffers） | GameService.cpp 重构 |

### 7.4 Phase 3-5 概要

| Phase | 周期 | 主要内容 |
|-------|------|---------|
| Phase 3 | 4 周 | 聊天系统、行会 UI、掉落拾取、世界频道 |
| Phase 4 | 4 周 | HUD 打磨、迷你地图、音效、粒子特效 |
| Phase 5 | 2 周 | 网络压测、内存优化、灰度发布 |

---

## 八、风险评估

| 风险 | 等级 | 缓解措施 |
|------|------|---------|
| SNC 服务端 JSON 解析性能瓶颈 | 🔴 高 | Phase 2 同步替换为 FlatBuffers |
| Unity TCP 粘包处理错误导致断线 | 🟡 中 | 实现 LengthFieldBasedFrameDecoder 标准模式 |
| 服务器→客户端消息 ID 未定义 | 🟡 中 | 立即补充定义（见 Protocol.cs 中的 0x1200-0x1209） |
| FlatBuffers union 字段对齐错误 | 🟡 中 | 编写集成测试覆盖所有消息类型 |
| 格子移动在 3D 空间的浮点精度 | 🟢 低 | 使用整数格子坐标，渲染时转换为 float |
| 多场景切换时的实体状态丢失 | 🟢 低 | 场景切换时持久化玩家数据到 Redis |

---

## 九、与 SNC 服务端对齐检查清单

在开始客户端开发前，需确认以下服务端事项：

- [ ] **补充 S→C 消息 ID 定义**（[GameService.h](file:///c:/Users/10422/Documents/code/snc/mmo/include/snc/mmorpg/legend/GameService.h) 当前只有 C→S）
- [ ] **替换 JSON 解析为 FlatBuffers/Protobuf**（[GameService.cpp:274](file:///c:/Users/10422/Documents/code/snc/mmo/src/GameService.cpp#L274) 当前为手工 JSON 查找）
- [ ] **定义响应消息结构**（MoveResponse、AttackResponse、DamageResult 等）
- [ ] **实现客户端消息处理**（GameService 中各 DispatchXxx_ 函数需填充 payload 解析）
- [ ] **添加心跳处理**（当前 0x0001/0x0002 定义在 Protocol.h，但 GameService 未实现响应）
- [ ] **实现登录认证流程**（AccountService 需返回 JWT + 玩家初始数据）

---

## 十、结论

**推荐使用 Unity 2022 LTS + 自研 TCP 网络层 + FlatBuffers 序列化**，理由如下：

1. **协议对齐成本最低**：8 字节头部与 SNC 完全一致，只需实现 FrameDecoder
2. **开发效率高**：C# 上手快，Unity 编辑器迭代快，适合快速原型
3. **移动端友好**：传奇类目标用户广泛，移动端覆盖必要
4. **性能足够**：传奇类游戏实体数量少（单场景 < 200），Unity URP 轻松应对
5. **与 SNC 生态兼容**：可同时使用现有的 HotUpdateMgr 思路做 Lua 热更（如有需要）

UE5 仅在以下情况推荐：团队有强 C++ 背景、目标仅为 PC/主机、需要 Nanite/Lumen 高端画质。
