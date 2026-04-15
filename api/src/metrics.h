#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

namespace ofiq_api {

class Metrics {
public:
    static Metrics& instance();

    void inc_requests_total(int status);
    void observe_request_duration_ms(double ms);
    void observe_inference_duration_ms(double ms);
    void inc_decode_failed();
    void inc_queue_full();

    void set_workers_total(std::size_t v);
    void set_workers_ready(std::size_t v);
    void set_workers_busy(std::size_t v);
    void set_queue_depth(std::size_t v);
    void set_queue_size(std::size_t v);

    std::string render_prometheus() const;

    // Histogram buckets (ms): 50, 100, 200, 300, 500, 800, 1500, 3000, +inf
    // Public so that helpers in the .cc file's anonymous namespace can read them.
    static constexpr std::array<double, 9> kBuckets{
        50, 100, 200, 300, 500, 800, 1500, 3000, std::numeric_limits<double>::infinity()};

private:
    Metrics();

    std::atomic<std::uint64_t> requests_total_2xx_{0};
    std::atomic<std::uint64_t> requests_total_4xx_{0};
    std::atomic<std::uint64_t> requests_total_5xx_{0};
    std::atomic<std::uint64_t> decode_failed_{0};
    std::atomic<std::uint64_t> queue_full_{0};

    std::array<std::atomic<std::uint64_t>, 9> req_hist_{};
    std::array<std::atomic<std::uint64_t>, 9> inf_hist_{};
    std::atomic<double>        req_sum_ms_{0.0};
    std::atomic<std::uint64_t> req_count_{0};
    std::atomic<double>        inf_sum_ms_{0.0};
    std::atomic<std::uint64_t> inf_count_{0};

    std::atomic<std::size_t> workers_total_{0};
    std::atomic<std::size_t> workers_ready_{0};
    std::atomic<std::size_t> workers_busy_{0};
    std::atomic<std::size_t> queue_depth_{0};
    std::atomic<std::size_t> queue_size_{0};
};

} // namespace ofiq_api
