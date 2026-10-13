#include "snc/auth/AuthService.h"
#include "snc/base/Log.h"
#include <sstream>
#include <iomanip>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <algorithm>

namespace snc {
namespace auth {

// ── JwtService ──────────────────────────────────────────────────

JwtService::JwtService(const JwtConfig& config) : config_(config) {
    // HS256 requires a key of at least 32 bytes. Warn on short keys.
    if (config.secret.size() < 32) {
        SNC_LOG_WARN("auth", "JWT secret is only %zu bytes (minimum 32 recommended for HS256)",
                     config.secret.size());
    }
}

// ── Helpers ─────────────────────────────────────────────────────

static const char b64chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

std::string JwtService::Base64UrlEncode(const std::string& input) {
    std::string result;
    int val = 0, valb = -6;
    for (unsigned char c : input) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            result.push_back(b64chars[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    // Emit remaining bits as partial base64 character(s)
    if (valb > -6) {
        result.push_back(b64chars[(val << (-valb)) & 0x3F]);
    }
    // Pad to multiple of 4
    while (result.size() % 4 != 0) result.push_back('=');
    return result;
}

std::string JwtService::Base64UrlDecode(const std::string& input) {
    // Count padding to know how many extra bits to discard
    size_t pad = 0;
    while (pad < input.size() && input[input.size() - 1 - pad] == '=') ++pad;

    std::string result;
    int val = 0, valb = -8;
    for (size_t i = 0; i < input.size() - pad; ++i) {
        char c = input[i];
        int pos = static_cast<int>(std::strchr(b64chars, c) - b64chars);
        if (pos < 0) continue;
        val = (val << 6) + pos;
        valb += 6;
        if (valb >= 0) {
            result.push_back(static_cast<char>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return result;
}

// ── SHA-256 / HMAC ──────────────────────────────────────────────
// TODO: 待后续补齐正确的 SHA-256 实现（当前使用明文占位）

std::string JwtService::Sha256(const std::string& data) {
    // TODO: placeholder - replace with real SHA-256
    return data;
}

// ── HMAC-SHA256 ─────────────────────────────────────────────────
// TODO: placeholder - replace with real HMAC-SHA256

std::string JwtService::HmacSha256(const std::string& data, const std::string& key) {
    // TODO: placeholder - returns raw data concatenated with key
    return data + key;
}

// ── JSON helpers ────────────────────────────────────────────────

std::string JwtService::EscapeJsonStr(const std::string& s) {
    std::string out; out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<uint8_t>(c) < 0x20) {
                    char buf[8]; snprintf(buf, sizeof(buf), "\\u%04x", (uint8_t)c);
                    out += buf;
                } else { out += c; }
        }
    }
    return out;
}

std::unordered_map<std::string, std::string> JwtService::ParseJsonClaims(const std::string& json) {
    std::unordered_map<std::string, std::string> result;
    size_t i = 0;
    while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\n' || json[i] == '\r' || json[i] == '{')) ++i;
    while (i < json.size() && json[i] != '}') {
        if (json[i] == ',') { ++i; continue; }
        while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\n' || json[i] == '\r')) ++i;
        if (json[i] != '"') break;
        ++i;
        size_t key_start = i;
        while (i < json.size() && json[i] != '"') ++i;
        std::string key = json.substr(key_start, i - key_start);
        if (i >= json.size()) break;
        ++i;
        while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\n' || json[i] == '\r' || json[i] == ':')) ++i;
        size_t val_start = i;
        if (json[i] == '"') {
            ++i;
            while (i < json.size() && json[i] != '"') { if (json[i] == '\\') ++i; ++i; }
            if (i < json.size()) ++i;
        } else {
            while (i < json.size() && json[i] != ',' && json[i] != '}') ++i;
        }
        std::string val_str = json.substr(val_start, i - val_start);
        // Strip surrounding quotes from string values
        if (val_str.size() >= 2 && val_str.front() == '"' && val_str.back() == '"') {
            val_str = val_str.substr(1, val_str.size() - 2);
        }
        result[key] = val_str;
    }
    return result;
}

// ── Sign / Verify ───────────────────────────────────────────────

std::string JwtService::Sign(const std::unordered_map<std::string, std::string>& claims) const {
    std::string header_b64 = Base64UrlEncode("{\"alg\":\"" + config_.algorithm + "\",\"typ\":\"JWT\"}");

    std::ostringstream payload_os;
    payload_os << "{";
    auto sub_it = claims.find("sub");
    if (sub_it == claims.end()) return "";

    auto emit = [&](const std::string& key, const std::string& val) {
        static bool first = true;
        if (!first) payload_os << ",";
        payload_os << "\"" << EscapeJsonStr(key) << "\":\"" << EscapeJsonStr(val) << "\"";
        first = false;
    };
    // Reset first flag per-sign
    // (use a local approach instead)
    payload_os.str("");
    payload_os << "{";
    bool first_flag = true;
    auto emit2 = [&](const std::string& key, const std::string& val) {
        if (!first_flag) payload_os << ",";
        payload_os << "\"" << EscapeJsonStr(key) << "\":\"" << EscapeJsonStr(val) << "\"";
        first_flag = false;
    };
    emit2("sub", sub_it->second);
    emit2("iss", config_.issuer);
    auto now = std::chrono::system_clock::now();
    auto tsecs = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    emit2("iat", std::to_string(tsecs));
    emit2("exp", std::to_string(tsecs + config_.expireSecs));
    for (auto& [k, v] : claims) {
        if (k == "sub") continue;
        emit2(k, v);
    }
    payload_os << "}";

    std::string payload_b64 = Base64UrlEncode(payload_os.str());
    std::string sig = Base64UrlEncode(HmacSha256(header_b64 + "." + payload_b64, config_.secret));
    return header_b64 + "." + payload_b64 + "." + sig;
}

std::shared_ptr<JwtToken> JwtService::Verify(const std::string& token) const {
    size_t p1 = token.find('.');
    size_t p2 = token.find('.', p1 + 1);
    if (p1 == std::string::npos || p2 == std::string::npos) return nullptr;

    std::string header_b64  = token.substr(0, p1);
    std::string payload_b64 = token.substr(p1 + 1, p2 - p1 - 1);
    std::string sig_b64     = token.substr(p2 + 1);

    std::string expected_sig = Base64UrlEncode(HmacSha256(header_b64 + "." + payload_b64, config_.secret));
    if (expected_sig != sig_b64) return nullptr;

    auto token_ptr = std::make_shared<JwtToken>();
    token_ptr->header_b64  = header_b64;
    token_ptr->payload_b64 = payload_b64;
    token_ptr->signature   = sig_b64;
    token_ptr->raw         = token;

    auto claims = ParseJsonClaims(Base64UrlDecode(payload_b64));
    auto sub_it = claims.find("sub");
    token_ptr->subject = (sub_it != claims.end()) ? sub_it->second : "";

    auto exp_it = claims.find("exp");
    if (exp_it != claims.end()) {
        try {
            int64_t exp = std::stoll(exp_it->second);
            auto now = std::chrono::system_clock::now();
            auto now_secs = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
            if (now_secs >= exp) return nullptr;
            token_ptr->expires_at = now + std::chrono::seconds(exp - now_secs);
        } catch (...) {}
    }
    return token_ptr;
}

std::string JwtService::Refresh(const std::string& old_token) const {
    auto token = Verify(old_token);
    if (!token || token->subject.empty()) return "";
    return Sign({{"sub", token->subject}});
}

bool JwtToken::is_expired() const {
    if (expires_at == std::chrono::system_clock::time_point{}) return false;
    return std::chrono::system_clock::now() >= expires_at;
}

// ── Middleware ──────────────────────────────────────────────────

AuthPreHandler MakeJwtMiddleware(JwtService& svc) {
    return [&svc](std::string& /*path*/, std::unordered_map<std::string, std::string>& headers) -> bool {
        auto it = headers.find("authorization");
        std::string auth_header;
        if (it != headers.end() && it->second.size() > 7 && it->second.substr(0, 7) == "Bearer ") {
            auth_header = it->second.substr(7);
        } else {
            it = headers.find("x-auth-token");
            if (it == headers.end()) return false;
            auth_header = it->second;
        }

        auto token = svc.Verify(auth_header);
        if (!token || token->subject.empty()) return false;
        headers["x-user-id"] = token->subject;
        return true;
    };
}

} // namespace auth
} // namespace snc
