/*********************************************************************
  miniSDR v3

  By DD4WH and Frank Bösing

  GPL V3

  =================
  ADC : Pin A2 / 16
  DAC : Pin A21 DAC0

  I2C-SCL : PIN 19
  I2C-SDA : PIN 18
  =================

  Hardware:
    MCP2036

  How does it work:
    Teensy produces LO RX frequency on BCLK (pin 9) or MCLK (pin 11)
    MCP2036 mixes LO with RF coming from antenna --> direct conversion
    MCP amplifies and lowpass filters the IF signal
    Teensy ADC samples incoming IF signal with sample rate == IF * 4
    Software Oscillators cos & sin are used to produce I & Q signals and simultaneously translate the I & Q signals to audio baseband
    I & Q are filtered by linear phase FIR [tbd]
    Demodulation --> SSB, AM or synchronous AM
    Decoding of time signals or other digital modes [tbd]
    auto-notch filter to eliminate birdies
    IIR biquad filter to shape baseband audio
    Audio output through Teensy DAC

*********************************************************************/
#include "stations.h"
#include <util/crc16.h>

#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>

AudioInputAnalog         adc1;           //xy=429,313
AudioAmplifier           amp_adc;           //xy=583,313
AudioRecordQueue         queue_adc;      //xy=754,310

AudioPlayQueue           queue_dac;      //xy=442,233
AudioFilterBiquad        biquad1_dac;        //xy=613,234
AudioFilterBiquad        biquad2_dac;        //xy=613,234
AudioAmplifier           amp_dac;           //xy=764,230
AudioOutputAnalog        dac1;           //xy=914,227

AudioConnection          patchCord1(adc1, amp_adc);
AudioConnection          patchCord2(queue_dac, biquad1_dac);
AudioConnection          patchCord3(amp_adc, queue_adc);
AudioConnection          patchCord4a(biquad1_dac, biquad2_dac);
AudioConnection          patchCord4(biquad2_dac, amp_dac);
AudioConnection          patchCord5(amp_dac, dac1);

#define OLED 1

#if OLED
#include "Adafruit_SSD1306.h"
#include <Adafruit_GFX.h>
#define I2C_SPEED   2000000
#define OLED_I2CADR 0x3C    //Adafruit: 0x3D
#define OLED_RESET  255

#if (SSD1306_LCDHEIGHT != 64)
#error("Height incorrect, please fix Adafruit_SSD1306.h!");
#endif

Adafruit_SSD1306 display(OLED_RESET);
#endif //if OLED

#define AUDIOMEMORY     20

#define _IF             6000        // intermediate frequency
#define SAMPLE_RATE     (_IF * 4)   // new Audio-Library sample rate
#define CORR_FACT       (AUDIO_SAMPLE_RATE_EXACT / SAMPLE_RATE) // Audio-Library correction factor

#define I2S_FREQ_MAX    36000000
#define I2S0_TCR2_DIV   0


const char sdrname[] = "Mini - SDR";
int mode             = SYNCAM;
uint8_t ANR_on       = 0;         // automatic notch filter ON/OFF
int input            = 0;
float freq;
settings_t settings;

unsigned long time_needed     = 0;
unsigned long time_needed_max = 0;

int16_t minval = 32767;
int16_t maxval = -minval;
unsigned long demodulation(void);

//-------------------------------------------------------

uint16_t settingsCrc(void) {
  uint16_t crc = 0;
  uint8_t * p;
  p = (uint8_t*)&settings;
  for (unsigned i = 0; i < sizeof(settings_t) - sizeof(settings.crc); i++, p++) {
    crc = _crc16_update(crc, *p);
  }
  return crc;
}

void EEPROMsaveSettings(void) {
  settings.crc = settingsCrc();
  eeprom_write_block(&settings, 0, sizeof(settings_t));
}

