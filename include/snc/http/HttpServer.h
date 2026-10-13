#pragma once

// snc_http — 轻量级 HTTP 服务器模块
//
// 零外部依赖，自包含实现。
// 功能：
//   - HTTP/1.1 解析（GET, POST, PUT, DELETE）
//   - 路由表 + 路径参数匹配（如 /api/admin/player/:id）
//   - JSON 响应构建
//   - 中间件链（在 handler 前执行，返回 false 可中断请求）
//   - 基于已有 TCP Socket 基础设施运行

#include <cstdint>
#include <string>
#include <memory>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <algorithm>
#include <sstream>
#include <cstring>
#include <cctype>
#include "snc/base/Common.h"

#ifdef _WIN32
// windows.h 已通过 Common.h 引入；取消定义以下宏以避免与 HttpMethod 枚举冲突
#ifdef GET
#undef GET
#endif
#ifdef POST
#undef POST
#endif
#ifdef PUT
#undef PUT
#endif
#ifdef DELETE
#undef DELETE
#endif
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#endif

namespace snc {
namespace http {

// ── 请求 / 响应类型 ────────────────────────────────────────────

enum class HttpMethod : uint8_t {
    HTTP_GET    = 0,
    HTTP_POST   = 1,
    HTTP_PUT    = 2,
    HTTP_DELETE = 3,
    HTTP_HEAD   = 4,
    HTTP_OPTIONS = 5,
    HTTP_PATCH  = 6,
    HTTP_UNKNOWN = 7,
};

/// HttpRequest — 解析后的 HTTP 请求
struct HttpRequest {
    HttpMethod method    = HttpMethod::HTTP_UNKNOWN;
    std::string  path;                                          ///< 请求路径
    std::string  query_string;                                  ///< URL 查询字符串
    std::unordered_map<std::string, std::string> headers;      ///< 请求头
    std::string  body;                                          ///< 请求体

    /// 按 key 查找请求头，未找到返回空字符串
    std::string GetHeader(const std::string& key) const {
        auto it = headers.find(key);
        return (it != headers.end()) ? it->second : "";
    }

    /// 解析 body 中的简单 JSON 对象为键值对（仅支持扁平结构，值为 string/number/bool）
    std::unordered_map<std::string, std::string> JsonBody() const;
};

/// HttpResponse — HTTP 响应
struct HttpResponse {
    int     status_code = 200;
    std::string body;
    std::unordered_map<std::string, std::string> headers;

    /// 快捷工厂：返回 JSON 格式响应
    static HttpResponse Json(int code, const std::string& json_body) {
        HttpResponse r;
        r.status_code = code;
        r.body = json_body;
        r.headers["Content-Type"] = "application/json; charset=utf-8";
        return r;
    }

    static HttpResponse Ok(const std::string& body = "")      { return Json(200, body); }
    static HttpResponse NotFound()                            { return Json(404, "{\"error\":\"not_found\"}"); }
    static HttpResponse BadRequest()                          { return Json(400, "{\"error\":\"bad_request\"}"); }
    static HttpResponse Unauthorized()                        { return Json(401, "{\"error\":\"unauthorized\"}"); }
    static HttpResponse Forbidden()                           { return Json(403, "{\"error\":\"forbidden\"}"); }
    static HttpResponse InternalError(const std::string& msg = "internal error") {
        return Json(500, "{\"error\":\"" + msg + "\"}");
    }
};

using Handler   = std::function<HttpResponse(const HttpRequest&)>; ///< 路由处理器
using Middleware = std::function<bool(HttpRequest&, HttpResponse&)>; ///< 中间件（返回 false 中断请求链）

// ── 路由 ────────────────────────────────────────────────────────

/// Route — 单条路由规则，含路径模式、方法和处理器
class Route {
public:
    std::string       pattern;   ///< 路径模式，如 "/api/auth/wx-login" 或 "/api/admin/player/:id"
    HttpMethod        method;
    Handler           handler;

    /// 从路径中提取命名参数："/api/admin/player/:id" 传入 "123" → {"id": "123"}
    std::unordered_map<std::string, std::string> MatchParams(const std::string& path) const;

    bool Matches(const std::string& path, HttpMethod m) const {
        if (m != method) return false;
        auto params = MatchParams(path);
        return !params.empty();
    }
};

// ── 路由器 ──────────────────────────────────────────────────────

/// Router — 路由注册表，支持按路径模式和方法查找处理器
class Router {
public:
    void Get(const std::string& path, Handler h)      { routes_.push_back({path, HttpMethod::HTTP_GET,    std::move(h)}); }
    void Post(const std::string& path, Handler h)     { routes_.push_back({path, HttpMethod::HTTP_POST,   std::move(h)}); }
    void Put(const std::string& path, Handler h)      { routes_.push_back({path, HttpMethod::HTTP_PUT,    std::move(h)}); }
    void Delete(const std::string& path, Handler h)   { routes_.push_back({path, HttpMethod::HTTP_DELETE, std::move(h)}); }

    /// 查找匹配的路由处理器；未找到返回 nullptr
    const Handler* Find(const std::string& path, HttpMethod method) const {
        for (auto& r : routes_) {
            if (r.method == method && !r.MatchParams(path).empty()) {
                return &r.handler;
            }
        }
        return nullptr;
    }

    /// 查找匹配路由并返回提取的路径参数
    std::unordered_map<std::string, std::string> ResolveParams(const std::string& path,
                                                               HttpMethod method) const {
        for (auto& r : routes_) {
            if (r.method == method) {
                auto params = r.MatchParams(path);
                if (!params.empty()) return params;
            }
        }
        return {};
    }

private:
    std::vector<Route> routes_;
};

// ── HTTP 服务器 ─────────────────────────────────────────────────

/// HttpServer — 基于 select/IOCP 的轻量 HTTP 服务器
///
/// 工作流程：
///   1. Listen(port) 绑定并监听端口，返回 true 表示成功
///   2. Run() 阻塞启动事件循环，accept 新连接后分发到 HandleClient_
///   3. 每个客户端连接在独立线程中运行，处理完请求后关闭
class HttpServer {
public:
    HttpServer();
    ~HttpServer();

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    Router& Routes() { return routes_; }

    /// 添加中间件（在每个 handler 之前执行；返回 false 则中止请求）
    void Use(Middleware mw) { middlewares_.push_back(std::move(mw)); }

    /// 监听指定端口，返回 true 表示成功
    bool Listen(int port);
    /// 启动事件循环（阻塞调用）
    void Run();
    /// 停止服务器并关闭所有连接
    void Stop();

    /// 当前活跃连接数
    size_t ActiveConnections() const;

private:
    void AcceptLoop_();    ///< 接受新连接并分发到事件循环
    void EventLoop_();     ///< 主事件循环（select/IOCP），处理 IO 多路复用
    void HandleClient_(int clientFd);   ///< 处理单个客户端的 HTTP 请求
    void SendResponse_(int fd, const HttpResponse& resp); ///< 将响应写入 socket

    Router             routes_;                              ///< 路由注册表
    std::vector<Middleware> middlewares_;                    ///< 中间件链
    int                 listenFd_ = -1;                     ///< 监听 socket
    std::atomic<bool>   running_{false};                     ///< 服务器运行标志
    std::thread         eventThread_;                        ///< 事件循环线程

    mutable std::mutex              connMutex_;               ///< 保护 connTimes_
    std::unordered_map<int, std::chrono::steady_clock::time_point> connTimes_; ///< 各连接的建立时间
};

} // namespace http
} // namespace snc
