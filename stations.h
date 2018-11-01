
enum  { LSB, USB, AM, SYNCAM };
const char modestr[][5] = {"LSB", "USB", "AM", "SAM"};

struct station_t {
  float freq;
  uint8_t mode;
  char sname[24];
};

const station_t stations_default[] =
{
  /*  0 */ {234000, SYNCAM, "RTL"},
  /*  1 */ {531000, SYNCAM, "Jil FM, Algeria"},
  /*  2 */ {639000, SYNCAM, "Cesky R"},
           {540000, USB, "Hungary"}, 
  /*  3 */ {621000, SYNCAM, "Belgium"},
  /*  3 */ {1350000, SYNCAM, "I AM Radio"},
  /*  3 */ {810000, SYNCAM, "(Northern) Macedonia"},
  /*  3 */ {1215000, SYNCAM, "G Absolute Radio"},
  /*  4 */ {4810000, SYNCAM, "Armenia Radio"},
  /*  4 */ {5025000, SYNCAM, "Radio Rebelde, Kuba"},
  /*  5 */ {5970000, SYNCAM, "China Radio Intl"},  
  /*  5 */ {5980000, SYNCAM, "Turkey"},
  /*  6 */ {6040000, SYNCAM, "Radio Romania Intl"},
  /*  7 */ {9420000, SYNCAM, "Greece"},
  /*  8 */ {9995500, SYNCAM, "Russian time signal"}
};
