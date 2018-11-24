#ifndef sdr_stations_h_
#define sdr_stations_h_

enum  { SYNCAM, AM, LSB, USB, CW };
const char modestr[][5] = {"SAM", "AM", "LSB", "USB", "CW"};

#define MAX_EMEMORY 26
#define EEPROM_STORAGE_VERSION 7

typedef struct {
	int freq;
	uint8_t mode;
	uint8_t notch;
	uint16_t filterBandwidth; //0 = default
	char sname[33];
} __attribute__((packed)) station_t;

typedef struct {
	uint8_t lastStation;
	int lastFreq;
	uint8_t lastMode;
	uint8_t lastNotch;
	uint16_t lastFilterBandwidth;
	station_t station[MAX_EMEMORY];
	uint8_t version;
	uint16_t crc;
}  __attribute__((packed)) settings_t;

const settings_t settings_default =
{
	/* lastStation */ 0,
	/* lastFreq    */ 0,
	/* lastMode    */ SYNCAM,
	/* lastNotch   */ 0,
	/* lastFilterBandwidth */ 2800,
	{
		/*  a */ { 183000, SYNCAM, 1, 0, "Europe 1 FRA"},
		/*  b */ { 216000, SYNCAM, 1, 0, "Radio Monte Carlo FRA"},
		/*  c */ { 234000, SYNCAM, 1, 0, "RTLuxembourg FRA"},
		/*  d */ { 531000, SYNCAM, 1, 0,  "Jil FM, Algeria"},
		/*  e */ { 639000, SYNCAM, 1, 0,  "Cesky R"},
		/*  f */ { 540000, SYNCAM, 1, 0,  "MR 1-Kossuth Radio HUN"},
		/*  g */ { 576000, SYNCAM, 1, 0,  "Radio Horizont BEL"},
		/*  h */ { 585000, SYNCAM, 1, 0,  "Radio Nacional ESP"},
		/*  j */ { 711000, SYNCAM, 1, 0,  "France Info"},
		/*  i */ { 621000, SYNCAM, 1, 0,  "RTBF international BEL"},
		/*  k */ { 810000, SYNCAM, 1, 0,  "Radio Skopje 1 MKD"},
		/*  l */ {1008000, SYNCAM, 1, 0,  "GrootNieusradio NL"},
		/*  m */ {1053000, SYNCAM, 1, 0,  "TalkSport GB"},
		/*  n */ {1215000, SYNCAM, 1, 0,  "G Absolute Radio GB"},
		/*  o */ {1350000, SYNCAM, 1, 0,  "I AM Radio"},
		/*  p */ {1467000, SYNCAM, 1, 0,  "Trans World Radio FRA"},
		/*  q */ {1602000, SYNCAM, 1, 0,  "Radio Seagull NL"}, // On a ship in the Wadden Sea
		/*  r */ {4810000, SYNCAM, 1, 0,  "Armenia Radio"},
		/*  s */ {5025000, SYNCAM, 1, 0,  "Radio Rebelde, Kuba"},
		/*  t */ {5970000, SYNCAM, 1, 0,  "China Radio Intl"},
		/*  u */ {5980000, SYNCAM, 1, 0,  "Turkey"},
		/*  v */ {6040000, SYNCAM, 1, 0,  "Radio Romania Intl"},
		/*  w */ {9420000, SYNCAM, 1, 0,  "Greece"},

		/*  x */ { 129100, CW,     0, 600,  "DCF49"},
		/*  y */ {9995500, SYNCAM, 0, 600, "Russian time signal"},
		/*  z */ {  77500, CW,     0, 600,  "DCF77"}
	},
	/* version    */ EEPROM_STORAGE_VERSION,
	/* crc        */ 0x4711
};


typedef struct {
	int fmin, fmax, bandwidth;
	char display[6];
} bands_t;

// http://www.db9ja.de/bandplan-bis30mhz.html
const bands_t bands[] = {
	{    3000,    30000, 2500, "VLF"  }, //BW. 2000Hz??
//LW
	{   30000,   135700, 2500, "LW"   }, //BW. 2500Hz??
	{  135700,   136000,  200, "2200m"}, // Telegrafie, QRSS
	{  136000,   137400,  200, "2200m"}, // Telegrafie
	{  137400,   137600,  200, "2200m"},
	{  137600,   137800,  200, "2200m"}, // sehr langsame Telegrafie
	{  152000,   282000, 5500, "LW"   }, // Broadcast
//MW
	{  300000,   472000,  800, "MW"   }, // BW:800 ??
	{  472000,   526500,  800, "630m" },
	{  526500,  1606500, 9000, "MW"   }, // Broadcast
	{ 1606500,  1810000, 2700, "MW"   }, // BW:2700 ??
	{ 1810000,  1838000,  200, "160m" }, // Telegrafie
	{ 1838000,  1840000,  500, "160m" },
	{ 1840000,  1843000, 2700, "160m" },
	{ 1843000,  2000000, 2700, "160m" },
	{ 2000000,  3000000, 2700, "MW"   },
//80m
	{ 3500000,  3510000,  200, "80m"  }, // Telegrafie
	{ 3510000,  3560000,  200, "80m"  }, // Telegrafie
	{ 3560000,  3580000,  200, "80m"  }, // Telegrafie
	{ 3580000,  3590000,  500, "80m"  }, // Digimode
	{ 3590000,  3600000,  500, "80m"  }, // Digimode, automatische digitale Stationen
	{ 3600000,  3800000, 2700, "80m"  },
//75m
	{ 3900000,  4000000, 9000, "75m"  }, // Broadcast
//60m
	{ 5351500,  5354000,  200, "60m"  }, // CW, Schmalband-Betriebsarten
	{ 5354000,  5366000, 2700, "60m"  }, // alle Schmalband-Betriebsarten inkl.SSB
	{ 5366000,  5366500,   20, "60m"  }, // Weaksignal, narrow Band
//49m broadcast
	{ 5800000,  6200000, 9000, "49m"  },
//40m
	{ 7000000,  7025000,  200, "40m"  }, // bevorzugter CW-Contestbereich
	{ 7025000,  7040000,  200, "40m"  }, // 7030 kHz - CW-QRP Aktivitätszentrum.
	{ 7040000,  7047000,  500, "40m"  }, // Digimode
	{ 7047000,  7050000,  500, "40m"  }, // Digimode, automatisch arbeitende Stationen.
	{ 7050000,  7053000, 2700, "40m"  }, // Digimode, automatisch arbeitende Stationen.
	{ 7060000,  7200000, 2700, "40m"  }, // bevorzugter SSB-Contestbereich.
//41m	(?)
	{ 7200000,  7600000, 9000, "41m"  }, // Broadcast
//31m
	{ 9400000, 10000000, 9000, "31m"  }, //	Broadcast
//30m
	{10100000, 10140000,  200, "30m"  }, //
	{10140000, 10150000,  500, "30m"  }, //

//20m
/*
   {14000000, 14350000,    0, "20m"  }, //
   {18068000, 18168000,   -1, "17m"  }, //
   {21000000, 21450000,   -1, "15m"  }, //
   {24890000, 24990000,   -1, "12m"  }, //
   {28000000, 29700000,   -1, "10m"  }, //
 */

};

#endif
