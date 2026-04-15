#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <ofiq_lib.h>
#include <ofiq_structs.h>

namespace ofiq_api {

struct InferenceRequest {
    OFIQ::Image                                                 image;
    std::string                                                 request_id;
    std::chrono::steady_clock::time_point                       enqueued_at;
    std::promise<OFIQ::FaceImageQualityAssessment>              promise;
    std::promise<std::string>                                   error_promise;
    bool                                                        used_error = false;
};

struct WorkerPoolConfig {
    std::size_t worker_count   = 8;
    std::size_t queue_depth    = 256;
    std::string config_dir     = "/opt/ofiq/data";
    std::string config_file    = "ofiq_config.jaxn";
};

// A bounded MPMC queue + N worker threads. Each worker owns its own
// fully-initialized OFIQ::Interface instance (loaded once at startup).
// Submission is non-blocking: returns false (back-pressure) if queue full.
class WorkerPool {
public:
    explicit WorkerPool(WorkerPoolConfig cfg);
    ~WorkerPool();

    WorkerPool(const WorkerPool&)            = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    // Block until every worker either finished initialize() OK or failed.
    // Returns true iff at least one worker is ready to serve traffic.
    bool wait_until_ready(std::chrono::milliseconds timeout);

    // Submit returns nullopt if the queue is full.
    // The returned future resolves with either a valid assessment or an
    // empty assessment (caller checks the error_future).
    struct Handle {
        std::future<OFIQ::FaceImageQualityAssessment> result;
        std::future<std::string>                      error;
    };
    std::optional<Handle> submit(OFIQ::Image image, std::string request_id);

    void shutdown();

    // ----- introspection (for /metrics & /readyz) -----
    std::size_t workers_total() const { return cfg_.worker_count; }
    std::size_t workers_ready() const { return ready_count_.load(std::memory_order_relaxed); }
    std::size_t workers_busy()  const { return busy_count_.load(std::memory_order_relaxed); }
    std::size_t queue_size() const;
    std::size_t queue_depth() const   { return cfg_.queue_depth; }
    bool ready() const                { return workers_ready() > 0; }

private:
    void worker_main(std::size_t idx);

    WorkerPoolConfig                          cfg_;
    std::vector<std::thread>                  workers_;
    std::deque<std::unique_ptr<InferenceRequest>> queue_;
    mutable std::mutex                        mtx_;
    std::condition_variable                   cv_pop_;
    std::condition_variable                   cv_push_;
    std::atomic<bool>                         shutting_down_{false};
    std::atomic<std::size_t>                  ready_count_{0};
    std::atomic<std::size_t>                  busy_count_{0};
    std::atomic<std::size_t>                  init_failures_{0};
};

} // namespace ofiq_api
