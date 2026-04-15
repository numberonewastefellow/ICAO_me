// Minimal smoke test for stb_image -> OFIQ::Image conversion.
// Builds a tiny PPM in memory (which stb_image supports if STBI_NO_PNM is not
// set) — but stb_image does NOT decode PPM. So instead we encode a 4x4 BMP by
// hand which IS supported.
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "ofiq_runner.h"

#define CHECK(x) do { \
    if (!(x)) { std::fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #x); std::abort(); } \
} while (0)

// Build a tiny 2x2 BMP (24bpp) with known pixels.
static std::vector<uint8_t> make_bmp_2x2() {
    // BMP file header (14) + DIB header (40) + pixel data (with row padding).
    const uint16_t W = 2, H = 2;
    const uint32_t row_size = ((24 * W + 31) / 32) * 4;  // padded to 4 bytes
    const uint32_t pixel_bytes = row_size * H;
    const uint32_t file_size = 54 + pixel_bytes;

    std::vector<uint8_t> buf(file_size, 0);
    auto put16 = [&](size_t off, uint16_t v) { buf[off]=v; buf[off+1]=v>>8; };
    auto put32 = [&](size_t off, uint32_t v) {
        buf[off]=v; buf[off+1]=v>>8; buf[off+2]=v>>16; buf[off+3]=v>>24;
    };

    buf[0]='B'; buf[1]='M';
    put32(2,  file_size);
    put32(10, 54);     // pixel data offset

    put32(14, 40);     // DIB header size
    put32(18, W);
    put32(22, H);
    put16(26, 1);
    put16(28, 24);     // bpp
    put32(30, 0);      // BI_RGB
    put32(34, pixel_bytes);

    // BMP rows are bottom-up. Pixels are BGR.
    // Row 0 (bottom): red, green
    // Row 1 (top):    blue, white
    uint8_t* row0 = &buf[54 + 0 * row_size];
    row0[0]=0;   row0[1]=0;   row0[2]=255;   // red
    row0[3]=0;   row0[4]=255; row0[5]=0;     // green
    uint8_t* row1 = &buf[54 + 1 * row_size];
    row1[0]=255; row1[1]=0;   row1[2]=0;     // blue
    row1[3]=255; row1[4]=255; row1[5]=255;   // white
    return buf;
}

int main() {
    auto bmp = make_bmp_2x2();
    ofiq_api::DecodeError err;
    auto dec = ofiq_api::decode_image(bmp.data(), bmp.size(), err);
    if (!dec.ok()) {
        std::printf("decode failed: %s\n", err.message.c_str());
        return 1;
    }
    CHECK(dec.image.width == 2);
    CHECK(dec.image.height == 2);
    CHECK(dec.image.depth == 24);
    CHECK(dec.image.data != nullptr);

    // OFIQ::Image is BGR scanline. Top-left pixel after decode should be blue
    // (BMP top row stored at top after stb_image flip, BGR ordering).
    const uint8_t* d = dec.image.data.get();
    // Pixel layout (row-major from top):
    //   (0,0)=blue (B=255,G=0,R=0)
    //   (0,1)=white (B=255,G=255,R=255)
    //   (1,0)=red (B=0,G=0,R=255)
    //   (1,1)=green (B=0,G=255,R=0)
    auto px = [&](int y, int x) -> const uint8_t* { return &d[(y * 2 + x) * 3]; };

    auto p00 = px(0, 0);
    auto p01 = px(0, 1);
    auto p10 = px(1, 0);
    auto p11 = px(1, 1);

    CHECK(p00[0] == 255 && p00[1] == 0   && p00[2] == 0);    // blue
    CHECK(p01[0] == 255 && p01[1] == 255 && p01[2] == 255);  // white
    CHECK(p10[0] == 0   && p10[1] == 0   && p10[2] == 255);  // red
    CHECK(p11[0] == 0   && p11[1] == 255 && p11[2] == 0);    // green

    std::puts("image_decode_test OK");
    return 0;
}
