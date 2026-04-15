#include "ofiq_runner.h"

#include <cstring>
#include <limits>
#include <memory>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wpedantic"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_PSD
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_FAILURE_USERMSG
#include <stb_image.h>
#pragma GCC diagnostic pop

namespace ofiq_api {

DecodedImage decode_image(const void* bytes, std::size_t len, DecodeError& err) {
    DecodedImage out{};

    if (!bytes || len == 0) {
        err.message = "empty image payload";
        return out;
    }
    if (len > std::numeric_limits<int>::max()) {
        err.message = "image too large";
        return out;
    }

    int w = 0, h = 0, ch = 0;
    stbi_uc* rgb = stbi_load_from_memory(
        static_cast<const stbi_uc*>(bytes),
        static_cast<int>(len),
        &w, &h, &ch, /*desired_channels=*/3);

    if (!rgb) {
        err.message = std::string("decode failed: ") + (stbi_failure_reason() ? stbi_failure_reason() : "unknown");
        return out;
    }
    if (w <= 0 || h <= 0 || w > 65535 || h > 65535) {
        stbi_image_free(rgb);
        err.message = "invalid image dimensions";
        return out;
    }

    const std::size_t pixels = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    const std::size_t nbytes = pixels * 3;

    auto buf = std::shared_ptr<uint8_t[]>(
        new uint8_t[nbytes], std::default_delete<uint8_t[]>());

    // RGB -> BGR (OFIQ::Image expects 24bpp BGR scanlines)
    const uint8_t* src = rgb;
    uint8_t* dst = buf.get();
    for (std::size_t i = 0; i < pixels; ++i) {
        dst[0] = src[2];
        dst[1] = src[1];
        dst[2] = src[0];
        src += 3;
        dst += 3;
    }
    stbi_image_free(rgb);

    out.image = OFIQ::Image(static_cast<uint16_t>(w),
                            static_cast<uint16_t>(h),
                            /*depth=*/24,
                            buf);
    out.orig_width    = w;
    out.orig_height   = h;
    out.orig_channels = ch == 0 ? 3 : ch;
    return out;
}

nlohmann::json assessment_to_json(const OFIQ::FaceImageQualityAssessment& a) {
    using nlohmann::json;

    json measures = json::object();
    for (const auto& [m, r] : a.qAssessments) {
        const char* name = measure_name(m);
        json entry = json::object();
        entry["raw"]    = r.rawScore;
        entry["scalar"] = r.scalar;
        entry["status"] =
            r.code == OFIQ::QualityMeasureReturnCode::Success        ? "success" :
            r.code == OFIQ::QualityMeasureReturnCode::FailureToAssess ? "failure" :
                                                                       "uninitialized";
        measures[name] = std::move(entry);
    }

    json bbox = json::object();
    bbox["x"]      = a.boundingBox.xleft;
    bbox["y"]      = a.boundingBox.ytop;
    bbox["width"]  = a.boundingBox.width;
    bbox["height"] = a.boundingBox.height;

    json out = json::object();
    out["measures"]      = std::move(measures);
    out["bounding_box"]  = std::move(bbox);
    return out;
}

} // namespace ofiq_api
