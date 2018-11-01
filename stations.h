
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
           {638500, USB, "Cesky R"}, 
  /*  3 */ {1215000, SYNCAM, "G Absolute Radio"},
  /*  4 */ {4810000, SYNCAM, "Armenia Radio"},
  /*  5 */ {5970000, SYNCAM, "China Radio Intl"},
  /*  6 */ {6040000, SYNCAM, "Radio Romania Intl"},
  /*  7 */ {9420000, SYNCAM, "Greece"},
  /*  8 */ {9995500, SYNCAM, "Russian time signal"}
};
