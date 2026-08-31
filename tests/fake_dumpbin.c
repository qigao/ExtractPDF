#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    char symbol[256];
    unsigned int ordinal = 1;
    FILE *baseline;

    if (argc < 2)
        return 2;
    baseline = fopen(argv[argc - 1], "rb");
    if (baseline == NULL)
        return 3;

    puts("  86 number of functions");
    puts("  85 number of names");
    puts("    ordinal hint RVA      name");
    while (fgets(symbol, sizeof(symbol), baseline) != NULL) {
        size_t length = strcspn(symbol, "\r\n");
        symbol[length] = '\0';
        if (length == 0)
            continue;
        printf("%11u %4X %08X %s\n", ordinal, ordinal - 1,
               0x1000u + ordinal * 16u, symbol);
        ++ordinal;
    }
    fclose(baseline);
    printf("%11u %4X %08X accidental_export\n", ordinal, ordinal - 1,
           0x1000u + ordinal * 16u);
    ++ordinal;
    printf("%11u %4X %08X [NONAME]\n", ordinal, ordinal - 1,
           0x1000u + ordinal * 16u);
    return 0;
}
