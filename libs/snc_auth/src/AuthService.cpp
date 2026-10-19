// SPDX-License-Identifier: MIT
#include "snc/auth/AuthService.h"
#include "snc/base/Log.h"
#include <sstream>
#include <iomanip>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <random>

namespace snc {
namespace auth {

// ── JwtService ──────────────────────────────────────────────────

JwtService::JwtService(const JwtConfig& config) : config_(config) {
    // HS256 requires a key of at least 16 bytes for reasonable security.
    // Reject shorter keys outright to prevent trivially weak tokens.
    if (config.secret.size() < 16) {
        throw std::invalid_argument(
            "JWT secret must be at least 16 bytes (got " +
            std::to_string(config.secret.size()) + ")");
    }
    if (config.secret.size() < 32) {
        SNC_LOG_WARN("auth", "JWT secret is only %zu bytes (32+ bytes recommended for HS256)",
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

// ── SHA-256 ─────────────────────────────────────────────────────

namespace {
// Pure C++ SHA-256 implementation (FIPS 180-4)
struct Sha256Ctx {
    uint32_t h[8];
    uint64_t bitlen;
    uint8_t  buf[64];
};

static inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
static inline uint32_t sha256_ch(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (~x & z); }
static inline uint32_t sha256_maj(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (x & z) ^ (y & z); }
static inline uint32_t sha256_sig0(uint32_t x) { return rotr(x, 2)  ^ rotr(x, 13) ^ rotr(x, 22); }
static inline uint32_t sha256_sig1(uint32_t x) { return rotr(x, 6)  ^ rotr(x, 11) ^ rotr(x, 25); }
static inline uint32_t sha256_eps0(uint32_t x) { return rotr(x, 7)  ^ rotr(x, 18) ^ (x >> 3);  }
static inline uint32_t sha256_eps1(uint32_t x) { return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10); }

static const uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2,
};

static void sha256_transform(uint32_t h[8], const uint8_t block[64]) {
    uint32_t W[64];
    for (int i = 0; i < 16; ++i)
        W[i] = (uint32_t)block[i*4] << 24 | (uint32_t)block[i*4+1] << 16 |
               (uint32_t)block[i*4+2] << 8  | (uint32_t)block[i*4+3];
    for (int i = 16; i < 64; ++i)
        W[i] = sha256_eps1(W[i-2]) + W[i-7] + sha256_eps0(W[i-15]) + W[i-16];

    uint32_t a=h[0], b=h[1], c=h[2], d=h[3], e=h[4], f=h[5], g=h[6], hh=h[7];
    for (int i = 0; i < 64; ++i) {
        uint32_t t1 = hh + sha256_sig1(e) + sha256_ch(e,f,g) + K[i] + W[i];
        uint32_t t2 = sha256_sig0(a) + sha256_maj(a,b,c);
        hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
}

static void sha256_init(Sha256Ctx& ctx) {
    ctx.h[0]=0x6a09e667; ctx.h[1]=0xbb67ae85; ctx.h[2]=0x3c6ef372; ctx.h[3]=0xa54ff53a;
    ctx.h[4]=0x510e527f; ctx.h[5]=0x9b05688c; ctx.h[6]=0x1f83d9ab; ctx.h[7]=0x5be0cd19;
    ctx.bitlen = 0;
}

static void sha256_update(Sha256Ctx& ctx, const uint8_t* data, size_t len) {
    size_t off = (size_t)ctx.bitlen / 8 % 64;
    ctx.bitlen += (uint64_t)len * 8;
    for (size_t i = 0; i < len; ++i) {
        ctx.buf[off++] = data[i];
        if (off == 64) { sha256_transform(ctx.h, ctx.buf); off = 0; }
    }
}

static std::string sha256_final(const Sha256Ctx& ctx) {
    // Work on a local copy to avoid mutating the const ctx
    Sha256Ctx tmp;
    tmp.bitlen = ctx.bitlen;
    std::memcpy(tmp.h, ctx.h, sizeof(tmp.h));
    std::memcpy(tmp.buf, ctx.buf, sizeof(tmp.buf));

    size_t off = (size_t)(tmp.bitlen / 8 % 64);
    uint8_t* b = tmp.buf;
    b[off++] = 0x80;
    if (off > 56) { std::memset(b+off, 0, 64-off); sha256_transform(tmp.h, b); std::memset(b, 0, 56); }
    else { std::memset(b+off, 0, 56-off); }
    uint8_t* tail = b + 56;
    tail[0] = (uint8_t)(tmp.bitlen >> 56); tail[1] = (uint8_t)(tmp.bitlen >> 48);
    tail[2] = (uint8_t)(tmp.bitlen >> 40); tail[3] = (uint8_t)(tmp.bitlen >> 32);
    tail[4] = (uint8_t)(tmp.bitlen >> 24); tail[5] = (uint8_t)(tmp.bitlen >> 16);
    tail[6] = (uint8_t)(tmp.bitlen >> 8);  tail[7] = (uint8_t)(tmp.bitlen);
    sha256_transform(tmp.h, b);
    std::string result;
    result.reserve(32);
    for (int i = 0; i < 8; ++i) {
        result += (char)(tmp.h[i] >> 24);
        result += (char)((tmp.h[i] >> 16) & 0xff);
        result += (char)((tmp.h[i] >> 8)  & 0xff);
        result += (char)(tmp.h[i] & 0xff);
    }
    return result;
}

// ── HMAC-SHA256 (RFC 2104) ──────────────────────────────────────
static std::string hmac_sha256(const std::string& key, const std::string& message) {
    uint8_t k[64] = {0};
    if (key.size() > 64) {
        Sha256Ctx tmp; sha256_init(tmp); sha256_update(tmp, (const uint8_t*)key.data(), key.size());
        std::string hash = sha256_final(tmp);
        std::memcpy(k, hash.data(), 32);
    } else {
        std::memcpy(k, key.data(), key.size());
    }

    uint8_t ipad[64], opad[64];
    for (int i = 0; i < 64; ++i) { ipad[i] = k[i] ^ 0x36; opad[i] = k[i] ^ 0x5c; }

    Sha256Ctx inner; sha256_init(inner); sha256_update(inner, ipad, 64);
    sha256_update(inner, (const uint8_t*)message.data(), message.size());
    std::string inner_hash = sha256_final(inner);

    Sha256Ctx outer; sha256_init(outer); sha256_update(outer, opad, 64);
    sha256_update(outer, (const uint8_t*)inner_hash.data(), 32);
    return sha256_final(outer);
}
} // namespace

std::string JwtService::Sha256(const std::string& data) {
    Sha256Ctx ctx; sha256_init(ctx); sha256_update(ctx, (const uint8_t*)data.data(), data.size());
    return sha256_final(ctx);
}

std::string JwtService::HmacSha256(const std::string& data, const std::string& key) {
    return hmac_sha256(key, data);
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

std::string HashSha256(const std::string& data) {
    Sha256Ctx ctx; sha256_init(ctx); sha256_update(ctx, (const uint8_t*)data.data(), data.size());
    std::string raw = sha256_final(ctx);
    std::ostringstream oss;
    for (unsigned char c : raw) oss << std::hex << std::setw(2) << std::setfill('0') << (uint32_t)c;
    return oss.str();
}

// ── Password hashing with salt ──────────────────────────────────

namespace {
std::string GenerateRandomHex_(size_t bytes) {
    static thread_local std::mt19937 rng(std::random_device{}());
    static thread_local std::uniform_int_distribution<int> dist(0, 15);
    std::ostringstream oss;
    for (size_t i = 0; i < bytes; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)dist(rng);
    }
    return oss.str();
}
} // namespace

std::string HashPasswordWithSalt(const std::string& password) {
    std::string salt = GenerateRandomHex_(16); // 16 bytes = 32 hex chars
    std::string hash = HashSha256(salt + password);
    return salt + "$" + hash;
}

std::string ExtractSalt_(const std::string& salted_hash) {
    auto pos = salted_hash.find('$');
    if (pos == std::string::npos || pos == 0 || pos + 1 >= salted_hash.size()) return "";
    return salted_hash.substr(0, pos);
}

} // namespace auth
} // namespace snc
