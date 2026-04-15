#pragma once
#include <string>
#include <string_view>

namespace ofiq_api::log {

enum class Level { Debug, Info, Warn, Error };

void set_level(Level lvl);

// Structured JSON line written to stdout. fields is a JSON-object fragment
// without the enclosing braces (e.g. R"("request_id":"abc","ms":12)").
void emit(Level lvl, std::string_view msg, std::string_view fields = {});

inline void debug(std::string_view m, std::string_view f = {}) { emit(Level::Debug, m, f); }
inline void info (std::string_view m, std::string_view f = {}) { emit(Level::Info,  m, f); }
inline void warn (std::string_view m, std::string_view f = {}) { emit(Level::Warn,  m, f); }
inline void error(std::string_view m, std::string_view f = {}) { emit(Level::Error, m, f); }

std::string escape(std::string_view s);

} // namespace ofiq_api::log
