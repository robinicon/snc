/// @file MasterService.cpp
/// @brief Master 服 — 全局服务状态管理、GM 命令分发、健康统计（stub 实现）。

#include "snc/mmorpg/MasterService.h"
#include "snc/http/HttpServer.h"
#include "snc/auth/AuthService.h"
#include "snc/base/Log.h"

#include <chrono>
#include <sstream>
#include <iomanip>

namespace snc {
namespace mmorpg {

MasterService::MasterService(discovery::ServiceRegistry::Ptr registry)
    : registry_(std::move(registry)) {}

void MasterService::RegisterRpcHandlers(rpc::RpcServer& rpcServer) {
    using namespace rpc;
    rpcServer.RegisterHandler(0x3000,
        [this](IConnection::Ptr conn, RpcSession::Ptr session, Msg::Ptr msg,
               std::function<void(RpcError, const uint8_t*, size_t)> respond) {
            OnHeartbeat(conn, session, std::move(msg), std::move(respond));
        });
    rpcServer.RegisterHandler(0x3001,
        [this](IConnection::Ptr conn, RpcSession::Ptr session, Msg::Ptr msg,
               std::function<void(RpcError, const uint8_t*, size_t)> respond) {
            OnGmCommand(conn, session, std::move(msg), std::move(respond));
        });
    rpcServer.RegisterHandler(0x3002,
        [this](IConnection::Ptr conn, RpcSession::Ptr session, Msg::Ptr msg,
               std::function<void(RpcError, const uint8_t*, size_t)> respond) {
            OnQueryServices(conn, session, std::move(msg), std::move(respond));
        });
}

void MasterService::RegisterAdminRoutes(http::Router& routes, auth::JwtService& jwtSvc) {
    (void)jwtSvc;
    routes.Get("/api/admin/services",
        [this](const http::HttpRequest&) -> http::HttpResponse {
            return http::HttpResponse::Json(200, QueryServiceStatus());
        });
    routes.Post("/api/admin/gm",
        [this](const http::HttpRequest& req) -> http::HttpResponse {
            auto body = req.JsonBody();
            auto gv = [&](const std::string& k) -> std::string {
                auto it = body.find(k);
                return (it != body.end()) ? it->second : std::string();
            };
            GmCommand cmd;
            cmd.command    = gv("command");
            cmd.target     = gv("target");
            cmd.args       = gv("args");
            cmd.operator_id = gv("operator_id");
            if (cmd.command.empty()) return http::HttpResponse::BadRequest();
            std::string result = ExecuteGm(cmd);
            return http::HttpResponse::Json(200, result);
        });
}

std::string MasterService::QueryServiceStatus() {
    std::ostringstream oss;
    oss << "{\"services\":[";
    bool first = true;
    if (registry_) {
        for (auto& name : registry_->ListServices()) {
            auto list = registry_->GetAll(name);
            for (auto& ep : list) {
                if (!first) oss << ",";
                oss << "{\"name\":\"" << ep->serviceName
                    << "\",\"host\":\"" << ep->host
                    << "\",\"port\":" << ep->port
                    << ",\"weight\":" << ep->weight
                    << ",\"healthy\":" << (ep->healthy ? "true" : "false")
                    << "}";
                first = false;
            }
        }
    }
    oss << "],\"count\":" << (registry_ ? registry_->EndpointCount() : 0) << "}";
    return oss.str();
}

std::string MasterService::ExecuteGm(const GmCommand& cmd) {
    std::ostringstream oss;
    oss << "{\"result\":\"executed\",\"command\":\"" << cmd.command
        << "\",\"target\":\"" << cmd.target
        << "\",\"operator\":\"" << cmd.operator_id << "\"}";
    AppendHeartbeatLog_("GM: " + cmd.command + " target=" + cmd.target);
    return oss.str();
}

std::vector<std::string> MasterService::GetHeartbeatLog(int limit) const {
    std::lock_guard<std::mutex> lk(log_mtx_);
    std::vector<std::string> out;
    int n = 0;
    for (auto it = heartbeat_log_.rbegin(); it != heartbeat_log_.rend() && n < limit; ++it, ++n)
        out.push_back(*it);
    return out;
}

// ── 私有 RPC 处理器 ────────────────────────────────────────────────

void MasterService::OnHeartbeat(core::IConnection::Ptr conn,
                                rpc::RpcSession::Ptr session,
                                core::Msg::Ptr msg,
                                std::function<void(rpc::RpcError, const uint8_t*, size_t)> respond) {
    (void)conn; (void)session;
    if (!msg || !msg->Payload()) {
        respond(rpc::RpcError::UNKNOWN, nullptr, 0);
        return;
    }
    HeartbeatPayload hb{};
    std::string s(msg->Payload(), msg->PayloadLen());
    auto get_val = [&](const std::string& key) -> std::string {
        std::string kv = "\"" + key + "\":\"";
        auto pos = s.find(kv);
        if (pos == std::string::npos) return "";
        pos += kv.size();
        auto end = s.find('"', pos);
        return (end != std::string::npos) ? s.substr(pos, end - pos) : "";
    };
    auto get_int = [&](const std::string& key) -> int {
        std::string kv = "\"" + key + "\":";
        auto pos = s.find(kv);
        if (pos == std::string::npos) return 0;
        pos += kv.size();
        try { return std::stoi(s.substr(pos)); } catch (...) { return 0; }
    };
    hb.serviceName = get_val("service_name");
    hb.type        = static_cast<ServiceType>(get_int("type"));
    hb.playerCount = get_int("player_count");
    hb.mapCount    = get_int("map_count");
    hb.uptimeSecs  = get_int("uptime_secs");

    if (!hb.serviceName.empty() && registry_) {
        registry_->Register(hb.serviceName, "127.0.0.1", 0, 1);
        auto ts = std::chrono::system_clock::now();
        auto t  = std::chrono::duration_cast<std::chrono::seconds>(ts.time_since_epoch()).count();
        AppendHeartbeatLog_(std::to_string(t) + " HEARTBEAT " + hb.serviceName
                           + " type=" + std::to_string((int)hb.type)
                           + " players=" + std::to_string(hb.playerCount));
    }

    std::string resp_body = "{\"status\":\"ok\",\"service\":\"" + hb.serviceName + "\"}";
    respond(rpc::RpcError::OK,
            reinterpret_cast<const uint8_t*>(resp_body.c_str()),
            resp_body.size());
}

void MasterService::OnGmCommand(core::IConnection::Ptr conn,
                                rpc::RpcSession::Ptr session,
                                core::Msg::Ptr msg,
                                std::function<void(rpc::RpcError, const uint8_t*, size_t)> respond) {
    (void)conn; (void)session;
    if (!msg || !msg->Payload()) {
        respond(rpc::RpcError::UNKNOWN, nullptr, 0);
        return;
    }
    GmCommand cmd;
    std::string s(msg->Payload(), msg->PayloadLen());
    auto get_val = [&](const std::string& key) -> std::string {
        std::string kv = "\"" + key + "\":\"";
        auto pos = s.find(kv);
        if (pos == std::string::npos) return "";
        pos += kv.size();
        auto end = s.find('"', pos);
        return (end != std::string::npos) ? s.substr(pos, end - pos) : "";
    };
    cmd.command    = get_val("command");
    cmd.target     = get_val("target");
    cmd.args       = get_val("args");
    cmd.operator_id = get_val("operator_id");

    std::string result = ExecuteGm(cmd);
    respond(rpc::RpcError::OK,
            reinterpret_cast<const uint8_t*>(result.c_str()),
            result.size());
}

void MasterService::OnQueryServices(core::IConnection::Ptr conn,
                                    rpc::RpcSession::Ptr session,
                                    core::Msg::Ptr msg,
                                    std::function<void(rpc::RpcError, const uint8_t*, size_t)> respond) {
    (void)conn; (void)session; (void)msg;
    std::string status = QueryServiceStatus();
    respond(rpc::RpcError::OK,
            reinterpret_cast<const uint8_t*>(status.c_str()),
            status.size());
}

void MasterService::AppendHeartbeatLog_(const std::string& entry) {
    std::lock_guard<std::mutex> lk(log_mtx_);
    heartbeat_log_.push_back(entry);
    if (heartbeat_log_.size() > 200)
        heartbeat_log_.erase(heartbeat_log_.begin());
}

} // namespace mmorpg
} // namespace snc
