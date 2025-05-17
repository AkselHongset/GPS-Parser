#ifndef GPS1_H
#define GPS1_H

#include "Common.h"

void read_gps1(const std::string& port, DWORD baud_rate);
NMEAGGA parse_gga(const std::string& sentence);

// Global HANDLE for GPS1 slik at RTCM-tråden kan skrive til den
extern HANDLE gps1_handle;

#endif // GPS1_H