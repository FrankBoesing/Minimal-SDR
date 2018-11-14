/*********************************************************************
   miniSDR v3

   By DD4WH and Frank Bösing

   GPL V3

   - Userinterface -

 **********************************************************************/

#include <Arduino.h>
#include <Bounce2.h>
#include <Encoder.h>
#include <i2c_t3.h>
#include <Adafruit_GFX.h>
#include "src/Adafruit_SSD1306/Adafruit_SSD1306.h"
#include "src/CMSIS_5/arm_math.h"
#include "src/CMSIS_5/arm_const_structs.h"
#include "src/Audio/Audio.h"
#include "stations.h"
#include "UI.h"

#include "src/Adafruit_SSD1306/font_Arial.h"
#include "src/Adafruit_SSD1306/font_digital-7-italic.h"
#include "src/Adafruit_SSD1306/font_digital-7-mono.h"
//#include "src/Adafruit_SSD1306/font_UbuntuMono-Regular.h"
//#include "src/Adafruit_SSD1306/font_UbuntuMono-Bold.h"

//-------------------------------------------------------

#define I2C_SPEED       2000000
#define OLED_I2CADR     0x3C    //Adafruit: 0x3D
#define OLED_RESET      255

// settings for LCD
#define LCD_DISPLAYSTART        18 //Start content at y = 20

//-------------------------------------------------------

extern settings_t settings;
extern int mode;
extern int ANR_on; // off: 0, automatic notch filter:1, automatic noise reduction: 2
extern int AGC_on; // automatic gain control ON/OFF
extern int Spectrum_on;
extern int filter_bandwidth;
extern float freq;

//-------------------------------------------------------
//External functions
void tune(float freq);
void EEPROMsaveSettings(void);

//-------------------------------------------------------

Adafruit_SSD1306 display(OLED_RESET);

Bounce btnCenter = Bounce(BTN_CENTER, BTN_DEBOUNCE);
Bounce btnUp(BTN_UP, BTN_DEBOUNCE);
Bounce btnDown(BTN_DOWN, BTN_DEBOUNCE);
Bounce btnLeft(BTN_LEFT, BTN_DEBOUNCE);
Bounce btnRight(BTN_RIGHT, BTN_DEBOUNCE);

Encoder encoder(ENC_1, ENC_2);
Bounce encCenter(ENC_CENTER, BTN_DEBOUNCE);

uint8_t * buffer;
unsigned bufsize;
int btn = -1;
int encoderPos = 0;
//-------------------------------------------------------
//Forward declarations
void printAt(int x, int y, const char * txt);
void printFreq(void);
//-------------------------------------------------------
//Menu:
//
//Menu Forward Declarations:
void _mM(void);
void _mMode(void);
void _mANR(void);
typedef void (*mf_t)(void);

//Consts
#define MENU_TIMEOUT 6000
const int menuMaxX = 4;
const int menuMaxStrlen = 20;
//Menu - Main
const char menu[][menuMaxX][menuMaxStrlen] = {
	{"Memory", "Mode", "Noise Reduction",   "Settings"     },
	{"",       "SAM",  "Off",               "Filter Bandw."},
	{"",       "AM",   "Notch",             "# of Taps"    },
	{"",       "LSB",  "Noise",             ""             },
	{"",       "USB",  "",                  ""             }
};
const int menuMaxY = sizeof(menu) / menuMaxX / menuMaxStrlen;

//Menu - Main Functions
const mf_t menufunc[menuMaxY * menuMaxX] = {
	_mM,       _mM,    _mM,                _mM,
	NULL,      _mMode, _mANR,              NULL,
	NULL,      _mMode, _mANR,              NULL,
	NULL,      _mMode, _mANR,              NULL,
	NULL,      _mMode, NULL,               NULL,
};

//-------------------------------------------------------
int spectrum_on_old = Spectrum_on;
int spectrumCounter = 0;
int menuActive = 1; // -1 = Menu is inactive
int menuOld    = -99;

void menuExit(void) {
	display.fillRect(0, 0, SSD1306_LCDWIDTH, SSD1306_LCDHEIGHT, BLACK);
	Spectrum_on = spectrum_on_old;
	spectrumCounter = 0;
	Serial.print("Spektrum:");
	Serial.print(Spectrum_on);
	menuActive = menuOld = -1;
	showFreq();             //showFreq is the default screen
}

