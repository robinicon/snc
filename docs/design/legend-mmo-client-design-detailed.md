# 传奇类 MMORPG 客户端详细设计文档

**日期**：2026-10-15
**状态**：详细设计
**配套服务器**：SNC C++ 框架（[mmorpg-legend-v2.1-plan.md](file:///c:/Users/10422/Documents/code/snc/docs/design/mmorpg-legend-v2.1-plan.md)）
**配套概览文档**：[legend-mmo-client-design.md](file:///c:/Users/10422/Documents/code/snc/docs/design/legend-mmo-client-design.md)

---

## 一、概述

本文档为传奇类 MMORPG Unity 客户端的详细设计，直接指导开发实现。

**技术栈**：Unity 2022 LTS + C# + FlatBuffers + 自研 TCP 网络层

**与 SNC 服务端对齐**：
- 协议格式：8 字节头部 `[msgId int32][payloadLen int32]`（参见 [Protocol.h](file:///c:/Users/10422/Documents/code/snc/include/snc/base/Protocol.h)）
- 消息 ID 空间：客户端→Gate = 0x1000-0x1FFF
- 序列化：FlatBuffers（替代当前 GameService.cpp 中的 JSON 手工解析）

---

## 二、项目结构

```
LegendClient/
├── Assets/
│   ├── Scripts/
│   │   ├── Core/
│   │   │   ├── Network/
│   │   │   │   ├── TcpClient.cs           # TCP 连接管理
│   │   │   │   ├── FrameDecoder.cs        # 粘包/拆包处理
│   │   │   │   ├── MsgRouter.cs           # 按 msgId 分发
│   │   │   │   ├── Heartbeat.cs           # 心跳保活
│   │   │   │   └── Protocol.cs            # 协议常量（与 SNC 对齐）
│   │   │   ├── StateMachine/
│   │   │   │   ├── IGameState.cs          # 状态接口
│   │   │   │   ├── GameStateMachine.cs    # 全局状态机
│   │   │   │   └── States/
│   │   │   │       ├── LobbyState.cs      # 登录大厅
│   │   │   │       ├── SelectServerState.cs # 选服
│   │   │   │       ├── GameState.cs       # 游戏中
│   │   │   │       └── DeadState.cs       # 死亡等待复活
│   │   │   ├── EventBus/
│   │   │   │   └── EventBus.cs            # 事件总线
│   │   │   └── Managers/
│   │   │       ├── EntityManager.cs       # 实体管理器
│   │   │       ├── SceneManager.cs        # 场景管理
│   │   │       └── ObjectPool.cs          # 对象池
│   │   ├── Game/
│   │   │   ├── Player/
│   │   │   │   ├── PlayerController.cs    # 本地玩家控制
│   │   │   │   ├── PlayerInput.cs         # 格子移动输入
│   │   │   │   └── PlayerStats.cs         # 本地属性缓存
│   │   │   ├── Combat/
│   │   │   │   ├── CombatSystem.cs        # 战斗系统（伤害计算+预测）
│   │   │   │   ├── DamagePopup.cs         # 伤害数字
│   │   │   │   ├── SkillManager.cs        # 技能 CD/UI
│   │   │   │   └── SkillDatabase.cs       # 技能数据表
│   │   │   ├── Entity/
│   │   │   │   ├── EntityBase.cs          # 实体基类
│   │   │   │   ├── MonsterAI.cs           # 本地怪物动画驱动
│   │   │   │   ├── NPC.cs                 # NPC 交互
│   │   │   │   └── GroundDrop.cs          # 地面掉落物
│   │   │   ├── UI/
│   │   │   │   ├── HUD/
│  │   │   │   │   ├── HealthBar.cs
│   │   │   │   │   ├── MpBar.cs
│   │   │   │   │   ├── ExpBar.cs
│   │   │   │   │   ├── Minimap.cs
│   │   │   │   │   └── StatusBar.cs
│   │   │   │   ├── Chat/
│   │   │   │   │   ├── ChatWindow.cs
│   │   │   │   │   └── ChatChannel.cs
│   │   │   │   ├── Inventory/
│   │   │   │   │   ├── InventoryUI.cs
│   │   │   │   │   ├── EquipmentUI.cs
│   │   │   │   │   └── ItemTooltip.cs
│   │   │   │   ├── Guild/
│   │   │   │   │   └── GuildWindow.cs
│   │   │   │   └── Login/
│   │   │   │       ├── LoginUI.cs
│   │   │   │       └── SelectServerUI.cs
│   │   │   └── Data/
│   │   │       ├── PlayerData.cs
│   │   │       ├── WorldChannelData.cs
│   │   │       └── DropData.cs
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
│   └── Scripts.Generated/
│       └── LegendMsg.cs          # FlatBuffers 生成代码
├── Packages/
├── ProjectSettings/
└── README.md
```

---

## 三、网络层详细设计（核心）

### 3.1 Protocol.cs — 协议常量（与 SNC 完全对齐）

**对应服务端文件**：[Protocol.h](file:///c:/Users/10422/Documents/code/snc/include/snc/base/Protocol.h)、[GameService.h](file:///c:/Users/10422/Documents/code/snc/mmo/include/snc/mmorpg/legend/GameService.h)

```csharp
// Core/Network/Protocol.cs
using System;

/// <summary>
/// 与 SNC 服务端 Protocol.h / GameService.h 完全对齐的协议常量
/// 服务器端: Protocol.h (0x0001-0x0002, 0xFEED, 0x1000-0x4FFF)
///           GameService.h (0x1100-0x1106 C→S, 0x2000-0x2100 网关转发)
/// </summary>
public static class Protocol
{
    // ── 协议基础常量 ──────────────────────────────────────────
    public const int HeaderSize   = 8;   // 4B msgId + 4B payloadLen
    public const int MaxPayload   = 16 * 1024 * 1024; // 16MB（与 kMaxPayloadSize 一致）

    // ── 控制消息（定义于 Protocol.h）─────────────────────────
    public const int MsgPing      = 0x0001;   // 心跳 Ping（客户端→Gate）
    public const int MsgPong      = 0x0002;   // 心跳 Pong（Gate→客户端）
    public const int MsgRpcCtrl   = 0xFEED;   // RPC 控制消息（预留）

    // ── 认证消息（客户端→服务端，需补充定义）─────────────────
    public const int MsgAuthLogin     = 0x1000; // 登录请求（含 JWT token）
    public const int MsgAuthResponse  = 0x1001; // 登录结果（含玩家初始数据）
    public const int MsgKeepAlive     = 0x1002; // 自定义心跳（应用层备用）

    // ── 传奇业务消息 C→S（定义于 GameService.h）─────────────
    public const int MsgClientMove        = 0x1100;
    public const int MsgClientAttack      = 0x1101;
    public const int MsgClientNPCDialog   = 0x1102;
    public const int MsgClientChat        = 0x1103;
    public const int MsgClientPickup      = 0x1104;
    public const int MsgClientSkill       = 0x1105;
    public const int MsgClientGuildReq    = 0x1106;

    // ── 传奇业务消息 S→C（需服务端补充定义，分配 0x1200-0x1209）
    public const int MsgServerPlayerSync   = 0x1200; // 玩家属性全量同步
    public const int MsgServerPosSync      = 0x1201; // 实体位置同步（批量）
    public const int MsgServerDamageResult = 0x1202; // 伤害结果
    public const int MsgServerDeathNotify  = 0x1203; // 死亡通知
    public const int MsgServerDropNotify   = 0x1204; // 掉落物刷新
    public const int MsgServerLevelUp      = 0x1205; // 升级通知
    public const int MsgServerChatBroadcast= 0x1206; // 聊天广播
    public const int MsgServerGuildNotice  = 0x1207; // 行会通知
    public const int MsgServerSceneEnter   = 0x1208; // 进入场景
    public const int MsgServerSceneLeave   = 0x1209; // 离开场景
    public const int MsgServerMoveResponse = 0x120A; // 移动响应（确认/拒绝）
    public const int MsgServerError        = 0x120B; // 通用错误提示

    // ── 端点地址（配置化，从配置文件读取）──────────────────
    public const string DefaultHost = "127.0.0.1";
    public const int      DefaultPort = 8888;
}
```

### 3.2 TcpClient.cs — TCP 连接管理

```csharp
// Core/Network/TcpClient.cs
using System;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using UnityEngine;

/// <summary>
/// TCP 客户端管理，负责连接、发送、异步读取和重连。
/// 与 SNC TcpServer/TcpConnection 对应（见 include/snc/core/TcpServer.h）。
/// </summary>
public class TcpClient : MonoBehaviour
{
    public static TcpClient Instance { get; private set; }

    private TcpClient _socket;
    private NetworkStream _stream;
    private bool _connected;
    private CancellationTokenSource _cts;
    private Thread _readThread;
    private object _sendLock = new object();

    // 对外事件
    public Action OnConnected;
    public Action OnDisconnected;
    public Action<string> OnError;

    private void Awake()
    {
        if (Instance != null && Instance != this) { Destroy(gameObject); return; }
        Instance = this;
    }

    // ── 连接 ────────────────────────────────────────────────
    public void Connect(string host, int port)
    {
        if (_connected) return;

        _cts = new CancellationTokenSource();
        _socket = new TcpClient();

        // 异步连接
        _socket.BeginConnect(host, port, OnConnectResult, _socket);
    }

    private void OnConnectResult(IAsyncResult ar)
    {
        try
        {
            _socket.EndConnect(ar);
            _stream = _socket.GetStream();
            _connected = true;

            Debug.Log($"[TcpClient] Connected to {_socket.Client.RemoteEndPoint}");
            OnConnected?.Invoke();

            // 启动异步读取线程
            _readThread = new Thread(ReadLoop) { IsBackground = true };
            _readThread.Start();
        }
        catch (Exception e)
        {
            _connected = false;
            Debug.LogError($"[TcpClient] Connect failed: {e.Message}");
            OnError?.Invoke(e.Message);
            TryReconnect();
        }
    }

    // ── 读取循环（阻塞式，运行在独立线程）───────────────────
    private void ReadLoop()
    {
        var decoder = new FrameDecoder();

        while (_connected && !_cts.Token.IsCancellationRequested)
        {
            try
            {
                byte[] buffer = new byte[4096];
                int bytesReceived = _stream.Read(buffer, 0, buffer.Length);

                if (bytesReceived == 0)
                {
                    Debug.LogWarning("[TcpClient] Server closed connection");
                    break;
                }

                // 交给 FrameDecoder 处理粘包
                var frames = decoder.Feed(buffer, bytesReceived);
                foreach (var frame in frames)
                {
                    // 在主线程上分发（通过 Action 队列）
                    InvokeOnMainThread(() => DispatchFrame(frame));
                }
            }
            catch (Exception e)
            {
                Debug.LogError($"[TcpClient] Read error: {e.Message}");
                break;
            }
        }

        Disconnect();
    }

    // ── 发送消息 ────────────────────────────────────────────
    /// <summary>
    /// 构造 8 字节头部 + payload，发送二进制消息。
    /// 与 SNC Msg::Create(msgId, payload, len) 对应（见 include/snc/core/Msg.h）。
    /// </summary>
    public void SendMessage(int msgId, byte[] payload)
    {
        if (!_connected) { Debug.LogWarning("[TcpClient] Not connected, drop msg 0x" + msgId.ToString("X4")); return; }

        // 校验 payload 长度（与 kMaxPayloadSize 对齐）
        if (payload.Length > Protocol.MaxPayload)
        {
            Debug.LogError($"[TcpClient] Payload too large: {payload.Length}");
            return;
        }

        try
        {
            // 主机字节序（与 SNC 一致，见 Msg.h 注释）
            byte[] header = new byte[Protocol.HeaderSize];
            WriteInt32HostOrder(msgId, header, 0);
            WriteInt32HostOrder(payload.Length, header, 4);

            lock (_sendLock)
            {
                _stream.Write(header, 0, Protocol.HeaderSize);
                _stream.Write(payload, 0, payload.Length);
                _stream.Flush();
            }
        }
        catch (Exception e)
        {
            Debug.LogError($"[TcpClient] Send error: {e.Message}");
            Disconnect();
        }
    }

    // ── 断开 ────────────────────────────────────────────────
    public void Disconnect()
    {
        _connected = false;
        _cts?.Cancel();

        try { _stream?.Close(); } catch { }
        try { _socket?.Close(); } catch { }

        OnDisconnected?.Invoke();
    }

    // ── 重连（指数退避）──────────────────────────────────────
    private void TryReconnect()
    {
        Invoke(nameof(ReconnectAttempt), 3f);
    }

    private void ReconnectAttempt()
    {
        if (!_connected) Connect(Protocol.DefaultHost, Protocol.DefaultPort);
    }

    // ── 辅助 ────────────────────────────────────────────────
    private void InvokeOnMainThread(Action action)
    {
        StartCoroutine(InvokeCoroutine(action));
    }

    private System.Collections.IEnumerator InvokeCoroutine(Action action)
    {
        yield return null;
        action?.Invoke();
    }

    // 主机字节序写入（Unity 运行在 little-endian，与服务端一致）
    private static void WriteInt32HostOrder(int value, byte[] buffer, int offset)
    {
        BitConverter.GetBytes(IPAddress.HostToNetworkOrder(value))
                   .CopyTo(buffer, offset);
    }
}
```

### 3.3 FrameDecoder.cs — 粘包/拆包处理

```csharp
// Core/Network/FrameDecoder.cs
using System.Collections.Generic;

/// <summary>
/// LengthFieldBasedFrameDecoder：处理 TCP 粘包/拆包。
/// 协议格式：[msgId:int32(4B)][payloadLen:int32(4B)][payload:payloadLen bytes]
/// 与 SNC TcpConnection::handleInput_() 逻辑对称（见 src/core/TcpConnection.cpp）。
/// </summary>
public class FrameDecoder
{
    private readonly List<byte> _buffer = new List<byte>(4096);

    /// <summary>
    /// 喂入新读取的字节，返回完整帧列表（可能多帧）。
    /// </summary>
    /// <param name="data">从 Stream.Read 读取的字节数组</param>
    /// <param name="length">有效字节数</param>
    public List<byte[]> Feed(byte[] data, int length)
    {
        _buffer.AddRange(data, 0, length);
        var frames = new List<byte[]>();

        while (CanReadFrame())
        {
            // 读取头部
            int msgId   = ReadInt32(_buffer, 0);
            int payloadLen = ReadInt32(_buffer, 4);
            int frameSize = Protocol.HeaderSize + payloadLen;

            // 校验长度（防恶意包）
            if (payloadLen < 0 || payloadLen > Protocol.MaxPayload)
            {
                Debug.LogError($"[FrameDecoder] Invalid payloadLen={payloadLen}, msgId=0x{msgId:X4}");
                _buffer.Clear();
                return frames;
            }

            if (_buffer.Count < frameSize)
                break; // 数据不完整，等待下一批

            // 提取 payload
            byte[] payload = _buffer.GetRange(Protocol.HeaderSize, payloadLen).ToArray();
            _buffer.RemoveRange(0, frameSize);

            frames.Add(payload);
        }

        return frames;
    }

    private bool CanReadFrame()
    {
        return _buffer.Count >= Protocol.HeaderSize;
    }

    private static int ReadInt32(List<byte> buf, int offset)
    {
        return System.BitConverter.ToInt32(buf.ToArray(), offset);
    }
}
```

### 3.4 MsgRouter.cs — 消息路由

```csharp
// Core/Network/MsgRouter.cs
using System;
using System.Collections.Generic;
using UnityEngine;

/// <summary>
/// 消息路由器：按 msgId 将 payload 分发给对应处理器。
/// 对应 SNC GateService 的分发逻辑（见 mmo/src/GateService.cpp）。
/// </summary>
public class MsgRouter : MonoBehaviour
{
    public static MsgRouter Instance { get; private set; }

    private readonly Dictionary<int, Action<byte[]>> _handlers = new Dictionary<int, Action<byte[]>>();

    private void Awake()
    {
        if (Instance != null && Instance != this) { Destroy(gameObject); return; }
        Instance = this;
    }

    public void Register(int msgId, Action<byte[]> handler)
    {
        _handlers[msgId] = handler;
    }

    public void Unregister(int msgId)
    {
        _handlers.Remove(msgId);
    }

    /// <summary>
    /// 由 TcpClient 调用，处理解码后的 payload。
    /// </summary>
    public void Dispatch(int msgId, byte[] payload)
    {
        if (_handlers.TryGetValue(msgId, out var handler))
        {
            try { handler(payload); }
            catch (Exception e)
            {
                Debug.LogError($"[MsgRouter] Handler exception for 0x{msgId:X4}: {e}");
            }
        }
        else
        {
            Debug.LogWarning($"[MsgRouter] Unknown msgId: 0x{msgId:X4}, payloadLen={payload.Length}");
        }
    }

    /// <summary>
    /// TcpClient 读取线程调用此方法分发完整帧。
    /// </summary>
    public void HandleFrame(byte[] frame)
    {
        if (frame.Length < Protocol.HeaderSize) return;

        int msgId   = System.BitConverter.ToInt32(frame, 0);
        int payloadLen = System.BitConverter.ToInt32(frame, 4);
        byte[] payload = new byte[payloadLen];
        System.Array.Copy(frame, Protocol.HeaderSize, payload, 0, payloadLen);

        Dispatch(msgId, payload);
    }
}
```

### 3.5 Heartbeat.cs — 心跳保活

```csharp
// Core/Network/Heartbeat.cs
using System.Collections;
using UnityEngine;

/// <summary>
/// 心跳保活模块，定期发送 Ping（0x0001），检测 Pong（0x0002）。
/// 对应 SNC Protocol.h 中 kMsgPing/kMsgPong 定义。
/// </summary>
public class Heartbeat : MonoBehaviour
{
    [SerializeField] private float _interval = 30f;
    [SerializeField] private float _timeout  = 90f;

    private float _lastPongTime;
    private bool  _active;

    public void StartHeartbeat()
    {
        _active = true;
        _lastPongTime = Time.time;
        StartCoroutine(PingLoop());
    }

    public void StopHeartbeat()
    {
        _active = false;
        StopAllCoroutines();
    }

    private IEnumerator PingLoop()
    {
        while (_active)
        {
            yield return new WaitForSeconds(_interval);
            if (_active)
                TcpClient.Instance.SendMessage(Protocol.MsgPing, new byte[0]);
        }
    }

    /// <summary>
    /// 收到 Pong 时调用，重置超时计时器。
    /// </summary>
    public void OnPong()
    {
        _lastPongTime = Time.time;
    }

    private void Update()
    {
        if (!_active) return;
        if (Time.time - _lastPongTime > _timeout)
        {
            Debug.LogWarning("[Heartbeat] Timeout, reconnecting...");
            TcpClient.Instance.Disconnect();
            TcpClient.Instance.Connect(Protocol.DefaultHost, Protocol.DefaultPort);
        }
    }
}
```

### 3.6 消息注册表（初始化时绑定）

```csharp
// Core/Network/NetworkManager.cs
using UnityEngine;

public class NetworkManager : MonoBehaviour
{
    private void Start()
    {
        var router = MsgRouter.Instance;
        var hb     = FindObjectOfType<Heartbeat>();

        // ── 服务端→客户端消息（S→C）─────────────────────────
        router.Register(Protocol.MsgPong,         p => hb?.OnPong());
        router.Register(Protocol.MsgAuthResponse, p => OnAuthResponse(p));
        router.Register(Protocol.MsgServerPlayerSync, p => OnPlayerSync(p));
        router.Register(Protocol.MsgServerPosSync,    p => OnPosSync(p));
        router.Register(Protocol.MsgServerDamageResult, p => OnDamageResult(p));
        router.Register(Protocol.MsgServerDeathNotify,  p => OnDeathNotify(p));
        router.Register(Protocol.MsgServerDropNotify,   p => OnDropNotify(p));
        router.Register(Protocol.MsgServerLevelUp,      p => OnLevelUp(p));
        router.Register(Protocol.MsgServerChatBroadcast,p => OnChatBroadcast(p));
        router.Register(Protocol.MsgServerGuildNotice,  p => OnGuildNotice(p));
        router.Register(Protocol.MsgServerSceneEnter,   p => OnSceneEnter(p));
        router.Register(Protocol.MsgServerSceneLeave,   p => OnSceneLeave(p));
        router.Register(Protocol.MsgServerMoveResponse, p => OnMoveResponse(p));
        router.Register(Protocol.MsgServerError,        p => OnError(p));

        // ── 连接回调 ──────────────────────────────────────
        TcpClient.Instance.OnConnected  += () => {
            Debug.Log("[Network] Connected, starting heartbeat");
            hb?.StartHeartbeat();
            TransitionToState<ESelectServerState>();
        };
        TcpClient.Instance.OnDisconnected += () => {
            hb?.StopHeartbeat();
            Debug.LogWarning("[Network] Disconnected");
        };
    }

    private void OnAuthResponse(byte[] payload)
    {
        // 解析 MsgAuthResponse（0x1001）
        // payload: [jwtToken:string] 或 [error:string]
        var token = System.Text.Encoding.UTF8.GetString(payload);
        PlayerPrefs.SetString("jwt_token", token);
        PlayerPrefs.Save();
        Debug.Log("[Network] Auth response received");
    }

    // ... 其他 handler（见第四节）
}
```

---

## 四、核心系统详细设计

### 4.1 状态机（FSM）

```csharp
// Core/StateMachine/IGameState.cs
using UnityEngine;

public interface IGameState
{
    void Enter();
    void Exit();
    void Update();
}

// Core/StateMachine/GameStateMachine.cs
using System;
using System.Collections.Generic;

public class GameStateMachine : MonoBehaviour
{
    private IGameState _current;
    private readonly Dictionary<Type, IGameState> _states = new();

    public void Register<T>(T state) where T : IGameState => _states[typeof(T)] = state;

    public void TransitionTo<T>() where T : IGameState
    {
        var next = _states[typeof(T)];
        _current?.Exit();
        _current = next;
        _current.Enter();
        Debug.Log($"[FSM] -> {typeof(T).Name}");
    }

    private void Update() => _current?.Update();
}

// ── 状态实现 ───────────────────────────────────────────────
// Core/StateMachine/States/LobbyState.cs
public class LobbyState : IGameState
{
    public void Enter()  => LoginUI.Instance.Show();
    public void Exit()   => LoginUI.Instance.Hide();
    public void Update()
    {
        if (LoginUI.Instance.IsLoggedIn)
            FindObjectOfType<GameStateMachine>()
                .TransitionTo<SelectServerState>();
    }
}

// Core/StateMachine/States/SelectServerState.cs
public class SelectServerState : IGameState
{
    public void Enter()  => SelectServerUI.Instance.Show();
    public void Exit()   => SelectServerUI.Instance.Hide();
    public void Update()
    {
        if (SelectServerUI.Instance.ServerSelected)
            FindObjectOfType<GameStateMachine>()
                .TransitionTo<GameState>();
    }
}

// Core/StateMachine/States/GameState.cs
public class GameState : IGameState
{
    public void Enter()  => GameSceneManager.Instance.LoadScene("Map_Bicheng");
    public void Exit()   => GameSceneManager.Instance.UnloadScene();
    public void Update() { /* 游戏主循环 */ }
}

// Core/StateMachine/States/DeadState.cs
public class DeadState : IGameState
{
    private float _respawnTimer;
    public void Enter()  => _respawnTimer = 5f;
    public void Exit()   { }
    public void Update()
    {
        _respawnTimer -= Time.deltaTime;
        if (_respawnTimer <= 0)
            FindObjectOfType<GameStateMachine>()
                .TransitionTo<GameState>();
    }
}
```

### 4.2 实体管理（EntityManager）

```csharp
// Core/Managers/EntityManager.cs
using System.Collections.Generic;
using UnityEngine;

public class EntityManager : MonoBehaviour
{
    // entityId → EntityBase（运行时实体）
    private readonly Dictionary<int, EntityBase> _entities = new();
    // 对象池：type → Queue<EntityBase>
    private readonly Dictionary<int, Queue<EntityBase>> _pools = new();

    /// <summary>
    /// 创建或更新实体（服务器推送 EntitySync 时使用）
    /// 对应 SNC ServerToClient::EntitySync（mmorpg-legend-v2.1-plan.md §3.3）
    /// </summary>
    public EntityBase GetOrCreate(int entityId, EntityType type, Vector3 pos)
    {
        if (_entities.TryGetValue(entityId, out var existing))
            return existing;

        var entity = GetFromPool(type);
        entity.Initialize(entityId, type, pos);
        _entities[entityId] = entity;
        return entity;
    }

    public void UpdatePosition(int entityId, float x, float z)
    {
        if (_entities.TryGetValue(entityId, out var e))
            e.MoveTo(new Vector3(x, 0, z));
    }

    public void RemoveEntity(int entityId)
    {
        if (_entities.TryGetValue(entityId, out var e))
        {
            ReturnToPool(e);
            _entities.Remove(entityId);
        }
    }

    // 视野管理：只保留视野内实体
    public void CleanupOutOfView(Vector3 playerPos, float viewRadius)
    {
        var toRemove = new List<int>();
        foreach (var kv in _entities)
        {
            if (kv.Key == PlayerData.LocalEntityId) continue; // 不删除自己
            if (Vector3.Distance(kv.Value.Position, playerPos) > viewRadius)
                toRemove.Add(kv.Key);
        }
        foreach (var id in toRemove) RemoveEntity(id);
    }

    private EntityBase GetFromPool(EntityType type)
    {
        if (!_pools.TryGetValue((int)type, out var q) || q.Count == 0)
            return Instantiate prefabByType[type]; // 从 Resources 加载
        return q.Dequeue();
    }

    private void ReturnToPool(EntityBase entity)
    {
        entity.Deactivate();
        int key = (int)entity.Type;
        if (!_pools.ContainsKey(key)) _pools[key] = new Queue<EntityBase>();
        _pools[key].Enqueue(entity);
    }
}

public enum EntityType { Player = 0, Monster = 1, NPC = 2, Pet = 3, GroundDrop = 4 }
```

### 4.3 格子移动与输入（PlayerInput）

传奇的核心操作：**左键点击格子移动，右键攻击，数字键 1-9 释放技能**。

```csharp
// Game/Player/PlayerInput.cs
using UnityEngine;

public class PlayerInput : MonoBehaviour
{
    [SerializeField] private float _gridSize = 2.0f; // 格子大小（世界单位）

    private PlayerController _controller;

    private void Start() => _controller = FindObjectOfType<PlayerController>();

    private void Update()
    {
        HandleMovement();
        HandleAttack();
        HandleSkills();
    }

    // ── 左键点击格子移动 ──────────────────────────────────────
    private void HandleMovement()
    {
        if (!Input.GetMouseButtonDown(0)) return;

        var ray = Camera.main.ScreenPointToRay(Input.mousePosition);
        if (Physics.Raycast(ray, out var hit))
        {
            //  Snap 到最近格子中心
            float gx = Mathf.Round(hit.point.x / _gridSize) * _gridSize;
            float gz = Mathf.Round(hit.point.z / _gridSize) * _gridSize;

            _controller.SendMoveRequest(gx, gz);
        }
    }

    // ── 右键攻击 ──────────────────────────────────────────────
    private void HandleAttack()
    {
        if (!Input.GetMouseButtonDown(1)) return;

        var ray = Camera.main.ScreenPointToRay(Input.mousePosition);
        if (Physics.Raycast(ray, out var hit))
        {
            var target = hit.collider.GetComponent<EntityBase>();
            if (target != null && target.CanBeAttacked())
                _controller.SendAttackRequest(target.EntityId, 0); // 0 = 普通攻击
        }
    }

    // ── 数字键 1-9 释放技能 ───────────────────────────────────
    private void HandleSkills()
    {
        for (int i = 0; i < 9; i++)
        {
            if (Input.GetKeyDown((KeyCode)('1' + i)))
            {
                int skillId = GetSkillIdBySlot(i);
                if (skillId > 0)
                {
                    var target = GetTargetUnderCursor();
                    _controller.SendSkillRequest(skillId, target?.EntityId ?? -1);
                }
            }
        }
    }

    private int GetSkillIdBySlot(int slot)
    {
        // 读取 SkillDatabase，按槽位返回 skillId
        return SkillDatabase.Instance.GetSkillBySlot(slot);
    }

    private EntityBase GetTargetUnderCursor()
    {
        var ray = Camera.main.ScreenPointToRay(Input.mousePosition);
        if (Physics.Raycast(ray, out var hit))
            return hit.collider.GetComponent<EntityBase>();
        return null;
    }
}
```

### 4.4 战斗系统（CombatSystem）

传奇战斗是**服务器权威 + 客户端预测**模式：客户端发起攻击请求，服务器计算伤害，返回结果。

```csharp
// Game/Combat/CombatSystem.cs
using System.Collections.Generic;
using UnityEngine;

public class CombatSystem : MonoBehaviour
{
    // 本地技能 CD 缓存（服务器权威，此处仅做 UI 展示优化）
    private readonly Dictionary<int, float> _skillCdEndTimes = new();

    // 待服务器确认的伤害预测
    private readonly List<PendingDamage> _pendingPredictions = new();

    // ── 发起攻击（客户端→服务端 0x1101）──────────────────────
    public void Attack(int targetId, int skillId = 0)
    {
        if (!CanUseSkill(skillId)) return;

        // 1. 本地立即播放攻击动画（预测，提升手感）
        PlayAttackAnimation(skillId);

        // 2. 发送攻击请求
        var payload = BuildAttackPayload(targetId, skillId);
        TcpClient.Instance.SendMessage(Protocol.MsgClientAttack, payload);

        // 3. 记录预测
        _pendingPredictions.Add(new PendingDamage { TargetId = targetId, SkillId = skillId });

        // 4. 启动本地 CD
        if (skillId > 0) StartLocalCooldown(skillId);
    }

    // ── 服务器返回伤害结果（0x1202）──────────────────────────
    public void OnDamageResult(byte[] payload)
    {
        // 解析 DamageResult：[targetId:int32][damage:int32][isCritical:bool][skillId:int32]
        int targetId   = System.BitConverter.ToInt32(payload, 0);
        int damage     = System.BitConverter.ToInt32(payload, 4);
        bool isCrit    = payload[8] != 0;
        int skillId    = System.BitConverter.ToInt32(payload, 9);

        // 移除预测
        _pendingPredictions.RemoveAll(p => p.TargetId == targetId);

        // 显示伤害数字
        ShowDamageNumber(targetId, damage, isCrit);
    }

    // ── 技能 CD 管理 ─────────────────────────────────────────
    public bool CanUseSkill(int skillId)
    {
        if (skillId <= 0) return true; // 普通攻击无 CD
        if (_skillCdEndTimes.TryGetValue(skillId, out var endTime))
            return Time.time >= endTime;
        return true;
    }

    public void ApplyServerCooldown(int skillId, float durationSec)
    {
        _skillCdEndTimes[skillId] = Time.time + durationSec;
    }

    // ── 辅助 ─────────────────────────────────────────────────
    private byte[] BuildAttackPayload(int targetId, int skillId)
    {
        var ms = new System.IO.MemoryStream(16);
        var bw = new System.IO.BinaryWriter(ms);
        bw.Write(targetId);
        bw.Write(skillId);
        bw.Write((int)PlayerData.LocalEntityId);
        return ms.ToArray();
    }

    private void PlayAttackAnimation(int skillId)
    {
        // 触发玩家角色的攻击动画状态机
        var animator = GetComponent<Animator>();
        if (skillId == 0) animator.SetTrigger("Attack");
        else              animator.SetTrigger("Skill_" + skillId);
    }

    private void StartLocalCooldown(int skillId)
    {
        float duration = SkillDatabase.Instance.GetCooldownMs(skillId) / 1000f;
        _skillCdEndTimes[skillId] = Time.time + duration;
    }

    private void ShowDamageNumber(int targetId, int damage, bool isCritical)
    {
        var entity = FindObjectOfType<EntityManager>().GetEntity(targetId);
        if (entity == null) return;
        DamagePopup.Spawn(entity.WorldPosition, damage, isCritical);
    }

    private struct PendingDamage { public int TargetId; public int SkillId; }
}
```

### 4.5 事件总线（EventBus）

```csharp
// Core/EventBus/EventBus.cs
using System;
using System.Collections.Generic;

/// <summary>
/// 轻量事件总线，解耦各系统间通信。
/// 示例：CombatSystem 发出 DamageEvent，UI 层订阅并刷新血条。
/// </summary>
public static class EventBus
{
    private static readonly Dictionary<Type, Delegate> _handlers = new();

    public static void Register<T>(Action<T> handler)
    {
        _handlers[typeof(T)] = (Delegate)_handlers[typeof(T)].Combine(handler);
    }

    public static void Unregister<T>(Action<T> handler)
    {
        _handlers[typeof(T)] = ((Action<T>)_handlers[typeof(T)]).Remove(handler);
    }

    public static void Publish<T>(T evt)
    {
        if (_handlers.TryGetValue(typeof(T), out var d) && d != null)
            ((Action<T>)d)?.Invoke(evt);
    }
}

// ── 事件定义 ────────────────────────────────────────────────
public struct DamageEvent  { public int TargetId; public int Damage; public bool IsCritical; }
public struct LevelUpEvent { public int PlayerId; public int NewLevel; }
public struct DeathEvent   { public int EntityId; public int KillerId; public bool IsPlayer; }
public struct ChatEvent    { public string Sender; public string Message; public int Channel; }
public struct DropEvent    { public int DropId;   public int  ItemId;  public int Count; }
```

---

## 五、FlatBuffers Schema 完整定义

为客户端与服务端统一协议，定义 `legend.fbs`，**替代当前 GameService.cpp 中的 JSON 手工解析**。

```flatbuffers
// legend.fbs — 传奇 MMO 消息 Schema（对齐 SNC GameService.h 消息 ID）
//
// 编译命令：
//   flatc --csharp --gen-object-api --cs-gen-json-compatible-accessors \
//         -o Assets/Scripts.Generated legend.fbs
//
// 服务端对应：mmo/src/GameService.cpp（需同步替换 JSON 解析）

namespace LegendProto;

// ── 枚举定义 ───────────────────────────────────────────────
enum EntityType : byte {
    Player    = 0,
    Monster   = 1,
    NPC       = 2,
    Pet       = 3,
    GroundDrop = 4,
}

enum ClassType : byte {
    Warrior   = 0,  // 战士
    Magician  = 1,  // 法师
    Taoist    = 2,  // 道士
}

enum FaceDirection : byte {
    South = 0,
    West  = 1,
    North = 2,
    East  = 3,
}

enum ChatChannel : byte {
    World   = 0,
    Guild   = 1,
    Whisper = 2,
}

// ── 基础结构 ───────────────────────────────────────────────
table ItemSlot {
    slotIndex : short;   // 装备槽 0-9，背包槽 10-39
    itemId    : int;
    count     : int;
    isEquipped: bool;
}

table StatChange {
    statName  : string;
    oldValue  : int;
    newValue  : int;
}

// ── C→S 请求消息（客户端发往服务器）────────────────────────

// 0x1100 MsgClientMove — 移动请求
table MoveRequest {
    playerId  : int;
    targetX   : float;
    targetZ   : float;
    timestamp : long;   // 客户端时间戳，防重放攻击
}

// 0x1101 MsgClientAttack — 攻击请求
table AttackRequest {
    attackerId : int;
    targetId   : int;
    skillId    : int;   // 0 = 普通攻击，>0 = 技能 ID
    timestamp  : long;
}

// 0x1103 MsgClientChat — 聊天请求
table ChatRequest {
    playerId  : int;
    channel   : ChatChannel;
    targetId  : int;    // 私聊目标 entityID，0 = 非私聊
    message   : string;
}

// 0x1104 MsgClientPickup — 拾取请求
table PickupRequest {
    playerId : int;
    dropId   : long;    // 对应 DropData.drop_id
}

// 0x1105 MsgClientSkill — 技能请求（扩展用，0x1101 已覆盖普通攻击）
table SkillRequest {
    playerId  : int;
    skillId   : int;
    targetId  : int;
    timestamp : long;
}

// 0x1106 MsgClientGuildReq — 行会请求
table GuildRequest {
    playerId : int;
    guildId  : int;
    action   : GuildAction;  // JOIN=0, LEAVE=1, CREATE=2, NOTICE=3
    content  : string;
}

enum GuildAction : byte {
    Join    = 0,
    Leave   = 1,
    Create  = 2,
    Notice  = 3,
}

// ── S→C 响应消息（服务器发往客户端）────────────────────────

// 0x1200 MsgServerPlayerSync — 玩家属性全量同步
// 登录成功后或属性变化时推送
table PlayerSync {
    playerId        : int;
    name            : string;
    classType       : ClassType;
    level           : int;
    exp             : long;
    hp              : int;
    maxHp           : int;
    mp              : int;
    maxMp           : int;
    pos_x           : float;
    pos_z           : float;
    faceDirection   : FaceDirection;
    equipment       : [ItemSlot];    // 10 槽装备
    bagItems        : [ItemSlot];    // 30 槽背包
    skills          : [SkillEntry];
    isDead          : bool;
}

table SkillEntry {
    skillId                 : int;
    level                   : int;
    cooldownRemaining_ms    : long;
}

// 0x1201 MsgServerPosSync — 实体位置批量同步
// 服务器定期推送（如 100ms 一次），包含视野内所有实体的最新位置
table PositionSync {
    entities : [EntityPos];
}

table EntityPos {
    entityId      : int;
    entityType    : EntityType;
    x             : float;
    z             : float;
    faceDirection : FaceDirection;
    isMoving      : bool;
    hp            : int;    // 可选，HP<=0 时不推送
    targetEntityId: int;    // 正在攻击的目标
}

// 0x1202 MsgServerDamageResult — 伤害结果
table DamageResult {
    targetId    : int;
    damage      : int;
    isCritical  : bool;
    isMiss      : bool;
    skillId     : int;
    attackerId  : int;
}

// 0x1203 MsgServerDeathNotify — 死亡通知
table DeathNotify {
    entityId        : int;
    killerId        : int;
    isPlayer        : bool;
    respawnDelay_ms : long;
    dropList        : [DropNotify];  // 死亡掉落
}

// 0x1204 MsgServerDropNotify — 掉落物刷新
table DropNotify {
    dropId    : long;
    entityId  : int;     // 地面实体 ID
    x         : float;
    z         : float;
    itemId    : int;
    count     : int;
    lifetime_ms : long;  // 剩余存活时间（ms）
}

// 0x1205 MsgServerLevelUp — 升级通知
table LevelUpNotify {
    playerId     : int;
    newLevel     : int;
    hpRestore    : int;
    mpRestore    : int;
    statsChanged : [StatChange];
}

// 0x1206 MsgServerChatBroadcast — 聊天广播
table ChatBroadcast {
    senderId    : int;
    senderName  : string;
    channelId   : ChatChannel;
    message     : string;
    timestamp   : long;
}

// 0x1207 MsgServerGuildNotice — 行会通知
table GuildNotice {
    guildId   : int;
    guildName : string;
    notice    : string;
    timestamp : long;
}

// 0x1208 MsgServerSceneEnter — 进入场景通知
table SceneEnter {
    sceneId     : int;
    spawnX      : float;
    spawnZ      : float;
    otherEntities : [EntityPos];  // 场景内已有实体快照
}

// 0x1209 MsgServerSceneLeave — 离开场景通知（预留）
table SceneLeave {
    sceneId : int;
}

// 0x120A MsgServerMoveResponse — 移动响应（确认/拒绝）
table MoveResponse {
    playerId : int;
    success  : bool;
    reason   : string;    // 拒绝原因（障碍物/距离过远等），空 = 成功
    finalX   : float;
    finalZ   : float;
}

// 0x120B MsgServerError — 通用错误
table ErrorNotify {
    errorCode : int;
    message   : string;
}

// ── 根表 ───────────────────────────────────────────────────
// 客户端不直接解析此表，而是根据 msgId 直接反序列化为对应 table
// 此处仅作 Schema 聚合，方便维护
table MessageBundle {
    msgId    : int;
    payload  : [byte];   // 二进制 payload，客户端按 msgId 分发后反序列化
}

root_type MessageBundle;
```

**FlatBuffers 与 SNC 协议的集成方式**：

```
客户端发送:
  [msgId:0x1100][payloadLen:N][FlatBuffers serialized MoveRequest N bytes]

服务端接收（需改造 GameService.cpp）:
  读取 msgId → 反序列化对应 FlatBuffers table → 执行业务逻辑
```

---

## 六、UI 系统设计

### 6.1 经典传奇 HUD 布局

```
┌─────────────────────────────────────────────────────────────────┐
│  [ minimap 80×80 ]                              [ HP ███████ ]  │
│                                 [ MP ████     ] [ EXP ██     ] │
│                                                                 │
│                                                                 │
│              [ 游戏主画面 — 角色/怪物/NPC 渲染区域 ]              │
│                                                                 │
│                                                                 │
│  ┌──────────────┐  ┌──────────────┐  ┌─────────────────────┐  │
│  │ 技能快捷栏    │  │  聊天窗口     │  │  背包 / 装备 / 任务 │  │
│  │ (底部中央)   │  │  (左下角)     │  │  (右侧可折叠面板)   │  │
│  │ [1][2][3]..  │  │  > 世界频道   │  │                     │  │
│  └──────────────┘  └──────────────┘  └─────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

### 6.2 关键 UI 组件

```csharp
// Game/UI/HUD/HealthBar.cs
using UnityEngine.UI;
using UnityEngine;

public class HealthBar : StatusBar
{
    protected override void UpdateBar(int current, int max)
    {
        _slider.value = max > 0 ? (float)current / max : 0f;
        _text.text = $"{current}/{max}";
    }
}

// Game/UI/HUD/MpBar.cs  — 同上，绑定 MP
// Game/UI/HUD/ExpBar.cs — 同上，绑定 EXP（细条）

// Game/UI/HUD/Minimap.cs
using UnityEngine;
using UnityEngine.UI;

public class Minimap : MonoBehaviour
{
    [SerializeField] private RawImage _mapImage;
    [SerializeField] private float     _mapScale = 0.15f; // 地图缩放比

    private Texture2D _mapTexture;

    public void Initialize(int sceneId, int size)
    {
        _mapTexture = new Texture2D(size, size);
        _mapImage.texture = _mapTexture;
    }

    public void UpdateEntities(EntityBase[] entities, Vector3 playerPos)
    {
        var pixels = _mapTexture.GetPixels();
        Array.Fill(pixels, Color.clear);

        int w = _mapTexture.width;
        foreach (var e in entities)
        {
            int px = Mathf.RoundToInt((e.Position.x - playerPos.x) / _mapScale + w / 2f);
            int py = Mathf.RoundToInt((e.Position.z - playerPos.z) / _mapScale + w / 2f);
            if (px >= 0 && px < w && py >= 0 && py < w)
                pixels[py * w + px] = Color.red; // 怪物红色，玩家绿色
        }
        _mapTexture.SetPixels(pixels);
        _mapTexture.Apply();
    }
}
```

---

## 七、与 SNC 服务端对接清单（阻塞项）

客户端开发前，服务端必须完成以下补齐工作，否则客户端无法联调：

| # | 事项 | 文件 | 优先级 |
|---|------|------|--------|
| 1 | **补充 S→C 消息 ID 定义**（0x1200-0x120B） | [GameService.h](file:///c:/Users/10422/Documents/code/snc/mmo/include/snc/mmorpg/legend/GameService.h) | 🔴 高 |
| 2 | **将 JSON 解析替换为 FlatBuffers**（[GameService.cpp:274-306](file:///c:/Users/10422/Documents/code/snc/mmo/src/GameService.cpp#L274-L306)） | [GameService.cpp](file:///c:/Users/10422/Documents/code/snc/mmo/src/GameService.cpp) | 🔴 高 |
| 3 | **实现 MoveResponse（0x120A）**：移动请求确认后返回成功/失败及最终坐标 | [GameService.cpp](file:///c:/Users/10422/Documents/code/snc/mmo/src/GameService.cpp) | 🔴 高 |
| 4 | **实现心跳 Pong（0x0002）响应** | [GameService.cpp](file:///c:/Users/10422/Documents/code/snc/mmo/src/GameService.cpp) | 🟡 中 |
| 5 | **实现登录认证流程**：AccountService 生成 JWT，Gate 转发到 Game，Game 返回玩家初始数据 | AccountService / GateService / GameService | 🔴 高 |
| 6 | **实现 PositionSync 定时推送**（100ms 周期，广播视野内实体位置） | GameService.cpp | 🟡 中 |
| 7 | **实现伤害计算并返回 DamageResult**（使用 combat.lua 中的公式） | GameService.cpp | 🟡 中 |

---

## 八、分阶段实施计划

### Phase 1（Week 1-2）：网络层 + 基础框架
- [ ] 实现 TcpClient.cs（连接/断线/重连）
- [ ] 实现 FrameDecoder.cs（粘包处理）
- [ ] 实现 MsgRouter.cs（消息分发）
- [ ] 实现 Heartbeat.cs（心跳保活）
- [ ] 完成 Protocol.cs（与 SNC 对齐）
- [ ] 搭建 FSM 状态机框架

### Phase 2（Week 3-4）：登录 + 场景 + 实体
- [ ] 实现登录 UI（JWT 认证对接）
- [ ] 实现 EntityManager（实体创建/销毁/对象池）
- [ ] 实现基础场景加载（比奇城）
- [ ] 处理 MsgServerSceneEnter（0x1208）加载场景实体快照

### Phase 3（Week 5-6）：移动 + 战斗
- [ ] 实现 PlayerInput（格子移动 + 右键攻击）
- [ ] 实现 CombatSystem（伤害预测 + 本地 CD）
- [ ] 对接 MsgServerDamageResult（0x1202）显示伤害数字
- [ ] 对接 MsgServerDeathNotify（0x1203）触发死亡状态

### Phase 4（Week 7-8）：UI + 社交
- [ ] 实现 HUD（HP/MP/EXP 条 + 迷你地图）
- [ ] 实现聊天窗口（世界频道 + 行会频道）
- [ ] 实现背包/装备 UI
- [ ] 对接 MsgServerChatBroadcast（0x1206）

### Phase 5（Week 9-10）：联调优化
- [ ] 与服务端联调，修复协议不一致问题
- [ ] 性能测试（单场景 100 实体帧率）
- [ ] 内存优化（对象池调优）
- [ ] 打包发布（Windows / Android）

---

## 九、单元测试与集成测试方案

### 单元测试（C# xUnit）

```csharp
// Tests/Network/FrameDecoderTests.cs
[TestClass]
public class FrameDecoderTests
{
    [TestMethod]
    public void TestSingleFrame()
    {
        var decoder = new FrameDecoder();
        // 构造一条完整消息：msgId=0x1100, payloadLen=8, payload=[0..7]
        byte[] data = BuildMessage(0x1100, new byte[8]);
        var frames = decoder.Feed(data, data.Length);
        Assert.AreEqual(1, frames.Count);
        Assert.AreEqual(8, frames[0].Length);
    }

    [TestMethod]
    public void TestSplitFrame()
    {
        var decoder = new FrameDecoder();
        byte[] header = new byte[8];
        BitConverter.GetBytes((int)0x1100).CopyTo(header, 0);
        BitConverter.GetBytes(8).CopyTo(header, 4);

        // 先收头部，再收 payload
        var frames1 = decoder.Feed(header, 8);
        Assert.AreEqual(0, frames1.Count); // 不完整，不返回

        var frames2 = decoder.Feed(new byte[8], 8);
        Assert.AreEqual(1, frames2.Count); // 现在完整了
    }

    [TestMethod]
    public void TestCoalescedFrames()
    {
        var decoder = new FrameDecoder();
        byte[] msg1 = BuildMessage(0x1100, new byte[4]);
        byte[] msg2 = BuildMessage(0x1101, new byte[4]);
        var frames = decoder.FeedConcat(msg1, msg2);
        Assert.AreEqual(2, frames.Count);
    }

    [TestMethod]
    public void TestMalformedPayloadLen()
    {
        var decoder = new FrameDecoder();
        byte[] data = new byte[12];
        BitConverter.GetBytes((int)0x1100).CopyTo(data, 0);
        BitConverter.GetBytes(-1).CopyTo(data, 4); // 非法长度
        var frames = decoder.Feed(data, 12);
        Assert.AreEqual(0, frames.Count); // 应拒绝非法包
    }

    private byte[] BuildMessage(int msgId, byte[] payload)
    {
        var ms = new System.IO.MemoryStream();
        var bw = new System.IO.BinaryWriter(ms);
        bw.Write((int)msgId);
        bw.Write(payload.Length);
        bw.Write(payload);
        return ms.ToArray();
    }
}
```

### 集成测试（与服务端联调）

```
集成测试场景：
  1. Connect → Auth → Receive PlayerSync → Verify player attributes
  2. Send MoveRequest → Receive MoveResponse → Verify position updated
  3. Send AttackRequest → Receive DamageResult → Verify damage value
  4. Send ChatRequest → Receive ChatBroadcast → Verify chat window updated
  5. Kill monster → Receive DeathNotify → Verify death UI + respawn timer
  6. Pick up drop → Receive DropNotify → Verify inventory updated
  7. Level up → Receive LevelUpNotify → Verify exp bar + stats
```

---

## 十、结论

**推荐使用 Unity 2022 LTS + 自研 TCP + FlatBuffers**，理由：

1. **协议对齐成本最低**：8 字节头部与 SNC 完全一致，只需实现 FrameDecoder
2. **开发效率高**：C# 上手快，Unity 编辑器迭代快，适合快速原型
3. **移动端友好**：传奇类目标用户广泛，移动端覆盖必要
4. **性能足够**：传奇类游戏实体数量少（单场景 < 200），Unity URP 轻松应对

**关键前置依赖**：服务端需先补齐 S→C 消息 ID 定义（0x1200-0x120B）并将 JSON 解析替换为 FlatBuffers，否则客户端无法完成联调。
