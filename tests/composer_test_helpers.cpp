#include "composer_test_helpers.h"

#include "backend/jpeg_encoder.h"

#include <qpdf/Buffer.hh>
#include <qpdf/Pl_Buffer.hh>

#include <cstdlib>
#include <cstring>
#include <memory>

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
