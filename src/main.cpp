#include "httplib.h"
#include <nlohmann/json.hpp>
#include <string>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "openhab/openhab.h"
}

using json = nlohmann::json;

// ── CORS ─────────────────────────────────────────────────────────────────────
static void setCORS(httplib::Response& res) {
    res.set_header("Access-Control-Allow-Origin",  "*");
    res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
    res.set_header("Access-Control-Max-Age",       "3600");
}

// ── Response helpers ──────────────────────────────────────────────────────────
static void ok(httplib::Response& res, char* result, openhab_client_t* c) {
    setCORS(res);
    if (result) {
        res.set_content(result, "application/json");
        free(result);
    } else {
        res.status = 502;
        const char* err = openhab_last_error(c);
        json j = {{"error", err ? err : "unknown error"},
                  {"status", openhab_last_status(c)}};
        res.set_content(j.dump(), "application/json");
    }
}

static void ok_json(httplib::Response& res, const json& j) {
    setCORS(res);
    res.set_content(j.dump(), "application/json");
}

static void err_res(httplib::Response& res, const std::string& msg, int code = 502) {
    setCORS(res);
    res.status = code;
    res.set_content(json{{"error", msg}}.dump(), "application/json");
}

// ── Client factory ────────────────────────────────────────────────────────────
static openhab_client_t* makeClient(const httplib::Request& req) {
    try {
        auto b = json::parse(req.body);
        std::string url      = b.contains("url")      && b["url"].is_string()      ? b["url"].get<std::string>()      : "https://myopenhab.org";
        std::string username = b.contains("username") && b["username"].is_string() ? b["username"].get<std::string>() : "";
        std::string password = b.contains("password") && b["password"].is_string() ? b["password"].get<std::string>() : "";
        std::string token    = b.contains("token")    && b["token"].is_string()    ? b["token"].get<std::string>()    : "";
        return openhab_client_create(
            url.c_str(),
            username.empty() ? nullptr : username.c_str(),
            password.empty() ? nullptr : password.c_str(),
            token.empty()    ? nullptr : token.c_str());
    } catch (...) {
        return nullptr;
    }
}

static std::string bodyStr(const httplib::Request& req) {
    try {
        auto b = json::parse(req.body);
        if (b.contains("body") && b["body"].is_string())
            return b["body"].get<std::string>();
    } catch (...) {}
    return "";
}

static std::string paramStr(const httplib::Request& req, const std::string& key,
                             const std::string& def = "") {
    try {
        auto b = json::parse(req.body);
        if (b.contains("params") && !b["params"].is_null() &&
            b["params"].contains(key) && b["params"][key].is_string())
            return b["params"][key].get<std::string>();
    } catch (...) {}
    return def;
}

// ── Macro for simple POST endpoints ──────────────────────────────────────────
#define ROUTE(path, expr) \
    svr.Post(path, [](const httplib::Request& req, httplib::Response& res) { \
        auto* c = makeClient(req); \
        if (!c) { err_res(res, "Failed to create client"); return; } \
        ok(res, expr, c); \
        openhab_client_destroy(c); \
    })