void initEEPROM(void) {

  eeprom_read_block(&settings, 0, sizeof(settings_t));

  //1. Calculate CRC to check if content is valid
  uint16_t crc = settingsCrc();

  if ( (crc != settings.crc) || (settings.version != EEPROM_STORAGE_VERSION) ) {
    //Init default values:
    memcpy(&settings, &settings_default, sizeof(settings_t));
    EEPROMsaveSettings();
    Serial.println("EEPROM initialized.");
  }

#if 1
  Serial.println("EEPROM:");
  //Serial.printf("Version: %d\n", settings.version);
  //Serial.printf("CRC: %x\n", settings.crc);
  Serial.printf("Last Station: %d\n", settings.lastStation);
  Serial.printf("Last Freq: %d\n", settings.lastFreq);
  Serial.printf("Last Mode: %d\n", settings.lastMode);
  int i = 0;
  while (i < MAX_EMEMORY) {
    if (settings.station[i].freq != 0.0)
      Serial.printf("%c: %8d\t%s\t%s\n", 'a' + i, (int) settings.station[i].freq, modestr[settings.station[i].mode], settings.station[i].sname);
    i++;
  }
#endif

}
//-------------------------------------------------------

void loadLastSettings(void) {

  if (settings.lastFreq != 0) {
    freq = settings.lastFreq;
    mode = settings.lastMode;
  } else {
    int i = settings.lastStation;
    freq = settings.station[i].freq;
    mode = settings.station[i].mode;
  }

}

//-------------------------------------------------------

void showFreq(float freq, int mode)
{
  //  Serial.print(freq / 1000.0, 4);
  //  Serial.println("kHz");
#if OLED
  const int x = 0;
  const int y = 20;
  const int size = 3;

  display.setTextSize(1);
  display.setCursor(0, 20 + 3 * 8);
  display.fillRect(0, 20 + 3 * 8, 4 * 8, 8, 0);
  display.println(modestr[mode]);

  display.setTextSize(1);
  display.setCursor(0, 20 + 4 * 8);
  display.fillRect(0, 20 + 4 * 8, 4 * 8, 8, 0);
  if (ANR_on == 1) display.println("Notch");

  display.setTextSize(size);
  display.fillRect(x, y, display.width(), size * 8, 0);
  display.setCursor(x, y);

  float f = freq / 1000;
  int n;
  if (f < 100.0) n = 4;
  else if (f < 1000.0) n = 3;
  else if (f < 10000.0) n = 2;
  else n = 1;
  display.print(f, n);

  display.display();
#endif
}

//-------------------------------------------------------

#define PDB_CONFIG (PDB_SC_TRGSEL(15) | PDB_SC_PDBEN | PDB_SC_CONT | PDB_SC_PDBIE | PDB_SC_DMAEN)

float setPDB_freq(float fsamp)
{
  int mod = (uint32_t)((F_BUS) / fsamp + 0.5); //AUDIO_SAMPLE_RATE_EXACT
  PDB0_SC = 0;
  PDB0_MOD = mod  - 1;
  PDB0_SC = PDB_CONFIG | PDB_SC_LDOK;
  PDB0_SC = PDB_CONFIG | PDB_SC_SWTRIG;
  return (F_BUS) / mod;
}

//-------------------------------------------------------

