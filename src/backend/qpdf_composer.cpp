#include "qpdf_composer.h"

#include "../internal.h"

#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string number(double value)
{
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(4) << value;
    std::string result = stream.str();
    while (result.size() > 1u && result.back() == '0')
        result.pop_back();
    if (!result.empty() && result.back() == '.')
        result.pop_back();
    return result;
}

char const* base_font_name(quantapdf_composer_font font)
{
    static char const* const names[] = {
        "/Helvetica", "/Helvetica-Bold", "/Helvetica-Oblique",
        "/Helvetica-BoldOblique", "/Times-Roman", "/Times-Bold",
        "/Times-Italic", "/Times-BoldItalic", "/Courier",
        "/Courier-Bold", "/Courier-Oblique", "/Courier-BoldOblique"};
    return names[static_cast<int>(font)];
}

unsigned char winansi_byte(unsigned int codepoint)
{
    struct mapping {
        unsigned int unicode;
        unsigned char byte;
    };
    static mapping const special[] = {
        {0x20ac, 0x80}, {0x201a, 0x82}, {0x0192, 0x83}, {0x201e, 0x84},
        {0x2026, 0x85}, {0x2020, 0x86}, {0x2021, 0x87}, {0x02c6, 0x88},
        {0x2030, 0x89}, {0x0160, 0x8a}, {0x2039, 0x8b}, {0x0152, 0x8c},
        {0x017d, 0x8e}, {0x2018, 0x91}, {0x2019, 0x92}, {0x201c, 0x93},
        {0x201d, 0x94}, {0x2022, 0x95}, {0x2013, 0x96}, {0x2014, 0x97},
        {0x02dc, 0x98}, {0x2122, 0x99}, {0x0161, 0x9a}, {0x203a, 0x9b},
        {0x0153, 0x9c}, {0x017e, 0x9e}, {0x0178, 0x9f}};
    if (codepoint <= 0x7f || (codepoint >= 0xa0 && codepoint <= 0xff))
        return static_cast<unsigned char>(codepoint);
    for (auto const& item: special) {
        if (item.unicode == codepoint)
            return item.byte;
    }
    return '?';
}

std::string to_winansi(char const* utf8)
{
    std::string result;
    auto cursor = reinterpret_cast<unsigned char const*>(utf8);
    while (*cursor != 0) {
        unsigned int codepoint;
        unsigned int count;
        if (*cursor < 0x80) {
            codepoint = *cursor;
            count = 1;
        } else if (*cursor < 0xe0) {
            codepoint = *cursor & 0x1f;
            count = 2;
        } else {
            codepoint = *cursor & 0x0f;
            count = 3;
        }
        for (unsigned int i = 1; i < count; ++i)
            codepoint = (codepoint << 6) | (cursor[i] & 0x3f);
        result.push_back(static_cast<char>(winansi_byte(codepoint)));
        cursor += count;
    }
    return result;
}

double glyph_width(unsigned char glyph, quantapdf_composer_font font)
{
    if (font >= QUANTAPDF_COMPOSER_FONT_COURIER)
        return 600.0;
    if (glyph == ' ' || glyph == '\t')
        return font >= QUANTAPDF_COMPOSER_FONT_TIMES_ROMAN ? 250.0 : 278.0;
    if (glyph >= '0' && glyph <= '9')
        return 500.0;
    if (glyph == 'i' || glyph == 'l' || glyph == 'I')
        return 278.0;
    if (glyph == 'm' || glyph == 'M' || glyph == 'w' || glyph == 'W')
        return 833.0;
    if (glyph < 0x30)
        return 278.0;
    return font >= QUANTAPDF_COMPOSER_FONT_TIMES_ROMAN ? 500.0 : 556.0;
}

double text_width(
    std::string const& text,
    quantapdf_composer_font font,
    double font_size)
{
    double units = 0.0;
    for (unsigned char glyph: text)
        units += glyph_width(glyph, font);
    return units * font_size / 1000.0;
}