void showMenu(void) {

	static unsigned long lastChange = millis();
	if (menuActive < 0) return;

	//Timeout
	unsigned long t = millis();
	if (t - lastChange > MENU_TIMEOUT && menuOld != -1) {
		menuExit();
		return;
	}
	if (menuActive == menuOld) return;
	lastChange = t;

	const int lines = 3;

	int i, w, h, l;
	int y = LCD_DISPLAYSTART;
	int x = 0;
	int menuY = menuActive / menuMaxX;
	int menuX = menuActive - menuY * menuMaxX;

	if (menuY == 0) {       //Main Menu
		display.fillRect(0, y, SSD1306_LCDWIDTH, SSD1306_LCDHEIGHT - y, BLACK);
		y += 2;
		display.setFont(Arial_12); h = 15;
		i = 0;
		l = 0;
		if (menuX > lines - 1) i = menuX - (lines - 1);
		x = 8;
		do {
			if (i == menuX) {
				display.fillRect(0, y - 2, SSD1306_LCDWIDTH, h + 2, WHITE);
				display.setTextColor(BLACK);
				printAt(0, y, ">");
			} else {
				display.setTextColor(WHITE);
			}
			printAt(x, y, menu[menuY][i]);
			i++; l++;
			y += h;
		} while (i < menuMaxX && l < lines);
	}

	else
	if (menufunc[menuActive] != NULL) {       //submenu
		//Title
		display.setFont(Arial_16);
		display.fillRect(0, 0, SSD1306_LCDWIDTH, SSD1306_LCDHEIGHT, BLACK);
		printAt(4, 0, menu[0][menuX]);
		display.print(":");

		y += 2;
		display.setFont(Arial_12); h = 15;
		i = 1;
		l = 0;
		if (menuY > lines - 1) i = menuY - (lines - 1);
		x = 8;
		do {
			if (i == menuY) {
				display.fillRect(0, y - 2, SSD1306_LCDWIDTH, h + 2, WHITE);
				display.setTextColor(BLACK);
				printAt(0, y, ">");
			} else {
				display.setTextColor(WHITE);
			}
			printAt(x, y, menu[i][menuX]);
			i++;
			l++;
			y += h;
		} while (i < menuMaxY && l < lines);
	}

	display.display();
	menuOld = menuActive;
}
//-------------------------------------------------------

void _mM(void) {
	//MainMenu - Navigation
	int menuY = menuActive / menuMaxX;
	int menuX = menuActive - menuY * menuMaxX;

	if (btn == BTN_UP && menuX > 0) {
		menuActive--;
	}
	else
	if (btn == BTN_DOWN && menuX < menuMaxX - 1) {
		menuActive++;
	}
	else
	if (btn == BTN_CENTER) {
		//Enter Submenu
		int t = menuActive + menuMaxX;
		if (menufunc[t] != NULL) {
			menuActive = t;
			Spectrum_on = 0;
		}
	}
}
//-------------------------------------------------------
int _MSubNav(void) {
	//SubMenu - Navigation, Set Integer
	int menuY = menuActive / menuMaxX;
	int menuX = menuActive - menuY * menuMaxX;

	if (btn == BTN_UP && menuY > 1) {
		menuActive -= menuMaxX;
	}
	else
	if (btn == BTN_DOWN
	    && menuY < menuMaxY - 1
	    && menufunc[menuActive + menuMaxX] != NULL) {
		menuActive += menuMaxX;
	}
	else
	if (btn == BTN_CENTER) {
		//Exit menu, return selected submenu
		menuExit();
		return menuY - 1;
	}
	return -1;
}
void _mInteger(int * val) {
	int v;
	v = _MSubNav();
	if (v > -1) *val = v;
}
void _mMode(void) {
	_mInteger(&mode);
}
void _mANR(void) {
	_mInteger(&ANR_on);
}