float setI2S_freq(float fsamp)
{

  float rfreq = 0;
  float minfehler = 1e7;
  unsigned fract = 1, divi = 1;

  unsigned pin = 9;
  unsigned x = (I2S0_TCR2_DIV + 1) * 2;

  if (fsamp > (float)(I2S_FREQ_MAX / x)) {
    pin = 11;
    x = 1;
  } else {
    pin = 9;
  }

  unsigned b = ((SIM_SOPT2 & SIM_SOPT2_IRC48SEL) ? F_PLL : F_CPU) / x;

  for (unsigned i = 1; i < 256; i++) {
    unsigned d = round(b / fsamp * i);
    float freq = b * i / (float)d ;
    float fehler = fabs(fsamp - freq);
    if ( fehler < minfehler && d < 4096 ) {
      fract = i;
      divi = d;
      minfehler = fehler;
      rfreq = freq;
      //Serial.printf("%fHz<->%fHz(%d/%d) Fehler:%f PIN %d\n", fsamp, freq, fract, divi, minfehler, pin);
      if (fehler == 0.0f) break;
    }
  }

  // Serial.printf("%fHz<->%fHz(%d/%d) Fehler:%f PIN %d\n", fsamp, rfreq, fract, divi, fabs(fsamp - rfreq), pin);

  uint32_t MDR = I2S_MDR_FRACT( (fract - 1) ) | I2S_MDR_DIVIDE( (divi - 1) );

  if (MDR > 0) {

    //Select pin:
    if (pin == 9) {
      //CORE_PIN11_CONFIG = PORT_PCR_MUX(6); // pin 11, PTC6, I2S0_MCLK
      CORE_PIN11_CONFIG = 0;
      CORE_PIN9_CONFIG  = PORT_PCR_MUX(6);// enable pin  9, PTC3, I2S0_TX_BCLK
    } else {
      //CORE_PIN9_CONFIG  = PORT_PCR_MUX(6);// enable pin  9, PTC3, I2S0_TX_BCLK
      CORE_PIN9_CONFIG  = 0;
      CORE_PIN11_CONFIG = PORT_PCR_MUX(6); // pin 11, PTC6, I2S0_MCLK
    }

    while (I2S0_MCR & I2S_MCR_DUF) {
      ;
    }

    I2S0_MDR = MDR;
  }

  return rfreq;
}

//-------------------------------------------------------

void initI2S(void)
{
  SIM_SCGC6 |= SIM_SCGC6_I2S;
  const int wordwidth = 0;
  // configure transmitter
  I2S0_TMR = 0;
  I2S0_TCR1 = I2S_TCR1_TFW(1);
  I2S0_TCR2 = I2S_TCR2_SYNC(0) | I2S_TCR2_BCP | I2S_TCR2_MSEL(1) | I2S_TCR2_BCD | I2S_TCR2_DIV(I2S0_TCR2_DIV);
  I2S0_TCR3 = I2S_TCR3_TCE;
  I2S0_TCR4 = I2S_TCR4_FRSZ(1) | I2S_TCR4_SYWD(wordwidth) | I2S_TCR4_MF | I2S_TCR4_FSP | I2S_TCR4_FSD;
  I2S0_TCR5 = I2S_TCR5_WNW(wordwidth) | I2S_TCR5_W0W(wordwidth) | I2S_TCR5_FBT(wordwidth);

  uint32_t mclk_src = (SIM_SOPT2 & SIM_SOPT2_IRC48SEL) ? 0 : 3;
  // enable MCLK output
  I2S0_MCR = I2S_MCR_MICS(mclk_src) | I2S_MCR_MOE;

  setI2S_freq(10000);

  // TX clock enable
  I2S0_TCSR |= I2S_TCSR_TE | I2S_TCSR_BCE;

}

//-------------------------------------------------------

void tune(float freq) {
  float freq_actual;
  float freq_diff;
  float pdb_freq;
  float pdb_freq_actual;

  AudioNoInterrupts();
  PDB0_SC = 0;
#if !(defined(__MK66FX1M0__))
  if (mode == SYNCAM) mode = AM;
#endif

  // BCLK:
  freq_actual = setI2S_freq(freq + _IF);
  freq_diff = freq_actual - _IF - freq;

  // PDB (ADC + DAC) :
  pdb_freq = freq_diff * 4.0 + SAMPLE_RATE;     //*4.0 due to I/Q
  pdb_freq_actual = setPDB_freq(pdb_freq);

  showFreq(freq, mode);

#if 1
  Serial.println("Tune:");
  Serial.printf("BCLK Soll: %f ist: %f Diff: %fHz\n", freq, freq_actual - _IF, freq_diff);
  Serial.printf("PDB  Soll: %f ist: %f Diff: %fHz\n", pdb_freq, pdb_freq_actual, pdb_freq_actual - pdb_freq);
  Serial.println();
#endif

  AudioProcessorUsageMaxReset();
  time_needed_max = 0;
  AudioInterrupts();
}

