/*********************************************************************

   Adafruit_SSD1306 - heavily modified to use the faster i2c_t3 library
   by nox : https://github.com/nox771/i2c_t3

   Added True Type font capability, taken from ILI9341_t3 library
   by Paul Stoffregen: https://github.com/PaulStoffregen/ILI9341_t3

   Added some additional functions.
   Frank Bösing, 11/2018

   Attention:
   - Works with I2C Displays only!
   - SPI removed
   - Works with Teensy 3.x only!

 */
/*********************************************************************/
/* This file for compatibility with the ILI9341_t3 TrueType Fonts    */
/**********************************************************************/

 #ifndef _ILI9341_t3_h
 #define _ILI9341_t3_h

typedef struct {    //FB
	const unsigned char *index;
	const unsigned char *unicode;
	const unsigned char *data;
	unsigned char version;
	unsigned char reserved;
	unsigned char index1_first;
	unsigned char index1_last;
	unsigned char index2_first;
	unsigned char index2_last;
	unsigned char bits_index;
	unsigned char bits_width;
	unsigned char bits_height;
	unsigned char bits_xoffset;
	unsigned char bits_yoffset;
	unsigned char bits_delta;
	unsigned char line_space;
	unsigned char cap_height;
} ILI9341_t3_font_t;

 #endif
