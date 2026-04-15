#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <string>

#include "verdict.h"
#include "worker_pool.h"

namespace ofiq_api {

struct ServerConfig {
    std::string host             = "0.0.0.0";
    int         port             = 8080;
    std::size_t max_upload_bytes = 10 * 1024 * 1024; // 10 MB
    std::chrono::milliseconds request_timeout{30000};
    std::chrono::milliseconds startup_timeout{120000};
    int         keep_alive_max_count = 100;

    // If set to a readable file path, GET / re-reads this file from disk on
    // every request (great for HTML iteration). Otherwise the embedded
    // index_html baked into the binary at compile time is served.
    std::string index_html_path;
};

class Server {
public:
    Server(ServerConfig srv_cfg, WorkerPoolConfig wp_cfg, VerdictPolicy policy);
    ~Server();

    Server(const Server&)            = delete;
    Server& operator=(const Server&) = delete;

    bool start();   // blocks until stop()
    void stop();

private:
    ServerConfig                    cfg_;
    VerdictPolicy                   policy_;
    std::unique_ptr<WorkerPool>     pool_;

    // opaque pimpl for httplib::Server (avoids leaking the header here)
    struct Impl;
    std::unique_ptr<Impl>           impl_;

    std::atomic<std::uint64_t>      next_request_id_{1};
};

} // namespace ofiq_api
