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
    ROUTE("/api/uuid",           openhab_uuid_getUUID(c));
    ROUTE("/api/systeminfo",     openhab_systeminfo_getSystemInfo(c));
    ROUTE("/api/systeminfo/uom", openhab_systeminfo_getUoMInfo(c));

    // ── Items ─────────────────────────────────────────────────────────────────
    ROUTE("/api/items",                openhab_items_getItems(c,nullptr,nullptr,nullptr,0,nullptr,0,nullptr));
    ROUTE("/api/items/metadata/purge", openhab_items_purgeOrphanedMetadata(c));

    svr.Post("/api/items/([^/]+)/state/update", [](const httplib::Request& req, httplib::Response& res) {
        auto* c = makeClient(req); if (!c){err_res(res,"client");return;}
        ok(res, openhab_items_updateItemState(c, req.matches[1].str().c_str(), bodyStr(req).c_str(), nullptr), c);
        openhab_client_destroy(c);
    });
    svr.Post("/api/items/([^/]+)/command", [](const httplib::Request& req, httplib::Response& res) {
        auto* c = makeClient(req); if (!c){err_res(res,"client");return;}
        ok(res, openhab_items_sendCommand(c, req.matches[1].str().c_str(), bodyStr(req).c_str()), c);
        openhab_client_destroy(c);
    });
    svr.Post("/api/items/([^/]+)/delete", [](const httplib::Request& req, httplib::Response& res) {
        auto* c = makeClient(req); if (!c){err_res(res,"client");return;}
        ok(res, openhab_items_deleteItem(c, req.matches[1].str().c_str()), c);
        openhab_client_destroy(c);
    });
    svr.Post("/api/items/([^/]+)/state", [](const httplib::Request& req, httplib::Response& res) {
        auto* c = makeClient(req); if (!c){err_res(res,"client");return;}
        ok(res, openhab_items_getItemState(c, req.matches[1].str().c_str()), c);
        openhab_client_destroy(c);
    });
    svr.Post("/api/items/([^/]+)/tags/([^/]+)/add", [](const httplib::Request& req, httplib::Response& res) {
        auto* c = makeClient(req); if (!c){err_res(res,"client");return;}
        ok(res, openhab_items_addTag(c, req.matches[1].str().c_str(), req.matches[2].str().c_str()), c);
        openhab_client_destroy(c);
    });
    svr.Post("/api/items/([^/]+)/tags/([^/]+)/remove", [](const httplib::Request& req, httplib::Response& res) {
        auto* c = makeClient(req); if (!c){err_res(res,"client");return;}
        ok(res, openhab_items_removeTag(c, req.matches[1].str().c_str(), req.matches[2].str().c_str()), c);
        openhab_client_destroy(c);
    });
    svr.Post("/api/items/([^/]+)/members/([^/]+)/add", [](const httplib::Request& req, httplib::Response& res) {
        auto* c = makeClient(req); if (!c){err_res(res,"client");return;}
        ok(res, openhab_items_addGroupMember(c, req.matches[1].str().c_str(), req.matches[2].str().c_str()), c);
        openhab_client_destroy(c);
    });
    svr.Post("/api/items/([^/]+)/members/([^/]+)/remove", [](const httplib::Request& req, httplib::Response& res) {
        auto* c = makeClient(req); if (!c){err_res(res,"client");return;}
        ok(res, openhab_items_removeGroupMember(c, req.matches[1].str().c_str(), req.matches[2].str().c_str()), c);
        openhab_client_destroy(c);
    });
    svr.Post("/api/items/([^/]+)/metadata/namespaces", [](const httplib::Request& req, httplib::Response& res) {
        auto* c = makeClient(req); if (!c){err_res(res,"client");return;}
        ok(res, openhab_items_getMetadataNamespaces(c, req.matches[1].str().c_str(), nullptr), c);
        openhab_client_destroy(c);
    });
    svr.Post("/api/items/([^/]+)", [](const httplib::Request& req, httplib::Response& res) {
        auto* c = makeClient(req); if (!c){err_res(res,"client");return;}
        ok(res, openhab_items_getItem(c, req.matches[1].str().c_str(), nullptr, 1, nullptr), c);
        openhab_client_destroy(c);
    });

    // ── Things ────────────────────────────────────────────────────────────────
    ROUTE("/api/things", openhab_things_getThings(c,0,0,nullptr));
    svr.Post("/api/things/([^/]+)/status",   [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_things_getThingStatus(c,req.matches[1].str().c_str(),nullptr),c); openhab_client_destroy(c); });
    svr.Post("/api/things/([^/]+)/enable",   [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_things_setThingStatus(c,req.matches[1].str().c_str(),1,nullptr),c); openhab_client_destroy(c); });
    svr.Post("/api/things/([^/]+)/disable",  [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_things_setThingStatus(c,req.matches[1].str().c_str(),0,nullptr),c); openhab_client_destroy(c); });
    svr.Post("/api/things/([^/]+)/delete",   [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_things_deleteThing(c,req.matches[1].str().c_str(),0,nullptr),c); openhab_client_destroy(c); });
    svr.Post("/api/things/([^/]+)/firmwares",[](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_things_getThingFirmwares(c,req.matches[1].str().c_str(),nullptr),c); openhab_client_destroy(c); });
    svr.Post("/api/things/([^/]+)",          [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_things_getThing(c,req.matches[1].str().c_str(),nullptr),c); openhab_client_destroy(c); });

    // ── Rules ─────────────────────────────────────────────────────────────────
    ROUTE("/api/rules", openhab_rules_getRules(c,nullptr,nullptr,0,0));
    svr.Post("/api/rules/([^/]+)/enable",    [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_rules_setRuleState(c,req.matches[1].str().c_str(),1),c); openhab_client_destroy(c); });
    svr.Post("/api/rules/([^/]+)/disable",   [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_rules_setRuleState(c,req.matches[1].str().c_str(),0),c); openhab_client_destroy(c); });
    svr.Post("/api/rules/([^/]+)/runnow",    [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_rules_runNow(c,req.matches[1].str().c_str(),nullptr),c); openhab_client_destroy(c); });
    svr.Post("/api/rules/([^/]+)/delete",    [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_rules_deleteRule(c,req.matches[1].str().c_str()),c); openhab_client_destroy(c); });
    svr.Post("/api/rules/([^/]+)/actions",   [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_rules_getActions(c,req.matches[1].str().c_str()),c); openhab_client_destroy(c); });
    svr.Post("/api/rules/([^/]+)/triggers",  [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_rules_getTriggers(c,req.matches[1].str().c_str()),c); openhab_client_destroy(c); });
    svr.Post("/api/rules/([^/]+)/conditions",[](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_rules_getConditions(c,req.matches[1].str().c_str()),c); openhab_client_destroy(c); });
    svr.Post("/api/rules/([^/]+)",           [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_rules_getRule(c,req.matches[1].str().c_str()),c); openhab_client_destroy(c); });

    // ── Addons ────────────────────────────────────────────────────────────────
    ROUTE("/api/addons",             openhab_addons_getAddons(c,nullptr,nullptr));
    ROUTE("/api/addons/types",       openhab_addons_getAddonTypes(c,nullptr));
    ROUTE("/api/addons/suggestions", openhab_addons_getAddonSuggestions(c,nullptr));
    ROUTE("/api/addons/services",    openhab_addons_getAddonServices(c,nullptr));
    svr.Post("/api/addons/([^/]+)/install",   [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_addons_installAddon(c,req.matches[1].str().c_str(),nullptr),c); openhab_client_destroy(c); });
    svr.Post("/api/addons/([^/]+)/uninstall", [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_addons_uninstallAddon(c,req.matches[1].str().c_str(),nullptr),c); openhab_client_destroy(c); });
    svr.Post("/api/addons/([^/]+)",           [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_addons_getAddon(c,req.matches[1].str().c_str(),nullptr,nullptr),c); openhab_client_destroy(c); });

    // ── Audio ─────────────────────────────────────────────────────────────────
    ROUTE("/api/audio/defaultsink",   openhab_audio_getDefaultSink(c));
    ROUTE("/api/audio/defaultsource", openhab_audio_getDefaultSource(c));
    ROUTE("/api/audio/sinks",         openhab_audio_getSinks(c));
    ROUTE("/api/audio/sources",       openhab_audio_getSources(c));

    // ── Logging ───────────────────────────────────────────────────────────────
    ROUTE("/api/logging", openhab_logging_getLoggers(c));
    svr.Post("/api/logging/([^/]+)/set",    [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} auto level=paramStr(req,"level","INFO"); ok(res,openhab_logging_modifyOrAddLogger(c,req.matches[1].str().c_str(),level.c_str()),c); openhab_client_destroy(c); });
    svr.Post("/api/logging/([^/]+)/delete", [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_logging_removeLogger(c,req.matches[1].str().c_str()),c); openhab_client_destroy(c); });
    svr.Post("/api/logging/([^/]+)",        [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_logging_getLogger(c,req.matches[1].str().c_str()),c); openhab_client_destroy(c); });

    // ── Links / ChannelTypes / ThingTypes / ConfigDescriptions ────────────────
    ROUTE("/api/links",              openhab_links_getLinks(c,nullptr,nullptr));
    ROUTE("/api/links/orphan",       openhab_links_getOrphanLinks(c));
    ROUTE("/api/channel-types",      openhab_channel_types_getChannelTypes(c,nullptr,nullptr));
    ROUTE("/api/thing-types",        openhab_thing_types_getThingTypes(c,nullptr,nullptr));
    ROUTE("/api/config-descriptions",openhab_config_descriptions_getConfigDescriptions(c,nullptr,nullptr));

    // ── Persistence ───────────────────────────────────────────────────────────
    ROUTE("/api/persistence",       openhab_persistence_getServices(c));
    ROUTE("/api/persistence/items", openhab_persistence_getItemsFromService(c,nullptr));
    svr.Post("/api/persistence/items/([^/]+)", [](const httplib::Request& req, httplib::Response& res) {
        auto* c=makeClient(req); if(!c){err_res(res,"client");return;}
        auto svc=paramStr(req,"serviceId","");
        ok(res,openhab_persistence_getItemPersistenceData(c,req.matches[1].str().c_str(),
           svc.empty()?nullptr:svc.c_str(),nullptr,nullptr,0,0,0),c);
        openhab_client_destroy(c);
    });

    // ── Discovery + Inbox ─────────────────────────────────────────────────────
    ROUTE("/api/discovery", openhab_discovery_getDiscoveryBindings(c));
    svr.Post("/api/discovery/([^/]+)/scan", [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_discovery_startBindingScan(c,req.matches[1].str().c_str()),c); openhab_client_destroy(c); });
    ROUTE("/api/inbox", openhab_inbox_getDiscoveredThings(c));
    svr.Post("/api/inbox/([^/]+)/approve",  [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_inbox_approveDiscoveryResult(c,req.matches[1].str().c_str(),bodyStr(req).c_str(),nullptr),c); openhab_client_destroy(c); });
    svr.Post("/api/inbox/([^/]+)/ignore",   [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_inbox_ignoreDiscoveryResult(c,req.matches[1].str().c_str()),c); openhab_client_destroy(c); });
    svr.Post("/api/inbox/([^/]+)/unignore", [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_inbox_unignoreDiscoveryResult(c,req.matches[1].str().c_str()),c); openhab_client_destroy(c); });
    svr.Post("/api/inbox/([^/]+)/delete",   [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_inbox_removeDiscoveryResult(c,req.matches[1].str().c_str()),c); openhab_client_destroy(c); });

    // ── Sitemaps ──────────────────────────────────────────────────────────────
    ROUTE("/api/sitemaps", openhab_sitemaps_getSitemaps(c));
    svr.Post("/api/sitemaps/([^/]+)/([^/]+)", [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_sitemaps_getSitemapPage(c,req.matches[1].str().c_str(),req.matches[2].str().c_str(),nullptr,0,nullptr),c); openhab_client_destroy(c); });
    svr.Post("/api/sitemaps/([^/]+)",         [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_sitemaps_getSitemap(c,req.matches[1].str().c_str(),nullptr,0,nullptr),c); openhab_client_destroy(c); });

    // ── Tags / Templates / ModuleTypes / ProfileTypes ─────────────────────────
    ROUTE("/api/tags",         openhab_tags_getTags(c,nullptr,nullptr));
    svr.Post("/api/tags/([^/]+)/delete", [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_tags_deleteTag(c,req.matches[1].str().c_str()),c); openhab_client_destroy(c); });
    svr.Post("/api/tags/([^/]+)",        [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_tags_getTag(c,req.matches[1].str().c_str(),nullptr),c); openhab_client_destroy(c); });
    ROUTE("/api/templates",    openhab_templates_getTemplates(c,nullptr));
    ROUTE("/api/module-types", openhab_module_types_getModuleTypes(c,nullptr,nullptr));
    ROUTE("/api/profile-types",openhab_profile_types_getProfileTypes(c,nullptr,nullptr,nullptr));

    // ── Transformations ───────────────────────────────────────────────────────
    ROUTE("/api/transformations",          openhab_transformations_getTransformations(c));
    ROUTE("/api/transformations/services", openhab_transformations_getTransformationServices(c));
    svr.Post("/api/transformations/([^/]+)/delete", [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_transformations_deleteTransformation(c,req.matches[1].str().c_str()),c); openhab_client_destroy(c); });
    svr.Post("/api/transformations/([^/]+)",        [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_transformations_getTransformation(c,req.matches[1].str().c_str()),c); openhab_client_destroy(c); });

    // ── UI / Services / Iconsets ──────────────────────────────────────────────
    ROUTE("/api/ui/tiles",    openhab_ui_getUITiles(c));
    svr.Post("/api/ui/components/([^/]+)", [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_ui_getUIComponents(c,req.matches[1].str().c_str()),c); openhab_client_destroy(c); });
    ROUTE("/api/services",    openhab_services_getServices(c,nullptr));
    svr.Post("/api/services/([^/]+)/config", [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_services_getServiceConfig(c,req.matches[1].str().c_str()),c); openhab_client_destroy(c); });
    svr.Post("/api/services/([^/]+)",        [](const httplib::Request& req, httplib::Response& res) { auto* c=makeClient(req); if(!c){err_res(res,"client");return;} ok(res,openhab_services_getService(c,req.matches[1].str().c_str(),nullptr),c); openhab_client_destroy(c); });
    ROUTE("/api/iconsets",    openhab_iconsets_getIconsets(c,nullptr));

    // ── Auth ──────────────────────────────────────────────────────────────────
    ROUTE("/api/auth/apitokens", openhab_auth_getAPITokens(c));
    ROUTE("/api/auth/sessions",  openhab_auth_getSessions(c));

    // ── Voice ─────────────────────────────────────────────────────────────────
    ROUTE("/api/voice/voices",       openhab_voice_getVoices(c));
    ROUTE("/api/voice/defaultvoice", openhab_voice_getDefaultVoice(c));
    ROUTE("/api/voice/interpreters", openhab_voice_getInterpreters(c,nullptr));
    svr.Post("/api/voice/say", [](const httplib::Request& req, httplib::Response& res) {
        auto* c=makeClient(req); if(!c){err_res(res,"client");return;}
        ok(res,openhab_voice_sayText(c,bodyStr(req).c_str(),nullptr,nullptr,-1),c);
        openhab_client_destroy(c);
    });

    // ── Actions ───────────────────────────────────────────────────────────────
    ROUTE("/api/actions", openhab_actions_getActions(c));
    svr.Post("/api/actions/([^/]+)", [](const httplib::Request& req, httplib::Response& res) {
        auto* c=makeClient(req); if(!c){err_res(res,"client");return;}
        ok(res,openhab_actions_getThingActions(c,req.matches[1].str().c_str()),c);
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
