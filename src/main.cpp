// Backend of the c-openhab-rest-client test app: makes every function of the C library
// callable over HTTP. The browser sends the openHAB address, the credentials, the class
// and method name of python-openhab-rest-client and the arguments as JSON object.
//
//   GET  /                     health check (wake-up on Render)
//   GET  /api/methods          all functions with parameters (methods.json)
//   POST /api/connect          {url, token | username+password}
//   POST /api/call             {url, token, ..., class, method, args: {...}}
//   GET  /api/events/stream    ?url&token&...&class&method&args=<json>  (Server-Sent Events)
#include "httplib.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

extern "C" {
#include <openhab/openhab.h>
#include "calls.h"
}

namespace {

struct JsonDeleter { void operator()(cJSON* j) const { cJSON_Delete(j); } };
using Json = std::unique_ptr<cJSON, JsonDeleter>;
struct ClientDeleter { void operator()(openhab_client_t* c) const { openhab_client_destroy(c); } };
using Client = std::unique_ptr<openhab_client_t, ClientDeleter>;

std::string methodsJson;

void setCORS(httplib::Response& res) {
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type");
    res.set_header("Access-Control-Max-Age", "3600");
}

void sendJson(httplib::Response& res, const cJSON* value, int status = 200) {
    setCORS(res);
    res.status = status;
    char* text = openhab_json_print(value);
    res.set_content(text ? text : "null", "application/json");
    openhab_free(text);
}

void sendError(httplib::Response& res, const std::string& msg, int status) {
    Json j(cJSON_CreateObject());
    cJSON_AddStringToObject(j.get(), "error", msg.c_str());
    sendJson(res, j.get(), status);
}

const char* str(const cJSON* o, const char* key) {
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsString(v) && *v->valuestring ? v->valuestring : nullptr;
}

// Client from url/token/username/password; the constructor checks the connection.
Client makeClient(const char* url, const char* token, const char* user, const char* pass) {
    openhab_client_args a{};
    a.token = token;
    a.username = user;
    a.password = pass;
    return Client(openhab_client_new_(url ? url : "https://myopenhab.org", a));
}

Client makeClient(const cJSON* body) {
    return makeClient(str(body, "url"), str(body, "token"), str(body, "username"),
                      str(body, "password"));
}

std::string param(const httplib::Request& req, const char* key) {
    return req.has_param(key) ? req.get_param_value(key) : std::string();
}

const char* orNull(const std::string& s) { return s.empty() ? nullptr : s.c_str(); }

}  // namespace

