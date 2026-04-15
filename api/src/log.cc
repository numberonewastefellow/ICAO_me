#include "log.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>

namespace ofiq_api::log {

namespace {
std::atomic<Level> g_level{Level::Info};
std::mutex g_io_mtx;

const char* level_name(Level lvl) {
    switch (lvl) {
        case Level::Debug: return "debug";
        case Level::Info:  return "info";
        case Level::Warn:  return "warn";
        case Level::Error: return "error";
    }
    return "info";
}

std::string iso8601_now() {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto secs = time_point_cast<seconds>(now);
    auto ms = duration_cast<milliseconds>(now - secs).count();
    std::time_t t = system_clock::to_time_t(secs);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[64];
    std::snprintf(buf, sizeof(buf),
                  "%04d-%02d-%02dT%02d:%02d:%02d.%03lldZ",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec,
                  static_cast<long long>(ms));
    return buf;
}
} // namespace

void set_level(Level lvl) { g_level.store(lvl, std::memory_order_relaxed); }

std::string escape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char hex[8];
                    std::snprintf(hex, sizeof(hex), "\\u%04x", c);
                    out += hex;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

void emit(Level lvl, std::string_view msg, std::string_view fields) {
    if (static_cast<int>(lvl) < static_cast<int>(g_level.load(std::memory_order_relaxed))) {
        return;
    }
    std::string line;
    line.reserve(160 + msg.size() + fields.size());
    line += R"({"ts":")";
    line += iso8601_now();
    line += R"(","lvl":")";
    line += level_name(lvl);
    line += R"(","msg":")";
    line += escape(msg);
    line += '"';
    if (!fields.empty()) {
        line += ',';
        line += fields;
    }
    line += "}\n";

    std::lock_guard<std::mutex> lk(g_io_mtx);
    std::fwrite(line.data(), 1, line.size(), stdout);
    std::fflush(stdout);
}

} // namespace ofiq_api::log
