#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

#include "log.h"
#include "metrics.h"
#include "server.h"
#include "verdict.h"
#include "worker_pool.h"

namespace {

std::atomic<ofiq_api::Server*> g_server{nullptr};

void handle_signal(int sig) {
    auto* s = g_server.load(std::memory_order_relaxed);
    if (s) s->stop();
    (void)sig;
}

const char* env_or(const char* key, const char* def) {
    const char* v = std::getenv(key);
    return (v && *v) ? v : def;
}

std::size_t env_size(const char* key, std::size_t def) {
    const char* v = std::getenv(key);
    if (!v || !*v) return def;
    try { return static_cast<std::size_t>(std::stoull(v)); }
    catch (...) { return def; }
}

int env_int(const char* key, int def) {
    const char* v = std::getenv(key);
    if (!v || !*v) return def;
    try { return std::stoi(v); }
    catch (...) { return def; }
}

} // namespace

int main(int /*argc*/, char** /*argv*/) {
    using namespace ofiq_api;

    if (const char* lvl = std::getenv("OFIQ_LOG_LEVEL")) {
        if (!std::strcmp(lvl, "debug"))      log::set_level(log::Level::Debug);
        else if (!std::strcmp(lvl, "info"))  log::set_level(log::Level::Info);
        else if (!std::strcmp(lvl, "warn"))  log::set_level(log::Level::Warn);
        else if (!std::strcmp(lvl, "error")) log::set_level(log::Level::Error);
    }

    WorkerPoolConfig wp_cfg;
    wp_cfg.config_dir   = env_or("OFIQ_CONFIG_DIR",  "/opt/ofiq/data");
    wp_cfg.config_file  = env_or("OFIQ_CONFIG_FILE", "ofiq_config.jaxn");
    wp_cfg.worker_count = env_size("OFIQ_WORKERS", 0);
    if (wp_cfg.worker_count == 0) {
        wp_cfg.worker_count = std::min<std::size_t>(8, std::thread::hardware_concurrency());
        if (wp_cfg.worker_count == 0) wp_cfg.worker_count = 1;
    }
    wp_cfg.queue_depth  = env_size("OFIQ_QUEUE_DEPTH", 256);

    ServerConfig srv_cfg;
    srv_cfg.host             = env_or("OFIQ_HOST", "0.0.0.0");
    srv_cfg.port             = env_int("OFIQ_PORT", 8080);
    srv_cfg.max_upload_bytes = env_size("OFIQ_MAX_UPLOAD_MB", 10) * 1024 * 1024;
    srv_cfg.request_timeout  = std::chrono::milliseconds(
        env_int("OFIQ_REQUEST_TIMEOUT_MS", 30000));
    srv_cfg.startup_timeout  = std::chrono::milliseconds(
        env_int("OFIQ_STARTUP_TIMEOUT_MS", 120000));
    srv_cfg.index_html_path  = env_or("OFIQ_INDEX_HTML", "");

    const std::string thresholds_path = env_or("OFIQ_THRESHOLDS", "/etc/ofiq-api/thresholds.json");
    auto policy = VerdictPolicy::load_or_default(thresholds_path);

    {
        std::ostringstream os;
        os << R"("workers":)" << wp_cfg.worker_count
           << R"(,"queue_depth":)" << wp_cfg.queue_depth
           << R"(,"port":)" << srv_cfg.port
           << R"(,"max_upload_mb":)" << (srv_cfg.max_upload_bytes / (1024 * 1024))
           << R"(,"thresholds":")" << log::escape(thresholds_path) << R"(")";
        log::info("startup config", os.str());
    }

    Server server(srv_cfg, wp_cfg, std::move(policy));
    g_server.store(&server, std::memory_order_relaxed);

    std::signal(SIGINT,  handle_signal);
    std::signal(SIGTERM, handle_signal);
    std::signal(SIGPIPE, SIG_IGN);

    const bool ok = server.start();
    g_server.store(nullptr, std::memory_order_relaxed);

    log::info(ok ? "shutdown clean" : "shutdown with errors");
    return ok ? 0 : 1;
}