int main() {
    std::ifstream f("methods.json");
    std::stringstream ss;
    ss << f.rdbuf();
    methodsJson = ss.str().empty() ? "[]" : ss.str();

    httplib::Server svr;

    svr.Options(".*", [](const httplib::Request&, httplib::Response& res) {
        setCORS(res);
        res.status = 204;
    });

    svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
        setCORS(res);
        res.set_content(std::string(R"({"status":"ok","library":"c-openhab-rest-client","version":")")
                        + OPENHAB_CLIENT_VERSION + "\"}", "application/json");
    });

    svr.Get("/api/methods", [](const httplib::Request&, httplib::Response& res) {
        setCORS(res);
        res.set_content(methodsJson, "application/json");
    });

    svr.Post("/api/connect", [](const httplib::Request& req, httplib::Response& res) {
        Json body(cJSON_Parse(req.body.c_str()));
        if (!cJSON_IsObject(body.get())) return sendError(res, "Invalid JSON body", 400);
        Client c = makeClient(body.get());
        if (!c) return sendError(res, "Out of memory", 500);
        Json j(cJSON_CreateObject());
        cJSON_AddBoolToObject(j.get(), "loggedIn", openhab_client_is_logged_in(c.get()));
        cJSON_AddBoolToObject(j.get(), "isCloud", openhab_client_is_cloud(c.get()));
        cJSON_AddStringToObject(j.get(), "url", openhab_client_base_url(c.get()));
        if (const char* e = openhab_client_last_error(c.get()))
            cJSON_AddStringToObject(j.get(), "error", e);
        sendJson(res, j.get());
    });

    svr.Post("/api/call", [](const httplib::Request& req, httplib::Response& res) {
        Json body(cJSON_Parse(req.body.c_str()));
        if (!cJSON_IsObject(body.get())) return sendError(res, "Invalid JSON body", 400);
        const char* cls = str(body.get(), "class");
        const char* method = str(body.get(), "method");
        if (!cls || !method) return sendError(res, "class and method are required", 400);
        const cJSON* args = cJSON_GetObjectItemCaseSensitive(body.get(), "args");
        Json empty(cJSON_CreateObject());
        if (!cJSON_IsObject(args)) args = empty.get();

        Client c = makeClient(body.get());
        if (!c) return sendError(res, "Out of memory", 500);
        Json result(openhab_call(c.get(), cls, method, args));
        if (!result)
            return sendError(res, std::string("Unknown function ") + cls + "." + method, 400);
        sendJson(res, result.get());
    });

    // Event stream: the C library reads openHAB, the backend forwards every event.
    svr.Get("/api/events/stream", [](const httplib::Request& req, httplib::Response& res) {
        setCORS(res);
        std::string cls = param(req, "class"), method = param(req, "method");
        if (cls.empty()) { cls = "Events"; method = "getEvents"; }
        Json args(cJSON_Parse(param(req, "args").c_str()));
        if (!cJSON_IsObject(args.get())) args.reset(cJSON_CreateObject());
        std::string topics = param(req, "topics");   // version 1.x of the test app
        if (!topics.empty() && !cJSON_GetObjectItem(args.get(), "topics"))
            cJSON_AddStringToObject(args.get(), "topics", topics.c_str());

        std::string url = param(req, "url"), token = param(req, "token"),
                    user = param(req, "username"), pass = param(req, "password");
        auto client = std::make_shared<Client>(
            makeClient(orNull(url), orNull(token), orNull(user), orNull(pass)));
        openhab_sse_t* s = openhab_open_stream(client->get(), cls.c_str(), method.c_str(),
                                               args.get());
        if (!s) return sendError(res, "Unknown event stream " + cls + "." + method, 400);
        if (openhab_sse_is_error(s)) {
            Json e(openhab_sse_error_result(s));
            openhab_sse_close(s);
            return sendJson(res, e.get(), 502);
        }
        res.set_header("Cache-Control", "no-cache");
        res.set_chunked_content_provider(
            "text/event-stream",
            [s](size_t, httplib::DataSink& sink) {
                // wait up to 15 s; ALIVE messages keep the connection to the browser open
                Json ev(openhab_sse_next_event(s, 15000, 1));
                std::string chunk;
                if (ev) {
                    char* text = openhab_json_print(ev.get());
                    chunk = std::string("data: ") + text + "\n\n";
                    openhab_free(text);
                } else if (openhab_sse_is_open(s)) {
                    chunk = ": keep-alive\n\n";
                } else {
                    Json e(openhab_sse_error_result(s));
                    if (e) {
                        char* text = openhab_json_print(e.get());
                        std::string msg = std::string("event: error\ndata: ") + text + "\n\n";
                        sink.write(msg.data(), msg.size());
                        openhab_free(text);
                    }
                    sink.done();
                    return true;
                }
                return sink.write(chunk.data(), chunk.size());
            },
            [s, client](bool) { openhab_sse_close(s); });
    });

    const char* portEnv = std::getenv("PORT");
    int port = portEnv ? std::atoi(portEnv) : 8080;
    svr.set_read_timeout(30);
    svr.set_write_timeout(30);
    std::cout << "c-openhab-rest-client " << OPENHAB_CLIENT_VERSION
              << " backend listening on port " << port << std::endl;
    svr.listen("0.0.0.0", port);
    return 0;
}