//-------------------------------------------------------
void serialUI(void) {
	if (!Serial.available()) return;

	static int input = 0;
	char ch = Serial.read();

	if (ch >= 'a' && ch <= 'z') {
		int i = ch - 'a';
		if (settings.station[i].freq != 0.0) {
			settings.lastStation = i;
			settings.lastFreq = 0;
			freq = settings.station[i].freq;
			mode = settings.station[i].mode;
			ANR_on = settings.station[i].notch;
			Serial.printf("%c: %8d\t%s\t%s\n", 'a' + i, (int) settings.station[i].freq, modestr[settings.station[i].mode], settings.station[i].sname);
			tune(freq);
		} else {
			Serial.println("Empty.");
		}
	}
	else if (ch == 'L') {
		mode = LSB;
		settings.lastMode = mode;
		tune(freq);
	}
	else if (ch == 'U') {
		mode = USB;
		settings.lastMode = mode;
		tune(freq);
	}
	else if (ch == 'A') {
		mode = AM;
		settings.lastMode = mode;
		tune(freq);
	}
#if defined(__MK66FX1M0__)
	else if (ch == 'S') {
		mode = SYNCAM;
		settings.lastMode = mode;
		tune(freq);
	}
	else if (ch == 'N') {
		if (ANR_on == 2)
		{
			ANR_on = 0;
			Serial.println("Auto-Notch OFF");
		}
		else if (ANR_on == 0)
		{
			ANR_on = 1;
			Serial.println("Auto-Notch ON");
		}
		else if (ANR_on == 1)
		{
			ANR_on = 2;
			Serial.println("Auto-Noise ON");
		}
		tune(freq);
	}
#endif
	else if (ch == 'G') {
		if (AGC_on)
		{
			AGC_on = 0;
			Serial.println("AGC OFF");
		}
		else
		{
			AGC_on = 1;
			Serial.println("AGC ON");
		}
		showFreq();
	}
	else if (ch == '!') {
		EEPROMsaveSettings();
		Serial.println("Settings saved");
	}
	else if (ch >= '0' && ch <= '9') {
		input = input * 10 + (ch - '0');
	} else if (ch == '\r') {
		if (input > 0) {
			if (input < 1000) input *= 1000;
			freq = input;
			settings.lastFreq = input;
			input = 0;
			tune(freq);
		}
	}
}
//-------------------------------------------------------
static arm_rfft_instance_q15 FFT;

void initSpectrum(void) {
	arm_rfft_init_q15 (&FFT, 128, 0, 1);
}

void showSpectrum(int16_t * data)
{
#define SPECTRUM_DELETE_COLOUR BLACK
#define SPECTRUM_DRAW_COLOUR WHITE

	if (!Spectrum_on) return;

	static const int spectrum_height = 16;
	static const int spectrum_y = 0;
	static const int spectrum_x = 0;

	// FFT with 128 points
	int16_t FFT_out [256];
	int16_t FFT_in [128];

	int16_t pixelnew[128];
	int y_new, y1_new;
	int y1_new_minus = 0;

	spectrumCounter++;
	if (spectrumCounter == 25)
	{
		spectrumCounter = 0;
		memcpy(FFT_in, data, sizeof(FFT_in));
		arm_rfft_q15(&FFT, FFT_in, FFT_out);

		display.fillRect(0, spectrum_y, display.width(), spectrum_y + spectrum_height, SPECTRUM_DELETE_COLOUR);

		for (int16_t x = 0; x < 127; x++)
		{
			pixelnew[x] = abs(FFT_out[127 - x]) / 200;

			if ((x > 1) && (x < 127))
			// moving window - weighted average of 5 points of the spectrum to smooth spectrum in the frequency domain
			// weights:  x: 50% , x-1/x+1: 36%, x+2/x-2: 14%
			{
				if (0)
				{
					y_new = pixelnew[x] * 0.5 + pixelnew[x - 1] * 0.18 + pixelnew[x + 1] * 0.18 + pixelnew[x - 2] * 0.07 + pixelnew[x + 2] * 0.07;
				}
				else
				{
					y_new = pixelnew[x];
				}
			}
			else
			{
				y_new = pixelnew[x];
			}


			if (y_new > (spectrum_height))
			{
				y_new = (spectrum_height);
			}
			y1_new  = (spectrum_y + spectrum_height - 1) - y_new;

			if (x == 0)
			{
				y1_new_minus = y1_new;
			}
			if (x == 127)
			{
				y1_new_minus = y1_new;
			}

			{

				// DRAW NEW LINE/POINT
				if (y1_new - y1_new_minus > 1)
				{                         // plot line upwards
					display.drawFastVLine(x + spectrum_x, y1_new_minus + 1, y1_new - y1_new_minus, SPECTRUM_DRAW_COLOUR);
				}
				else if (y1_new - y1_new_minus < -1)
				{                         // plot line downwards
					display.drawFastVLine(x + spectrum_x, y1_new, y1_new_minus - y1_new, SPECTRUM_DRAW_COLOUR);
				}
				else
				{
					display.drawPixel(x + spectrum_x, y1_new, SPECTRUM_DRAW_COLOUR);                               // write new pixel
				}

				y1_new_minus = y1_new;

			}
		}             // end for loop
		display.display(spectrum_height);
	}
}

//-------------------------------------------------------

