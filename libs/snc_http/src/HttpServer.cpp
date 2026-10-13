#include "snc/http/HttpServer.h"

#include <chrono>
#include <iomanip>
#include <numeric>
#include <vector>

namespace snc {
namespace http {

// ── Helpers ─────────────────────────────────────────────────────

static std::string TrimStr(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end   = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static std::string ToLowerStr(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    return out;
}

static std::vector<std::string> SplitString(const std::string& s, char delim) {
    std::vector<std::string> parts;
    std::stringstream ss(s);
    std::string part;
    while (std::getline(ss, part, delim)) parts.push_back(part);
    return parts;
}

// ── HttpRequest::JsonBody ───────────────────────────────────────

std::unordered_map<std::string, std::string> HttpRequest::JsonBody() const {
    std::unordered_map<std::string, std::string> result;
    if (body.empty() || body[0] != '{') return result;

    // Minimal JSON parser: finds "key": value pairs
    size_t i = 1; // skip '{'
    while (i < body.size()) {
        // Skip whitespace
        while (i < body.size() && std::isspace(static_cast<unsigned char>(body[i]))) ++i;
        if (i >= body.size() || body[i] == '}') break;

        // Parse key (quoted string)
        if (body[i] != '"') { ++i; continue; }
        ++i;
        size_t keyStart = i;
        while (i < body.size() && body[i] != '"') {
            if (body[i] == '\\') ++i;
            ++i;
        }
        std::string key = body.substr(keyStart, i - keyStart);
        if (i >= body.size()) break;
        ++i; // skip closing quote

        // Skip whitespace and colon
        while (i < body.size() && std::isspace(static_cast<unsigned char>(body[i]))) ++i;
        if (i >= body.size() || body[i] != ':') break;
        ++i;
        while (i < body.size() && std::isspace(static_cast<unsigned char>(body[i]))) ++i;

        // Parse value
        std::string value;
        if (i < body.size() && body[i] == '"') {
            // String value
            ++i;
            size_t valStart = i;
            while (i < body.size() && body[i] != '"') {
                if (body[i] == '\\') ++i;
                ++i;
            }
            value = body.substr(valStart, i - valStart);
            if (i < body.size()) ++i; // skip closing quote
        } else {
            // Number / bool / null
            size_t valStart = i;
            while (i < body.size() && body[i] != ',' && body[i] != '}') ++i;
            value = body.substr(valStart, i - valStart);
        }

        result[key] = TrimStr(value);

        // Skip comma
        while (i < body.size() && std::isspace(static_cast<unsigned char>(body[i]))) ++i;
        if (i < body.size() && body[i] == ',') ++i;
    }
    return result;
}

// ── Route::MatchParams ──────────────────────────────────────────

std::unordered_map<std::string, std::string> Route::MatchParams(const std::string& path) const {
    auto pathParts  = SplitString(path, '/');
    auto patternParts = SplitString(pattern, '/');

    if (pathParts.empty() || patternParts.empty()) return {};
    // Both start with empty string due to leading '/'
    if (pathParts.front().empty()) pathParts.erase(pathParts.begin());
    if (patternParts.front().empty()) patternParts.erase(patternParts.begin());

    if (pathParts.size() != patternParts.size()) return {};

    std::unordered_map<std::string, std::string> params;
    for (size_t i = 0; i < patternParts.size(); ++i) {
        if (patternParts[i].size() > 0 && patternParts[i][0] == ':') {
            // Parameter
            params[patternParts[i].substr(1)] = pathParts[i];
        } else if (patternParts[i] != pathParts[i]) {
            return {};
        }
    }
    return params;
}

// ── HttpServer ──────────────────────────────────────────────────

namespace {

#ifdef _WIN32
using SocketType = SOCKET;
#define SOCK_CLOSE(s)  closesocket(s)
#define SOCK_ERR()     WSAGetLastError()
#else
using SocketType = int;
#define SOCK_CLOSE(s)  ::close(s)
#define SOCK_ERR()     errno
#endif

static bool SetNonBlocking(SocketType fd, bool enable) {
#ifdef _WIN32
    unsigned long mode = enable ? 1 : 0;
    return ioctlsocket(fd, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(fd, F_GETFL, 0);
    return fcntl(fd, F_SETFL, enable ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK)) != -1;
#endif
}

static bool SetNoDelay(SocketType fd) {
    int val = 1;
#ifdef _WIN32
    return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
                      reinterpret_cast<const char*>(&val), sizeof(val)) == 0;
#else
    return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val)) == 0;
#endif
}

// Simple HTTP request parser.
// Returns true if a complete request was parsed.
static bool ParseHttpRequest(const char* data, size_t len, HttpRequest& req) {
    std::string buffer(data, len);
    // Find header/body separator (\r\n\r\n)
    size_t sepPos = buffer.find("\r\n\r\n");
    if (sepPos == std::string::npos) return false; // incomplete headers

    std::string headerSection = buffer.substr(0, sepPos);
    std::string bodySection   = buffer.substr(sepPos + 4);

    // Parse request line: "METHOD /path HTTP/1.1"
    auto lines = SplitString(headerSection, '\r');
    if (lines.empty()) return false;

    auto parts = SplitString(TrimStr(lines[0]), ' ');
    if (parts.size() < 2) return false;

    req.path = parts[1];
    // Extract query string
    auto qpos = req.path.find('?');
    if (qpos != std::string::npos) {
        req.query_string = req.path.substr(qpos + 1);
        req.path = req.path.substr(0, qpos);
    }

    // Determine method
    std::string methodLower = ToLowerStr(parts[0]);
    if (methodLower == "get")      req.method = HttpMethod::HTTP_GET;
    else if (methodLower == "post")  req.method = HttpMethod::HTTP_POST;
    else if (methodLower == "put")   req.method = HttpMethod::HTTP_PUT;
    else if (methodLower == "delete") req.method = HttpMethod::HTTP_DELETE;
    else if (methodLower == "head")  req.method = HttpMethod::HTTP_HEAD;
    else if (methodLower == "options") req.method = HttpMethod::HTTP_OPTIONS;
    else if (methodLower == "patch")  req.method = HttpMethod::HTTP_PATCH;
    else req.method = HttpMethod::HTTP_UNKNOWN;

    // Parse headers
    for (size_t i = 1; i < lines.size(); ++i) {
        auto hparts = SplitString(TrimStr(lines[i]), ':');
        if (hparts.size() >= 2) {
            std::string key   = ToLowerStr(TrimStr(hparts[0]));
            std::string value = TrimStr(hparts[1]);
            // Rejoin if value contains colons (e.g. "Host: example.com:8080")
            for (size_t j = 2; j < hparts.size(); ++j) {
                value += ":" + hparts[j];
            }
            req.headers[key] = value;
        }
    }

    // Parse body
    // Check Content-Length
    auto clIt = req.headers.find("content-length");
    if (clIt != req.headers.end()) {
        int contentLen = std::stoi(clIt->second);
        if (static_cast<int>(bodySection.size()) < contentLen) return false; // incomplete body
        req.body = bodySection.substr(0, contentLen);
    } else {
        req.body = bodySection;
    }

    return true;
}

} // anonymous namespace

