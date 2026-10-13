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

    virtual bool Connect(const std::string& connStr) = 0;
    virtual void Disconnect() {}
    virtual bool IsConnected() const = 0;

    /// Execute a query and return results. connStr is ignored if already connected.
    virtual Result Query(const std::string& sql) = 0;

    /// Execute a write (INSERT/UPDATE/DELETE) — returns affected row count.
    virtual Result Execute(const std::string& sql) { return Query(sql); }

    /// Transaction support
    virtual bool BeginTransaction() { return false; }
    virtual bool Commit() { return false; }
    virtual bool Rollback() { return false; }

    struct Stats {
        int64_t queries     = 0;
        int64_t errors      = 0;
        double  avgLatencyMs = 0.0;
    };
    virtual Stats GetStats() const { return {}; }
};

// ── In-Memory Stub Database (for testing, no external deps) ─────

class StubDatabase : public IDatabase {
public:
    bool Connect(const std::string&) override { connected_ = true; return true; }
    void Disconnect() override { connected_ = false; table_.clear(); }
    bool IsConnected() const override { return connected_; }

    Result Query(const std::string& sql) override {
        auto t0 = std::chrono::steady_clock::now();
        ++totalQueries_;
        auto result = ParseAndExec_(sql);
        auto t1 = std::chrono::steady_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        totalLatencyMs_ += ms;
        result.latencyMs = ms;
        return result;
    }

    Stats GetStats() const override {
        return {totalQueries_.load(), totalErrors_.load(),
                totalQueries_ > 0 ? totalLatencyMs_ / totalQueries_ : 0.0};
    }

private:
    // ── Minimal SQL parser for INSERT / SELECT ─────────────────

    struct InsertInfo {
        std::string table;
        std::vector<std::string> cols;
        std::vector<std::string> vals; // string representations
    };

    struct SelectInfo {
        std::string table;
        std::vector<std::string> cols; // empty = *
        std::string whereCol;
        std::string whereVal;
    };

    Result ParseAndExec_(const std::string& sql) {
        Result r;
        std::string s = TrimCopy_(sql);
        if (s.empty()) { r.ok = true; return r; }

        // Convert to upper for keyword detection
        std::string upper = s;
        for (auto& c : upper) c = toupper(static_cast<unsigned char>(c));

        if (upper.find("SELECT ") == 0 || upper.find("SELECT\n") == 0) {
            r = ExecSelect_(s);
        } else if (upper.find("INSERT INTO ") == 0) {
            r = ExecInsert_(s);
        } else if (upper.find("UPDATE ") == 0) {
            r = ExecUpdate_(s);
        } else if (upper.find("DELETE FROM ") == 0) {
            r = ExecDelete_(s);
        } else if (upper.find("CREATE TABLE") == 0) {
            // Stub: CREATE TABLE is a no-op (schema is inferred from INSERT)
            r.ok = true;
        } else {
            r.ok = false;
            r.error = "Unsupported SQL: " + s;
            ++totalErrors_;
        }
        return r;
    }

