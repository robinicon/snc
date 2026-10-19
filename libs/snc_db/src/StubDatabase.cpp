// SPDX-License-Identifier: MIT
/// @file StubDatabase.cpp
/// @brief In-memory stub database implementation.

#include "snc/db/StubDatabase.h"
#include <cctype>
#include <algorithm>

namespace snc {
namespace db {

// ── IDatabase interface ─────────────────────────────────────────

bool StubDatabase::Connect(const std::string&) {
    connected_ = true;
    return true;
}

void StubDatabase::Disconnect() {
    connected_ = false;
    table_.clear();
}

bool StubDatabase::IsConnected() const {
    return connected_;
}

std::shared_ptr<IDatabase> CreateStubDatabase() {
    return std::make_shared<StubDatabase>();
}

// ── Query ───────────────────────────────────────────────────────

Result StubDatabase::Query(const std::string& sql) {
    auto t0 = std::chrono::steady_clock::now();
    ++totalQueries_;
    auto result = ParseAndExec_(sql);
    auto t1 = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    totalLatencyMs_ += ms;
    result.latencyMs = ms;
    return result;
}

StubDatabase::Stats StubDatabase::GetStats() const {
    return {totalQueries_.load(), totalErrors_.load(),
            totalQueries_ > 0 ? totalLatencyMs_ / totalQueries_ : 0.0};
}

// ── SQL dispatch ────────────────────────────────────────────────

Result StubDatabase::ParseAndExec_(const std::string& sql) {
    Result r;
    std::string s = TrimCopy_(sql);
    if (s.empty()) { r.ok = true; return r; }

    std::string upper = s;
    for (auto& c : upper) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));

    if (upper.find("SELECT ") == 0 || upper.find("SELECT\n") == 0) {
        r = ExecSelect_(s);
    } else if (upper.find("INSERT INTO ") == 0) {
        r = ExecInsert_(s);
    } else if (upper.find("UPDATE ") == 0) {
        r = ExecUpdate_(s);
    } else if (upper.find("DELETE FROM ") == 0) {
        r = ExecDelete_(s);
    } else if (upper.find("CREATE TABLE") == 0) {
        r.ok = true;
    } else {
        r.ok = false;
        r.error = "Unsupported SQL: " + s;
        ++totalErrors_;
    }
    return r;
}

// ── SELECT ──────────────────────────────────────────────────────

