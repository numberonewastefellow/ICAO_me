#include "verdict.h"

#include <algorithm>
#include <fstream>
#include <sstream>

#include "log.h"
#include "ofiq_runner.h"

namespace ofiq_api {

namespace {
const std::unordered_map<std::string, double> kDefaultThresholds = {
    {"UnifiedQualityScore",      70},
    {"BackgroundUniformity",     60},
    {"IlluminationUniformity",   60},
    {"DynamicRange",             60},
    {"Sharpness",                60},
    {"CompressionArtifacts",     60},
    {"NaturalColour",            60},
    {"SingleFacePresent",        70},
    {"EyesOpen",                 70},
    {"MouthClosed",              70},
    {"EyesVisible",              70},
    {"MouthOcclusionPrevention", 70},
    {"FaceOcclusionPrevention",  70},
    {"InterEyeDistance",         70},
    {"HeadSize",                 60},
    {"LeftwardCropOfTheFaceImage",  60},
    {"RightwardCropOfTheFaceImage", 60},
    {"MarginAboveOfTheFaceImage",   60},
    {"MarginBelowOfTheFaceImage",   60},
    {"HeadPoseYaw",              60},
    {"HeadPosePitch",            60},
    {"HeadPoseRoll",             60},
    {"ExpressionNeutrality",     70},
    {"NoHeadCoverings",          70},
    {"UnderExposurePrevention",  60},
    {"OverExposurePrevention",   60},
};
} // namespace

VerdictPolicy VerdictPolicy::defaults() {
    VerdictPolicy p;
    p.thresholds_ = kDefaultThresholds;
    return p;
}

VerdictPolicy VerdictPolicy::load_or_default(const std::string& path) {
    if (path.empty()) {
        return defaults();
    }
    std::ifstream f(path);
    if (!f.is_open()) {
        log::warn("thresholds file not found, using defaults",
                  std::string(R"("path":")") + log::escape(path) + R"(")");
        return defaults();
    }
    try {
        auto root = nlohmann::json::parse(f, /*cb=*/nullptr, /*allow_exceptions=*/true,
                                          /*ignore_comments=*/true);
        VerdictPolicy p;
        if (!root.contains("thresholds") || !root["thresholds"].is_object()) {
            log::warn("thresholds file has no 'thresholds' object, using defaults");
            return defaults();
        }
        for (const auto& [k, v] : root["thresholds"].items()) {
            if (v.is_number()) {
                p.thresholds_[k] = v.get<double>();
            }
        }
        if (p.thresholds_.empty()) {
            log::warn("thresholds map empty after parse, using defaults");
            return defaults();
        }
        std::ostringstream os;
        os << R"("path":")" << log::escape(path) << R"(","count":)" << p.thresholds_.size();
        log::info("loaded thresholds", os.str());
        return p;
    } catch (const std::exception& e) {
        log::warn("thresholds parse failed, using defaults",
                  std::string(R"("error":")") + log::escape(e.what()) + R"(")");
        return defaults();
    }
}

nlohmann::json VerdictPolicy::evaluate(const OFIQ::FaceImageQualityAssessment& a) const {
    using nlohmann::json;

    json failed = json::array();
    bool any_threshold_evaluated = false;
    double uqs_scalar = -1.0;
    bool   uqs_ok     = false;

    for (const auto& [m, r] : a.qAssessments) {
        const std::string name = measure_name(m);
        if (name == "UnifiedQualityScore") {
            uqs_scalar = r.scalar;
        }

        auto it = thresholds_.find(name);
        if (it == thresholds_.end()) {
            continue;
        }
        any_threshold_evaluated = true;

        if (r.code != OFIQ::QualityMeasureReturnCode::Success) {
            failed.push_back({{"measure", name},
                              {"scalar", r.scalar},
                              {"threshold", it->second},
                              {"reason", "measurement_failed"}});
            continue;
        }
        if (r.scalar < it->second) {
            failed.push_back({{"measure", name},
                              {"scalar", r.scalar},
                              {"threshold", it->second},
                              {"reason", "below_threshold"}});
        }
        if (name == "UnifiedQualityScore") {
            uqs_ok = (r.scalar >= it->second);
        }
    }

    // Full threshold map so the frontend can render every measure with its
    // threshold (not just the ones that failed).
    json thresholds_json = json::object();
    for (const auto& [k, v] : thresholds_) {
        thresholds_json[k] = v;
    }

    json out = json::object();
    out["icao_compliant"]  = any_threshold_evaluated && failed.empty();
    out["uqs"]             = uqs_scalar;
    out["uqs_ok"]          = uqs_ok;
    out["failed"]          = std::move(failed);
    out["thresholds"]      = std::move(thresholds_json);
    out["thresholds_count"] = static_cast<int>(thresholds_.size());
    return out;
}

} // namespace ofiq_api
