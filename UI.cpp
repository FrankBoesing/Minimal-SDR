/*********************************************************************
  #define BTN_UP  miniSDR v3

  By DD4WH and Frank Bösing

  GPL V3

  - Userinterface -

**********************************************************************/

#include <Arduino.h>
#include <Bounce.h>
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


//-------------------------------------------------------

#define I2C_SPEED       2000000
#define OLED_I2CADR     0x3C    //Adafruit: 0x3D
#define OLED_RESET      255

// settings for LCD
#define LCD_DISPLAYSTART        18 //Start content at y = 20 

//-------------------------------------------------------

extern settings_t settings;
extern int mode;
extern uint8_t ANR_on; // off: 0, automatic notch filter:1, automatic noise reduction: 2
extern uint8_t AGC_on; // automatic gain control ON/OFF
extern float AGC_val;  // agc actual value
extern uint8_t clk_errcmp; // en-/disable clk-error compensation
extern float freq;

//-------------------------------------------------------

void tune(float freq);
void EEPROMsaveSettings(void);

//-------------------------------------------------------

Adafruit_SSD1306 display(OLED_RESET);

Bounce btnCenter(BTN_CENTER, BTN_DEBOUNCE);
Bounce btnUp(BTN_UP, BTN_DEBOUNCE);
Bounce btnDown(BTN_DOWN, BTN_DEBOUNCE);
Bounce btnLeft(BTN_LEFT, BTN_DEBOUNCE);
Bounce btnRight(BTN_RIGHT, BTN_DEBOUNCE);

Encoder knobLeft(ENC_1, ENC_2);
Bounce encCenter(ENC_CENTER, BTN_DEBOUNCE);

static uint8_t * buffer;
static unsigned bufsize;

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
int spectrumCounter = 0;

void initSpectrum(void) {
  arm_rfft_init_q15 (&FFT, 128, 0, 1);
}

void showSpectrum(int16_t * data)
{
#define SPECTRUM_DELETE_COLOUR BLACK
#define SPECTRUM_DRAW_COLOUR WHITE

  static const int16_t spectrum_height = 16;
  static const int16_t spectrum_y = 0;
  static const int16_t spectrum_x = 0;

  // FFT with 128 points
  int16_t FFT_out [128];
  int16_t FFT_in [128];

  int16_t pixelnew[128];
  int16_t y_new, y1_new;
  int16_t y1_new_minus = 0;

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
        { // plot line upwards
          display.drawFastVLine(x + spectrum_x, y1_new_minus + 1, y1_new - y1_new_minus, SPECTRUM_DRAW_COLOUR);
        }
        else if (y1_new - y1_new_minus < -1)
        { // plot line downwards
          display.drawFastVLine(x + spectrum_x, y1_new, y1_new_minus - y1_new, SPECTRUM_DRAW_COLOUR);
        }
        else
        {
          display.drawPixel(x + spectrum_x, y1_new, SPECTRUM_DRAW_COLOUR); // write new pixel
        }

        y1_new_minus = y1_new;

      }
    } // end for loop
    display.display(spectrum_height);
  }
}

//-------------------------------------------------------

void printAt(int x, int y, const char * txt) {
  display.setCursor(x, y);
  display.print(txt);
}

//-------------------------------------------------------


//-------------------------------------------------------


//-------------------------------------------------------


//-------------------------------------------------------
void showFreq(void)
{
  int y = LCD_DISPLAYSTART;
  display.fillRect(0, y, SSD1306_LCDWIDTH, SSD1306_LCDHEIGHT - y , 0);
  int x = 0;
  int size = 26;

  //show frequency  
  //display.setFont(Digital7_24_Italic);
  display.setFont(Digital7_24);
  display.setCursor(x, y);
  float f = freq / 1000;
  int n = 1;
  if (f < 100.0) n = 4;
  else if (f < 1000.0) n = 3;
  else if (f < 10000.0) n = 2;
  display.print(f, n);

  display.setFont(Arial_8);
  printAt(110, y, "kHz");

  printAt(106, y + 14, modestr[mode]); //Display Mode
  
  y += size;  
  size = 10;  
  
  if (AGC_on) printAt(4 * 8, y, "AGC"); //AGC
  
  if (ANR_on == 1) printAt(8 * 8, y, "Notch");
  else if (ANR_on == 2) printAt(8 * 8, y, "Noise");

  y+= size;
  
  if (settings.lastFreq > 0) {
    //printAt(0, y, "Manual Setting");
  } else {
    printAt(0, y, settings.station[settings.lastStation].sname);
  }

  display.display();
  
}
//-------------------------------------------------------

void initUI(void) {

  pinMode(BTN_CENTER, INPUT_PULLUP);
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  pinMode(ENC_CENTER, INPUT_PULLUP);

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
void UI(void) {
  serialUI();
}
//-------------------------------------------------------

#if (SSD1306_LCDHEIGHT != 64)
#error("Height incorrect, please fix Adafruit_SSD1306.h!");
#endif

#if(_LCDML_DISP_rows > _LCDML_DISP_cfg_max_rows)
#error change value of _LCDML_DISP_cfg_max_rows in LCDMenuLib2.h
#endif
