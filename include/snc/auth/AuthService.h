#pragma once

// snc_auth — JWT 认证工具模块
//
// 提供基于 HS256 的 JWT 签名与校验，不含任何业务逻辑。
// 微信登录 / SSO 等流程属于业务层，由用户在 JwtService 基础上自行实现。

#include <string>
#include <unordered_map>
#include <memory>
#include <chrono>
#include <functional>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

namespace snc {
namespace auth {

// ── JWT 配置 ────────────────────────────────────────────────────

struct JwtConfig {
    std::string secret;      ///< 签名密钥（HS256 要求至少 32 字节）
    int         expireSecs  = 7200; ///< 默认有效期 2 小时
    std::string issuer      = "snc"; ///< 签发者标识
    std::string algorithm   = "HS256"; ///< 签名算法
};

// ── JWT Token ───────────────────────────────────────────────────

struct JwtToken {
    std::string header_b64;   ///< Base64url 编码的 Header
    std::string payload_b64;  ///< Base64url 编码的 Payload
    std::string signature;    ///< Base64url 编码的签名
    std::string raw;          ///< 完整 Token 字符串（header.payload.signature）

    // 从解码后的 Payload 提取的声明
    std::string subject;            ///< sub — 用户 ID
    std::string issuer_;
    std::string audience_;
    std::chrono::system_clock::time_point issued_at;   ///< 签发时间
    std::chrono::system_clock::time_point expires_at;  ///< 过期时间
    bool is_expired() const;
};

// ── JWT 签名/校验服务 ─────────────────────────────────────────────

class JwtService {
public:
    explicit JwtService(const JwtConfig& config);

    /// 使用给定 claims 签发 Token，sub（用户 ID）为必填项
    std::string Sign(const std::unordered_map<std::string, std::string>& claims) const;

    /// 校验并解码 Token，无效或已过期返回 nullptr
    std::shared_ptr<JwtToken> Verify(const std::string& token) const;

    /// 刷新 Token：保持相同 subject，重新签发新的过期时间
    std::string Refresh(const std::string& old_token) const;

    const JwtConfig& Config() const { return config_; }

private:
    static std::string Base64UrlEncode(const std::string& input);
    static std::string Base64UrlDecode(const std::string& input);
    static std::string HmacSha256(const std::string& data, const std::string& key);
    static std::string Sha256(const std::string& data);
    static std::unordered_map<std::string, std::string> ParseJsonClaims(const std::string& json);
    static std::string EscapeJsonStr(const std::string& s);

    JwtConfig config_;
};

// ── HTTP 中间件 ──────────────────────────────────────────────────

/// 从 Authorization 或 x-auth-token 请求头中提取并校验 JWT。
/// 校验成功时在 headers 中写入 "x-user-id"；失败返回 false。
using AuthPreHandler = std::function<bool(std::string&, std::unordered_map<std::string, std::string>&)>;

/// 基于 JwtService 构建认证中间件；传入 nullptr token 则跳过校验
AuthPreHandler MakeJwtMiddleware(JwtService& svc);

/// SHA-256 哈希（十六进制字符串），供账号密码哈希等场景使用
std::string HashSha256(const std::string& data);

} // namespace auth
} // namespace snc