Result StubDatabase::ExecSelect_(const std::string& sql) {
    Result r; r.ok = true;
    std::string rest = SkipKeyword_(sql, "SELECT");
    rest = SkipWhitespace_(rest);

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

    auto wherePos = FindKeywordPos_(rest, "WHERE");
    std::string tableName = wherePos != std::string::npos
        ? TrimCopy_(rest.substr(0, wherePos))
        : TrimCopy_(rest);
    rest = wherePos != std::string::npos ? rest.substr(wherePos + 5) : "";
    rest = SkipWhitespace_(rest);

    std::string whereCol, whereVal;
    if (!rest.empty()) {
        auto eqPos = rest.find('=');
        if (eqPos != std::string::npos) {
            whereCol = TrimQuotes_(TrimCopy_(rest.substr(0, eqPos)));
            whereVal = TrimQuotes_(TrimCopy_(rest.substr(eqPos + 1)));
        }
    }

    auto it = table_.find(tableName);
    if (it == table_.end()) { r.rows = {}; return r; }

    const auto& data = it->second;
    const auto& schema = schemas_[tableName];

    for (const auto& rowVec : data) {
        Row row;
        for (const auto& colName : cols) {
            size_t colIdx = std::string::npos;
            for (size_t k = 0; k < schema.size(); ++k) {
                if (schema[k] == colName) { colIdx = k; break; }
            }
            if (colIdx == std::string::npos || colIdx >= rowVec.size()) continue;
            row[colName] = rowVec[colIdx];
        }
        if (!whereCol.empty()) {
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

// ── INSERT ──────────────────────────────────────────────────────

Result StubDatabase::ExecInsert_(const std::string& sql) {
    Result r; r.ok = true;
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

    table_[tableName];
    schemas_[tableName] = cols;

    std::vector<Value> row;
    row.reserve(vals.size());
    if (!cols.empty() && cols[0] == "id" && !vals.empty() && vals[0] == "NULL") {
        auto& data = table_[tableName];
        int64_t next_id = static_cast<int64_t>(data.size()) + 1;
        row.push_back(Value::FromInt(next_id));
        for (size_t j = 1; j < vals.size(); ++j) row.push_back(Value::FromStr(vals[j]));
    } else {
        for (auto& v : vals) row.push_back(Value::FromStr(v));
    }
    auto& data = table_[tableName];
    data.push_back(std::move(row));
    r.rowsAffected = 1;
    r.lastInsertId = data.back()[0].int_val;
    return r;
}

// ── UPDATE ──────────────────────────────────────────────────────

Result StubDatabase::ExecUpdate_(const std::string& sql) {
    Result r; r.ok = true;
    std::string rest = SkipKeyword_(sql, "UPDATE");
    auto setPos = FindKeywordPos_(rest, "SET");
    if (setPos == std::string::npos) { r.ok = false; r.error = "UPDATE: no SET"; ++totalErrors_; return r; }
    std::string tableName = TrimCopy_(rest.substr(0, setPos));
    rest = rest.substr(setPos + 3);
    rest = SkipWhitespace_(rest);

    auto wherePos = FindKeywordPos_(rest, "WHERE");
    std::string setClause = wherePos != std::string::npos ? rest.substr(0, wherePos) : rest;
    std::string whereClause = wherePos != std::string::npos ? rest.substr(wherePos + 5) : "";
    setClause = TrimCopy_(setClause);
    whereClause = TrimCopy_(whereClause);

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

    std::string wCol, wVal;
    if (!whereClause.empty()) {
        auto eq = whereClause.find('=');
        if (eq != std::string::npos) {
            wCol = TrimQuotes_(TrimCopy_(whereClause.substr(0, eq)));
            wVal = TrimQuotes_(TrimCopy_(whereClause.substr(eq + 1)));
        }
    }

    auto it = table_.find(tableName);
    if (it == table_.end()) { r.rowsAffected = 0; return r; }
    int affected = 0;
    for (auto& rowVec : it->second) {
        if (!wCol.empty()) {
            size_t whereIdx = std::string::npos;
            auto& schema = schemas_[tableName];
            for (size_t k = 0; k < schema.size(); ++k) {
                if (schema[k] == wCol) { whereIdx = k; break; }
            }
            if (whereIdx == std::string::npos || whereIdx >= rowVec.size()) continue;
            const auto& cell = rowVec[whereIdx];
            bool match = false;
            if (cell.type == ValueType::INT) {
                try { match = (cell.int_val == std::stoll(wVal)); } catch (...) {}
            } else {
                match = (cell.str_val == wVal);
            }
            if (!match) continue;
        }
        auto& schema = schemas_[tableName];
        size_t col_idx = 0;
        for (auto& [colName, newVal] : updates) {
            size_t targetIdx = 0;
            bool found = false;
            for (size_t k = 0; k < schema.size(); ++k) {
                if (schema[k] == colName) { targetIdx = k; found = true; break; }
            }
            if (!found || targetIdx >= rowVec.size()) continue;
            rowVec[targetIdx] = Value::FromStr(newVal);
        }
        ++affected;
    }
    r.rowsAffected = affected;
    return r;
}

// ── DELETE ──────────────────────────────────────────────────────

Result StubDatabase::ExecDelete_(const std::string& sql) {
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

    auto hasMatch = [&](const std::vector<Value>& row) -> bool {
        if (wCol.empty()) return true;
        if (row.empty()) return false;
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
        return match;
    };
    for (const auto& row : it->second) {
        if (hasMatch(row)) ++affected;
    }
    it->second.erase(
        std::remove_if(it->second.begin(), it->second.end(),
            [&](const std::vector<Value>& row) { return hasMatch(row); }),
        it->second.end());
    r.rowsAffected = affected;
    return r;
}

// ── String helpers ──────────────────────────────────────────────

std::string StubDatabase::TrimCopy_(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\n\r");
    return s.substr(start, end - start + 1);
}

std::string StubDatabase::SkipKeyword_(const std::string& s, const std::string& kw) {
    std::string us = s;
    for (auto& c : us) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    size_t pos = us.find(kw);
    if (pos == std::string::npos) return s;
    return s.substr(pos + kw.size());
}

size_t StubDatabase::FindKeywordPos_(const std::string& s, const std::string& kw) {
    std::string us = s;
    for (auto& c : us) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    return us.find(kw);
}

std::string StubDatabase::SkipWhitespace_(const std::string& s) {
    size_t pos = s.find_first_not_of(" \t\n\r");
    return (pos == std::string::npos) ? "" : s.substr(pos);
}

std::string StubDatabase::TrimQuotes_(const std::string& s) {
    if (s.size() >= 2 && s.front() == '\'' && s.back() == '\'')
        return s.substr(1, s.size() - 2);
    return s;
}

void StubDatabase::SplitByComma_(const std::string& s, std::vector<std::string>& out) {
    out.clear();
    std::string cur;
    for (char c : s) {
        if (c == ',') { out.push_back(TrimCopy_(cur)); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) out.push_back(TrimCopy_(cur));
}

} // namespace db
} // namespace snc