//-------------------------------------------------------

void setup()   {
  AudioMemory(AUDIOMEMORY);
  PDB0_SC = 0; //Stop PDB

  Serial.println(sdrname);
  Serial.printf("F_CPU: %dMHz F_BUS: %dMHz\n", (int) (F_CPU / 1000000), (int) (F_BUS / 1000000));
  Serial.printf("IF: %dHz Samplerate: %dHz\n", _IF, _IF * 4 );
  Serial.println();

  initEEPROM();
  initI2S();

#if OLED
  display.begin(SSD1306_SWITCHCAPVCC, OLED_I2CADR);
  Wire.setClock(I2C_SPEED);
  display.clearDisplay();
  display.setTextColor(WHITE);

  display.setCursor(4, 0);
  display.setTextSize(2);
  display.println(sdrname);

  display.setTextSize(1);
  display.setCursor(110, 20 + 3 * 8);
  display.println("kHz");

  display.display();
#endif

  //amp_adc.gain(1.5); //amplifier after ADC (is this needed?)

  // Linkwitz-Riley: gain = {0.54, 1.3, 0.54, 1.3}
  // notch is very good, even with small Q
  // we have to restrict our audio bandwidth to at least 0.5 * IF,
  const float cutoff_freq = _IF * 0.5 * CORR_FACT;
  biquad1_dac.setLowpass(0,  cutoff_freq, 0.54);
  biquad1_dac.setLowpass(1, cutoff_freq, 1.3);
  biquad1_dac.setLowpass(2, cutoff_freq, 0.54);
  biquad1_dac.setLowpass(3, cutoff_freq, 1.3);
  biquad2_dac.setLowpass(0, cutoff_freq, 0.54);
  biquad2_dac.setLowpass(1, cutoff_freq, 1.3);
  biquad2_dac.setLowpass(2, cutoff_freq, 0.54);
  //biquad2_dac.setNotch(3, 3000 * CORR_FACT, 2.0); // eliminates some birdy
  amp_dac.gain(2.0); //amplifier before DAC

  AudioProcessorUsageMaxReset();
  loadLastSettings();
  tune(freq);
  queue_adc.begin();
}

//-------------------------------------------------------

void printAudioLibStatistics(void) {
#if 1

  const int audiolibStatisticsInterval = 10000; //ms
  int t = millis();
  static int audiolibLastStatistic = t;
  static int timestart = t;

  if (t - audiolibLastStatistic > audiolibStatisticsInterval) {
    audiolibLastStatistic = t;
    Serial.println();
    Serial.printf("Runtime: %d seconds\n", (t - timestart) / 1000);
    Serial.printf("AudioMemoryUsageMax: %d Blocks\n", AudioMemoryUsageMax());
    Serial.printf("AudioProcessorUsageMax: %.2f%%\n", AudioProcessorUsageMax());
    Serial.printf("Demodulation t-max: %d%cs\n", time_needed_max, 'µ');
    Serial.printf("Min: %d  Max: %d   Offset: %.2fmV   ADC-Bits: %d\n", minval, maxval, (minval + maxval) * 1.2f / 65536.0  , 16 - (__builtin_clz (max(-minval, maxval)) - 16));
    minval = maxval = 0;
  }

#endif
}

//-------------------------------------------------------

