#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include <ofiq_lib.h>
#include <ofiq_structs.h>

#include <nlohmann/json.hpp>

namespace ofiq_api {

struct DecodedImage {
    OFIQ::Image image;
    int orig_width  = 0;
    int orig_height = 0;
    int orig_channels = 0;

    bool ok() const { return image.data && image.width > 0 && image.height > 0; }
};

struct DecodeError {
    std::string message;
};

// Decode JPG/PNG/BMP/GIF/etc bytes into an OFIQ::Image (BGR, 24bpp).
// Returns DecodedImage with image.data == nullptr on failure (and fills err).
DecodedImage decode_image(const void* bytes, std::size_t len, DecodeError& err);

// Convert an OFIQ assessment result + bounding box into a JSON object.
// Includes per-measure raw + scalar scores using the human-readable measure
// names that match the OFIQSampleApp CSV output column headers.
nlohmann::json assessment_to_json(const OFIQ::FaceImageQualityAssessment& a);

// Stable measure name table (mirrors the OFIQSampleApp CSV headers).
inline const char* measure_name(OFIQ::QualityMeasure m) {
    using M = OFIQ::QualityMeasure;
    switch (m) {
        case M::UnifiedQualityScore:        return "UnifiedQualityScore";
        case M::BackgroundUniformity:       return "BackgroundUniformity";
        case M::IlluminationUniformity:     return "IlluminationUniformity";
        case M::Luminance:                  return "Luminance";
        case M::LuminanceMean:              return "LuminanceMean";
        case M::LuminanceVariance:          return "LuminanceVariance";
        case M::UnderExposurePrevention:    return "UnderExposurePrevention";
        case M::OverExposurePrevention:     return "OverExposurePrevention";
        case M::DynamicRange:               return "DynamicRange";
        case M::Sharpness:                  return "Sharpness";
        case M::CompressionArtifacts:       return "CompressionArtifacts";
        case M::NaturalColour:              return "NaturalColour";
        case M::SingleFacePresent:          return "SingleFacePresent";
        case M::EyesOpen:                   return "EyesOpen";
        case M::MouthClosed:                return "MouthClosed";
        case M::EyesVisible:                return "EyesVisible";
        case M::MouthOcclusionPrevention:   return "MouthOcclusionPrevention";
        case M::FaceOcclusionPrevention:    return "FaceOcclusionPrevention";
        case M::InterEyeDistance:           return "InterEyeDistance";
        case M::HeadSize:                   return "HeadSize";
        case M::CropOfTheFaceImage:         return "CropOfTheFaceImage";
        case M::LeftwardCropOfTheFaceImage: return "LeftwardCropOfTheFaceImage";
        case M::RightwardCropOfTheFaceImage:return "RightwardCropOfTheFaceImage";
        case M::MarginAboveOfTheFaceImage:  return "MarginAboveOfTheFaceImage";
        case M::MarginBelowOfTheFaceImage:  return "MarginBelowOfTheFaceImage";
        case M::HeadPose:                   return "HeadPose";
        case M::HeadPoseYaw:                return "HeadPoseYaw";
        case M::HeadPosePitch:              return "HeadPosePitch";
        case M::HeadPoseRoll:               return "HeadPoseRoll";
        case M::ExpressionNeutrality:       return "ExpressionNeutrality";
        case M::NoHeadCoverings:            return "NoHeadCoverings";
        case M::NotSet:                     return "NotSet";
    }
    return "Unknown";
}

} // namespace ofiq_api
