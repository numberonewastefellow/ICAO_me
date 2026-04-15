#include "worker_pool.h"

#include <chrono>
#include <sstream>
#include <utility>

#include "log.h"
#include "ofiq_runner.h"

namespace ofiq_api {

WorkerPool::WorkerPool(WorkerPoolConfig cfg) : cfg_(std::move(cfg)) {
    if (cfg_.worker_count == 0) cfg_.worker_count = 1;
    if (cfg_.queue_depth  == 0) cfg_.queue_depth  = 1;

    workers_.reserve(cfg_.worker_count);
    for (std::size_t i = 0; i < cfg_.worker_count; ++i) {
        workers_.emplace_back(&WorkerPool::worker_main, this, i);
    }
}

WorkerPool::~WorkerPool() { shutdown(); }

void WorkerPool::shutdown() {
    bool expected = false;
    if (!shutting_down_.compare_exchange_strong(expected, true)) return;

    {
        std::lock_guard<std::mutex> lk(mtx_);
        cv_pop_.notify_all();
        cv_push_.notify_all();
    }
    for (auto& t : workers_) {
        if (t.joinable()) t.join();
    }

    // Drain pending requests with errors so callers don't hang on futures.
    std::lock_guard<std::mutex> lk(mtx_);
    while (!queue_.empty()) {
        auto req = std::move(queue_.front());
        queue_.pop_front();
        try {
            req->error_promise.set_value("server shutting down");
            req->promise.set_value(OFIQ::FaceImageQualityAssessment{});
        } catch (...) { /* promise already satisfied */ }
    }
}

bool WorkerPool::wait_until_ready(std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (workers_ready() + init_failures_.load() >= cfg_.worker_count) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return workers_ready() > 0;
}

std::size_t WorkerPool::queue_size() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return queue_.size();
}

std::optional<WorkerPool::Handle> WorkerPool::submit(OFIQ::Image image, std::string request_id) {
    if (shutting_down_.load(std::memory_order_relaxed)) return std::nullopt;
    if (workers_ready() == 0) return std::nullopt;

    auto req = std::make_unique<InferenceRequest>();
    req->image       = std::move(image);
    req->request_id  = std::move(request_id);
    req->enqueued_at = std::chrono::steady_clock::now();

    Handle h{ req->promise.get_future(), req->error_promise.get_future() };

    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (queue_.size() >= cfg_.queue_depth) return std::nullopt;
        queue_.push_back(std::move(req));
    }
    cv_pop_.notify_one();
    return h;
}

void WorkerPool::worker_main(std::size_t idx) {
    std::ostringstream init_fields;
    init_fields << R"("worker":)" << idx;

    log::info("worker initializing", init_fields.str());

    std::shared_ptr<OFIQ::Interface> impl;
    try {
        impl = OFIQ::Interface::getImplementation();
    } catch (const std::exception& e) {
        std::ostringstream os;
        os << R"("worker":)" << idx << R"(,"error":")" << log::escape(e.what()) << R"(")";
        log::error("getImplementation threw", os.str());
        init_failures_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (!impl) {
        log::error("getImplementation returned null", init_fields.str());
        init_failures_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    OFIQ::ReturnStatus rs;
    try {
        rs = impl->initialize(cfg_.config_dir, cfg_.config_file);
    } catch (const std::exception& e) {
        std::ostringstream os;
        os << R"("worker":)" << idx << R"(,"error":")" << log::escape(e.what()) << R"(")";
        log::error("initialize threw", os.str());
        init_failures_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (rs.code != OFIQ::ReturnCode::Success) {
        std::ostringstream os;
        os << R"("worker":)" << idx << R"(,"info":")" << log::escape(rs.info) << R"(")";
        log::error("initialize failed", os.str());
        init_failures_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    ready_count_.fetch_add(1, std::memory_order_relaxed);
    log::info("worker ready", init_fields.str());

    while (true) {
        std::unique_ptr<InferenceRequest> req;
        {
            std::unique_lock<std::mutex> lk(mtx_);
            cv_pop_.wait(lk, [this] {
                return shutting_down_.load(std::memory_order_relaxed) || !queue_.empty();
            });
            if (shutting_down_.load(std::memory_order_relaxed) && queue_.empty()) {
                break;
            }
            req = std::move(queue_.front());
            queue_.pop_front();
        }

        busy_count_.fetch_add(1, std::memory_order_relaxed);
        OFIQ::FaceImageQualityAssessment assessment;
        try {
            const auto rs2 = impl->vectorQuality(req->image, assessment);
            if (rs2.code != OFIQ::ReturnCode::Success) {
                req->error_promise.set_value(
                    "vectorQuality failed: " + rs2.info);
                req->promise.set_value(std::move(assessment));
            } else {
                req->error_promise.set_value(std::string{});
                req->promise.set_value(std::move(assessment));
            }
        } catch (const std::exception& e) {
            req->error_promise.set_value(std::string("exception: ") + e.what());
            req->promise.set_value(OFIQ::FaceImageQualityAssessment{});
        } catch (...) {
            req->error_promise.set_value("unknown exception");
            req->promise.set_value(OFIQ::FaceImageQualityAssessment{});
        }
        busy_count_.fetch_sub(1, std::memory_order_relaxed);
    }

    log::info("worker exiting", init_fields.str());
}

} // namespace ofiq_api
