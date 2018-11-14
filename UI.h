/*********************************************************************
  miniSDR v3

  By DD4WH and Frank Bösing

  GPL V3

  - Userinterface -

**********************************************************************/
#ifndef sdr_ui_h_
#define sdr_ui_h_

//-------------------------------------------------------
//Pins:
//4-way tactile switch:
#define BTN_DEBOUNCE 10 //(ms)
#define BTN_CENTER  3
#define BTN_UP      1
#define BTN_DOWN    4
#define BTN_LEFT    0
#define BTN_RIGHT   2

//encoder:
#if 0
#define ENC_1       7
#define ENC_2       8
#define ENC_CENTER  10
#endif

#define ENC_1       25
#define ENC_2       26
#define ENC_CENTER  24

//-------------------------------------------------------

void initUI(void);
void UI(void);
void showFreq(void);
void showSpectrum(int16_t * data);

#endif
