/*********************************************************************
   miniSDR v3

   By DD4WH and Frank Bösing

   GPL V3

   Teensy 3.6
   =================
   ADC : Pin A2 / 16
   DAC : Pin A21 DAC0

   I2C-SCL : PIN 19
   I2C-SDA : PIN 18
   =================

   Teensy 3.2
   (some functionality not enabled)
   =================
   ADC : Pin A2 / 16
   DAC : Pin A14 DAC
   BCLK: Pin 9
   MCLK: Pin 11
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
/*

   TODO:
   Display:
    - S-Meter
    - Spectrum display optional
   Menu:
    - Filter-Bandwidth, # of taps
    - AGC settings

 */
/********************************************************************/
#include "stations.h"
#include <util/crc16.h>
#include "ui.h"
#include "src/CMSIS_5/arm_math.h"
#include "src/CMSIS_5/arm_const_structs.h"
#include "src/Audio/Audio.h"


AudioInputAnalog adc1;                   //xy=429,313
AudioAmplifier amp_adc;                     //xy=583,313
AudioRecordQueue queue_adc;              //xy=754,310

AudioPlayQueue queue_dac;                //xy=442,233
AudioFilterBiquad biquad1_dac;               //xy=613,234
AudioFilterBiquad biquad2_dac;               //xy=613,234
AudioAmplifier amp_dac;                     //xy=764,230
AudioOutputAnalog dac1;                  //xy=914,227

AudioConnection patchCord1(adc1, amp_adc);
AudioConnection patchCord2(queue_dac, biquad1_dac);
AudioConnection patchCord3(amp_adc, queue_adc);
AudioConnection patchCord4a(biquad1_dac, biquad2_dac);
AudioConnection patchCord4(biquad2_dac, amp_dac);
AudioConnection patchCord5(amp_dac, dac1);

#define AUDIOMEMORY     20
#define _IF             6000        // intermediate frequency
#define SAMPLE_RATE     (_IF * 4)   // new Audio-Library sample rate
#define CORR_FACT       (AUDIO_SAMPLE_RATE_EXACT / SAMPLE_RATE) // Audio-Library correction factor
#define FREQ_MIN        16000   // Hz
#define FREQ_MAX        9999500 // Hz

#define I2S_FREQ_MAX    18000000
#define I2S0_TCR2_DIV   0

int filter_bandwidth_default = 2800;
const float AGC_start = 0.25f;
const float AGC_Max   = 40.0f;

settings_t settings;
int mode              = AM;
int ANR_on            = 0;         // off: 0, automatic notch filter:1, automatic noise reduction: 2
int AGC_on            = 1;         // automatic gain control ON/OFF
int Spectrum_on       = 1;         // spektrum display on/off
int filter_bandwidth  = filter_bandwidth_default;
int freq;
float AGC_val         = AGC_start;      // agc actual value

const uint8_t clk_errcmp  = 1;       // en-/disable clk-error compensation

const uint32_t FIR_AM_num_taps  = 102;
const uint32_t FIR_SSB_num_taps = 86;
const uint32_t MAX_num_taps = FIR_AM_num_taps > FIR_SSB_num_taps ? FIR_AM_num_taps : FIR_SSB_num_taps;
arm_fir_instance_q15 FIR_I;
arm_fir_instance_q15 FIR_Q;
q15_t FIR_I_state [MAX_num_taps + AUDIO_BLOCK_SAMPLES];
q15_t FIR_Q_state [MAX_num_taps + AUDIO_BLOCK_SAMPLES];
int16_t FIR_AM_coeffs[FIR_AM_num_taps];

//int16_t FIR_I_coeffs[FIR_num_taps];
//int16_t FIR_Q_coeffs[FIR_num_taps];