HttpServer::HttpServer() {}
HttpServer::~HttpServer() { Stop(); }

bool HttpServer::Listen(int port) {
#ifdef _WIN32
    WSADATA wsaData{};
    if (WSAStartup(0x0202, &wsaData) != 0) return false;
#endif

    listenFd_ = static_cast<int>(socket(AF_INET, SOCK_STREAM, 0));
    if (listenFd_ < 0) return false;

    int val = 1;
    setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&val), sizeof(val));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = static_cast<uint16_t>(htons(static_cast<uint16_t>(port)));

    if (::bind(static_cast<SocketType>(listenFd_),
               reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        SOCK_CLOSE(static_cast<SocketType>(listenFd_));
        listenFd_ = -1;
        return false;
    }

    if (::listen(static_cast<SocketType>(listenFd_), 128) != 0) {
        SOCK_CLOSE(static_cast<SocketType>(listenFd_));
        listenFd_ = -1;
        return false;
    }

    SetNonBlocking(static_cast<SocketType>(listenFd_), true);
    SetNoDelay(static_cast<SocketType>(listenFd_));
    running_.store(true);
    eventThread_ = std::thread(&HttpServer::EventLoop_, this);
    return true;
}

void HttpServer::Run() {
    eventThread_.join();
}

void HttpServer::Stop() {
    running_.store(false);
    if (listenFd_ >= 0) {
#ifdef _WIN32
        SOCKET fake = socket(AF_INET, SOCK_STREAM, 0);
        if (fake != INVALID_SOCKET) { connect(fake, nullptr, 0); closesocket(fake); }
#else
        int fake = socket(AF_INET, SOCK_STREAM, 0);
        if (fake >= 0) { close(fake); }
#endif
    }
    eventThread_.join();
    if (listenFd_ >= 0) {
        SOCK_CLOSE(static_cast<SocketType>(listenFd_));
        listenFd_ = -1;
    }
}

