# ExtractPDF
Extract image and text from PDF file by mupdf


# Compile #

`$ gcc -c libpdf.c -I../../include`

`$ gcc -shared -o libpdf.dll libpdf.o -L/d/dev/mupdf/build/debug/ -lmupdf -lz -lopenjpeg -ljpeg -ljbig2dec -lfreetype \
-lmupdf-js-none`

# Execute #

`c:\\> gcc -o libpdf.exe libpdf.o -L/d/dev/mupdf/build/debug/ -lmupdf -lz -lopenjpeg -ljpeg -ljbig2dec -lfreetype -lmupdf-js\
-none

* As you can see, this worked both on Windows and linux. and sucessfully called by .Net
