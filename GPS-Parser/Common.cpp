#include "Common.h"

// Implementasjon av konstruktører
NMEAGGA::NMEAGGA() : timestamp(""), latitude(0.0), lat_dir(' '), longitude(0.0), lon_dir(' '),
                     gps_qual(0), num_sats(0), altitude(0.0), valid(false) {}

UBXNAVRELPOSNED::UBXNAVRELPOSNED() : relPosN(0.0), relPosE(0.0), relPosD(0.0), relPosLength(0.0),
                                     relPosHeading(0.0), carrSoln(0), isMoving(false), valid(false) {}

// Globale variabler
NMEAGGA latest_gga;
UBXNAVRELPOSNED latest_relposned;
std::mutex data_mutex;