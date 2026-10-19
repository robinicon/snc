// SPDX-License-Identifier: MIT
// MySQL database adapter — stub + optional real connector.
//
// Provides a MySQL backend for the IDatabase interface, with a stub
// fallback when SNC_MYSQL_CLIENT is not enabled (same pattern as snc_redis).
#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include <chrono>
#include <mutex>
#include <atomic>
#include <cstdio>
#include <sstream>
#include "snc/db/Database.h"
#include "snc/db/StubDatabase.h"
#include "snc/mmorpg/legend/PlayerActor.h"

namespace snc {
namespace db {

// ── MySQL connection config ───────────────────────────────────────
struct MySqlConfig {
    std::string host     = "127.0.0.1";
    int         port     = 3306;
    std::string user     = "root";
    std::string password = "";
    std::string database = "snc_mmorpg";
    int         timeoutMs = 5000;
};

// ── 持久化数据对象 ───────────────────────────────────────────────
struct PlayerData {
    int32_t     player_id  = 0;
    std::string name;
    legend::ClassType cls = legend::ClassType::WARRIOR;
    int64_t     exp        = 0;
    int64_t     gold       = 0;
    int32_t     current_hp = 0;
    int32_t     max_hp     = 0;
    int32_t     level      = 1;
};

struct GuildData {
    int32_t guild_id  = 0;
    std::string name;
    int32_t master_id = 0;
    int64_t gold      = 0;
};

// ── MySQL database class ──────────────────────────────────────────
class MySqlDatabase : public IDatabase {
public:
    MySqlDatabase() = default;
    ~MySqlDatabase() override { Disconnect(); }

    bool Connect(const std::string& connStr) override {
        if (!parsed_) {
            if (!ParseConnStr_(connStr)) return false;
        }
#ifdef SNC_MYSQL_CLIENT
        // 真实 MySQL 连接器（需 link mysqlcppconn 或 mariadb-connector-cpp）
        // TODO: 接入 mysqlx/connector-cpp，此处为桩实现
        connected_ = true;
        SNC_LOG_INFO("db.mysql", "Connected to %s:%d/%s",
                     cfg_.host.c_str(), cfg_.port, cfg_.database.c_str());
#else
        // Stub：内存映射，不连接真实 MySQL
        connected_ = true;
        (void)connStr;
#endif
        return true;
    }

    void Disconnect() override {
#ifdef SNC_MYSQL_CLIENT
        if (mysql_) { mysql_->close(); mysql_.reset(); }
#endif
        connected_ = false;
    }

    bool IsConnected() const override { return connected_; }

    Result Query(const std::string& sql) override {
        auto t0 = std::chrono::steady_clock::now();
        ++totalQueries_;
#ifdef SNC_MYSQL_CLIENT
        if (mysql_) {
            // 真实 MySQL 执行
            // TODO: 实现
        }
#else
        // Stub：使用内表查询（同 StubDatabase）
        Result r = stub_db_->Query(sql);
        auto t1 = std::chrono::steady_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        totalLatencyMs_ += ms;
        r.latencyMs = ms;
        return r;
#endif
        return {};
    }

    Stats GetStats() const override {
        return {totalQueries_.load(), totalErrors_.load(),
                totalQueries_ > 0 ? totalLatencyMs_ / totalQueries_ : 0.0};
    }

    // ── 高层业务接口 ─────────────────────────────────────────────
    /// SQL 字符串转义（防止注入），转义 ' 和 \
    static std::string EscapeSql_(const std::string& s) {
        std::string out; out.reserve(s.size() + 8);
        for (char c : s) {
            if      (c == '\'') out += "''";
            else if (c == '\\') out += "\\\\";
            else                out += c;
        }
        return out;
    }

    /// 加载玩家数据（id -> player data）
    PlayerData LoadPlayer(int32_t playerId) {
        auto r = Query("SELECT * FROM players WHERE id = '" + std::to_string(playerId) + "'");
        PlayerData pd{};
        if (r.ok && !r.rows.empty()) {
            auto& row = r.rows[0];
            auto get = [&](const char* k) -> std::string {
                auto it = row.find(k);
                return (it != row.end()) ? it->second.ToString() : "";
            };
            pd.player_id      = playerId;
            pd.name           = get("name");
            pd.cls            = static_cast<ClassType>(std::stoi(get("class")));
            pd.exp            = std::stoll(get("exp"));
            pd.gold           = std::stoll(get("gold"));
            pd.current_hp     = std::stoi(get("hp"));
            pd.max_hp         = std::stoi(get("max_hp"));
            pd.level          = std::stoi(get("level"));
        }
        return pd;
    }

