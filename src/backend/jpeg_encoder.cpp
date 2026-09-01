#include "jpeg_encoder.h"

#include <qpdf/Pl_DCT.hh>
#include <qpdf/QPDFExc.hh>

#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace quantapdf::detail {

namespace {

void configure_compression(
    jpeg_image_spec const& spec,
    jpeg_compress_struct* cinfo)
{
    J_COLOR_SPACE const color_space =
        spec.components == 1 ? JCS_GRAYSCALE : JCS_RGB;
    jpeg_set_colorspace(cinfo, color_space);
    jpeg_set_quality(cinfo, spec.quality, TRUE);
    cinfo->dct_method = JDCT_ISLOW;
    cinfo->optimize_coding = FALSE;
    cinfo->arith_code = FALSE;
    cinfo->smoothing_factor = 0;
    cinfo->restart_interval = 0;
    cinfo->restart_in_rows = 0;
    cinfo->scan_info = nullptr;
    cinfo->num_scans = 0;

    if (spec.components == 1) {
        cinfo->write_JFIF_header = TRUE;
        cinfo->JFIF_major_version = 1;
        cinfo->JFIF_minor_version = 1;
        cinfo->density_unit = 0;
        cinfo->X_density = 1;
        cinfo->Y_density = 1;
        cinfo->write_Adobe_marker = FALSE;
    } else {
        cinfo->write_JFIF_header = FALSE;
        cinfo->write_Adobe_marker = TRUE;
        for (int index = 0; index < 3; ++index) {
            cinfo->comp_info[index].h_samp_factor = 1;
            cinfo->comp_info[index].v_samp_factor = 1;
        }
    }
}

bool sample_size_overflows(jpeg_image_spec const& spec) noexcept
{
    std::size_t const maximum = std::numeric_limits<std::size_t>::max();
    if (spec.width > maximum / spec.height) {
        return true;
    }
    std::size_t const pixels = spec.width * spec.height;
    return pixels > maximum / static_cast<std::size_t>(spec.components);
}

} // namespace

class jpeg_encoder::impl {
  public:
    impl(jpeg_image_spec const& spec, Pipeline* next) :
        config_(Pl_DCT::make_compress_config(
            [spec](jpeg_compress_struct* cinfo) {
                configure_compression(spec, cinfo);
            })),
        pipeline_(std::make_unique<Pl_DCT>(
            "quantapdf JPEG encoder",
            next,
            static_cast<JDIMENSION>(spec.width),
            static_cast<JDIMENSION>(spec.height),
            spec.components,
            spec.components == 1 ? JCS_GRAYSCALE : JCS_RGB,
            config_.get()))
    {
    }

    Pipeline* pipeline() noexcept
    {
        return pipeline_.get();
    }

  private:
    std::unique_ptr<Pl_DCT::CompressConfig> config_;
    std::unique_ptr<Pl_DCT> pipeline_;
};

jpeg_encoder::jpeg_encoder(std::unique_ptr<impl> impl) noexcept :
    impl_(std::move(impl))
{
}

jpeg_encoder::~jpeg_encoder() = default;

quantapdf_status
jpeg_encoder::create(
    jpeg_image_spec const& spec,
    Pipeline* next,
    std::unique_ptr<jpeg_encoder>* out) noexcept
{
    if (out == nullptr) {
        return QUANTAPDF_ERROR_ARGUMENT;
    }
    out->reset();
    if (next == nullptr || spec.width == 0 || spec.height == 0 ||
        (spec.components != 1 && spec.components != 3) || spec.quality < 1 ||
        spec.quality > 100) {
        return QUANTAPDF_ERROR_ARGUMENT;
    }

    std::size_t const jdimension_max =
        static_cast<std::size_t>(std::numeric_limits<JDIMENSION>::max());
    if (spec.width > jdimension_max || spec.height > jdimension_max ||
        sample_size_overflows(spec)) {
        return QUANTAPDF_ERROR_FORMAT;
    }

    try {
        auto implementation = std::make_unique<impl>(spec, next);
        out->reset(new jpeg_encoder(std::move(implementation)));
        return QUANTAPDF_OK;
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (QPDFExc const&) {
        return QUANTAPDF_ERROR_BACKEND;
    } catch (std::exception const&) {
        return QUANTAPDF_ERROR_BACKEND;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}

Pipeline*
jpeg_encoder::pipeline() noexcept
{
    return impl_ == nullptr ? nullptr : impl_->pipeline();
}

} // namespace quantapdf::detail
