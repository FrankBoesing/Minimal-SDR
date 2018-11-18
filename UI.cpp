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
#include "src/Adafruit_SSD1306_i2ct3/Adafruit_SSD1306_i2ct3.h"
#include "src/CMSIS_5/arm_math.h"
#include "src/CMSIS_5/arm_const_structs.h"
#include "src/Audio/Audio.h"
#include "stations.h"
#include "UI.h"

#include "src/Adafruit_SSD1306_i2ct3/font_Arial.h"
//#include "src/Adafruit_SSD1306_i2ct3/font_digital-7-italic.h"
#include "src/Adafruit_SSD1306_i2ct3/font_digital-7-mono.h"
//#include "src/Adafruit_SSD1306/font_UbuntuMono-Regular.h"
//#include "src/Adafruit_SSD1306/font_UbuntuMono-Bold.h"

//-------------------------------------------------------

#define I2C_SPEED       2000000
#define OLED_I2CADR     0x3C    //Adafruit: 0x3D
#define OLED_RESET      255

// settings for LCD
#define LCD_DISPLAYSTART        18 //Start content at y = 20

//-------------------------------------------------------
extern int filter_bandwidth_default;
extern settings_t settings;
extern int mode;
extern int ANR_on; // off: 0, automatic notch filter:1, automatic noise reduction: 2
extern int AGC_on; // automatic gain control ON/OFF
extern int Spectrum_on;
extern int filter_bandwidth;
extern int freq;

//-------------------------------------------------------
//External functions
void tune(void);
void EEPROMsaveSettings(void);
void calc_demod_filter(void);

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
int freqStep; //-1 = eeprom-memory
//-------------------------------------------------------
//Forward declarations
void printAt(int x, int y, const char * txt);
void printCentered(int x, int y, int maxwidth, const char * txt);
void printFreq(void);
boolean tuneStation(int i);
//-------------------------------------------------------
//Menu:
//
//Menu Forward Declarations:
void buttons();
int _mTune(void);
int _mM(void);
int _mMode(void);
int _mANR(void);
int _mBandwAM(void);
int _mSave(void);
typedef int (*mf_t)(void);

//Consts
#define MENU_TIMEOUT 6000 //ms
//const int menuMaxX = 6;
const int menuMaxX = 5;
const int menuMaxStrlen = 20;
//Menu - Main
const char menu[][menuMaxX][menuMaxStrlen] = {
	{"Tuning",       "Mode", "Noise Reduction",  "Filter AM",   /*  "Filter SSB", */ "Settings"},
	{"Memory",       "SAM",  "Off",              "Bandwidth",   /*  "Bandwidth",  */ "Save now"},
	{"Step 1 kHz",   "AM",   "Notch",            "# of Taps",   /*  "# of Taps",  */ ""},
	{"Step 0.1 kHz", "LSB",  "Noise",            "",            /*  "",           */ ""},
	{"",             "USB",  "",                 "",            /*  "",           */ ""}
};
const int menuMaxY = sizeof(menu) / menuMaxX / menuMaxStrlen;

//Menu - Main Functions
const mf_t menufunc[menuMaxY * menuMaxX] = {
	_mM,            _mM,     _mM,                _mM,           /*    _mM,   */ _mM,
	_mTune,         _mMode,  _mANR,              _mBandwAM,     /*    NULL,   */ _mSave,
	_mTune,         _mMode,  _mANR,              _mBandwAM,     /*    NULL,   */ NULL,
	_mTune,         _mMode,  _mANR,              NULL,          /*    NULL,   */ NULL,
	NULL,           _mMode,  NULL,               NULL,          /*    NULL,   */ NULL
};

//-------------------------------------------------------
void printMenuTitle(int menuX, int menuY);

int spectrum_on_old = Spectrum_on;
int spectrumCounter = 0;
int menuActive = -1; // -1 = Menu is inactive
int menuLastActive = 0; //Last Active Main Menu
int menuOld    = -99;
boolean menuSubInitialized = false;

unsigned long menuLastChange = millis();

void menuExit(void) {
	display.clearDisplay();
	showFreq();
	Spectrum_on = spectrum_on_old;
	spectrumCounter = 0;
	menuActive = menuOld = -1;
	menuSubInitialized = false;
}

