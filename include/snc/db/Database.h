// SPDX-License-Identifier: MIT
// Database adapter layer — abstract interface with stub implementation.
//
// Provides a uniform query interface so game services can swap backends
// (MySQL, PostgreSQL, SQLite) without changing application code.
// The stub implementation supports in-memory key-value for testing.
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

namespace snc {
namespace db {

/// Column value — dynamically typed (string / int64 / double / bool)
enum class ValueType : int {
    STRING = 0,
    INT    = 1,
    FLOAT  = 2,
    BOOL   = 3,
    NULL_V = 4,
};

struct Value {
    ValueType type = ValueType::NULL_V;
    std::string str_val;
    int64_t     int_val  = 0;
    double      float_val = 0.0;
    bool        bool_val  = false;

    static Value Null() { return Value{ValueType::NULL_V}; }
    static Value FromStr(const std::string& s) {
        // Try to parse as integer
        try {
            size_t pos;
            int64_t ival = std::stoll(s, &pos);
            if (pos == s.size()) return Value{ValueType::INT, "", ival, 0.0, false};
        } catch (...) {}
        try {
            size_t pos;
            double fval = std::stod(s, &pos);
            if (pos == s.size()) return Value{ValueType::FLOAT, "", 0, fval, false};
        } catch (...) {}
        return Value{ValueType::STRING, s};
    }
    static Value FromInt(int64_t i) { return Value{ValueType::INT, "", i, 0.0, false}; }
    static Value FromFloat(double f) { return Value{ValueType::FLOAT, "", 0, f, false}; }
    static Value FromBool(bool b) { return Value{ValueType::BOOL, "", 0, 0.0, b}; }

    std::string ToString() const {
        switch (type) {
            case ValueType::INT:    return std::to_string(int_val);
            case ValueType::FLOAT:  return std::to_string(float_val);
            case ValueType::BOOL:   return bool_val ? "true" : "false";
            case ValueType::STRING: return str_val;
            default:                return "";
        }
    }
};

inline const char* ValueTypeStr(ValueType t) {
    switch (t) {
        case ValueType::STRING: return "STRING";
        case ValueType::INT:    return "INT";
        case ValueType::FLOAT:  return "FLOAT";
        case ValueType::BOOL:   return "BOOL";
        default:                return "NULL";
    }
}

/// One row in a result set
using Row = std::map<std::string, Value>;
using Rows = std::vector<Row>;

/// Query result
struct Result {
    bool           ok       = false;
    std::string    error;
    Rows           rows;
    int64_t        rowsAffected = 0;
    int64_t        lastInsertId  = 0;
    double         latencyMs   = 0.0;
};

/// Abstract database connection — implement for MySQL / SQLite / etc.
class IDatabase {
public:
    virtual ~IDatabase() = default;

    /// Connect to the database. Returns true on success.
    virtual bool Connect(const std::string& connStr) = 0;

    /// Disconnect from the database.
    virtual void Disconnect() = 0;

    /// Check if connected.
    virtual bool IsConnected() const = 0;

    /// Execute a SQL query. Returns the result.
    virtual Result Query(const std::string& sql) = 0;

    /// Execute a DDL/DML statement (alias for Query — stub supports both).
    virtual Result Execute(const std::string& sql) { return Query(sql); }

    struct Stats {
        int64_t totalQueries = 0;
        int64_t totalErrors  = 0;
        double  avgLatencyMs = 0.0;
    };
    virtual Stats GetStats() const { return {}; }
};

// ── In-Memory Stub Database (for testing, no external deps) ─────
// Implementation lives in libs/snc_db/src/StubDatabase.cpp.
// Include "snc/db/StubDatabase.h" to use it.

} // namespace db
} // namespace snc
