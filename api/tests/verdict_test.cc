#include <cstdio>
#include <cstdlib>

#include <ofiq_lib.h>
#include <ofiq_structs.h>

#include "verdict.h"

#define CHECK(x) do { \
    if (!(x)) { std::fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #x); std::abort(); } \
} while (0)

using namespace OFIQ;
using ofiq_api::VerdictPolicy;

static QualityMeasureResult ok(double scalar, double raw = 0) {
    return QualityMeasureResult(raw, scalar, QualityMeasureReturnCode::Success);
}

static FaceImageQualityAssessment make_assessment(double uqs, double mouth_closed) {
    FaceImageQualityAssessment a;
    a.qAssessments[QualityMeasure::UnifiedQualityScore] = ok(uqs);
    a.qAssessments[QualityMeasure::MouthClosed]         = ok(mouth_closed);
    a.qAssessments[QualityMeasure::EyesOpen]            = ok(95);
    a.qAssessments[QualityMeasure::HeadPoseYaw]         = ok(95);
    a.qAssessments[QualityMeasure::HeadPosePitch]       = ok(95);
    a.qAssessments[QualityMeasure::HeadPoseRoll]        = ok(95);
    a.qAssessments[QualityMeasure::Sharpness]           = ok(95);
    return a;
}

int main() {
    auto policy = VerdictPolicy::defaults();
    CHECK(policy.thresholds().count("UnifiedQualityScore") == 1);

    {
        auto a = make_assessment(85, 90);
        auto v = policy.evaluate(a);
        CHECK(v["icao_compliant"].get<bool>() == true);
        CHECK(v["failed"].size() == 0);
    }

    {
        auto a = make_assessment(60, 90);
        auto v = policy.evaluate(a);
        CHECK(v["icao_compliant"].get<bool>() == false);
        bool found = false;
        for (auto& f : v["failed"]) {
            if (f["measure"] == "UnifiedQualityScore") { found = true; break; }
        }
        CHECK(found);
    }

    {
        auto a = make_assessment(85, 23);   // smiling photo
        auto v = policy.evaluate(a);
        CHECK(v["icao_compliant"].get<bool>() == false);
        bool found = false;
        for (auto& f : v["failed"]) {
            if (f["measure"] == "MouthClosed") { found = true; break; }
        }
        CHECK(found);
    }

    std::puts("verdict_test OK");
    return 0;
}