std::vector<std::string> layout_lines(
    std::string const& text,
    quantapdf_composer_text_options const& options,
    double width)
{
    std::vector<std::string> lines;
    std::string line;
    std::size_t last_space = std::string::npos;

    auto publish = [&]() {
        while (!line.empty() && line.back() == ' ')
            line.pop_back();
        lines.push_back(line);
        line.clear();
        last_space = std::string::npos;
    };
    for (char value: text) {
        if (value == '\r')
            continue;
        if (value == '\n') {
            publish();
            continue;
        }
        line.push_back(value == '\t' ? ' ' : value);
        if (value == ' ' || value == '\t')
            last_space = line.size() - 1u;
        if (options.wrap && line.size() > 1u &&
            text_width(line, options.font, options.font_size) > width) {
            if (last_space != std::string::npos) {
                std::string remainder = line.substr(last_space + 1u);
                line.resize(last_space);
                publish();
                line = remainder;
            } else {
                char overflow = line.back();
                line.pop_back();
                publish();
                line.push_back(overflow);
            }
        }
    }
    if (!line.empty() || text.empty() || text.back() == '\n')
        publish();
    return lines;
}

std::string pdf_string(std::string const& value)
{
    std::string result = "(";
    char buffer[5];
    for (unsigned char byte: value) {
        if (byte == '(' || byte == ')' || byte == '\\') {
            result.push_back('\\');
            result.push_back(static_cast<char>(byte));
        } else if (byte < 0x20 || byte >= 0x7f) {
            std::snprintf(buffer, sizeof(buffer), "\\%03o", byte);
            result += buffer;
        } else {
            result.push_back(static_cast<char>(byte));
        }
    }
    result.push_back(')');
    return result;
}

QPDFObjectHandle make_font(quantapdf_composer_font font)
{
    auto dictionary = QPDFObjectHandle::newDictionary();
    dictionary.replaceKey("/Type", QPDFObjectHandle::newName("/Font"));
    dictionary.replaceKey("/Subtype", QPDFObjectHandle::newName("/Type1"));
    dictionary.replaceKey(
        "/BaseFont", QPDFObjectHandle::newName(base_font_name(font)));
    dictionary.replaceKey(
        "/Encoding", QPDFObjectHandle::newName("/WinAnsiEncoding"));
    return dictionary;
}

std::string page_content(
    quantapdf_composer const* composer,
    std::size_t page_index)
{
    auto const& page = composer->pages[page_index];
    std::string content;
    double red = ((page.background_argb >> 16u) & 0xffu) / 255.0;
    double green = ((page.background_argb >> 8u) & 0xffu) / 255.0;
    double blue = (page.background_argb & 0xffu) / 255.0;
    content += "q " + number(red) + " " + number(green) + " " +
        number(blue) + " rg 0 0 " + number(page.width_points) + " " +
        number(page.height_points) + " re f Q\n";

    for (std::size_t i = 0; i < composer->operation_count; ++i) {
        auto const& operation = composer->operations[i];
        if (operation.page_index != page_index ||
            operation.kind != QUANTAPDF_COMPOSER_OPERATION_TEXT)
            continue;
        auto const& options = operation.value.text.options;
        auto text = to_winansi(operation.value.text.text_utf8);
        auto lines = layout_lines(
            text, options, operation.bounds.x1 - operation.bounds.x0);
        double line_height = options.font_size * options.line_height_multiplier;
        double y = page.height_points - operation.bounds.y0 - options.font_size;
        red = ((options.argb >> 16u) & 0xffu) / 255.0;
        green = ((options.argb >> 8u) & 0xffu) / 255.0;
        blue = (options.argb & 0xffu) / 255.0;
        for (auto const& line: lines) {
            double width = text_width(line, options.font, options.font_size);
            double x = operation.bounds.x0;
            if (options.alignment == QUANTAPDF_COMPOSER_TEXT_ALIGN_CENTER)
                x += (operation.bounds.x1 - operation.bounds.x0 - width) / 2.0;
            else if (options.alignment == QUANTAPDF_COMPOSER_TEXT_ALIGN_RIGHT)
                x = operation.bounds.x1 - width;
            if (y < page.height_points - operation.bounds.y1)
                break;
            content += "BT /F" + std::to_string(static_cast<int>(options.font)) +
                " " + number(options.font_size) + " Tf " + number(red) +
                " " + number(green) + " " + number(blue) + " rg 1 0 0 1 " +
                number(x) + " " + number(y) + " Tm " + pdf_string(line) +
                " Tj ET\n";
            y -= line_height;
        }
    }
    return content;
}

} // namespace