void serialUI(void) {
  if (!Serial.available()) return;

  char ch = Serial.read();

  if (ch >= 'a' && ch <= 'z') {
    int i = ch - 'a';
    if (settings.station[i].freq != 0.0) {
      settings.lastStation = i;
      settings.lastFreq = 0;
      freq = settings.station[i].freq;
      mode = settings.station[i].mode;
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
  else if (ch == 'S') {
    mode = SYNCAM;
    settings.lastMode = mode;
    tune(freq);
  }
#if defined(__MK66FX1M0__)
  else if (ch == 'N') {
    if (ANR_on)
    {
      ANR_on = 0;
      Serial.println("Auto-Notch OFF");
      tune(freq);
    }
    else
    {
      ANR_on = 1;
      Serial.println("Auto-Notch ON");
      tune(freq);
    }
  }
#endif
  else if (ch == '!') {
    EEPROMsaveSettings();
    Serial.println("Settings saved");
  }
  else if (ch >= '0' && ch <= '9') {
    input = input * 10 + (ch - '0');
    //Serial.print("Input = "); Serial.println(input);
  } else if (ch == '\r') {
    if (input > 0) {
      freq = input;
      settings.lastFreq = input;
      input = 0;
      tune(freq);
    }
  }
}

//-------------------------------------------------------

void loop() {
  serialUI();
  printAudioLibStatistics();
  //asm ("wfi");
  time_needed = demodulation();
  if (time_needed > time_needed_max) time_needed_max = time_needed;
}

//-------------------------------------------------------

int32_t I_buffer[AUDIO_BLOCK_SAMPLES];
int32_t Q_buffer[AUDIO_BLOCK_SAMPLES];

unsigned long demodulation(void) {

  if ( queue_adc.available() < 1 ) return 0;
  if ( !queue_dac.available() ) return 0;

  unsigned long time_start = micros();

  /*
      Read data from input queue
      I/Q conversion
  */

  // frequency translation without multiplication
  // multiply the signal from the ADC with a Cosine for I channel
  // multiply the signal from the ADC with a Sine   for Q channel
  // if IF == sample rate / 4,
  // the Cosine is {1, 0, -1, 0, 1, 0, -1, 0 . . . .}
  // and the Sine is {0, 1, 0, -1, 0, 1, 0, -1 . . . .}
#if 0
  int16_t * p_adc;
  p_adc = queue_adc.readBuffer();
  for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i += 4) {

    I_buffer[i]     = p_adc[i];
    I_buffer[i + 1] = 0;
    I_buffer[i + 2] = - p_adc[i + 2];
    I_buffer[i + 3] = 0;

    Q_buffer[i]     = 0;
    Q_buffer[i + 1] = p_adc[i + 1];
    Q_buffer[i + 2] = 0;
    Q_buffer[i + 3] = - p_adc[i + 3];

  }
#else
  int16_t * p_adc;
  p_adc = queue_adc.readBuffer();
  int16_t min = minval;
  int16_t max = maxval;
  for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i += 4) {
    /*
        // Load next two samples in a single access
        uint32_t data = *__SIMD32(p32_adc)++;
        I_buffer[i + 0] = (int16_t) (data >> 16);
        Q_buffer[i + 1] = (int16_t) data;
    */
    int16_t s0 = p_adc[i + 0];
    int16_t s1 = p_adc[i + 1];
    I_buffer[i]     = s0;
    Q_buffer[i + 1] = s1;
    uint32_t data = (((uint16_t)s0) << 16) | s1;

    (void)__SSUB16(max, data);// Parallel comparison of max and new samples
    max = __SEL(max, data);   // Select max on each 16-bits half
    (void)__SSUB16(data, min);// Parallel comparison of new samples and min
    min = __SEL(min, data);   // Select min on each 16-bits half

    s0 = p_adc[i + 2];
    s1 = p_adc[i + 3];
    I_buffer[i + 2] = - s0;
    Q_buffer[i + 3] = - s1;
    data = (((uint16_t)s0) << 16) | s1;

    (void)__SSUB16(max, data);
    max = __SEL(max, data);
    (void)__SSUB16(data, min);
    min = __SEL(min, data);
#if 0
    I_buffer[i + 1] = I_buffer[i + 3] = 0;
    Q_buffer[i + 0] = Q_buffer[i + 2] = 0;
#endif
  }
  // Now we have maximum on even samples on low halfword of max
  // and maximum on odd samples on high halfword
  // look for max between halfwords 1 & 0 by comparing on low halfword
  (void)__SSUB16(max, max >> 16);
  maxval = __SEL(max, max >> 16);// Select max on low 16-bits
  (void)__SSUB16(min >> 16, min);// look for min between halfwords 1 & 0 by comparing on low halfword
  minval = __SEL(min, min >> 16);// Select min on low 16-bits

#endif

  queue_adc.freeBuffer();

  // TODO:

  // Here, we have to filter separately the I & Q channel with a linear phase filter
  // so a FIR filter with symmetrical coefficients should be used
  // Do not use an IIR filter

  /*
    - demodulation
    - write data to output-qeue
  */
  int16_t * p_dac;
  p_dac = queue_dac.getBuffer();

  switch (mode) {
    //-------------------------------------------------------
    case LSB: {
        for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
          p_dac[i] = I_buffer[i] - Q_buffer[i];
        }
        break;
      }
    //-------------------------------------------------------
    case USB: {
        // USB demodulation
        for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
          p_dac[i] = I_buffer[i] + Q_buffer[i];
        }
        break;
      }
      //-------------------------------------------------------