// +45°, 86 taps, Fc=1.33kHz, BW=1.92kHz, Kaiser beta = 1.8, raised cosine 0.88
const int16_t FIR_I_coeffs[FIR_SSB_num_taps] = { -3, -16, -33, -52, -63, -63, -48, -25, -5, -4, -30, -85, -157, -221, -253, -233, -163, -66, 17, 41, -20, -164, -349, -506, -561, -468, -231, 83, 365, 496, 395, 65, -392, -791, -915, -586, 264, 1549, 3035, 4388, 5262, 5400, 4710, 3298, 1448, -458, -2043, -3030, -3312, -2964, -2203, -1314, -559, -108, -8, -185, -496, -780, -922, -878, -676, -399, -141, 25, 72, 16, -93, -196, -250, -237, -167, -72, 15, 68, 80, 57, 18, -17, -36, -35, -19, 3, 21, 29, 28, 20};
// -45°, 86 taps, Fc=1.33kHz, BW=1.92kHz, Kaiser beta = 1.8, raised cosine 0.88
const int16_t FIR_Q_coeffs[FIR_SSB_num_taps] = {20, 28, 29, 21, 3, -19, -35, -36, -17, 18, 57, 80, 68, 15, -72, -167, -237, -250, -196, -93, 16, 72, 25, -141, -399, -676, -878, -922, -780, -496, -185, -8, -108, -559, -1314, -2203, -2964, -3312, -3030, -2043, -458, 1448, 3298, 4710, 5400, 5262, 4388, 3035, 1549, 264, -586, -915, -791, -392, 65, 395, 496, 365, 83, -231, -468, -561, -506, -349, -164, -20, 41, 17, -66, -163, -233, -253, -221, -157, -85, -30, -4, -5, -25, -48, -63, -63, -52, -33, -16, -3};

// AM, 66 taps, cutoff = 4kHz, 24ksps sample rate
//const int16_t FIR_AM_coeffs[FIR_AM_num_taps] = {-2,8,22,24,-2,-39,-44,10,77,72,-33,-141,-106,82,234,135,-177,-367,-146,337,544,113,-610,-788,4,1088,1155,-327,-2125,-1949,1486,6886,10973,10973,6886,1486,-1949,-2125,-327,1155,1088,4,-788,-610,113,544,337,-146,-367,-177,135,234,82,-106,-141,-33,72,77,10,-44,-39,-2,24,22,8,-2};
// narrow filters for CW (morse signals) and digital signals (DCF77 etc.)
// centre your signal of interest at 800Hz !!!
// +45°, 86 taps, Fc=800Hz, BW=400Hz, Kaiser beta = 1.8, raised cosine 0.88
//const int16_t FIR_I_coeffs[FIR_SSB_num_taps] = {6,1,-5,-12,-18,-22,-24,-21,-14,-3,12,29,44,54,54,41,11,-40,-112,-204,-314,-435,-558,-673,-769,-832,-851,-818,-724,-567,-349,-77,236,575,919,1247,1537,1767,1920,1983,1949,1817,1593,1290,925,521,103,-306,-680,-1000,-1250,-1419,-1504,-1505,-1431,-1294,-1108,-891,-660,-432,-222,-39,107,215,283,315,315,292,252,204,153,106,66,35,14,3,-1,2,8,15,21,26,29,29,27,23};
// -45°, 86 taps, Fc=800Hz, BW=400Hz, Kaiser beta = 1.8, raised cosine 0.88
//const int16_t FIR_Q_coeffs[FIR_SSB_num_taps] = {23,27,29,29,26,21,15,8,2,-1,3,14,35,66,106,153,204,252,292,315,315,283,215,107,-39,-222,-432,-660,-891,-1108,-1294,-1431,-1505,-1504,-1419,-1250,-1000,-680,-306,103,521,925,1290,1593,1817,1949,1983,1920,1767,1537,1247,919,575,236,-77,-349,-567,-724,-818,-851,-832,-769,-673,-558,-435,-314,-204,-112,-40,11,41,54,54,44,29,12,-3,-14,-21,-24,-22,-18,-12,-5,1,6};

unsigned long time_needed     = 0;
unsigned long time_needed_max = 0;

void calc_demod_filter(void);
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
	settings.lastMode = mode;
	settings.lastNotch = ANR_on;
	settings.lastFilterBandwidth = filter_bandwidth;

	//If tuned to a saved station, overwrite its settings:
	if (settings.lastFreq == 0) {
		int i = settings.lastStation;
		settings.station[i].mode = mode;
		settings.station[i].notch = ANR_on;
		settings.station[i].filterBandwidth = filter_bandwidth;
	}

	settings.crc = settingsCrc();
	eeprom_write_block(&settings, 0, sizeof(settings_t));
	Serial.println("Settings saved");
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
	Serial.printf("Last Bandwidth: %d\n", settings.lastFilterBandwidth);
	int i = 0;
	while (i < MAX_EMEMORY) {
		if (settings.station[i].freq != 0)
			Serial.printf("%c: %8d\t%s\t%s\n", 'a' + i, (int) settings.station[i].freq, modestr[settings.station[i].mode], settings.station[i].sname);
		i++;
	}
	Serial.println();
