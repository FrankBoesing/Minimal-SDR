
enum  { LSB, USB, AM, SYNCAM };
const char modestr[][5] = {"LSB", "USB", "AM", "SAM"};

#define MAX_EMEMORY 26
#define EEPROM_STORAGE_VERSION 1

typedef struct {
  float freq;
  uint8_t mode;
  uint8_t notch;
  char sname[24];
} __attribute__((packed)) station_t;

typedef struct {
  uint8_t lastStation;
  float lastFreq;
  uint8_t lastMode;
  uint8_t lastNotch;
  station_t station[MAX_EMEMORY];
  uint8_t version;
  uint16_t crc;
}  __attribute__((packed)) settings_t;

const settings_t settings_default =
{
  /* lastStation */ 0,
  /* lastFreq    */ 0.0,
  /* lastMode    */ SYNCAM,
  /* lastNotch   */ 0,
  {
    /*  a */ { 234000, SYNCAM, 1, "RTL"},
    /*  b */ { 531000, SYNCAM, 1, "Jil FM, Algeria"},
    /*  c */ { 639000, SYNCAM, 1, "Cesky R"},
    /*  d */ { 540000, USB   , 1, "Hungary"},
    /*  e */ { 621000, SYNCAM, 1, "Belgium"},
    /*  f */ {1350000, SYNCAM, 1, "I AM Radio"},
    /*  g */ { 810000, SYNCAM, 1, "(Northern) Macedonia"},
    /*  h */ {1215000, SYNCAM, 1, "G Absolute Radio"},
    /*  i */ {4810000, SYNCAM, 1, "Armenia Radio"},
    /*  j */ {5025000, SYNCAM, 1, "Radio Rebelde, Kuba"},
    /*  k */ {5970000, SYNCAM, 1, "China Radio Intl"},
    /*  l */ {5980000, SYNCAM, 1, "Turkey"},
    /*  m */ {6040000, SYNCAM, 1, "Radio Romania Intl"},
    /*  n */ {9420000, SYNCAM, 1, "Greece"},
    /*  o */ {0, 0, 0, ""},
    /*  p */ {0, 0, 0, ""},
    /*  q */ {0, 0, 0, ""},
    /*  r */ {0, 0, 0, ""},
    /*  s */ {0, 0, 0, ""},
    /*  t */ {0, 0, 0, ""},
    /*  u */ {0, 0, 0, ""},
    /*  v */ {0, 0, 0, ""},
    /*  w */ {0, 0, 0, ""},
    /*  x */ { 129100, SYNCAM, 1, "DCF49"},
    /*  y */ {9995500, SYNCAM, 0, "Russian time signal"},
    /*  z */ {  77500, AM    , 0, "DCF77"}
  },
  /* version    */ EEPROM_STORAGE_VERSION,
  /* crc        */ 0x4711
};