#if defined(__MK64FX512__) || defined(__MK66FX1M0__)
    case AM: {
        float32_t audio;

        for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
          arm_sqrt_f32 ( I_buffer[i] *  I_buffer[i] + Q_buffer[i] * Q_buffer[i], &audio );
          p_dac[i] = audio;
        }
        break;
      }
#else
    case AM: {
        q31_t audio;
        for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
          arm_sqrt_q31 (  I_buffer[i] *  I_buffer[i] + Q_buffer[i] * Q_buffer[i], &audio );
          p_dac[i] = audio >> 16;
        }
        break;
      }
#endif
    //-------------------------------------------------------
    case SYNCAM: {
        // synchronous AM demodulation - the one with the PLL ;-)
        // code adapted from the wdsp library by Warren Pratt, GNU GPLv3
        static const float32_t omegaN = 400.0; // PLL is able to grab a carrier that is not more than omegaN Hz away
        static const float32_t zeta = 0.65; // the higher, the faster the PLL, the lower, the more stable the carrier is grabbed
        static const float32_t omega_min = 2.0 * PI * - 4000.0 / SAMPLE_RATE; // absolute minimum frequency the PLL can correct for
        static const float32_t omega_max = 2.0 * PI * 4000.0 / SAMPLE_RATE; // absolute maximum frequency the PLL can correct for
        static const float32_t g1 = 1.0 - exp(-2.0 * omegaN * zeta / SAMPLE_RATE); // used inside the algorithm
        static const float32_t g2 = - g1 + 2.0 * (1 - exp(- omegaN * zeta / SAMPLE_RATE) * cosf(omegaN / SAMPLE_RATE * sqrtf(1.0 - zeta * zeta))); // used inside the algorithm

        static float32_t fil_out = 0.0;
        static float32_t omega2 = 0.0;
        static float32_t phzerror = 0.0;

        float32_t ai;
        float32_t bi;
        float32_t aq;
        float32_t bq;
        float32_t det;
        float32_t Sin;
        float32_t Cos;
        float32_t audio;
        float32_t del_out;
        float32_t corr[2];

        for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++)
        {
          Sin = sinf(phzerror);
          Cos = cosf(phzerror);
          ai = Cos * I_buffer[i];
          bi = Sin * I_buffer[i];
          aq = Cos * Q_buffer[i];
          bq = Sin * Q_buffer[i];

          corr[0] = +ai + bq;
          corr[1] = -bi + aq;

          audio = corr[0];
          p_dac[i] = audio;

          // BEWARE: with a Teensy 3.2 in fixed point, this will really take a lot of time to calculate!
          // use atan2 in that case
          det = atan2f(corr[1], corr[0]);

          del_out = fil_out;
          omega2 = omega2 + g2 * det;
          if (omega2 < omega_min) omega2 = omega_min;
          else if (omega2 > omega_max) omega2 = omega_max;
          fil_out = g1 * det + omega2;
          phzerror = phzerror + del_out;

          // wrap round 2PI, modulus
          while (phzerror >= 2 * PI) phzerror -= 2.0 * PI;
          while (phzerror < 0.0) phzerror += 2.0 * PI;
        }
        break;
      } //SYNCAM

      //-------------------------------------------------------
  }

  // LMS automatic notch filter to eliminate annoying birdies

  // Automatic noise reduction
  // Variable-leak LMS algorithm
  // taken from (c) Warren Pratts wdsp library 2016
  // GPLv3 licensed