extern "C" quantapdf_status quantapdf_qpdf_compose(
    quantapdf_composer const* composer,
    unsigned char** out_data,
    size_t* out_size)
{
    if (out_data == nullptr || out_size == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;
    *out_data = nullptr;
    *out_size = 0u;
    if (composer == nullptr)
        return QUANTAPDF_ERROR_ARGUMENT;

    try {
        QPDF pdf;
        pdf.emptyPDF();
        for (std::size_t page_index = 0; page_index < composer->page_count;
             ++page_index) {
            auto page = QPDFObjectHandle::newDictionary();
            auto media_box = QPDFObjectHandle::newArray();
            auto resources = QPDFObjectHandle::newDictionary();
            auto fonts = QPDFObjectHandle::newDictionary();
            bool used_fonts[12] = {};

            for (std::size_t i = 0; i < composer->operation_count; ++i) {
                auto const& operation = composer->operations[i];
                if (operation.page_index == page_index &&
                    operation.kind == QUANTAPDF_COMPOSER_OPERATION_TEXT)
                    used_fonts[static_cast<int>(operation.value.text.options.font)] = true;
            }
            for (int font = 0; font < 12; ++font) {
                if (used_fonts[font])
                    fonts.replaceKey(
                        "/F" + std::to_string(font),
                        make_font(static_cast<quantapdf_composer_font>(font)));
            }
            resources.replaceKey("/Font", fonts);
            media_box.appendItem(QPDFObjectHandle::newInteger(0));
            media_box.appendItem(QPDFObjectHandle::newInteger(0));
            media_box.appendItem(QPDFObjectHandle::newReal(
                composer->pages[page_index].width_points, 4));
            media_box.appendItem(QPDFObjectHandle::newReal(
                composer->pages[page_index].height_points, 4));
            page.replaceKey("/Type", QPDFObjectHandle::newName("/Page"));
            page.replaceKey("/MediaBox", media_box);
            page.replaceKey("/Resources", resources);
            page.replaceKey(
                "/Contents", pdf.newStream(page_content(composer, page_index)));
            pdf.addPage(pdf.makeIndirectObject(page), false);
        }

        QPDFWriter writer(pdf);
        writer.setOutputMemory();
        writer.setStaticID(true);
        writer.setObjectStreamMode(qpdf_o_disable);
        writer.write();
        std::unique_ptr<Buffer> buffer(writer.getBuffer());
        if (buffer->getSize() == 0u)
            return QUANTAPDF_ERROR_BACKEND;
        auto* copied = static_cast<unsigned char*>(std::malloc(buffer->getSize()));
        if (copied == nullptr)
            return QUANTAPDF_ERROR_NOMEM;
        std::memcpy(copied, buffer->getBuffer(), buffer->getSize());
        *out_data = copied;
        *out_size = buffer->getSize();
        return QUANTAPDF_OK;
    } catch (std::bad_alloc const&) {
        return QUANTAPDF_ERROR_NOMEM;
    } catch (...) {
        return QUANTAPDF_ERROR_BACKEND;
    }
}