    Result ExecSelect_(const std::string& sql) {
        Result r; r.ok = true;
        // Parse: SELECT col1, col2 FROM table [WHERE col = val]
        std::string rest = SkipKeyword_(sql, "SELECT");
        rest = SkipWhitespace_(rest);

        // Extract columns
        auto fromPos = FindKeywordPos_(rest, "FROM");
        if (fromPos == std::string::npos) {
            r.ok = false; r.error = "SELECT: missing FROM"; ++totalErrors_; return r;
        }
        std::string colsStr = rest.substr(0, fromPos);
        rest = rest.substr(fromPos + 4);
        rest = SkipWhitespace_(rest);

        std::vector<std::string> cols;
        if (colsStr == "*") {
            cols.push_back("*");
        } else {
            SplitByComma_(colsStr, cols);
        }

        // Extract table name
        auto wherePos = FindKeywordPos_(rest, "WHERE");
        std::string tableName = wherePos != std::string::npos
            ? TrimCopy_(rest.substr(0, wherePos))
            : TrimCopy_(rest);
        rest = wherePos != std::string::npos ? rest.substr(wherePos + 5) : "";
        rest = SkipWhitespace_(rest);

        // Extract WHERE clause
        std::string whereCol, whereVal;
        if (!rest.empty()) {
            auto eqPos = rest.find('=');
            if (eqPos != std::string::npos) {
                whereCol = TrimCopy_(rest.substr(0, eqPos));
                whereVal = TrimQuotes_(TrimCopy_(rest.substr(eqPos + 1)));
            }
        }

        // Query the table
        auto it = table_.find(tableName);
        if (it == table_.end()) { r.rows = {}; return r; }

        const auto& data = it->second;
        const auto& schema = schemas_[tableName]; // column names: [id_col, col1, col2, ...]

        for (const auto& rowVec : data) {
            Row row;
            for (const auto& colName : cols) {
                // Find this column's index in the full schema
                size_t colIdx = std::string::npos;
                for (size_t k = 0; k < schema.size(); ++k) {
                    if (schema[k] == colName) { colIdx = k; break; }
                }
                if (colIdx == std::string::npos || colIdx >= rowVec.size()) continue;
                row[colName] = rowVec[colIdx];
            }
            if (!whereCol.empty()) {
                // Find the column index in the full schema (not just the selected columns)
                size_t whereIdx = std::string::npos;
                for (size_t k = 0; k < schema.size(); ++k) {
                    if (schema[k] == whereCol) { whereIdx = k; break; }
                }
                if (whereIdx == std::string::npos || whereIdx >= rowVec.size()) continue;
                const auto& cell = rowVec[whereIdx];
                bool match = false;
                if (cell.type == ValueType::INT) {
                    try { match = (cell.int_val == std::stoll(whereVal)); } catch (...) {}
                } else {
                    match = (cell.str_val == whereVal);
                }
                if (!match) continue;
            }
            r.rows.push_back(row);
        }
        return r;
    }

    Result ExecInsert_(const std::string& sql) {
        Result r; r.ok = true;
        // INSERT INTO table (col1, col2) VALUES (val1, val2)
        std::string rest = SkipKeyword_(sql, "INSERT");
        rest = SkipKeyword_(rest, "INTO");
        rest = SkipWhitespace_(rest);

        auto parenPos = rest.find('(');
        if (parenPos == std::string::npos) { r.ok = false; r.error = "INSERT: no cols"; ++totalErrors_; return r; }
        std::string tableName = TrimCopy_(rest.substr(0, parenPos));
        rest = rest.substr(parenPos + 1);

        auto closeParen = rest.find(')');
        if (closeParen == std::string::npos) { r.ok = false; r.error = "INSERT: no close paren"; ++totalErrors_; return r; }
        std::string colsStr = rest.substr(0, closeParen);
        rest = rest.substr(closeParen + 1);
        rest = SkipWhitespace_(rest);

        std::vector<std::string> cols;
        SplitByComma_(colsStr, cols);
        for (auto& c : cols) c = TrimQuotes_(c);

        // VALUES (...)
        rest = SkipKeyword_(rest, "VALUES");
        rest = SkipWhitespace_(rest);
        if (rest.empty() || rest[0] != '(') { r.ok = false; r.error = "INSERT: no VALUES"; ++totalErrors_; return r; }
        rest = rest.substr(1);
        closeParen = rest.find(')');
        std::string valsStr = rest.substr(0, closeParen);
        std::vector<std::string> vals;
        SplitByComma_(valsStr, vals);
        for (auto& v : vals) v = TrimQuotes_(v);

        if (vals.size() != cols.size()) { r.ok = false; r.error = "INSERT: col/val mismatch"; ++totalErrors_; return r; }

        // Store in table
        table_[tableName]; // ensure entry exists
        schemas_[tableName] = cols;

        std::vector<Value> row;
        row.reserve(vals.size());
        // Handle auto-increment: if first column is "id" and value is "NULL", assign sequential ID
        if (!cols.empty() && cols[0] == "id" && !vals.empty() && vals[0] == "NULL") {
            auto& data = table_[tableName];
            int64_t next_id = static_cast<int64_t>(data.size()) + 1;
            row.push_back(Value::FromInt(next_id));
            for (size_t j = 1; j < vals.size(); ++j) row.push_back(Value::FromStr(vals[j]));
        } else {
            for (auto& v : vals) {
                row.push_back(Value::FromStr(v));
            }
        }
        auto& data = table_[tableName];
        data.push_back(std::move(row));
        r.rowsAffected = 1;
        r.lastInsertId = data.back()[0].int_val;
        return r;
    }