#define ANR_DLINE_SIZE 256 //256 //512 //2048 funktioniert nicht, 128 & 256 OK                 // dline_size
  static const int ANR_taps =     32; //64;                       // taps
  static const int ANR_delay =    16; //16;                       // delay
  static const int ANR_dline_size = ANR_DLINE_SIZE;
  static const int ANR_buff_size = AUDIO_BLOCK_SAMPLES;
  //static int ANR_position = 0;
  static const float32_t ANR_two_mu =   0.001;   //0.0001                  // two_mu --> "gain"
  static const float32_t ANR_gamma =    0.1;                      // gamma --> "leakage"
  static float32_t ANR_lidx =     120.0;                      // lidx
  static const float32_t ANR_lidx_min = 0.0;                      // lidx_min
  static const float32_t ANR_lidx_max = 200.0;                      // lidx_max
  static float32_t ANR_ngamma =   0.001;                      // ngamma
  static const float32_t ANR_den_mult = 6.25e-10;                   // den_mult
  static const float32_t ANR_lincr =    1.0;                      // lincr
  static const float32_t ANR_ldecr =    3.0;                     // ldecr
  static int ANR_mask = ANR_dline_size - 1;
  static int ANR_in_idx = 0;
  static float32_t ANR_d [ANR_DLINE_SIZE];
  static float32_t ANR_w [ANR_DLINE_SIZE];

  if (ANR_on == 1) {
    // variable leak LMS algorithm for automatic notch or noise reduction
    // (c) Warren Pratt wdsp library 2016
    int i, j, idx;
    float32_t c0, c1;
    float32_t y, error, sigma, inv_sigp;
    float32_t nel, nev;

    for (i = 0; i < ANR_buff_size; i++)
    {
      ANR_d[ANR_in_idx] = p_dac[i];

      y = 0;
      sigma = 0;

      for (j = 0; j < ANR_taps; j++)
      {
        idx = (ANR_in_idx + j + ANR_delay) & ANR_mask;
        y += ANR_w[j] * ANR_d[idx];
        sigma += ANR_d[idx] * ANR_d[idx];
      }
      inv_sigp = 1.0 / (sigma + 1e-10);
      error = ANR_d[ANR_in_idx] - y;

      p_dac[i] = error;
      //if (ANR_notch) float_buffer_R[i] = error; // NOTCH FILTER
      //else  float_buffer_R[i] = y; // NOISE REDUCTION

      if ((nel = error * (1.0 - ANR_two_mu * sigma * inv_sigp)) < 0.0) nel = -nel;
      if ((nev = ANR_d[ANR_in_idx] - (1.0 - ANR_two_mu * ANR_ngamma) * y - ANR_two_mu * error * sigma * inv_sigp) < 0.0) nev = -nev;
      if (nev < nel) {
        if ((ANR_lidx += ANR_lincr) > ANR_lidx_max) ANR_lidx = ANR_lidx_max;
        else if ((ANR_lidx -= ANR_ldecr) < ANR_lidx_min) ANR_lidx = ANR_lidx_min;
      }
      ANR_ngamma = ANR_gamma * (ANR_lidx * ANR_lidx) * (ANR_lidx * ANR_lidx) * ANR_den_mult;

      c0 = 1.0 - ANR_two_mu * ANR_ngamma;
      c1 = ANR_two_mu * error * inv_sigp;

      for (j = 0; j < ANR_taps; j++)
      {
        idx = (ANR_in_idx + j + ANR_delay) & ANR_mask;
        ANR_w[j] = c0 * ANR_w[j] + c1 * ANR_d[idx];
      }
      ANR_in_idx = (ANR_in_idx + ANR_mask) & ANR_mask;
    }
  }
  queue_dac.playBuffer();

  return micros() - time_start;
}
