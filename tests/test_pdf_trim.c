#include <extractpdf/extractpdf.h>

#include <stdio.h>
#include <string.h>

int main(void)
{
    extractpdf_document *document = NULL;
    extractpdf_output *output = NULL;
    extractpdf_page_trim trim;

    if (extractpdf_open(TRIM_INTERACTIVE_PDF, NULL, &document) != EXTRACTPDF_OK ||
        document == NULL) {
        fprintf(stderr, "open trim fixture failed\n");
        return 1;
    }

    memset(&trim, 0, sizeof(trim));
    trim.struct_size = sizeof(trim);
    trim.page_index = 0;
    trim.bounds.x0 = 40.0f;
    trim.bounds.y0 = 30.0f;
    trim.bounds.x1 = 360.0f;
    trim.bounds.y1 = 270.0f;

    if (extractpdf_trim_pages(document, &trim, 1, &output) != EXTRACTPDF_OK) {
        fprintf(stderr, "valid trim failed\n");
        extractpdf_close(document);
        return 1;
    }

    if (output == NULL) {
        fprintf(stderr, "valid trim returned null output\n");
        extractpdf_close(document);
        return 1;
    }

    extractpdf_drop_output(output);
    extractpdf_close(document);
    return 0;
}
