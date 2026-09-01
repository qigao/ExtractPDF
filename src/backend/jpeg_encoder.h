#ifndef QUANTAPDF_BACKEND_JPEG_ENCODER_H
#define QUANTAPDF_BACKEND_JPEG_ENCODER_H

#include <quantapdf/quantapdf.h>

#include <cstddef>
#include <memory>

class Pipeline;

namespace quantapdf::detail {

struct jpeg_image_spec {
    std::size_t width;
    std::size_t height;
    int components;
    int quality;
};

class jpeg_encoder {
  public:
    ~jpeg_encoder();
    jpeg_encoder(jpeg_encoder const&) = delete;
    jpeg_encoder& operator=(jpeg_encoder const&) = delete;

    static quantapdf_status create(
        jpeg_image_spec const& spec,
        Pipeline* next,
        std::unique_ptr<jpeg_encoder>* out) noexcept;

    Pipeline* pipeline() noexcept;

  private:
    class impl;

    explicit jpeg_encoder(std::unique_ptr<impl> impl) noexcept;

    std::unique_ptr<impl> impl_;
};

} // namespace quantapdf::detail

#endif