void menuResetTimeout(void) {
	menuLastChange = millis();
}

void showMenu(void) {

	buttons();
	if (menuActive < 0) return;

	//Timeout
	if (millis() - menuLastChange > MENU_TIMEOUT && menuOld != -1) {
		menuExit();
		return;
	}

	if (menuActive == menuOld) return;

	const int lines = 3;

	int i, h, l;
	int y = LCD_DISPLAYSTART;
	int x = 0;
	int menuY = menuActive / menuMaxX;
	int menuX = menuActive - menuY * menuMaxX;

	display.fillRect(0, y, display.width(), display.height() - y, BLACK);
	if (menuY == 0) {       //Main Menu
		menuLastActive = menuActive;
		y += 2;
		display.setTextColor(WHITE);
		display.setFont(Arial_12); h = 15;
		i = 0;
		l = 0;
		if (menuX > lines - 1) i = menuX - (lines - 1);
		x = 8;
		do {
			if (i == menuX) {
				display.fillRoundRect(0, y - 2, display.width(), h + 2, 6, WHITE);
				//display.fillRect(0, y - 2, display.width(), h + 2, WHITE);
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

	else { //submenu

		printMenuTitle(menuX, 0);

		if (menufunc[menuActive] != NULL) {
			y += 2;
			display.setFont(Arial_12); h = 15;
			i = 1;
			l = 0;
			if (menuY > lines - 1) i = menuY - (lines - 1);
			x = 8;
			do {
				if (i == menuY) {
					display.fillRoundRect(0, y - 2, display.width(), h + 2, 6, WHITE);
					//display.fillRect(0, y - 2, display.width(), h + 2, WHITE);
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
	}

	display.display();
	display.setTextColor(WHITE);
	menuOld = menuActive;
	menuResetTimeout();
}
//-------------------------------------------------------
void printMenuTitle(int menuX, int menuY) {
	display.setFont(Arial_12);
	display.fillRoundRect(0, 0, display.width(), 16, 2, WHITE);
	display.setTextColor(BLACK);
	printCentered(0, 1, display.width(), menu[menuY][menuX]);
	display.setTextColor(WHITE);
}
//-------------------------------------------------------
int _mM(void) {
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
		int t = menuActive + 1 * menuMaxX;
		if (menufunc[t] != NULL) {
			//If submenu returns own position (remembers the last set value),
			//use it, otherwise (t2==0) use first
			int t2 = menufunc[t]();
			if (t > 0) menuActive = menuActive + t2 * menuMaxX;
			else menuActive = t;
			Spectrum_on = 0;
		}
	}
	return 0;
}
//-------------------------------------------------------
int _MSubNav(void) {
	Serial.println("msubnav");
	//SubMenu - Navigation, Set Integer
	int menuY = menuActive / menuMaxX;
//	int menuX = menuActive - menuY * menuMaxX;

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
		return menuY - 1;
	}
	return -1;
}
//-------------------------------------------------------
int _mInteger(int val) {
	int v;
	v = _MSubNav();
	return v;
}

int _mTune(void){
	int s = 0;
	if (freqStep == 1000) s = 1;
	else if (freqStep == 100) s = 2;
	int t = _mInteger(s);

	switch (t) {
	case 0: freqStep = -1; tuneStation(settings.lastStation); menuExit(); break;
	case 1: freqStep = 1000; menuExit(); break;
	case 2: freqStep = 100; menuExit(); break;
	}

	return s + 1;
}

int _mMode(void) {
	int t = _mInteger(mode);
	if (t > -1) {
		mode = t;
		menuExit();
	}
	//return menu-y-position:
	return mode + 1;
}

int _mANR(void) {
	int t = _mInteger(ANR_on);
	if (t > -1) {
		ANR_on = t;
		menuExit();
	}
	//return menu-y-position:
	return ANR_on + 1;
}

int _mSave(void) {
	int t = _mInteger(ANR_on);
	if (t > -1) {
		EEPROMsaveSettings();
		menuExit();
	}
	return 1;
}

int _mBandwidthAM(void) {
	const int step = 25;
	int menuY = menuActive / menuMaxX;
	int menuX = menuActive - menuY * menuMaxX;

	if (btn == BTN_DOWN && filter_bandwidth + step <= 5000) {
		filter_bandwidth += step;
		calc_demod_filter();
	}
	else
	if (btn == BTN_UP && filter_bandwidth - step > 100) {
		filter_bandwidth -= step;
		calc_demod_filter();
	}

	else
	if (btn == BTN_CENTER  && menuSubInitialized ) { menuExit(); return 0;}

	//Serial.printf("alt:%d neu:%d\n", menuOld, menuActive);


	display.clearDisplay();
	printMenuTitle(menuX, menuY);
	printCentered(0, 20, display.width(), "Adjust Hz:");
	char buf[9];
	display.setFont(Digital7_20);
	snprintf(buf, sizeof(buf) - 1, "%d", filter_bandwidth);
	printCentered(0, 40, display.width(), buf);

	display.display();
	menuResetTimeout();
	menuSubInitialized = true;

	return 1;
}

int _mBandwTapsAM(void) {
	const int step = 25;
	int menuY = menuActive / menuMaxX;
	int menuX = menuActive - menuY * menuMaxX;
	Serial.println("xx");
/*
   if (btn == BTN_UP && ..)
   else
   if (btn == BTN_DOWN && .. ) ..
   else
 */
	if (btn == BTN_CENTER  && menuSubInitialized ) { menuExit(); return 0;}

	//Serial.printf("alt:%d neu:%d\n", menuOld, menuActive);
	Serial.println("yy");

	display.clearDisplay();
	printMenuTitle(menuX, menuY);
	printCentered(0, 20, display.width(), "Todo:");
/*
   char buf[9];
   display.setFont(Digital7_20);
   snprintf(buf, sizeof(buf) - 1, "%d", filter_bandwidth);
   printCentered(0, 40, display.width(), buf);
 */
	printCentered(0, 40, display.width(), "Not Implemented");
	display.display();
	menuResetTimeout();
	menuSubInitialized = true;

	return 2;

}

int _mBandwAM(void) {
	static int lastUsed = 1;
	static int active = 0;

	if (active == 0) {
		int t = _mInteger(lastUsed);
		if (t > -1) {
			lastUsed = t;
			active = t + 1;
			Serial.println(t);
		}
	}

	switch (active) {
	case 1: active = _mBandwidthAM(); break;
	case 2: active = _mBandwTapsAM(); break;
	}

	//return menu-y-position:
	return lastUsed + 1;
}

//-------------------------------------------------------

boolean tuneStation(int i) {
	if (settings.station[i].freq != 0) {
		settings.lastStation = i;
		settings.lastFreq = 0;
		freq = settings.station[i].freq;
		mode = settings.station[i].mode;
		ANR_on = settings.station[i].notch;

		if (settings.station[i].filterBandwidth == 0)
			filter_bandwidth = filter_bandwidth_default;
		else
			filter_bandwidth = settings.station[i].filterBandwidth;
		calc_demod_filter();
		tune();
		Serial.printf("%c: %8d\t%s\t%s\n", 'a' + i, settings.station[i].freq, modestr[settings.station[i].mode], settings.station[i].sname);
		return true;
	}
	//Serial.println("Empty.");
	return false;
}

//-------------------------------------------------------

void serialUI(void) {
	if (!Serial.available()) return;

	static int input = 0;
	char ch = Serial.read();
	if (ch >= 'a' && ch <= ('a' + MAX_EMEMORY - 1)) {
		tuneStation(ch - 'a');
	}
	else if (ch == 'L') {
		mode = LSB;
		settings.lastMode = mode;
		tune();
	}
	else if (ch == 'U') {
		mode = USB;
		settings.lastMode = mode;
		tune();
	}
	else if (ch == 'A') {
		mode = AM;
		settings.lastMode = mode;
		tune();
	}
#if defined(__MK66FX1M0__)
	else if (ch == 'S') {
		mode = SYNCAM;
		settings.lastMode = mode;
		tune();
	}
	else if (ch == 'N') {
		if (ANR_on == 2) {
			ANR_on = 0;
//			Serial.println("Auto-Notch OFF");
		}
		else if (ANR_on == 0) {
			ANR_on = 1;
//			Serial.println("Auto-Notch ON");
		} else if (ANR_on == 1) {
			ANR_on = 2;
//			Serial.println("Auto-Noise ON");
		}
		tune();
	}
#endif
	else if (ch == 'G') {
		if (AGC_on) {
			AGC_on = 0;
//			Serial.println("AGC OFF");
		} else {
			AGC_on = 1;
//			Serial.println("AGC ON");
		}
		showFreq();
	}
	else if (ch == '!') {
		EEPROMsaveSettings();
	}
	else if (ch >= '0' && ch <= '9') {
		input = input * 10 + (ch - '0');
	}
	else if (ch == '\r' || ch == '\n') {
		if (input > 0) {
			if (input < 1000) input *= 1000;
			freq = input;
			settings.lastFreq = input;
			input = 0;
			tune();
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
#if defined(__MK64FX512__) || defined(__MK66FX1M0__)
#define SPECTRUM_DELETE_COLOUR BLACK
#define SPECTRUM_DRAW_COLOUR WHITE
#define SPECTRUM_SMOOTH 0

	if (!Spectrum_on) return;
	if (--spectrumCounter > 0) return;
	spectrumCounter = 25;

	static const int spectrum_height = 16;
	static const int spectrum_y = 0;
	static const int spectrum_x = 0;
	int y_new, y1_new;
	int y1_new_minus = 0;

	// FFT with 128 points
	int16_t FFT_out [264]; //(FB)should be 128 .. Bug?
	//int16_t FFT_in [128];
#if SPECTRUM_SMOOTH
	int16_t pixelnew[128];
#endif

	arm_rfft_q15(&FFT, data, FFT_out);
	//memcpy(FFT_in, data, sizeof(FFT_in));
	//arm_rfft_q15(&FFT, FFT_in, FFT_out);

	display.fillRect(0, spectrum_y, display.width(), spectrum_height, SPECTRUM_DELETE_COLOUR);

	for (int x = 0; x < 127; x++) {
		y_new = abs(FFT_out[127 - x]) / 200;

#if SPECTRUM_SMOOTH
		if ((x > 1) && (x < 126)) {
			// moving window - weighted average of 5 points of the spectrum to smooth spectrum in the frequency domain
			// weights:  x: 50% , x-1/x+1: 36%, x+2/x-2: 14%
			pixelnew[x] = y_new;
			y_new = pixelnew[x] * 0.5f + pixelnew[x - 1] * 0.18f + pixelnew[x + 1] * 0.18f
			        + pixelnew[x - 2] * 0.07f + pixelnew[x + 2] * 0.07f;
		}
#endif

		if (y_new > (spectrum_height))
			y_new = (spectrum_height);
		y1_new  = (spectrum_y + spectrum_height - 1) - y_new;

		if (x == 0)
			y1_new_minus = y1_new;
		else if (x == 127)
			y1_new_minus = y1_new;

		// DRAW NEW LINE/POINT
		int t = y1_new - y1_new_minus;

		if (t > 0)       // plot line upwards
			display.drawFastVLine(x + spectrum_x, y1_new_minus, t, SPECTRUM_DRAW_COLOUR);
		else if (t < 0)  // plot line downwards
			display.drawFastVLine(x + spectrum_x, y1_new, -t, SPECTRUM_DRAW_COLOUR);
		else             // write new pixel
			display.drawPixel(x + spectrum_x, y1_new, SPECTRUM_DRAW_COLOUR);

		y1_new_minus = y1_new;
	}               // end for loop
	display.display(spectrum_height);
#endif
}

//-------------------------------------------------------

void printAt(int x, int y, const char * txt) {
	display.setCursor(x, y);
	display.print(txt);
}

//-------------------------------------------------------

void printCentered(int x, int y, int maxwidth, const char * txt) {
	int w;
	if (maxwidth == 0) maxwidth = display.width();
	w = display.measureTextWidth(txt, 0);
	w = (maxwidth - w) >> 1;
	display.setCursor(x + w, y);
	display.print(txt);
}

//-------------------------------------------------------
void printFreq(int x, int y, int maxwidth) {
	char buf[9];
	display.setFont(Digital7_24);
	snprintf(buf, sizeof(buf) - 1, "%.2f", freq / 1000.0);
	printCentered(x, y, maxwidth, buf);
}
//-------------------------------------------------------
void showFreq(void) {

	int y = LCD_DISPLAYSTART;
	display.setTextColor(WHITE);
	display.fillRect(0, y, display.width(), display.height() - y, BLACK);

	printFreq(0, y, 104);
	int x = 106;
	display.setFont(Arial_8);
	printAt(x, y + 1, "kHz");
	printAt(x, y + 14, modestr[mode]);       //Display Mode

	y += 26;
	if (AGC_on) printAt(4 * 8, y, "AGC");       //AGC

	if (ANR_on == 1) printAt(8 * 8, y, "Notch");
	else if (ANR_on == 2) printAt(8 * 8, y, "Noise");

	y += 10;

	if (settings.lastFreq > 0) {
		//printAt(0, y, "Manual Setting");
	} else {
		const char * p = settings.station[settings.lastStation].sname;
		int x = (display.width() - display.measureTextWidth(p, 0)) >> 1;
		if (x < 0) x = 0;
		printAt(x, y, p);// print centered
	}

	display.display();

}

//-------------------------------------------------------
int encoderPosOld = 0;

inline int readEncoder(void) {
	return encoder.read() >> 1;
}

void buttons(void) {

	encoderPosOld = encoderPos;
	encoderPos = readEncoder();

	//Read buttons:
	if (encoderPos < encoderPosOld) btn = BTN_UP;
	else if (encoderPos > encoderPosOld) btn = BTN_DOWN;
	else if (encCenter.update() && encCenter.fallingEdge()) btn = BTN_CENTER;
	else if (btnCenter.update() && btnCenter.fallingEdge()) btn = BTN_CENTER;
	else if (btnUp.update() && btnUp.fallingEdge()) btn = BTN_UP;
	else if (btnDown.update() && btnDown.fallingEdge()) btn = BTN_DOWN;
	else if (btnLeft.update() && btnLeft.fallingEdge()) btn = BTN_LEFT;
	else if (btnRight.update() && btnRight.fallingEdge()) btn = BTN_RIGHT;
	else btn = -1;

	if (btn >= 0) {
		if (menuActive < 0 && btn == BTN_CENTER) {
			//Enter Menu
			menuActive = menuLastActive;
			spectrum_on_old = Spectrum_on;
		}
		else
		if (menuActive >= 0 && menufunc[menuActive] != NULL) {
			//Process Menufunction
			mf_t func;
			func = menufunc[menuActive];
			func();
		}
		else

		//if menu is not active, use encoder to set frequency
		//TODO: how to set fractions ? i.e. 77500 kHz (long center press to activate?)
		//TODO: Frequenzraster beachten (Mittelwelle!)
		if (menuActive < 0) {
			if (freqStep < 0) {
				int s = settings.lastStation;
				if (btn == BTN_UP && s > 0) { s--; tuneStation(s);  }
				else
				if (btn == BTN_DOWN && s < MAX_EMEMORY - 1) { s++; tuneStation(s);  }
			} else {
				if (btn == BTN_UP) { freq -= freqStep; tune(); }
				else
				if (btn == BTN_DOWN) { freq += freqStep;  tune(); }
				settings.lastFreq = freq;
			}
		}
	}

}
//-------------------------------------------------------

void initUI(void) {
	pinMode(BTN_CENTER, INPUT_PULLUP);
	pinMode(BTN_UP, INPUT_PULLUP);
	pinMode(BTN_DOWN, INPUT_PULLUP);
	pinMode(BTN_LEFT, INPUT_PULLUP);
	pinMode(BTN_RIGHT, INPUT_PULLUP);
	pinMode(ENC_CENTER, INPUT_PULLUP);
#if DBGFRANKB
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

	freqStep = (settings.lastFreq != 0) ? 1000 : -1;
	initSpectrum();
	encoderPosOld = encoderPos = readEncoder();
	buffer = display.getBufAddr();
	bufsize = display.getBufSize();

	spectrumCounter = 500;
}

//-------------------------------------------------------
void UI(void) {
	serialUI();
	showMenu();
}
//-------------------------------------------------------

#if (SSD1306_LCDHEIGHT != 64)
#error ("Height incorrect, please fix Adafruit_SSD1306_i2ct3.h!");
#endif