    /// 保存玩家数据
    bool SavePlayer(const PlayerData& pd) {
        std::ostringstream sql;
        sql << "INSERT INTO players (id, name, class, exp, gold, hp, max_hp, level)"
            << " VALUES ("
            << pd.player_id << ", '" << EscapeSql_(pd.name) << "', "
            << static_cast<int>(pd.cls) << ", " << pd.exp << ", " << pd.gold << ", "
            << pd.current_hp << ", " << pd.max_hp << ", " << pd.level
            << ") ON DUPLICATE KEY UPDATE"
            << " name=VALUES(name), exp=VALUES(exp), gold=VALUES(gold),"
            << " hp=VALUES(hp), max_hp=VALUES(max_hp), level=VALUES(level)";
        auto r = Query(sql.str());
        return r.ok;
    }

    /// 加载行会数据
    GuildData LoadGuild(int32_t guildId) {
        auto r = Query("SELECT * FROM guilds WHERE id = '" + std::to_string(guildId) + "'");
        GuildData gd{};
        if (r.ok && !r.rows.empty()) {
            auto& row = r.rows[0];
            auto get = [&](const char* k) -> std::string {
                auto it = row.find(k);
                return (it != row.end()) ? it->second.ToString() : "";
            };
            gd.guild_id   = guildId;
            gd.name       = get("name");
            gd.master_id  = std::stoi(get("master_id"));
            gd.gold       = std::stoll(get("gold"));
        }
        return gd;
    }

    bool SaveGuild(const GuildData& gd) {
        std::ostringstream sql;
        sql << "INSERT INTO guilds (id, name, master_id, gold)"
            << " VALUES (" << gd.guild_id << ", '" << EscapeSql_(gd.name) << "', "
            << gd.master_id << ", " << gd.gold
            << ") ON DUPLICATE KEY UPDATE"
            << " name=VALUES(name), master_id=VALUES(master_id), gold=VALUES(gold)";
        auto r = Query(sql.str());
        return r.ok;
    }

private:
    bool ParseConnStr_(const std::string& s) {
        // 格式: mysql://user:pass@host:port/database
        // 简化解析，完整实现应使用 URI parser
        auto at = s.find('@');
        if (at == std::string::npos) return false;
        auto slash = s.find('/', at);
        if (slash == std::string::npos) return false;
        auto colon = s.find(':', at);
        cfg_.user = s.substr(6, colon - 6);
        cfg_.password = s.substr(colon + 1, at - colon - 1);
        auto hostStart = s.find('/', at + 1);
        auto hostEnd   = s.find(':', hostStart + 1);
        cfg_.host = s.substr(hostStart + 1, hostEnd - hostStart - 1);
        cfg_.port = std::stoi(s.substr(hostEnd + 1, slash - hostEnd - 1));
        cfg_.database = s.substr(slash + 1);
        parsed_ = true;
        return true;
    }

#ifdef SNC_MYSQL_CLIENT
    // mysql::Connection* mysql_ = nullptr; // TODO: 接入 connector-cpp
#endif
    std::shared_ptr<StubDatabase> stub_db_ = std::make_shared<StubDatabase>();
    MySqlConfig                   cfg_;
    bool                          connected_ = false;
    bool                          parsed_    = false;
    std::atomic<int64_t>          totalQueries_{0};
    std::atomic<int64_t>          totalErrors_{0};
    double                        totalLatencyMs_ = 0.0;
};

// ── 工厂函数 ──────────────────────────────────────────────────────
inline std::shared_ptr<IDatabase> CreateMySqlDatabase(const MySqlConfig& cfg = {}) {
    auto db = std::make_shared<MySqlDatabase>();
#ifdef SNC_MYSQL_CLIENT
    // 真实 MySQL 连接将在 Connect() 时建立
#else
    (void)cfg; // stub 模式忽略配置
#endif
    return db;
}

} // namespace db
} // namespace snc