int main() {
    httplib::Server svr;

    // CORS preflight
    svr.Options(".*", [](const httplib::Request&, httplib::Response& res) {
        setCORS(res);
        res.status = 204;
    });

    // Healthcheck
    svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
        setCORS(res);
        res.set_content(R"({"status":"ok","library":"c-openhab-rest-client"})", "application/json");
    });

    // ── Connect ───────────────────────────────────────────────────────────────
    svr.Post("/api/connect", [](const httplib::Request& req, httplib::Response& res) {
        auto* c = makeClient(req);
        if (!c) { err_res(res, "Failed to create client"); return; }
        ok_json(res, {
            {"loggedIn", (bool)openhab_client_is_logged_in(c)},
            {"isCloud",  (bool)openhab_client_is_cloud(c)},
            {"url",      openhab_client_base_url(c)}
        });
        openhab_client_destroy(c);
    });

    // ── UUID + Systeminfo ─────────────────────────────────────────────────────
    ROUTE("/api/uuid",           openhab_uuid_get(c));
    ROUTE("/api/systeminfo",     openhab_systeminfo_get(c));
    ROUTE("/api/systeminfo/uom", openhab_systeminfo_uom(c));

    // ── Items ─────────────────────────────────────────────────────────────────
    ROUTE("/api/items",                openhab_items_get_all(c,nullptr,nullptr,nullptr,0,nullptr,0,nullptr));
    ROUTE("/api/items/metadata/purge", openhab_items_purge_metadata(c));

    svr.Post("/api/items/([^/]+)/state/update", [](const httplib::Request& req, httplib::Response& res) {
        auto* c = makeClient(req); if (!c){err_res(res,"client");return;}
        ok(res, openhab_items_update_state(c, req.matches[1].str().c_str(), bodyStr(req).c_str(), nullptr), c);
        openhab_client_destroy(c);
    });
    svr.Post("/api/items/([^/]+)/command", [](const httplib::Request& req, httplib::Response& res) {
        auto* c = makeClient(req); if (!c){err_res(res,"client");return;}
        ok(res, openhab_items_send_command(c, req.matches[1].str().c_str(), bodyStr(req).c_str()), c);
        openhab_client_destroy(c);
    });
    svr.Post("/api/items/([^/]+)/delete", [](const httplib::Request& req, httplib::Response& res) {
        auto* c = makeClient(req); if (!c){err_res(res,"client");return;}
        ok(res, openhab_items_delete(c, req.matches[1].str().c_str()), c);
        openhab_client_destroy(c);
    });
    svr.Post("/api/items/([^/]+)/state", [](const httplib::Request& req, httplib::Response& res) {
        auto* c = makeClient(req); if (!c){err_res(res,"client");return;}
        ok(res, openhab_items_get_state(c, req.matches[1].str().c_str()), c);
        openhab_client_destroy(c);
    });
    svr.Post("/api/items/([^/]+)/tags/([^/]+)/add", [](const httplib::Request& req, httplib::Response& res) {
        auto* c = makeClient(req); if (!c){err_res(res,"client");return;}
        ok(res, openhab_items_add_tag(c, req.matches[1].str().c_str(), req.matches[2].str().c_str()), c);
        openhab_client_destroy(c);
    });
    svr.Post("/api/items/([^/]+)/tags/([^/]+)/remove", [](const httplib::Request& req, httplib::Response& res) {
        auto* c = makeClient(req); if (!c){err_res(res,"client");return;}
        ok(res, openhab_items_remove_tag(c, req.matches[1].str().c_str(), req.matches[2].str().c_str()), c);
        openhab_client_destroy(c);
    });
    svr.Post("/api/items/([^/]+)/members/([^/]+)/add", [](const httplib::Request& req, httplib::Response& res) {
        auto* c = makeClient(req); if (!c){err_res(res,"client");return;}
        ok(res, openhab_items_add_group_member(c, req.matches[1].str().c_str(), req.matches[2].str().c_str()), c);
        openhab_client_destroy(c);
    });
    svr.Post("/api/items/([^/]+)/members/([^/]+)/remove", [](const httplib::Request& req, httplib::Response& res) {
        auto* c = makeClient(req); if (!c){err_res(res,"client");return;}
        ok(res, openhab_items_remove_group_member(c, req.matches[1].str().c_str(), req.matches[2].str().c_str()), c);
        openhab_client_destroy(c);
    });
    svr.Post("/api/items/([^/]+)/metadata/namespaces", [](const httplib::Request& req, httplib::Response& res) {
        auto* c = makeClient(req); if (!c){err_res(res,"client");return;}
        ok(res, openhab_items_get_metadata_namespaces(c, req.matches[1].str().c_str(), nullptr), c);
        openhab_client_destroy(c);
    });
    svr.Post("/api/items/([^/]+)", [](const httplib::Request& req, httplib::Response& res) {
        auto* c = makeClient(req); if (!c){err_res(res,"client");return;}
        ok(res, openhab_items_get(c, req.matches[1].str().c_str(), nullptr, 1, nullptr), c);
        openhab_client_destroy(c);
    });

    // ── Things ────────────────────────────────────────────────────────────────
    ROUTE("/api/things", openhab_things_get_all(c,0,0,nullptr));
    svr.Post("/api/things/([^/]+)/status",   [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_things_get_status(c,req.matches[1].str().c_str(),nullptr),c); openhab_client_destroy(c); });
    svr.Post("/api/things/([^/]+)/enable",   [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_things_set_enabled(c,req.matches[1].str().c_str(),1,nullptr),c); openhab_client_destroy(c); });
    svr.Post("/api/things/([^/]+)/disable",  [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_things_set_enabled(c,req.matches[1].str().c_str(),0,nullptr),c); openhab_client_destroy(c); });
    svr.Post("/api/things/([^/]+)/delete",   [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_things_delete(c,req.matches[1].str().c_str(),0,nullptr),c); openhab_client_destroy(c); });
    svr.Post("/api/things/([^/]+)/firmwares",[](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_things_get_firmwares(c,req.matches[1].str().c_str(),nullptr),c); openhab_client_destroy(c); });
    svr.Post("/api/things/([^/]+)",          [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_things_get(c,req.matches[1].str().c_str(),nullptr),c); openhab_client_destroy(c); });

    // ── Rules ─────────────────────────────────────────────────────────────────
    ROUTE("/api/rules", openhab_rules_get_all(c,nullptr,nullptr,0,0));
    svr.Post("/api/rules/([^/]+)/enable",    [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_rules_set_state(c,req.matches[1].str().c_str(),1),c); openhab_client_destroy(c); });
    svr.Post("/api/rules/([^/]+)/disable",   [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_rules_set_state(c,req.matches[1].str().c_str(),0),c); openhab_client_destroy(c); });
    svr.Post("/api/rules/([^/]+)/runnow",    [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_rules_run_now(c,req.matches[1].str().c_str(),nullptr),c); openhab_client_destroy(c); });
    svr.Post("/api/rules/([^/]+)/delete",    [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_rules_delete(c,req.matches[1].str().c_str()),c); openhab_client_destroy(c); });
    svr.Post("/api/rules/([^/]+)/actions",   [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_rules_get_actions(c,req.matches[1].str().c_str()),c); openhab_client_destroy(c); });
    svr.Post("/api/rules/([^/]+)/triggers",  [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_rules_get_triggers(c,req.matches[1].str().c_str()),c); openhab_client_destroy(c); });
    svr.Post("/api/rules/([^/]+)/conditions",[](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_rules_get_conditions(c,req.matches[1].str().c_str()),c); openhab_client_destroy(c); });
    svr.Post("/api/rules/([^/]+)",           [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_rules_get(c,req.matches[1].str().c_str()),c); openhab_client_destroy(c); });

    // ── Addons ────────────────────────────────────────────────────────────────
    ROUTE("/api/addons",             openhab_addons_get_all(c,nullptr,nullptr));
    ROUTE("/api/addons/types",       openhab_addons_get_types(c,nullptr));
    ROUTE("/api/addons/suggestions", openhab_addons_get_suggestions(c,nullptr));
    ROUTE("/api/addons/services",    openhab_addons_get_services(c,nullptr));
    svr.Post("/api/addons/([^/]+)/install",   [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_addons_install(c,req.matches[1].str().c_str(),nullptr),c); openhab_client_destroy(c); });
    svr.Post("/api/addons/([^/]+)/uninstall", [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_addons_uninstall(c,req.matches[1].str().c_str(),nullptr),c); openhab_client_destroy(c); });
    svr.Post("/api/addons/([^/]+)",           [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_addons_get(c,req.matches[1].str().c_str(),nullptr,nullptr),c); openhab_client_destroy(c); });

    // ── Audio ─────────────────────────────────────────────────────────────────
    ROUTE("/api/audio/defaultsink",   openhab_audio_get_default_sink(c));
    ROUTE("/api/audio/defaultsource", openhab_audio_get_default_source(c));
    ROUTE("/api/audio/sinks",         openhab_audio_get_sinks(c));
    ROUTE("/api/audio/sources",       openhab_audio_get_sources(c));

    // ── Logging ───────────────────────────────────────────────────────────────
    ROUTE("/api/logging", openhab_logging_get_all(c));
    svr.Post("/api/logging/([^/]+)/set",    [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} auto level=paramStr(req,"level","INFO"); ok(res,openhab_logging_set(c,req.matches[1].str().c_str(),level.c_str()),c); openhab_client_destroy(c); });
    svr.Post("/api/logging/([^/]+)/delete", [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_logging_delete(c,req.matches[1].str().c_str()),c); openhab_client_destroy(c); });
    svr.Post("/api/logging/([^/]+)",        [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_logging_get(c,req.matches[1].str().c_str()),c); openhab_client_destroy(c); });

    // ── Links / ChannelTypes / ThingTypes / ConfigDescriptions ────────────────
    ROUTE("/api/links",              openhab_links_get_all(c,nullptr,nullptr));
    ROUTE("/api/links/orphan",       openhab_links_get_orphan(c));
    ROUTE("/api/channel-types",      openhab_channel_types_get_all(c,nullptr,nullptr));
    ROUTE("/api/thing-types",        openhab_thing_types_get_all(c,nullptr,nullptr));
    ROUTE("/api/config-descriptions",openhab_config_descriptions_get_all(c,nullptr,nullptr));

    // ── Persistence ───────────────────────────────────────────────────────────
    ROUTE("/api/persistence",       openhab_persistence_get_services(c));
    ROUTE("/api/persistence/items", openhab_persistence_get_items(c,nullptr));
    svr.Post("/api/persistence/items/([^/]+)", [](const httplib::Request& req, httplib::Response& res) {
        auto* c=makeClient(req); if(!c){err_res(res,"client");return;}
        auto svc=paramStr(req,"serviceId","");
        ok(res,openhab_persistence_get_item_data(c,req.matches[1].str().c_str(),
           svc.empty()?nullptr:svc.c_str(),nullptr,nullptr,0,0,0),c);
        openhab_client_destroy(c);
    });

    // ── Discovery + Inbox ─────────────────────────────────────────────────────
    ROUTE("/api/discovery", openhab_discovery_get_bindings(c));
    svr.Post("/api/discovery/([^/]+)/scan", [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_discovery_scan(c,req.matches[1].str().c_str()),c); openhab_client_destroy(c); });
    ROUTE("/api/inbox", openhab_inbox_get_all(c));
    svr.Post("/api/inbox/([^/]+)/approve",  [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_inbox_approve(c,req.matches[1].str().c_str(),bodyStr(req).c_str(),nullptr),c); openhab_client_destroy(c); });
    svr.Post("/api/inbox/([^/]+)/ignore",   [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_inbox_ignore(c,req.matches[1].str().c_str()),c); openhab_client_destroy(c); });
    svr.Post("/api/inbox/([^/]+)/unignore", [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_inbox_unignore(c,req.matches[1].str().c_str()),c); openhab_client_destroy(c); });
    svr.Post("/api/inbox/([^/]+)/delete",   [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_inbox_delete(c,req.matches[1].str().c_str()),c); openhab_client_destroy(c); });

    // ── Sitemaps ──────────────────────────────────────────────────────────────
    ROUTE("/api/sitemaps", openhab_sitemaps_get_all(c));
    svr.Post("/api/sitemaps/([^/]+)/([^/]+)", [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_sitemaps_get_page(c,req.matches[1].str().c_str(),req.matches[2].str().c_str(),nullptr,0,nullptr),c); openhab_client_destroy(c); });
    svr.Post("/api/sitemaps/([^/]+)",         [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_sitemaps_get(c,req.matches[1].str().c_str(),nullptr,0,nullptr),c); openhab_client_destroy(c); });

    // ── Tags / Templates / ModuleTypes / ProfileTypes ─────────────────────────
    ROUTE("/api/tags",         openhab_tags_get_all(c,nullptr,nullptr));
    svr.Post("/api/tags/([^/]+)/delete", [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_tags_delete(c,req.matches[1].str().c_str()),c); openhab_client_destroy(c); });
    svr.Post("/api/tags/([^/]+)",        [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_tags_get(c,req.matches[1].str().c_str(),nullptr),c); openhab_client_destroy(c); });
    ROUTE("/api/templates",    openhab_templates_get_all(c,nullptr));
    ROUTE("/api/module-types", openhab_module_types_get_all(c,nullptr,nullptr));
    ROUTE("/api/profile-types",openhab_profile_types_get_all(c,nullptr,nullptr,nullptr));

    // ── Transformations ───────────────────────────────────────────────────────
    ROUTE("/api/transformations",          openhab_transformations_get_all(c));
    ROUTE("/api/transformations/services", openhab_transformations_get_services(c));
    svr.Post("/api/transformations/([^/]+)/delete", [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_transformations_delete(c,req.matches[1].str().c_str()),c); openhab_client_destroy(c); });
    svr.Post("/api/transformations/([^/]+)",        [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_transformations_get(c,req.matches[1].str().c_str()),c); openhab_client_destroy(c); });

    // ── UI / Services / Iconsets ──────────────────────────────────────────────
    ROUTE("/api/ui/tiles",    openhab_ui_get_tiles(c));
    svr.Post("/api/ui/components/([^/]+)", [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_ui_get_components(c,req.matches[1].str().c_str()),c); openhab_client_destroy(c); });
    ROUTE("/api/services",    openhab_services_get_all(c,nullptr));
    svr.Post("/api/services/([^/]+)/config", [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_services_get_config(c,req.matches[1].str().c_str()),c); openhab_client_destroy(c); });
    svr.Post("/api/services/([^/]+)",        [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_services_get(c,req.matches[1].str().c_str(),nullptr),c); openhab_client_destroy(c); });
    ROUTE("/api/iconsets",    openhab_iconsets_get_all(c,nullptr));

    // ── Auth ──────────────────────────────────────────────────────────────────
    ROUTE("/api/auth/apitokens", openhab_auth_get_tokens(c));
    ROUTE("/api/auth/sessions",  openhab_auth_get_sessions(c));

    // ── Voice ─────────────────────────────────────────────────────────────────
    ROUTE("/api/voice/voices",       openhab_voice_get_voices(c));
    ROUTE("/api/voice/defaultvoice", openhab_voice_get_default_voice(c));
    ROUTE("/api/voice/interpreters", openhab_voice_get_interpreters(c,nullptr));
    svr.Post("/api/voice/say", [](const httplib::Request& req, httplib::Response& res) {
        auto* c=makeClient(req); if(!c){err_res(res,"client");return;}
        ok(res,openhab_voice_say(c,bodyStr(req).c_str(),nullptr,nullptr,-1),c);
        openhab_client_destroy(c);
    });

    // ── Actions ───────────────────────────────────────────────────────────────
    ROUTE("/api/actions", openhab_actions_get_all(c));
    svr.Post("/api/actions/([^/]+)", [](const httplib::Request& req, httplib::Response& res) {
        auto* c=makeClient(req); if(!c){err_res(res,"client");return;}
        ok(res,openhab_actions_get(c,req.matches[1].str().c_str()),c);
        openhab_client_destroy(c);
    });

    // ── Start ─────────────────────────────────────────────────────────────────
    const char* portEnv = std::getenv("PORT");
    int port = portEnv ? std::stoi(portEnv) : 8080;

    svr.set_read_timeout(30);
    svr.set_write_timeout(30);

    std::cout << "c-openhab-rest-client backend listening on port " << port << std::endl;
    svr.listen("0.0.0.0", port);
    return 0;
}
