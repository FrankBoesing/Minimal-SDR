
enum  { LSB, USB, AM, SYNCAM };
const char modestr[][5] = {"LSB", "USB", "AM", "SAM"};

#define MAX_EMEMORY 26
#define EEPROM_STORAGE_VERSION 1

typedef struct {
  float freq;
  uint8_t mode;
  char sname[24];
} __attribute__((packed)) station_t;

typedef struct {
  uint8_t lastStation;
  float lastFreq;
  uint8_t lastMode;
  station_t station[MAX_EMEMORY];
  uint8_t version;
  uint16_t crc;
}  __attribute__((packed)) settings_t;

const settings_t settings_default =
{
  /* lastStation */ 0,
  /* lastFreq    */ 0.0,
  /* lastMode    */ SYNCAM,
  {
    /*  a */ {234000, SYNCAM, "RTL"},
    /*  b */ {531000, SYNCAM, "Jil FM, Algeria"},
    /*  c */ {639000, SYNCAM, "Cesky R"},
    /*  d */ {540000, USB, "Hungary"},
    /*  e */ {621000, SYNCAM, "Belgium"},
    /*  f */ {1350000, SYNCAM, "I AM Radio"},
    /*  g */ {810000, SYNCAM, "(Northern) Macedonia"},
    /*  h */ {1215000, SYNCAM, "G Absolute Radio"},
    /*  i */ {4810000, SYNCAM, "Armenia Radio"},
    /*  j */ {5025000, SYNCAM, "Radio Rebelde, Kuba"},
    /*  k */ {5970000, SYNCAM, "China Radio Intl"},
    /*  l */ {5980000, SYNCAM, "Turkey"},
    /*  m */ {6040000, SYNCAM, "Radio Romania Intl"},
    /*  n */ {9420000, SYNCAM, "Greece"},
    /*  o */ {9995500, SYNCAM, "Russian time signal"},
    /*  p */ {0, 0, ""},
    /*  q */ {0, 0, ""},
    /*  r */ {0, 0, ""},
    /*  s */ {0, 0, ""},
    /*  t */ {0, 0, ""},
    /*  u */ {0, 0, ""},
    /*  v */ {0, 0, ""},
    /*  w */ {0, 0, ""},
    /*  x */ {0, 0, ""},
    /*  y */ {0, 0, ""},
    /*  z */ {0, 0, ""}
  },
  /* version    */ EEPROM_STORAGE_VERSION,
  /* crc        */ 0x4711
};