    Result ExecUpdate_(const std::string& sql) {
        Result r; r.ok = true;
        // UPDATE table SET col=val WHERE col=val
        std::string rest = SkipKeyword_(sql, "UPDATE");
        auto setPos = FindKeywordPos_(rest, "SET");
        if (setPos == std::string::npos) { r.ok = false; r.error = "UPDATE: no SET"; ++totalErrors_; return r; }
        std::string tableName = TrimCopy_(rest.substr(0, setPos));
        rest = rest.substr(setPos + 3);
        rest = SkipWhitespace_(rest);

        // WHERE
        auto wherePos = FindKeywordPos_(rest, "WHERE");
        std::string setClause = wherePos != std::string::npos ? rest.substr(0, wherePos) : rest;
        std::string whereClause = wherePos != std::string::npos ? rest.substr(wherePos + 5) : "";
        setClause = TrimCopy_(setClause);
        whereClause = TrimCopy_(whereClause);

        // Parse SET col = val
        std::map<std::string, std::string> updates;
        {
            std::string cur = setClause;
            while (!cur.empty()) {
                auto eq = cur.find('=');
                if (eq == std::string::npos) break;
                std::string col = TrimQuotes_(TrimCopy_(cur.substr(0, eq)));
                cur = cur.substr(eq + 1);
                cur = SkipWhitespace_(cur);
                auto comma = cur.find(',');
                std::string val = comma != std::string::npos ? TrimQuotes_(TrimCopy_(cur.substr(0, comma))) : TrimQuotes_(TrimCopy_(cur));
                updates[col] = val;
                cur = comma != std::string::npos ? cur.substr(comma + 1) : "";
                cur = SkipWhitespace_(cur);
            }
        }

        // Parse WHERE col = val
        std::string wCol, wVal;
        if (!whereClause.empty()) {
            auto eq = whereClause.find('=');
            if (eq != std::string::npos) {
                wCol = TrimQuotes_(TrimCopy_(whereClause.substr(0, eq)));
                wVal = TrimQuotes_(TrimCopy_(whereClause.substr(eq + 1)));
            }
        }

        // Apply updates
        auto it = table_.find(tableName);
        if (it == table_.end()) { r.rowsAffected = 0; return r; }
        int affected = 0;
        for (auto& rowVec : it->second) {
            if (!wCol.empty()) {
                // Need header to find column index
                if (rowVec.empty()) continue;
                bool match = (rowVec[0].str_val == wVal); // simplistic: first col = where col
                if (!match) continue;
            }
            // Apply updates by column name match
            auto& schema = schemas_[tableName];
            size_t col_idx = 0;
            for (auto& [colName, newVal] : updates) {
                // Find column index in schema
                size_t targetIdx = 0;
                bool found = false;
                for (size_t k = 0; k < schema.size(); ++k) {
                    if (schema[k] == colName) { targetIdx = k; found = true; break; }
                }
                if (!found || targetIdx + 1 >= rowVec.size()) continue; // id is at index 0
                rowVec[targetIdx + 1] = Value::FromStr(newVal);
            }
            ++affected;
        }
        r.rowsAffected = affected;
        return r;
    }

