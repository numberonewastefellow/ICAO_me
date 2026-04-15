#include "metrics.h"

#include <cmath>
#include <cstdio>
#include <sstream>

namespace ofiq_api {

constexpr std::array<double, 9> Metrics::kBuckets;

Metrics::Metrics() {
    for (auto& a : req_hist_) a.store(0, std::memory_order_relaxed);
    for (auto& a : inf_hist_) a.store(0, std::memory_order_relaxed);
    req_sum_ms_.store(0.0, std::memory_order_relaxed);
    inf_sum_ms_.store(0.0, std::memory_order_relaxed);
    req_count_.store(0, std::memory_order_relaxed);
    inf_count_.store(0, std::memory_order_relaxed);
}

Metrics& Metrics::instance() {
    static Metrics m;
    return m;
}

namespace {
void atomic_add_double(std::atomic<double>& a, double v) {
    double cur = a.load(std::memory_order_relaxed);
    while (!a.compare_exchange_weak(cur, cur + v,
                                    std::memory_order_relaxed,
                                    std::memory_order_relaxed)) {
    }
}

void observe_into(std::array<std::atomic<std::uint64_t>, 9>& hist,
                  std::atomic<double>& sum, std::atomic<std::uint64_t>& count,
                  double ms) {
    for (std::size_t i = 0; i < Metrics::kBuckets.size(); ++i) {
        if (ms <= Metrics::kBuckets[i]) {
            hist[i].fetch_add(1, std::memory_order_relaxed);
            break;
        }
    }
    atomic_add_double(sum, ms);
    count.fetch_add(1, std::memory_order_relaxed);
}
} // namespace

void Metrics::inc_requests_total(int status) {
    if (status >= 200 && status < 300) requests_total_2xx_.fetch_add(1);
    else if (status >= 400 && status < 500) requests_total_4xx_.fetch_add(1);
    else if (status >= 500) requests_total_5xx_.fetch_add(1);
}

void Metrics::observe_request_duration_ms(double ms) {
    observe_into(req_hist_, req_sum_ms_, req_count_, ms);
}
void Metrics::observe_inference_duration_ms(double ms) {
    observe_into(inf_hist_, inf_sum_ms_, inf_count_, ms);
}
void Metrics::inc_decode_failed() { decode_failed_.fetch_add(1); }
void Metrics::inc_queue_full()    { queue_full_.fetch_add(1);    }

void Metrics::set_workers_total(std::size_t v) { workers_total_.store(v); }
void Metrics::set_workers_ready(std::size_t v) { workers_ready_.store(v); }
void Metrics::set_workers_busy (std::size_t v) { workers_busy_.store(v);  }
void Metrics::set_queue_depth  (std::size_t v) { queue_depth_.store(v);   }
void Metrics::set_queue_size   (std::size_t v) { queue_size_.store(v);    }

std::string Metrics::render_prometheus() const {
    std::ostringstream os;
    os.setf(std::ios::fixed);
    os.precision(3);

    auto req_2xx = requests_total_2xx_.load();
    auto req_4xx = requests_total_4xx_.load();
    auto req_5xx = requests_total_5xx_.load();

    os << "# HELP ofiq_requests_total Total HTTP requests grouped by status class.\n";
    os << "# TYPE ofiq_requests_total counter\n";
    os << "ofiq_requests_total{status=\"2xx\"} " << req_2xx << '\n';
    os << "ofiq_requests_total{status=\"4xx\"} " << req_4xx << '\n';
    os << "ofiq_requests_total{status=\"5xx\"} " << req_5xx << '\n';

    os << "# HELP ofiq_decode_failed_total Image decode failures.\n";
    os << "# TYPE ofiq_decode_failed_total counter\n";
    os << "ofiq_decode_failed_total " << decode_failed_.load() << '\n';

    os << "# HELP ofiq_queue_full_total Requests rejected due to queue back-pressure.\n";
    os << "# TYPE ofiq_queue_full_total counter\n";
    os << "ofiq_queue_full_total " << queue_full_.load() << '\n';

    auto emit_hist = [&](const char* name,
                         const std::array<std::atomic<std::uint64_t>, 9>& hist,
                         const std::atomic<double>& sum,
                         const std::atomic<std::uint64_t>& count) {
        os << "# HELP " << name << " Latency histogram in milliseconds.\n";
        os << "# TYPE " << name << " histogram\n";
        std::uint64_t cum = 0;
        for (std::size_t i = 0; i < kBuckets.size(); ++i) {
            cum += hist[i].load();
            os << name << "_bucket{le=\"";
            if (std::isinf(kBuckets[i])) os << "+Inf";
            else                         os << kBuckets[i];
            os << "\"} " << cum << '\n';
        }
        os << name << "_sum "   << sum.load()   << '\n';
        os << name << "_count " << count.load() << '\n';
    };

    emit_hist("ofiq_request_duration_ms", req_hist_, req_sum_ms_, req_count_);
    emit_hist("ofiq_inference_duration_ms", inf_hist_, inf_sum_ms_, inf_count_);

    os << "# HELP ofiq_workers_total Configured worker count.\n";
    os << "# TYPE ofiq_workers_total gauge\n";
    os << "ofiq_workers_total " << workers_total_.load() << '\n';
    os << "# HELP ofiq_workers_ready Workers that completed initialize().\n";
    os << "# TYPE ofiq_workers_ready gauge\n";
    os << "ofiq_workers_ready " << workers_ready_.load() << '\n';
    os << "# HELP ofiq_workers_busy Workers currently inside vectorQuality.\n";
    os << "# TYPE ofiq_workers_busy gauge\n";
    os << "ofiq_workers_busy " << workers_busy_.load() << '\n';
    os << "# HELP ofiq_queue_depth Maximum bounded queue depth.\n";
    os << "# TYPE ofiq_queue_depth gauge\n";
    os << "ofiq_queue_depth " << queue_depth_.load() << '\n';
    os << "# HELP ofiq_queue_size Current queue size.\n";
    os << "# TYPE ofiq_queue_size gauge\n";
    os << "ofiq_queue_size " << queue_size_.load() << '\n';

    return os.str();
}

} // namespace ofiq_api
