#include <extractpdf/extractpdf.h>

#include <stdint.h>
#include <stdio.h>

static extractpdf_output *output_sentinel(void)
{
    return (extractpdf_output *)(uintptr_t)1;
}

int main(void)
{
    extractpdf_document *document = NULL;
    extractpdf_output *output = output_sentinel();
    extractpdf_page_crop crop;

    if (extractpdf_open(CROP_INTERACTIVE_PDF, NULL, &document) != EXTRACTPDF_OK)
        return 1;

    crop.struct_size = sizeof(crop);
    crop.page_index = 0;
    crop.bounds.x0 = 50.0f;
    crop.bounds.y0 = 40.0f;
    crop.bounds.x1 = 350.0f;
    crop.bounds.y1 = 260.0f;

    if (extractpdf_crop_pages(document, &crop, 1, &output) != EXTRACTPDF_OK ||
        output == NULL) {
        fprintf(stderr, "valid crop failed\n");
        extractpdf_close(document);
        return 1;
    }

    extractpdf_drop_output(output);
    extractpdf_close(document);
    return 0;
}
