// SPDX-License-Identifier: MIT
#pragma once

/// @file StubDatabase.h
/// @brief In-memory stub database implementation for testing.
///
/// Provides a minimal SQL-compatible in-memory store supporting INSERT,
/// SELECT, UPDATE, DELETE, and CREATE TABLE (no-op).

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <memory>
#include <atomic>
#include <chrono>

#include "snc/db/Database.h"

namespace snc {
namespace db {

class StubDatabase : public IDatabase {
public:
    bool Connect(const std::string&) override;
    void Disconnect() override;
    bool IsConnected() const override;

    Result Query(const std::string& sql) override;
    Stats GetStats() const override;

private:
    // ── SQL parser / executors ──────────────────────────────────
    Result ParseAndExec_(const std::string& sql);
    Result ExecSelect_(const std::string& sql);
    Result ExecInsert_(const std::string& sql);
    Result ExecUpdate_(const std::string& sql);
    Result ExecDelete_(const std::string& sql);

    // ── String helpers ──────────────────────────────────────────
    static std::string TrimCopy_(const std::string& s);
    static std::string SkipKeyword_(const std::string& s, const std::string& kw);
    static size_t FindKeywordPos_(const std::string& s, const std::string& kw);
    static std::string SkipWhitespace_(const std::string& s);
    static std::string TrimQuotes_(const std::string& s);
    static void SplitByComma_(const std::string& s, std::vector<std::string>& out);

    // ── Storage ─────────────────────────────────────────────────
    std::unordered_map<std::string, std::vector<std::vector<Value>>> table_;
    std::unordered_map<std::string, std::vector<std::string>>        schemas_;
    bool                             connected_ = false;
    std::atomic<int64_t>             totalQueries_{0};
    std::atomic<int64_t>             totalErrors_{0};
    double                           totalLatencyMs_ = 0.0;
};

/// Factory: create a StubDatabase instance.
std::shared_ptr<IDatabase> CreateStubDatabase();

} // namespace db
} // namespace snc