size_t HttpServer::ActiveConnections() const {
    std::lock_guard<std::mutex> lk(connMutex_);
    return connTimes_.size();
}

void HttpServer::EventLoop_() {
    while (running_.load()) {
        fd_set readfds, errfds;
        FD_ZERO(&readfds);
        FD_ZERO(&errfds);

        int maxFd = listenFd_;
        if (maxFd >= 0) FD_SET(static_cast<UINT>(maxFd), &readfds);

        {
            std::lock_guard<std::mutex> lk(connMutex_);
            for (auto& [fd, _] : connTimes_) {
                if (fd >= 0) {
                    FD_SET(static_cast<UINT>(fd), &readfds);
                    FD_SET(static_cast<UINT>(fd), &errfds);
                    if (fd > maxFd) maxFd = fd;
                }
            }
        }

        if (maxFd < 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        timeval tv{};
        tv.tv_sec  = 0;
        tv.tv_usec = 50000; // 50ms

        int ret = select(maxFd + 1, &readfds, nullptr, &errfds, &tv);
        if (ret < 0) {
#ifdef _WIN32
            if (SOCK_ERR() == WSAEINTR) continue;
#endif
            break;
        }

        // Accept new connections
        if (listenFd_ >= 0 && FD_ISSET(static_cast<UINT>(listenFd_), &readfds)) {
            sockaddr_in clientAddr{};
            int addrLen = sizeof(clientAddr);
#ifdef _WIN32
            SOCKET clientFd = accept(listenFd_, reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
#else
            int clientFd = accept(listenFd_, reinterpret_cast<sockaddr*>(&clientAddr),
                                  static_cast<socklen_t*>(&addrLen));
#endif
            if (clientFd >= 0) {
                SetNonBlocking(static_cast<SocketType>(clientFd), true);
                SetNoDelay(static_cast<SocketType>(clientFd));
                std::lock_guard<std::mutex> lk(connMutex_);
                connTimes_[static_cast<int>(clientFd)] = std::chrono::steady_clock::now();
            }
        }

        // Process existing connections
        std::vector<int> toRemove;
        {
            std::lock_guard<std::mutex> lk(connMutex_);
            for (auto it = connTimes_.begin(); it != connTimes_.end(); ) {
                int fd = it->first;
                bool readable = FD_ISSET(static_cast<UINT>(fd), &readfds);
                bool errored  = FD_ISSET(static_cast<UINT>(fd), &errfds);

                if (errored) {
                    toRemove.push_back(fd);
                    ++it;
                    continue;
                }

                if (readable) {
                    HandleClient_(fd);
                    // Check if connection was closed during handling
                    if (fd < 0) {
                        toRemove.push_back(fd);
                        ++it;
                        continue;
                    }
                }

                // Connection timeout (60 seconds)
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - it->second).count();
                if (elapsed > 60) {
                    toRemove.push_back(fd);
                    ++it;
                    continue;
                }
                ++it;
            }
        }

        for (int fd : toRemove) {
            std::lock_guard<std::mutex> lk(connMutex_);
            auto it = connTimes_.find(fd);
            if (it != connTimes_.end()) {
                SOCK_CLOSE(static_cast<SocketType>(it->first));
                it->second = {};
                connTimes_.erase(it);
            }
        }
    }

#ifdef _WIN32
    WSACleanup();
#endif
}

void HttpServer::HandleClient_(int fd) {
    char readBuf[131072]; // 128 KB buffer
#ifdef _WIN32
    int n = recv(fd, readBuf, sizeof(readBuf), 0);
#else
    ssize_t n = recv(fd, readBuf, sizeof(readBuf), 0);
#endif
    if (n <= 0) {
        // Connection closed or error
        std::lock_guard<std::mutex> lk(connMutex_);
        auto it = connTimes_.find(fd);
        if (it != connTimes_.end()) {
            SOCK_CLOSE(static_cast<SocketType>(fd));
            connTimes_.erase(it);
        }
        return;
    }

    HttpRequest req;
    if (!ParseHttpRequest(readBuf, static_cast<size_t>(n), req)) {
        // Incomplete request — buffer for now (simplified: just close)
        std::lock_guard<std::mutex> lk(connMutex_);
        auto it = connTimes_.find(fd);
        if (it != connTimes_.end()) {
            SOCK_CLOSE(static_cast<SocketType>(fd));
            connTimes_.erase(it);
        }
        return;
    }

    // Run middleware chain
    HttpResponse resp;
    for (auto& mw : middlewares_) {
        if (!mw(req, resp)) {
            SendResponse_(fd, resp);
            return;
        }
    }

    // Route to handler
    const Handler* handler = routes_.Find(req.path, req.method);
    if (!handler) {
        resp = HttpResponse::NotFound();
    } else {
        resp = (*handler)(req);
    }

    SendResponse_(fd, resp);

    // Close connection after response (HTTP/1.0 style for simplicity)
    std::lock_guard<std::mutex> lk(connMutex_);
    auto it = connTimes_.find(fd);
    if (it != connTimes_.end()) {
        SOCK_CLOSE(static_cast<SocketType>(fd));
        connTimes_.erase(it);
    }
}

void HttpServer::SendResponse_(int fd, const HttpResponse& resp) {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << resp.status_code << " "
        << (resp.status_code == 200 ? "OK"
           : resp.status_code == 404 ? "Not Found"
           : resp.status_code == 401 ? "Unauthorized"
           : resp.status_code == 403 ? "Forbidden"
           : resp.status_code == 500 ? "Internal Server Error"
           : "Unknown")
        << "\r\n";

    size_t bodyLen = resp.body.size();
    oss << "Content-Length: " << bodyLen << "\r\n";
    oss << "Connection: close\r\n";
    for (auto& [k, v] : resp.headers) {
        oss << k << ": " << v << "\r\n";
    }
    oss << "\r\n";

    std::string header = oss.str();

    // Send header + body
    const char* p = header.c_str();
    size_t rem = header.size();
#ifdef _WIN32
    while (rem > 0) {
        int sent = send(fd, p, static_cast<int>(rem), 0);
        if (sent <= 0) break;
        p += sent;
        rem -= sent;
    }
    if (bodyLen > 0 && rem == 0) {
        p = resp.body.c_str();
        rem = bodyLen;
        while (rem > 0) {
            int sent = send(fd, p, static_cast<int>(rem), 0);
            if (sent <= 0) break;
            p += sent;
            rem -= sent;
        }
    }
#else
    // Writev for efficiency
    struct iovec iov[2];
    iov[0].iov_base = const_cast<char*>(header.c_str());
    iov[0].iov_len  = header.size();
    iov[1].iov_base = const_cast<char*>(resp.body.c_str());
    iov[1].iov_len  = bodyLen;
    ::writev(fd, iov, (bodyLen > 0) ? 2 : 1);
#endif
}

} // namespace http
} // namespace snc
