# ntscjpng

**Superceded by gamutthingy**

Convert a nominally sRGB png that in reality uses the NTSC-J color gamut to the sRGB color gamut, or vice versa, using the Bradford method.

Principally intended for color correcting texture assets for Final Fantasy 7 & 8.

Usage:  
`ntscjpng mode input.png output.png`  
Mode should be either `ntscj-to-srgb` or `srgb-to-ntscj`.  
Input should be an 8-bit sRGB or sRGBA png file.  
Output will be an 8-bit sRGBA png file.

Use ntscj-to-srgb mode when you have a nominally sRGB png that in reality uses the NTSC-J color gamut and you want it to look correct in FFNx running in sRGB mode.

Use srgb-to-ntscj mode when you have a true sRGB png and you want it to look correct in FFNx running in NTSC-J mode.

Roundtrip conversions are lossless (dithering aside) within the intersection of sRGB and NTSC-J and fairly close within the symmetric difference.

Note: NTSC-J used two slightly different white points for broadcasts and receivers. This program uses the white point for receivers.

PNG plumbing shamelessly borrowed from png2png example by John Cunningham Bowler.  
Quasirandom dithering method devised by Martin Roberts.

To build on Linux:  
install libpng-dev >= 1.6.0  
`gcc -o ntscjpng ntscjpng.c -lpng16 -lz -lm`
