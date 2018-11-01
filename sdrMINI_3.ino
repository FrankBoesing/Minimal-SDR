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

*********************************************************************/


#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>

AudioInputAnalog         adc1;
AudioRecordQueue         queue_adc;
AudioOutputAnalog        dac1;
AudioPlayQueue           queue_dac;
AudioConnection          patchCord1(adc1, queue_adc);
AudioConnection          patchCord2(queue_dac, dac1);

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


#define I2S_FREQ_START  234000 //RTL
//#define I2S_FREQ_START  639000 //
#define AUDIOMEMORY     20
#define _IF             6000        // intermediate frequency
#define SAMPLE_RATE     (_IF * 4)

#define I2S_FREQ_MAX    36000000
#define I2S0_TCR2_DIV   (0)


const char sdrname[] = "Mini - SDR";

enum  { LSB, USB, AM, SYNCAM };
int mode = SYNCAM;

void showFreq(float freq)
{
  //  Serial.print(freq / 1000.0, 4);
  //  Serial.println("kHz");
#if OLED
  const int x = 0;
  const int y = 20;
  const int size = 3;

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


float freq = I2S_FREQ_START;


void tune(float freq) {
  float freq_actual;
  float freq_diff;
  float pdb_freq;
  float pdb_freq_actual;

  // BCLK:
  freq_actual = setI2S_freq(freq + _IF);
  freq_diff = freq_actual - _IF - freq;

  // PDB (ADC + DAC) :
  pdb_freq = freq_diff * 4.0 + SAMPLE_RATE;     //*4.0 due to I/Q
  pdb_freq_actual = setPDB_freq(pdb_freq);

  showFreq(freq);

#if 1
  Serial.println("Tune:");
  Serial.printf("BCLK Soll: %f ist: %f Diff: %fHz\n", freq, freq_actual - _IF, freq_diff);
  Serial.printf("PDB  Soll: %f ist: %f Diff: %fHz\n", pdb_freq, pdb_freq_actual, pdb_freq_actual - pdb_freq);
  Serial.println();
#endif

}



void setup()   {
  AudioMemory(AUDIOMEMORY);
  queue_adc.begin();
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

  Serial.println(sdrname);
  Serial.printf("F_CPU: %dMHz F_BUS: %dMHz\n", (int) (F_CPU / 1000000), (int) (F_BUS / 1000000));
  Serial.printf("IF: %dHz Samplerate: %dHz\n", _IF, _IF * 4 );
  Serial.println();

  tune(freq);
  AudioProcessorUsageMaxReset();
}


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
    Serial.printf("AudioMemoryUsageMax: %d blocks\n", AudioMemoryUsageMax());
    Serial.printf("AudioProcessorUsageMax: %.2f\n", AudioProcessorUsageMax());
  }

#endif
}

void loop() {
  asm ("wfi");
  demodulation(mode);
  printAudioLibStatistics();
}


float32_t I_buffer[AUDIO_BLOCK_SAMPLES];
float32_t Q_buffer[AUDIO_BLOCK_SAMPLES];

void demodulation(int mode) {

  if ( queue_adc.available() < 1 ) return;
  if ( !queue_dac.available() ) return;

  /*
      Read data from input queue
      I/Q conversion
  */


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
  queue_adc.freeBuffer();




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
    case AM: {
        float32_t audio;

        for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
          arm_sqrt_f32 ( I_buffer[i] *  I_buffer[i] + Q_buffer[i] * Q_buffer[i], &audio );
          p_dac[i] = audio;
        }
        break;
      }
    //-------------------------------------------------------
    case SYNCAM: {
        // synchronous AM demodulation - the one with the PLL ;-)
        static const float32_t omegaN = 400.0;
        static const float32_t zeta = 0.65;
        static const float32_t omega_min = 2.0 * PI * - 4000.0 / SAMPLE_RATE;
        static const float32_t omega_max = 2.0 * PI * 4000.0 / SAMPLE_RATE;
        static const float32_t g1 = 1.0 - exp(-2.0 * omegaN * zeta / SAMPLE_RATE);
        static const float32_t g2 = - g1 + 2.0 * (1 - exp(- omegaN * zeta / SAMPLE_RATE) * cosf(omegaN / SAMPLE_RATE * sqrtf(1.0 - zeta * zeta)));

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
  queue_dac.playBuffer();

}
