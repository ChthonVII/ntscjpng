# ntscjpng
Convert a nominally sRGB png that in reality uses the NTSC-J color gamut to the sRGB color gamut using the Bradford method.

Principally intended for color correcting texture assets for Final Fantasy 7 & 8.

Usage:  
`ntscjpng input.png output.png`  
Input should be an 8-bit sRGB or sRGBA png file.  
Output will be an 8-bit sRGBA png file.

PNG plumbing shamelessly borrowed from png2png example by John Cunningham Bowler.

To build on Linux:  
`gcc -o pngtopng pngtopng.c -lpng16 -lz -lm`
