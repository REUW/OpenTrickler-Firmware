// Host HTTP server for the OpenTrickler simulator.
//
// src/http_rest.c is a vendored copy of lwIP's httpd and cannot be built on a
// PC. This replaces just the transport: the REST handlers themselves are the
// real firmware functions, called with the same signature and the same
// (params, values) arrays the device passes them. A handler writes a complete
// HTTP response into fs_file::data, so serving it is a straight socket write.
//
// The web portal HTML is read from disk on every request rather than compiled
// in, so you can edit src/html/web_portal.html and just refresh the browser.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
   typedef int socklen_t;
#  define CLOSESOCKET closesocket
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <unistd.h>
   typedef int SOCKET;
#  define INVALID_SOCKET (-1)
#  define CLOSESOCKET close
#endif

extern "C" {
#include <FreeRTOS.h>
#include "http_rest.h"
}

#include "sim_http.h"

namespace {

struct Route {
    std::string uri;
    rest_handler_t handler;
};

std::vector<Route> g_routes;
std::mutex g_routes_mutex;
std::string g_html_path;

// Firmware handlers are not reentrant; serialise requests the way the single
// lwIP httpd thread does on the device.
std::mutex g_handler_mutex;

std::string url_decode(const std::string &in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); i++) {
        if (in[i] == '%' && i + 2 < in.size()) {
            int v = 0;
            if (sscanf(in.substr(i + 1, 2).c_str(), "%2x", &v) == 1) {
                out.push_back((char)v);
                i += 2;
                continue;
            }
        }
        if (in[i] == '+') {
            out.push_back(' ');
        } else {
            out.push_back(in[i]);
        }
    }
    return out;
}

std::string read_file(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return std::string();
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void send_all(SOCKET s, const char *data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        int n = send(s, data + sent, (int)(len - sent), 0);
        if (n <= 0) {
            return;
        }
        sent += (size_t)n;
    }
}

void send_simple(SOCKET s, const char *status, const char *ctype, const std::string &body) {
    std::ostringstream hdr;
    hdr << "HTTP/1.1 " << status << "\r\n"
        << "Content-Type: " << ctype << "\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Cache-Control: no-store\r\n"
        << "Connection: close\r\n\r\n";
    std::string h = hdr.str();
    send_all(s, h.data(), h.size());
    send_all(s, body.data(), body.size());
}

void handle_rest(SOCKET sock, const std::string &uri, const std::string &query) {
    rest_handler_t handler = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_routes_mutex);
        for (const auto &r : g_routes) {
            if (r.uri == uri) {
                handler = r.handler;
                break;
            }
        }
    }

    if (handler == nullptr) {
        // Be explicit rather than silently returning something plausible:
        // several device endpoints (wifi, OTA, system control) have no
        // meaning on a PC and are deliberately not implemented here.
        std::ostringstream body;
        body << "{\"error\":\"Endpoint not available in simulator\",\"uri\":\""
             << uri << "\"}";
        send_simple(sock, "501 Not Implemented", "application/json", body.str());
        return;
    }

    // Split "a=1&b=2" into the parallel arrays the firmware expects.
    std::vector<std::string> keys, vals;
    std::stringstream qs(query);
    std::string pair;
    while (std::getline(qs, pair, '&')) {
        if (pair.empty()) continue;
        size_t eq = pair.find('=');
        if (eq == std::string::npos) {
            keys.push_back(url_decode(pair));
            vals.push_back("");
        } else {
            keys.push_back(url_decode(pair.substr(0, eq)));
            vals.push_back(url_decode(pair.substr(eq + 1)));
        }
    }

    std::vector<char *> kp, vp;
    for (size_t i = 0; i < keys.size(); i++) {
        kp.push_back(const_cast<char *>(keys[i].c_str()));
        vp.push_back(const_cast<char *>(vals[i].c_str()));
    }

    struct fs_file file;
    memset(&file, 0, sizeof(file));

    bool ok;
    {
        std::lock_guard<std::mutex> lock(g_handler_mutex);
        ok = handler(&file, (int)kp.size(), kp.empty() ? nullptr : kp.data(),
                     vp.empty() ? nullptr : vp.data());
    }

    if (!ok || file.data == nullptr || file.len <= 0) {
        send_simple(sock, "500 Internal Server Error", "application/json",
                    "{\"error\":\"Handler failed\"}");
        return;
    }

    // Handlers set FS_FILE_FLAGS_HEADER_INCLUDED and emit a full response
    // including status line and headers, so write it through untouched.
    send_all(sock, file.data, (size_t)file.len);
}

void handle_client(SOCKET sock) {
    std::string request;
    char buf[4096];
    // Read until end of headers; these requests have no body worth parsing.
    for (;;) {
        int n = recv(sock, buf, sizeof(buf), 0);
        if (n <= 0) break;
        request.append(buf, (size_t)n);
        if (request.find("\r\n\r\n") != std::string::npos) break;
        if (request.size() > 64 * 1024) break;
    }
    if (request.empty()) {
        CLOSESOCKET(sock);
        return;
    }

    std::istringstream rs(request);
    std::string method, target, version;
    rs >> method >> target >> version;

    std::string uri = target;
    std::string query;
    size_t q = target.find('?');
    if (q != std::string::npos) {
        uri = target.substr(0, q);
        query = target.substr(q + 1);
    }

    if (uri.rfind("/rest/", 0) == 0) {
        handle_rest(sock, uri, query);
    } else if (uri == "/" || uri == "/index.html" || uri == "/index.shtml") {
        std::string html = read_file(g_html_path);
        if (html.empty()) {
            std::ostringstream msg;
            msg << "<h1>web_portal.html not found</h1><p>Looked in: "
                << g_html_path << "</p>";
            send_simple(sock, "404 Not Found", "text/html", msg.str());
        } else {
            send_simple(sock, "200 OK", "text/html; charset=utf-8", html);
        }
    } else {
        send_simple(sock, "404 Not Found", "text/plain", "Not found");
    }

    CLOSESOCKET(sock);
}

void server_thread(int port) {
    SOCKET listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener == INVALID_SOCKET) {
        fprintf(stderr, "[sim] failed to create socket\n");
        return;
    }
    int yes = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((uint16_t)port);

    if (bind(listener, (sockaddr *)&addr, sizeof(addr)) != 0) {
        fprintf(stderr, "[sim] failed to bind port %d (already in use?)\n", port);
        CLOSESOCKET(listener);
        return;
    }
    if (listen(listener, 16) != 0) {
        fprintf(stderr, "[sim] listen failed\n");
        CLOSESOCKET(listener);
        return;
    }

    for (;;) {
        sockaddr_in client;
        socklen_t clen = sizeof(client);
        SOCKET s = accept(listener, (sockaddr *)&client, &clen);
        if (s == INVALID_SOCKET) continue;
        std::thread(handle_client, s).detach();
    }
}

}  // namespace

// The firmware calls this from its *_init() functions to publish endpoints.
extern "C" void rest_register_handler(char *uri, rest_handler_t handler) {
    std::lock_guard<std::mutex> lock(g_routes_mutex);
    g_routes.push_back(Route{std::string(uri), handler});
}

extern "C" void sim_http_start(int port, const char *html_path) {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    g_html_path = html_path ? html_path : "";
    std::thread(server_thread, port).detach();
}

extern "C" int sim_http_route_count(void) {
    std::lock_guard<std::mutex> lock(g_routes_mutex);
    return (int)g_routes.size();
}
