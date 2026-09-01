#include "composer_test_helpers.h"

#include "backend/jpeg_encoder.h"

#include <qpdf/Buffer.hh>
#include <qpdf/Pl_Buffer.hh>
#include <zlib.h>

#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

namespace {

void append_u32(std::vector<unsigned char>& data, unsigned long value)
{
    data.push_back(static_cast<unsigned char>((value >> 24u) & 0xffu));
    data.push_back(static_cast<unsigned char>((value >> 16u) & 0xffu));
    data.push_back(static_cast<unsigned char>((value >> 8u) & 0xffu));
    data.push_back(static_cast<unsigned char>(value & 0xffu));
}

void append_chunk(
    std::vector<unsigned char>& png,
    char const type[4],
    unsigned char const* payload,
    size_t size)
{
    append_u32(png, static_cast<unsigned long>(size));
    size_t const crc_start = png.size();
    png.insert(png.end(), type, type + 4);
    if (size != 0u)
        png.insert(png.end(), payload, payload + size);
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, png.data() + crc_start, static_cast<uInt>(size + 4u));
    append_u32(png, crc);
}

} // namespace

extern "C" int quantapdf_test_make_jpeg(
    unsigned char** out_data,
    size_t* out_size)
{
    unsigned char samples[8u * 4u * 3u];
    Pl_Buffer sink("composer-test-jpeg");
    std::unique_ptr<quantapdf::detail::jpeg_encoder> encoder;

    if (out_data == nullptr || out_size == nullptr)
        return 0;
    *out_data = nullptr;
    *out_size = 0u;
    for (size_t y = 0u; y < 4u; ++y) {
        for (size_t x = 0u; x < 8u; ++x) {
            size_t offset = (y * 8u + x) * 3u;
            samples[offset] = static_cast<unsigned char>(x * 32u);
            samples[offset + 1u] = static_cast<unsigned char>(y * 64u);
            samples[offset + 2u] = 32u;
        }
    }
    if (quantapdf::detail::jpeg_encoder::create(
            {8u, 4u, 3, 90}, &sink, &encoder) != QUANTAPDF_OK)
        return 0;
    encoder->pipeline()->write(samples, sizeof(samples));
    encoder->pipeline()->finish();
    std::unique_ptr<Buffer> buffer(sink.getBuffer());
    auto* copied = static_cast<unsigned char*>(std::malloc(buffer->getSize()));
    if (copied == nullptr)
        return 0;
    std::memcpy(copied, buffer->getBuffer(), buffer->getSize());
    *out_data = copied;
    *out_size = buffer->getSize();
    return 1;
}

extern "C" int quantapdf_test_make_png(
    int alpha,
    unsigned char** out_data,
    size_t* out_size)
{
    static unsigned char const signature[] = {
        0x89u, 'P', 'N', 'G', 0x0du, 0x0au, 0x1au, 0x0au};
    unsigned char ihdr[13] = {
        0u, 0u, 0u, 2u, 0u, 0u, 0u, 1u, 8u,
        static_cast<unsigned char>(alpha ? 6u : 2u), 0u, 0u, 0u};
    unsigned char rgba_scanline[] = {
        0u, 255u, 0u, 0u, 0u, 0u, 0u, 255u, 255u};
    unsigned char rgb_scanline[] = {
        0u, 255u, 0u, 0u, 0u, 255u, 0u};
    unsigned char const* scanline = alpha ? rgba_scanline : rgb_scanline;
    uLong scanline_size = alpha ? sizeof(rgba_scanline) : sizeof(rgb_scanline);
    uLongf compressed_size = compressBound(scanline_size);
    std::vector<unsigned char> compressed(compressed_size);
    std::vector<unsigned char> png(signature, signature + sizeof(signature));

    if (out_data == nullptr || out_size == nullptr)
        return 0;
    *out_data = nullptr;
    *out_size = 0u;
    if (compress2(
            compressed.data(), &compressed_size, scanline, scanline_size,
            Z_BEST_COMPRESSION) != Z_OK)
        return 0;
    compressed.resize(compressed_size);
    append_chunk(png, "IHDR", ihdr, sizeof(ihdr));
    append_chunk(png, "IDAT", compressed.data(), compressed.size());
    append_chunk(png, "IEND", nullptr, 0u);
    auto* copied = static_cast<unsigned char*>(std::malloc(png.size()));
    if (copied == nullptr)
        return 0;
    std::memcpy(copied, png.data(), png.size());
    *out_data = copied;
    *out_size = png.size();
    return 1;
}