    Result ExecDelete_(const std::string& sql) {
        Result r; r.ok = true;
        std::string rest = SkipKeyword_(sql, "DELETE");
        rest = SkipKeyword_(rest, "FROM");
        rest = SkipWhitespace_(rest);
        auto wherePos = FindKeywordPos_(rest, "WHERE");
        std::string tableName = wherePos != std::string::npos ? TrimCopy_(rest.substr(0, wherePos)) : TrimCopy_(rest);
        std::string whereClause = wherePos != std::string::npos ? TrimCopy_(rest.substr(wherePos + 5)) : "";

        auto it = table_.find(tableName);
        if (it == table_.end()) { r.rowsAffected = 0; return r; }

        int affected = 0;
        std::string wCol, wVal;
        if (!whereClause.empty()) {
            auto eq = whereClause.find('=');
            if (eq != std::string::npos) {
                wCol = TrimQuotes_(TrimCopy_(whereClause.substr(0, eq)));
                wVal = TrimQuotes_(TrimCopy_(whereClause.substr(eq + 1)));
            }
        }

        it->second.erase(
            std::remove_if(it->second.begin(), it->second.end(),
                [&](const std::vector<Value>& row) {
                    if (wCol.empty()) return true;
                    if (row.empty()) return false;
                    // Find column index in schema
                    auto& schema = schemas_[tableName];
                    size_t idx = std::string::npos;
                    for (size_t k = 0; k < schema.size(); ++k) {
                        if (schema[k] == wCol) { idx = k; break; }
                    }
                    if (idx == std::string::npos || idx >= row.size()) return false;
                    const auto& cell = row[idx];
                    bool match = false;
                    if (cell.type == ValueType::INT) {
                        try { match = (cell.int_val == std::stoll(wVal)); } catch (...) {}
                    } else {
                        match = (cell.str_val == wVal);
                    }
                    return !match;
                }),
            it->second.end());
        r.rowsAffected = affected;
        return r;
    }

    // ── String helpers ───────────────────────────────────────────

    static std::string TrimCopy_(const std::string& s) {
        size_t start = s.find_first_not_of(" \t\n\r");
        if (start == std::string::npos) return "";
        size_t end = s.find_last_not_of(" \t\n\r");
        return s.substr(start, end - start + 1);
    }

    static std::string SkipKeyword_(const std::string& s, const std::string& kw) {
        std::string us = s;
        for (auto& c : us) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
        size_t pos = us.find(kw);
        if (pos == std::string::npos) return s;
        return s.substr(pos + kw.size());
    }

    static size_t FindKeywordPos_(const std::string& s, const std::string& kw) {
        std::string us = s;
        for (auto& c : us) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
        return us.find(kw);
    }

    static std::string SkipWhitespace_(const std::string& s) {
        size_t pos = s.find_first_not_of(" \t\n\r");
        return (pos == std::string::npos) ? "" : s.substr(pos);
    }

    static std::string TrimQuotes_(const std::string& s) {
        if (s.size() >= 2 && s.front() == '\'' && s.back() == '\'')
            return s.substr(1, s.size() - 2);
        return s;
    }

    static void SplitByComma_(const std::string& s, std::vector<std::string>& out) {
        out.clear();
        std::string cur;
        for (char c : s) {
            if (c == ',') { out.push_back(TrimCopy_(cur)); cur.clear(); }
            else cur += c;
        }
        if (!cur.empty()) out.push_back(TrimCopy_(cur));
    }

    // ── Storage ──────────────────────────────────────────────────
    // table_[name] = vector of rows; each row = vector of Values
    // First column is always the "id" auto-increment column.
    std::unordered_map<std::string, std::vector<std::vector<Value>>> table_;
    std::unordered_map<std::string, std::vector<std::string>>        schemas_; // col names per table
    bool                             connected_ = false;
    std::atomic<int64_t>             totalQueries_{0};
    std::atomic<int64_t>             totalErrors_{0};
    double                           totalLatencyMs_ = 0.0;
};

// ── Factory ──────────────────────────────────────────────────────

inline std::shared_ptr<IDatabase> CreateStubDatabase() {
    return std::make_shared<StubDatabase>();
}

} // namespace db
} // namespace snc
