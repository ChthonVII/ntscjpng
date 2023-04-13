/*- ntscjpng
 *
 * COPYRIGHT: 2023 by Chris Bussard
 * LICENSE: GPLv3
 *
 * Convert a nominally sRGB png that in reality uses the NTSC-J color gamut to the sRGB color gamut using the Bradford method.
 * Principally intended for color correcting texture assets for Final Fantasy 7 & 8.
 * Usage:
 * ntscjpng input.png output.png
 * where input.png uses 8-bit sRGB or sRGBA and output.png will be 8-bit sRGBA.
 * 
 * png plumbing shamelessly borrowed from png2png example by John Cunningham Bowler (copyright waived).
 * quasirandom dithering method devised by Martin Roberts.
 * 
 * To build on Linux:
 * install libpng-dev >= 1.6.0
 * gcc -o ntscjpng ntscjpng.c -lpng16 -lz -lm
 * 
 */

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <math.h>

/* Normally use <png.h> here to get the installed libpng, but this is done to
 * ensure the code picks up the local libpng implementation:
 */
//#include "../../png.h"
#include <png.h> // Linux should have libpng-dev installed; Windows users can figure stuff out.
#if defined(PNG_SIMPLIFIED_READ_SUPPORTED) && \
    defined(PNG_SIMPLIFIED_WRITE_SUPPORTED)

// precomputed NTSC-J to SRGB color gamut conversion using Bradford Method
// Note:
// NTSC-J television sets had a whitepoint of 9300K+27mpcd (x=0.281, y=0.311)
// NTSC-J broadcasts had a whitepoint of 9300K+8mpcd (x=0.2838 y=0.2981)
// And neither of those is quite the same as CIE 9300K (x=0.2848 y=0.2932 or x=0.28315, y=0.29711, depending on which source you consult; discrepancy might relate to rivision of Planck's Law constants???)
// This matrix uses 9300K+27mpcd for NTSC-J white point and x=0.312713, y=0.329016 for D65 white point

const float NTSCJtoSRGBConversionMatrix[3][3] = {
    {1.34756301456925, -0.276463760747096, -0.071099263267176},
    {-0.031150036968175, 0.956512223260545, 0.074637860817515},
    {-0.024443490594835, -0.048150182045316, 1.07259361295816}
};

const float SRGBtoNTSCJConversionMatrix[3][3] = {
    {0.747740261849856, 0.217853505133354, 0.034406264690912},
    {0.022941129531242, 1.04849963723505, -0.071440739296512},
    {0.018070185951324, 0.052033179887888, 0.929896593506351}
};


// clamp a float between 0.0 and 1.0
float clampfloat(float input){
    if (input < 0.0) return 0.0;
    if (input > 1.0) return 1.0;
    return input;
}

// convert a 0-1 float value to 0-255 png_byte value with Martin Roberts' quasirandom dithering
// see: https://extremelearning.com.au/unreasonable-effectiveness-of-quasirandom-sequences/
// Aside from being just beautifully elegant, this dithering method is also perfect for our use case,
// in which our input might be might be a "swizzled" texture.
// These pose a problem for many other dithering methods.
// Any kind of error diffusion will diffuse error across swizzled tile boundaries.
// Even plain old ordered dithering is locally wrong where swizzled tile boundaries
// often don't line up with the Bayer matrix boundaries.
// But quasirandom doesn't care where you cut and splice it; it's still balanced.
// (Blue noise would work too, but that's a huge amount of overhead, while this is a very short function.)
png_byte quasirandomdither(float input, int x, int y){
    x++; // avoid x=0
    y++; // avoid y=0
    double dummy;
    float dither = modf(((float)x * 0.7548776662) + ((float)y * 0.56984029), &dummy);
    if (dither < 0.5){
        dither = 2.0 * dither;
    }
    else if (dither > 0.5) {
        dither = 2.0 - (2.0 * dither);
    }
    // if we ever get exactly 0.5, don't touch it; otherwise we might end up adding 1.0 to a black that means transparency.
    int output = (int)((input * 255.0) + dither);
    if (output > 255) output = 255;
    if (output < 0) output = 0;
    return (png_byte)output;
}

// sRGB gamma functions
float togamma(float input){
    if (input <= 0.0031308){
        return clampfloat(input * 12.92);
    }
    return clampfloat((1.055 * pow(input, (1.0/2.4))) - 0.055);
}
float tolinear(float input){
    if (input <= 0.04045){
        return clampfloat(input / 12.92);
    }
    return clampfloat(pow((input + 0.055) / 1.055, 2.4));
}