#endif

}
//-------------------------------------------------------

void loadLastSettings(void) {

	if (settings.lastFreq != 0) {
		freq = settings.lastFreq;
		mode = settings.lastMode;
		ANR_on = settings.lastNotch;
		filter_bandwidth = settings.lastFilterBandwidth;
	} else {
		int i = settings.lastStation;
		freq = settings.station[i].freq;
		mode = settings.station[i].mode;
		ANR_on = settings.station[i].notch;
		if (settings.station[i].filterBandwidth == 0)
			filter_bandwidth = filter_bandwidth_default;
		else
			filter_bandwidth = settings.station[i].filterBandwidth;
	}
	calc_demod_filter();
}
//-------------------------------------------------------
void calc_demod_filter(void) {
	calc_FIR_coeffs (FIR_AM_coeffs, FIR_AM_num_taps, (float32_t)filter_bandwidth, 70, 0, 0.0, (float32_t)SAMPLE_RATE);
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
		float freq = b * i / (float)d;
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
float pdb_freq_actual;

void tune() {
	float freq_actual;
	float freq_diff;
	float pdb_freq;

	if (freq < FREQ_MIN) freq = FREQ_MIN;
	if (freq > FREQ_MAX) freq = FREQ_MAX;

	AudioNoInterrupts();
	PDB0_SC = 0;
#if !(defined(__MK66FX1M0__))
	if (mode == SYNCAM) mode = AM;
#endif

	// BCLK:
	freq_actual = setI2S_freq(freq + _IF);
	freq_diff = freq_actual - _IF - freq;

	// PDB (ADC + DAC) :
	if (clk_errcmp) {
		pdb_freq = freq_diff * 4.0 + SAMPLE_RATE;     //*4.0 due to I/Q
	} else {
		pdb_freq = SAMPLE_RATE;
	}
	pdb_freq_actual = setPDB_freq(pdb_freq);

	showFreq();
	init_FIR();
	biquad2_dac.setNotch(0, pdb_freq_actual / 8.0 * CORR_FACT, 15.0); // eliminates some birdy

#if 1
	Serial.printf("Tune: %.1fkHz\n", (float)freq / 1000.0f);
	//Serial.printf("BCLK Soll: %f ist: %f Diff: %fHz\n", freq, freq_actual - _IF, freq_diff);
	//Serial.printf("PDB  Soll: %f ist: %f Diff: %fHz\n", pdb_freq, pdb_freq_actual, pdb_freq_actual - pdb_freq);
	//Serial.println();
#endif

	AudioProcessorUsageMaxReset();
	time_needed_max = 0;
	AudioInterrupts();
}

//-------------------------------------------------------

void setup()   {
	AudioMemory(AUDIOMEMORY);
	PDB0_SC = 0; //Stop PDB

	Serial.println("Minimal SDR");
	Serial.printf("F_CPU: %dMHz F_BUS: %dMHz\n", (int) (F_CPU / 1000000), (int) (F_BUS / 1000000));
	Serial.printf("IF: %dHz Samplerate: %dHz\n", _IF, _IF * 4 );
	Serial.println();

	initUI();
	initEEPROM();
	initI2S();

	amp_adc.gain(AGC_val); //amplifier after ADC (is this needed?)

	// Linkwitz-Riley: gain = {0.54, 1.3, 0.54, 1.3}
	// notch is very good, even with small Q
	// we have to restrict our audio bandwidth to at least 0.5 * IF,
	//  const float cutoff_freq = _IF * 0.5 * CORR_FACT;
	const float cutoff_freq = _IF * 0.9 * CORR_FACT;

	biquad1_dac.setLowpass(0,  cutoff_freq, 0.54);
	//biquad1_dac.setLowpass(1, cutoff_freq, 1.3);
	//biquad1_dac.setLowpass(2, cutoff_freq, 0.54);
	//biquad1_dac.setLowpass(3, cutoff_freq, 1.3);
	//biquad2_dac.setLowpass(0, cutoff_freq, 0.54);
	//biquad2_dac.setLowpass(1, cutoff_freq, 1.3);
	//biquad2_dac.setLowpass(2, cutoff_freq, 0.54);
	//biquad2_dac.setNotch(0, SAMPLE_RATE / 8 * CORR_FACT, 15.0); // eliminates some birdy

	amp_dac.gain(1.0); //amplifier before DAC

	init_FIR();

	AudioProcessorUsageMaxReset();
	loadLastSettings();

	tune();
	queue_adc.begin();
}

//-------------------------------------------------------

void printAudioLibStatistics(void) {
#if 1
	const int audiolibStatisticsInterval = 10000; //ms
	int t = millis();
	static int audiolibLastStatistic = t;

	if (t - audiolibLastStatistic > audiolibStatisticsInterval) {
		audiolibLastStatistic = t;
		//Serial.printf("Runtime: %d seconds\n", (t - timestart) / 1000);
		Serial.printf("AudioMemoryUsageMax: %d Blocks\n", AudioMemoryUsageMax());
		float demod = (time_needed_max / 1e6) * 100 / (AUDIO_BLOCK_SAMPLES / pdb_freq_actual);
		Serial.printf("AudioLibrary: %.2f%% + Demodulation: %.2f%% = %.2f%%\n", AudioProcessorUsageMax(), demod, AudioProcessorUsageMax() + demod );
		Serial.printf("AGC: %.2f\n", AGC_val);
		Serial.println();
		//minval = maxval = 0;
	}
#endif
}

//-------------------------------------------------------

void loop() {
	UI();
	printAudioLibStatistics();
	//asm ("wfi");
	time_needed = demodulation();
	if (time_needed > time_needed_max) time_needed_max = time_needed;
}

//-------------------------------------------------------
#define AGCBUF_SIZE 25 //One Entry per Audio Packet
void AGC(int16_t * block) {

	if (!AGC_on) return;

	static int16_t agc_buffer[AGCBUF_SIZE];
	static int agc_idx = AGCBUF_SIZE;

	uint32_t data;
	uint32_t * p;
	p = (uint32_t *) block;
	int minv = 32767;
	int maxv = -minv;

	for (int i = 0; i < AUDIO_BLOCK_SAMPLES / 2; i++) {
		data = *__SIMD32(p)++;// Load next two samples in a single access
		(void)__SSUB16(maxv, data);// Parallel comparison of max and new samples
		maxv = __SEL(maxv, data);   // Select max on each 16-bits half
		(void)__SSUB16(data, minv);// Parallel comparison of new samples and min
		minv = __SEL(minv, data);   // Select min on each 16-bits half
	}
	// Now we have maximum on even samples on low halfword of max
	// and maximum on odd samples on high halfword
	// look for max between halfwords 1 & 0 by comparing on low halfword
	(void)__SSUB16(maxv, maxv >> 16);
	maxv = __SEL(maxv, maxv >> 16);// Select max on low 16-bits
	(void)__SSUB16(minv >> 16, minv);// look for min between halfwords 1 & 0 by comparing on low halfword
	minv = __SEL(minv, minv >> 16);// Select min on low 16-bits

	minv = abs(minv);
	maxv = abs(maxv);
	(void)__SSUB16(maxv, minv);
	uint16_t absmax = __SEL(maxv, minv);

	agc_buffer[--agc_idx] = absmax;
	if (agc_idx < 0) agc_idx = AGCBUF_SIZE;

	int m = 0;
	for (int i = 0; i < AGCBUF_SIZE; i++) m += agc_buffer[i];
	int d = m / AGCBUF_SIZE;

	const float x = 16000;
	float f = x / d;
	if (f > 1.3) {
		float fagc = AGC_val + (AGC_val * f / 1500);
		if (fagc < AGC_Max) {
			AGC_val = fagc;
			amp_adc.gain(AGC_val);
		}
	}

	else if (AGC_val > 0.1) {
		if (f < 0.6) {
			AGC_val = AGC_val - (AGC_val * f / 50);
			amp_adc.gain(AGC_val);
		}
		else if (f < 0.7) {
			AGC_val = AGC_val - (AGC_val * f / 200);
			amp_adc.gain(AGC_val);
		}
		else if (f < 0.8) {
			AGC_val = AGC_val - (AGC_val * f / 2000);
			amp_adc.gain(AGC_val);
		}
		else if (f < 0.9) {
			AGC_val = AGC_val - (AGC_val * f / 4000);
			amp_adc.gain(AGC_val);
		}
	}

} //AGC
//-------------------------------------------------------

unsigned long demodulation(void) {

	if ( queue_dac.available() == false ) return 0;
	if ( queue_adc.available() < 1 ) return 0;

	//unsigned long time_start = micros();
	unsigned long time_start;
	int16_t I_buffer[AUDIO_BLOCK_SAMPLES];
	int16_t Q_buffer[AUDIO_BLOCK_SAMPLES];

	{
		int16_t * p_adc;
		p_adc = queue_adc.readBuffer();

		showSpectrum(p_adc);
		time_start = micros();
		AGC(p_adc);

		/*
		   I/Q conversion
		   frequency translation without multiplication
		   - multiply the signal from the ADC with a Cosine for I channel
		   - multiply the signal from the ADC with a Sine   for Q channel
		   if IF == sample rate / 4,
		   the Cosine is {1, 0, -1, 0, 1, 0, -1, 0 . . . .}
		   and the Sine is {0, 1, 0, -1, 0, 1, 0, -1 . . . .}
		 */

		for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i += 4) {

			I_buffer[i]     = p_adc[i + 0];
			I_buffer[i + 1] = 0;
			I_buffer[i + 2] = -p_adc[i + 2];
			I_buffer[i + 3] = 0;

			Q_buffer[i + 0] = 0;
			Q_buffer[i + 1] = p_adc[i + 1];
			Q_buffer[i + 3] = -p_adc[i + 3];
			Q_buffer[i + 2] = 0;

		}

		queue_adc.freeBuffer();
	}

	/*
	   Here, we have to filter separately the I & Q channel with a linear phase filter
	   so a FIR filter with symmetrical coefficients should be used
	   Do not use an IIR filter
	 */
	{
		q15_t I_FIR_out[AUDIO_BLOCK_SAMPLES];
		q15_t Q_FIR_out[AUDIO_BLOCK_SAMPLES];

		// arm_fir_q15(&FIR_I, I_buffer, I_buffer2, AUDIO_BLOCK_SAMPLES);
		// arm_fir_q15(&FIR_Q, Q_buffer, Q_buffer2, AUDIO_BLOCK_SAMPLES);
		arm_fir_fast_q15(&FIR_I, I_buffer, I_FIR_out, AUDIO_BLOCK_SAMPLES);
		arm_fir_fast_q15(&FIR_Q, Q_buffer, Q_FIR_out, AUDIO_BLOCK_SAMPLES);

		//arm_copy_q15(I_FIR_out, I_buffer, AUDIO_BLOCK_SAMPLES);
		//arm_copy_q15(Q_FIR_out, Q_buffer, AUDIO_BLOCK_SAMPLES);
		memcpy(I_buffer, I_FIR_out, sizeof(I_buffer));
		memcpy(Q_buffer, Q_FIR_out, sizeof(Q_buffer));
	}
	/*
	   - demodulation
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
	case SYNCAM:
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
#if defined(__MK64FX512__) || defined(__MK66FX1M0__)
	case SYNCAM: {
		// synchronous AM demodulation - the one with the PLL ;-)
		// code adapted from the wdsp library by Warren Pratt, GNU GPLv3

		// SYNCAM:
		static const float32_t omegaN = 400.0;     // PLL is able to grab a carrier that is not more than omegaN Hz away
		static const float32_t zeta = 0.45;     // the higher, the faster the PLL, the lower, the more stable the carrier is grabbed
		static const float32_t omega_min = 2.0 * PI * -4000.0 / SAMPLE_RATE;      // absolute minimum frequency the PLL can correct for
		static const float32_t omega_max = 2.0 * PI * 4000.0 / SAMPLE_RATE;     // absolute maximum frequency the PLL can correct for
		static const float32_t g1 = 1.0 - exp(-2.0 * omegaN * zeta / SAMPLE_RATE);     // used inside the algorithm
		static const float32_t g2 = -g1 + 2.0 * (1 - exp(-omegaN * zeta / SAMPLE_RATE) * cosf(omegaN / SAMPLE_RATE * sqrtf(1.0 - zeta * zeta)));       // used inside the algorithm
		static float32_t fil_out = 0.0f;
		static float32_t omega2 = 0.0f;
		static float32_t phzerror = 0.0f;

		float32_t ai;
		float32_t bi;
		float32_t aq;
		float32_t bq;
		float32_t det;
		float32_t Sin;
		float32_t Cos;
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

			p_dac[i] = corr[0];

			// BEWARE: with a Teensy 3.2, this will really take a lot of time to calculate!
			// use a optimized atan2 in that case
			det = atan2f(corr[1], corr[0]);
			//det = atan2(corr[1], corr[0]);

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
	}     //SYNCAM
#endif
		//-------------------------------------------------------
	}


#if defined(__MK64FX512__) || defined(__MK66FX1M0__)
	// LMS automatic notch filter to eliminate annoying birdies

	// Automatic noise reduction
	// Variable-leak LMS algorithm
	// taken from (c) Warren Pratts wdsp library 2016
	// GPLv3 licensed

	if (ANR_on > 0) {
		// variable leak LMS algorithm for automatic notch or noise reduction
		// (c) Warren Pratt wdsp library 2016

		// LMS automatic notch filter
#define ANR_DLINE_SIZE 512 //256 //512 //2048 funktioniert nicht, 128 & 256 OK                 // dline_size
		static const int ANR_taps =     64; //64;                       // taps
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

			//p_dac[i] = error;
			if (ANR_on == 1) p_dac[i] = error; // NOTCH FILTER
			else p_dac[i] = y;  // NOISE REDUCTION

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
#endif

	queue_dac.playBuffer();
	return micros() - time_start;
}

//-------------------------------------------------------

#define PIH             (PI / 2)
#define TPI             (PI * 2)

void calc_FIR_coeffs (int16_t* coeffs, int numCoeffs, float32_t fc, float32_t Astop, int type, float dfc, float Fsamprate)
// pointer to coefficients variable, no. of coefficients to calculate, frequency where it happens, stopband attenuation in dB,
// filter type, half-filter bandwidth (only for bandpass and notch)
{ // modified by WMXZ and DD4WH after
	// Wheatley, M. (2011): CuteSDR Technical Manual. www.metronix.com, pages 118 - 120, FIR with Kaiser-Bessel Window
	// assess required number of coefficients by
	//     numCoeffs = (Astop - 8.0) / (2.285 * TPI * normFtrans);
	// selecting high-pass, numCoeffs is forced to an even number for better frequency response

	int ii, jj;
	float32_t Beta;
	float32_t izb;
	float fcf = fc;
	int nc = numCoeffs;

	fc = fc / Fsamprate;
	dfc = dfc / Fsamprate;

	// calculate Kaiser-Bessel window shape factor beta from stop-band attenuation
	if (Astop < 20.96)
		Beta = 0.0;
	else if (Astop >= 50.0)
		Beta = 0.1102 * (Astop - 8.71);
	else
		Beta = 0.5842 * powf((Astop - 20.96), 0.4) + 0.07886 * (Astop - 20.96);

	izb = Izero (Beta);

	switch (type) {

	case 0:    // low pass filter
		fcf = fc * 2.0;
		nc =  numCoeffs;
		break;

	case 1:    // high-pass filter
		fcf = -fc;
		nc =  2 * (numCoeffs / 2);
		break;

	case 2:
	case 3:   // band-pass filter
		fcf = dfc;
		nc =  2 * (numCoeffs / 2);   // maybe not needed
		break;

	case 4:    // Hilbert transform
	{
		nc =  2 * (numCoeffs / 2);
		// clear coefficients
		for (ii = 0; ii < 2 * (nc - 1); ii++) coeffs[ii] = 0;
		// set real delay
		coeffs[nc] = 1;

		// set imaginary Hilbert coefficients
		for (ii = 1; ii < (nc + 1); ii += 2)
		{
			if (2 * ii == nc) continue;
			float x = (float)(2 * ii - nc) / (float)nc;
			float w = Izero(Beta * sqrtf(1.0f - x * x)) / izb;     // Kaiser window
			coeffs[2 * ii + 1] = 32767 * (1.0f / (PIH * (float)(ii - nc / 2)) * w);
		}
		return;
	}
	default:
		return;
	}

	for (ii = -nc, jj = 0; ii < nc; ii += 2, jj++)
	{
		float x = (float)ii / (float)nc;
		float w = Izero(Beta * sqrtf(1.0f - x * x)) / izb; // Kaiser window
		coeffs[jj] = fcf * m_sinc(ii, fcf) * w * 32767;
	}

	switch (type) {

	case 1:
		coeffs[nc / 2] += 1;
		break;

	case 2:
		for (jj = 0; jj < nc + 1; jj++) coeffs[jj] *= 2.0f * cosf(PIH * (2 * jj - nc) * fc);
		break;

	case 3:
		for (jj = 0; jj < nc + 1; jj++) coeffs[jj] *= -2.0f * cosf(PIH * (2 * jj - nc) * fc);
		coeffs[nc / 2] += 1;
		break;
	};
} // END calc_FIR_coeffs

float m_sinc(int m, float fc)
{ // fc is f_cut/(Fsamp/2)
	// m is between -M and M step 2

	if (m == 0) return 1.0f;
	float x = m * PIH;
	return sinf(x * fc) / (fc * x);
}

float32_t Izero (float32_t x)
{
	static const float32_t errorlimit = 1e-9;
	float32_t x2 = x / 2.0;
	float32_t summe = 1.0;
	float32_t ds = 1.0;
	float32_t di = 1.0;
	float32_t tmp;
	do {
		tmp = x2 / di;
		tmp *= tmp;
		ds *= tmp;
		summe += ds;
		di += 1.0;
	}   while (ds >= errorlimit * summe);
	return (summe);
} // END Izero

void init_FIR(void) {
	switch (mode) {
	case USB:
		arm_fir_init_q15(&FIR_I, FIR_SSB_num_taps, (q15_t *)FIR_I_coeffs, &FIR_I_state[0], AUDIO_BLOCK_SAMPLES);
		arm_fir_init_q15(&FIR_Q, FIR_SSB_num_taps, (q15_t *)FIR_Q_coeffs, &FIR_Q_state[0], AUDIO_BLOCK_SAMPLES);
		break;
	case LSB:
		arm_fir_init_q15(&FIR_I, FIR_SSB_num_taps, (q15_t *)FIR_I_coeffs, &FIR_I_state[0], AUDIO_BLOCK_SAMPLES);
		arm_fir_init_q15(&FIR_Q, FIR_SSB_num_taps, (q15_t *)FIR_Q_coeffs, &FIR_Q_state[0], AUDIO_BLOCK_SAMPLES);
		break;

	case AM:
		arm_fir_init_q15(&FIR_I, FIR_AM_num_taps, (q15_t *)FIR_AM_coeffs, &FIR_I_state[0], AUDIO_BLOCK_SAMPLES);
		arm_fir_init_q15(&FIR_Q, FIR_AM_num_taps, (q15_t *)FIR_AM_coeffs, &FIR_Q_state[0], AUDIO_BLOCK_SAMPLES);
		break;
	case SYNCAM:
		arm_fir_init_q15(&FIR_I, FIR_AM_num_taps, (q15_t *)FIR_AM_coeffs, &FIR_I_state[0], AUDIO_BLOCK_SAMPLES);
		arm_fir_init_q15(&FIR_Q, FIR_AM_num_taps, (q15_t *)FIR_AM_coeffs, &FIR_Q_state[0], AUDIO_BLOCK_SAMPLES);
		break;
	default:
		arm_fir_init_q15(&FIR_I, FIR_AM_num_taps, (q15_t *)FIR_AM_coeffs, &FIR_I_state[0], AUDIO_BLOCK_SAMPLES);
		arm_fir_init_q15(&FIR_Q, FIR_AM_num_taps, (q15_t *)FIR_AM_coeffs, &FIR_Q_state[0], AUDIO_BLOCK_SAMPLES);
		break;
	}
}
