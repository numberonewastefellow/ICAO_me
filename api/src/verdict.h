#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>
#include <ofiq_lib.h>
#include <ofiq_structs.h>

namespace ofiq_api {

// A pass/fail policy: the photo passes only if every threshold-bearing
// measure has scalar >= threshold. Measures absent from the table are
// informational and never cause a failure.
class VerdictPolicy {
public:
    // Loads thresholds from a JSON file shaped like:
    //   { "thresholds": { "UnifiedQualityScore": 70, "MouthClosed": 50, ... } }
    // Falls back to safe defaults (UQS >= 70, every other listed measure >= 70)
    // if the file is missing or malformed.
    static VerdictPolicy load_or_default(const std::string& path);

    // Built-in defaults (no file).
    static VerdictPolicy defaults();

    nlohmann::json evaluate(const OFIQ::FaceImageQualityAssessment& a) const;

    const std::unordered_map<std::string, double>& thresholds() const { return thresholds_; }

private:
    VerdictPolicy() = default;
    std::unordered_map<std::string, double> thresholds_;
};

} // namespace ofiq_api
