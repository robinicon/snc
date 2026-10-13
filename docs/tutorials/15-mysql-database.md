# 15 — 数据库适配器（IDatabase / StubDatabase）

**目标**：了解 `snc::db` 抽象层的统一查询接口，以及如何用 StubDatabase 进行开发测试。

## 概述

`snc_db` 模块提供统一的数据库抽象接口，业务代码只需面向 `IDatabase` 编写，底层可切换 MySQL、PostgreSQL 或 SQLite 实现。默认情况下（未接入真实数据库）使用 `StubDatabase` 在内存中模拟 SQL 执行，支持测试和快速原型开发。

头文件入口：

```cpp
#include <snc/db/Database.h>
```

## IDatabase 接口

```cpp
class IDatabase {
public:
    virtual ~IDatabase() = default;

    // 连接/断开
    virtual bool Connect(const std::string& connStr) = 0;
    virtual void Disconnect() {}
    virtual bool IsConnected() const = 0;

    // 查询（SELECT 等返回结果集的语句）
    virtual Result Query(const std::string& sql) = 0;

    // 写操作（INSERT/UPDATE/DELETE，有返回值）
    virtual Result Execute(const std::string& sql);   // 默认转发到 Query()

    // 事务（可选，部分实现不支持）
    virtual bool BeginTransaction();
    virtual bool Commit();
    virtual bool Rollback();

    // 统计信息
    struct Stats {
        int64_t queries     = 0;
        int64_t errors      = 0;
        double  avgLatencyMs = 0.0;
    };
    virtual Stats GetStats() const;
};
```

## 返回值结构

```cpp
struct Result {
    bool           ok           = false;   // 是否成功
    std::string    error;                  // 错误信息（失败时填充）
    Rows           rows;                   // 查询结果集
    int64_t        rowsAffected  = 0;      // 写操作影响的行数
    int64_t        lastInsertId  = 0;      // 自增 ID（INSERT 后返回）
    double         latencyMs     = 0.0;    // 本次查询耗时（毫秒）
};
```

`Row` 是 `std::map<std::string, Value>`，`Value` 支持四种类型：`STRING`、`INT`、`FLOAT`、`BOOL`。

## StubDatabase（开发/测试用）

```cpp
#include <snc/db/Database.h>

// 创建内存数据库
auto db = snc::db::CreateStubDatabase();
db->Connect("");  // Stub 无需真实连接串

// Schema 会自动推断，无需显式 CREATE TABLE；也可用 CREATE TABLE 作为文档
db->Execute("INSERT INTO players (id, name, gold) VALUES (NULL, 'Alice', 5000)");
// r1.rowsAffected == 1, r1.lastInsertId == 1

auto r2 = db->Execute("INSERT INTO players (id, name, gold) VALUES (NULL, 'Bob', 3000)");

// 查询（WHERE 条件列名必须与 INSERT 时的列名一致）
auto r3 = db->Query("SELECT name, gold FROM players WHERE id = '1'");
// r3.rows 包含 1 行：{ {"name":"Alice"}, {"gold":"5000"} }

// 更新
db->Execute("UPDATE players SET gold = 6000 WHERE id = '1'");

// 删除
db->Execute("DELETE FROM players WHERE id = '2'");

// 统计
auto stats = db->GetStats();
// stats.queries, stats.errors, stats.avgLatencyMs
```

### Stub 支持的 SQL

| 语句 | 说明 |
|------|------|
| `INSERT INTO t (cols) VALUES (vals)` | **自动推断 schema**，第一列通常为自增 `id` |
| `SELECT col1, col2 FROM t [WHERE col = val]` | 支持 `*` 和指定列，单条件 WHERE（列名须与 INSERT 一致） |
| `UPDATE t SET col=val [, ...] [WHERE col=val]` | 支持多列更新，单条件 WHERE |
| `DELETE FROM t [WHERE col=val]` | 可选 WHERE，条件为第一列（通常是 id） |
| `CREATE TABLE ...` | 支持但为 no-op，schema 由首次 INSERT 自动建立 |

> **限制**：Stub 仅支持简单 SQL，不支持 JOIN、子查询、聚合函数等复杂语法。

## 真实后端切换

切换到 MySQL 或其他后端时，替换 `CreateStubDatabase()` 即可，业务代码不变：

```cpp
// 使用真实 MySQL 后端（需自行实现 IDatabase 接口）
auto db = std::make_shared<MySqlDatabase>();
db->Connect("mysql://user:pass@host:3306/mydb");

// 之后用法完全相同
auto result = db->Query("SELECT gold FROM players WHERE id = 1");
```

## 注意事项

- **Stub 数据不持久化**：进程重启后所有数据丢失
- **类型推断**：`Value::FromStr()` 会尝试将字符串解析为整数或浮点数，解析失败则保留为字符串
- **线程安全**：`IDatabase` 实例通常不是线程安全的，跨线程访问请加锁或使用连接池
- **`Execute()` 默认实现**：调用 `Query(sql)` 并返回 `rowsAffected=0`，真实后端需重写

参见 [08 - Redis 客户端](08-redis-config.md)（Redis 用于缓存，SQL 用于持久化存储）