void printAt(int x, int y, const char * txt) {
	display.setCursor(x, y);
	display.print(txt);
}

//-------------------------------------------------------
void printFreq(void) {
	float f = freq / 1000;
	int n = 1;
	if (f < 100.0) n = 4;
	else if (f < 1000.0) n = 3;
	else if (f < 10000.0) n = 2;
	display.print(f, n);
}
//-------------------------------------------------------
void showFreq(void) {

	int y = LCD_DISPLAYSTART;
	display.setTextColor(WHITE);
	display.fillRect(0, y, SSD1306_LCDWIDTH, SSD1306_LCDHEIGHT - y, BLACK);
	int x = 0;
	int size = 26;

	//show frequency
	//display.setFont(Digital7_24_Italic);
	display.setFont(Digital7_24);
	display.setCursor(x, y);
	printFreq();

	display.setFont(Arial_8);
	x = 110;
	printAt(x, y + 1, "kHz");
	printAt(x, y + 14, modestr[mode]);       //Display Mode

	y += size;
	size = 10;

	if (AGC_on) printAt(4 * 8, y, "AGC");       //AGC

	if (ANR_on == 1) printAt(8 * 8, y, "Notch");
	else if (ANR_on == 2) printAt(8 * 8, y, "Noise");

	y += size;

	if (settings.lastFreq > 0) {
		//printAt(0, y, "Manual Setting");
	} else {
		printAt(0, y, settings.station[settings.lastStation].sname);
	}

	display.display();

}
//-------------------------------------------------------

void initUI(void) {
#if 0
	pinMode(BTN_CENTER, INPUT_PULLUP);
	pinMode(BTN_UP, INPUT_PULLUP);
	pinMode(BTN_DOWN, INPUT_PULLUP);
	pinMode(BTN_LEFT, INPUT_PULLUP);
	pinMode(BTN_RIGHT, INPUT_PULLUP);
	pinMode(ENC_CENTER, INPUT_PULLUP);
#else
	pinMode(BTN_CENTER, INPUT);
	pinMode(BTN_UP, INPUT);
	pinMode(BTN_DOWN, INPUT);
	pinMode(BTN_LEFT, INPUT);
	pinMode(BTN_RIGHT, INPUT);
	pinMode(ENC_CENTER, INPUT);
	pinMode(5, OUTPUT);
	digitalWriteFast(5, HIGH);       //3V
	pinMode(12, OUTPUT);
	digitalWriteFast(12, LOW);       //3GND
#endif
	display.begin(SSD1306_SWITCHCAPVCC, OLED_I2CADR);
	Wire.setClock(I2C_SPEED);
	display.clearDisplay();
	display.setTextColor(WHITE);

	display.setFont(Arial_16);
	printAt(0, 0, "Minimal SDR");

	display.display();
	initSpectrum();

	buffer = display.getBufAddr();
	bufsize = display.getBufSize();

	spectrumCounter = -500;
}

//-------------------------------------------------------
int encoderPosOld;
void buttons(void) {

	btnUp.update();
	btnDown.update();
	btnLeft.update();
	btnRight.update();
	btnCenter.update();
	encCenter.update();

	encoderPosOld = encoderPos;
	encoderPos = encoder.read() >> 1;

	//Read buttons:
	if (encCenter.fallingEdge()) btn = BTN_CENTER;
	else if (encoderPos < encoderPosOld) btn = BTN_UP;
	else if (encoderPos > encoderPosOld) btn = BTN_DOWN;
	else if (btnCenter.fallingEdge()) btn = BTN_CENTER;
	else if (btnUp.fallingEdge()) btn = BTN_UP;
	else if (btnDown.fallingEdge()) btn = BTN_DOWN;
	else if (btnLeft.fallingEdge()) btn = BTN_LEFT;
	else if (btnRight.fallingEdge()) btn = BTN_RIGHT;
	else btn = -1;

	if (menuActive < 0 && btn == BTN_CENTER) {
		//Enter Menu
		menuActive = 0;
		spectrum_on_old = Spectrum_on;
	}
	else
	if (btn >= 0  && menuActive >= 0 && menufunc[menuActive] != NULL) {
		//Process Menufunction
		mf_t func;
		func = menufunc[menuActive];
		func();
	}

}

//-------------------------------------------------------
void UI(void) {
	serialUI();
	buttons();
	showMenu();
}
//-------------------------------------------------------

#if (SSD1306_LCDHEIGHT != 64)
#error ("Height incorrect, please fix Adafruit_SSD1306.h!");
#endif
