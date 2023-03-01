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
 * png plumbing shamelessly borrowed from png2png example by John Cunningham Bowler (copyright waived)
 * 
 * To build on Linux:
 * gcc -o pngtopng pngtopng.c -lpng16 -lz -lm
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
const float ConversionMatrix[3][3] = {
    {1.42849423843304, -0.343794575385404, -0.084699613295359},
    {-0.028230868456879, 0.937886666562635, 0.09034421347425},
    {-0.026451048534459, -0.04977408617468, 1.07622507193376}
};

// clamp a float between 0.0 and 1.0
float clampfloat(float input){
    if (input < 0.0) return 0.0;
    if (input > 1.0) return 1.0;
    return input;
}

// convert a 0-1 float value to 0-255 png_byte value and put the rounding error (as 0 to 1 float) into "error"
png_byte roundandexporterror(float input, float* error){
    int output = (int)((input * 255.0) + 0.5);
    if (output > 255) output = 255;
    if (output < 0) output = 0;
    *error = ((input * 255.0) - (float)output)/255.0;
    return (png_byte)output;
}

// SRGB gamma functions
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
   printf("ntscjpng: converting %s from NTSC-J color gamut to sRGB color gamut and saving output to %s... ", argv[1], argv[2]);
   int result = 1;

   if (argc == 3){
      png_image image;

      /* Only the image structure version number needs to be set. */
      memset(&image, 0, sizeof image);
      image.version = PNG_IMAGE_VERSION;

      if (png_image_begin_read_from_file(&image, argv[1])){
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
                
                float* workbuffer = malloc(image.width * image.height * 3 * sizeof(float));
                
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
                        
                        // Multiply by our pre-computed NTSC-J to sRGB Bradford matrix
                        float newred = ConversionMatrix[0][0] * redvalue + ConversionMatrix[0][1] * greenvalue + ConversionMatrix[0][2] * bluevalue;
                        float newgreen = ConversionMatrix[1][0] * redvalue + ConversionMatrix[1][1] * greenvalue + ConversionMatrix[1][2] * bluevalue;
                        float newblue = ConversionMatrix[2][0] * redvalue + ConversionMatrix[2][1] * greenvalue + ConversionMatrix[2][2] * bluevalue;
                        
                        // clamp values to 0 to 1 range
                        newred = clampfloat(newred);
                        newgreen = clampfloat(newgreen);
                        newblue = clampfloat(newblue);
                                                
                        // back to SRGB
                        newred = togamma(newred);
                        newgreen = togamma(newgreen);
                        newblue = togamma(newblue);
                                                
                        // save to work buffer
                        workbuffer[ ((y * width) + x) * 3 ] = newred;
                        workbuffer[ (((y * width) + x) * 3) + 1 ] = newgreen;
                        workbuffer[ (((y * width) + x) * 3) + 2 ] = newblue;
                    }
                }

                // write back to the buffer using Floyd–Steinberg dithering
                // if applied to swizzled images, this will be slightly wrong (accumulating errors across swizzle boundaries), but still probably good enough
                for (int y=0; y<height; y++){
                    int ynext = y+1;
                    bool ynext_inbounds = (ynext < height);
                    for (int x=0; x<width; x++){
                        int xnext = x+1;
                        int xlast = x-1;
                        bool xnext_inbounds = (xnext < width);
                        bool xlast_inbounds = (xlast >= 0);
                        
                        float redvalue = workbuffer[ ((y * width) + x) * 3 ];
                        float greenvalue = workbuffer[ (((y * width) + x) * 3) + 1 ];
                        float bluevalue = workbuffer[ (((y * width) + x) * 3) + 2 ];
                        
                        float rederror;
                        float greenerror;
                        float blueerror;
                        png_byte newred = roundandexporterror(redvalue, &rederror);
                        png_byte newblue = roundandexporterror(bluevalue, &blueerror);
                        png_byte newgreen = roundandexporterror(greenvalue, &greenerror);
                        
                        buffer[ ((y * width) + x) * 4] = newred;
                        buffer[ (((y * width) + x) * 4) + 1 ] = newgreen;
                        buffer[ (((y * width) + x) * 4) + 2 ] = newblue;
                        
                        if (xnext_inbounds){
                            workbuffer[ ((y * width) + xnext) * 3 ] += ((rederror * 7.0) / 16.0);
                            workbuffer[ (((y * width) + xnext) * 3) + 1 ] += ((greenerror * 7.0) / 16.0);
                            workbuffer[ (((y * width) + xnext) * 3) + 2 ] += ((blueerror * 7.0) / 16.0);
                        }
                        if (ynext_inbounds){
                            if (xlast_inbounds){
                                workbuffer[ ((ynext * width) + xlast) * 3 ] += ((rederror * 3.0) / 16.0);
                                workbuffer[ (((ynext * width) + xlast) * 3) + 1 ] += ((greenerror * 3.0) / 16.0);
                                workbuffer[ (((ynext * width) + xlast) * 3) + 2 ] += ((blueerror * 3.0) / 16.0);
                            }
                            workbuffer[ ((ynext * width) + x) * 3 ] += ((rederror * 5.0) / 16.0);
                            workbuffer[ (((ynext * width) + x) * 3) + 1 ] += ((greenerror * 5.0) / 16.0);
                            workbuffer[ (((ynext * width) + x) * 3) + 2 ] += ((blueerror * 5.0) / 16.0);
                            if (xnext_inbounds){
                                workbuffer[ ((ynext * width) + xnext) * 3 ] += (rederror / 16.0);
                                workbuffer[ (((ynext * width) + xnext) * 3) + 1 ] += (greenerror / 16.0);
                                workbuffer[ (((ynext * width) + xnext) * 3) + 2 ] += (blueerror / 16.0);
                            }
                        }
                        
                    }
                }

                
                free(workbuffer);
                workbuffer = NULL;
                
                // End actual color conversion code
                // ------------------------------------------------------------------------------------------------------------------------------------------
                
                
               if (png_image_write_to_file(&image, argv[2], 0/*convert_to_8bit*/, buffer, 0/*row_stride*/, NULL/*colormap*/)){
                  result = 0;
                  printf("done.\n");
               }

               else {
                  fprintf(stderr, "ntscjpng: write %s: %s\n", argv[2], image.message);
               }
            }

            else {
               fprintf(stderr, "ntscjpng: read %s: %s\n", argv[1], image.message);
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
         /* Failed to read the first argument: */
         fprintf(stderr, "ntscjpng: %s: %s\n", argv[1], image.message);
      }
   }

   else {
      /* Wrong number of arguments */
      fprintf(stderr, "ntscjpng: usage: pngtopng input-file output-file\n");
   }

   return result;
}
#endif /* READ && WRITE */