int main(int argc, const char **argv){
   
   int result = 1;

   int mode = 0;
   if (argc == 4){
      if (strcmp(argv[1], "ntscj-to-srgb") == 0){
        mode = 1;
      }
      else if (strcmp(argv[1], "srgb-to-ntscj") == 0){
        mode = 2;
      }
   }
   if (mode > 0){
      
      if (mode == 1){
        printf("ntscjpng: converting %s from NTSC-J color gamut to sRGB color gamut and saving output to %s... ", argv[2], argv[3]);
      }
      else {
          printf("ntscjpng: converting %s from sRGB color gamut to NTSC-J color gamut and saving output to %s... ", argv[2], argv[3]);
      }
      png_image image;

      /* Only the image structure version number needs to be set. */
      memset(&image, 0, sizeof image);
      image.version = PNG_IMAGE_VERSION;

      if (png_image_begin_read_from_file(&image, argv[2])){
         png_bytep buffer;

         /* Change this to try different formats!  If you set a colormap format
          * then you must also supply a colormap below.
          */
         image.format = PNG_FORMAT_RGBA;

         buffer = malloc(PNG_IMAGE_SIZE(image));

         if (buffer != NULL){
            if (png_image_finish_read(&image, NULL/*background*/, buffer, 0/*row_stride*/, NULL/*colormap for PNG_FORMAT_FLAG_COLORMAP */)){
                
                // ------------------------------------------------------------------------------------------------------------------------------------------
                // Begin actual color conversion code
                                
                int width = image.width;
                int height = image.height;
                for (int y=0; y<height; y++){
                    for (int x=0; x<width; x++){
                        
                        // read out from buffer and convert to float
                        float redvalue = buffer[ ((y * width) + x) * 4]/255.0;
                        float greenvalue = buffer[ (((y * width) + x) * 4) + 1 ]/255.0;
                        float bluevalue = buffer[ (((y * width) + x) * 4) + 2 ]/255.0;
                        // don't touch alpha value
                        
                        // to linear RGB
                        redvalue = tolinear(redvalue);
                        greenvalue = tolinear(greenvalue);
                        bluevalue = tolinear(bluevalue);
                        // The FF7 videos had banding near black when decoded with any piecewise "toe slope" gamma function, suggesting that a pure curve function was needed. May need to try this if such banding appears.
                        //redvalue = clampfloat(pow(redvalue, 2.2));
                        //greenvalue = clampfloat(pow(greenvalue, 2.2));
                        //bluevalue = clampfloat(pow(bluevalue, 2.2));
                        
                        // Multiply by one of our pre-computed gamut conversion Bradford matrices
                        float newred, newgreen, newblue;
                        if (mode == 1){
                            newred = NTSCJtoSRGBConversionMatrix[0][0] * redvalue + NTSCJtoSRGBConversionMatrix[0][1] * greenvalue + NTSCJtoSRGBConversionMatrix[0][2] * bluevalue;
                            newgreen = NTSCJtoSRGBConversionMatrix[1][0] * redvalue + NTSCJtoSRGBConversionMatrix[1][1] * greenvalue + NTSCJtoSRGBConversionMatrix[1][2] * bluevalue;
                            newblue = NTSCJtoSRGBConversionMatrix[2][0] * redvalue + NTSCJtoSRGBConversionMatrix[2][1] * greenvalue + NTSCJtoSRGBConversionMatrix[2][2] * bluevalue;
                        }
                        else {
                            newred = SRGBtoNTSCJConversionMatrix[0][0] * redvalue + SRGBtoNTSCJConversionMatrix[0][1] * greenvalue + SRGBtoNTSCJConversionMatrix[0][2] * bluevalue;
                            newgreen = SRGBtoNTSCJConversionMatrix[1][0] * redvalue + SRGBtoNTSCJConversionMatrix[1][1] * greenvalue + SRGBtoNTSCJConversionMatrix[1][2] * bluevalue;
                            newblue = SRGBtoNTSCJConversionMatrix[2][0] * redvalue + SRGBtoNTSCJConversionMatrix[2][1] * greenvalue + SRGBtoNTSCJConversionMatrix[2][2] * bluevalue;
                        }
                        
                        // clamp values to 0 to 1 range
                        newred = clampfloat(newred);
                        newgreen = clampfloat(newgreen);
                        newblue = clampfloat(newblue);
                                                
                        // back to sRGB
                        newred = togamma(newred);
                        newgreen = togamma(newgreen);
                        newblue = togamma(newblue);
                                                
                        // convert back to 0-255 with quasirandom dithering, and save back to buffer
                        // use inverse x coord for red and inverse y coord for blue to decouple dither patterns across channels
                        // see https://blog.kaetemi.be/2015/04/01/practical-bayer-dithering/
                        buffer[ ((y * width) + x) * 4] = quasirandomdither(newred, width - x - 1, y);
                        buffer[ (((y * width) + x) * 4) + 1 ] = quasirandomdither(newgreen, x, y);
                        buffer[ (((y * width) + x) * 4) + 2 ] = quasirandomdither(newblue, x, height - y - 1);
                                                
                    }
                }
                
                // End actual color conversion code
                // ------------------------------------------------------------------------------------------------------------------------------------------
                
                
               if (png_image_write_to_file(&image, argv[3], 0/*convert_to_8bit*/, buffer, 0/*row_stride*/, NULL/*colormap*/)){
                  result = 0;
                  printf("done.\n");
               }

               else {
                  fprintf(stderr, "ntscjpng: write %s: %s\n", argv[3], image.message);
               }
            }

            else {
               fprintf(stderr, "ntscjpng: read %s: %s\n", argv[2], image.message);
            }

            free(buffer);
            buffer = NULL;
         }

         else {
            fprintf(stderr, "ntscjpng: out of memory: %lu bytes\n", (unsigned long)PNG_IMAGE_SIZE(image));

            /* This is the only place where a 'free' is required; libpng does
             * the cleanup on error and success, but in this case we couldn't
             * complete the read because of running out of memory and so libpng
             * has not got to the point where it can do cleanup.
             */
            png_image_free(&image);
         }
      }

      else {
         /* Failed to read the input file argument: */
         fprintf(stderr, "ntscjpng: %s: %s\n", argv[2], image.message);
      }
   }

   else {
      /* Wrong number of arguments */
      fprintf(stderr, "ntscjpng: usage: ntscjpng mode input-file output-file, where mode is \"ntscj-to-srgb\" or \"srgb-to-ntscj\" \n");
   }

   return result;
}
#endif /* READ && WRITE */
