#ifndef GPS2_H
#define GPS2_H

#include "Common.h"

void read_gps2(const std::string& port, DWORD baud_rate);
UBXNAVRELPOSNED parse_ubx_nav_relposned(const std::vector<uint8_t>& payload);

#endif // GPS2_H