#ifndef GPS2_H
#define GPS2_H

#include "Common.h"
#include <vector>
#include <string>

// Funksjon for å parse UBX-NAV-RELPOSNED meldinger
UBXNAVRELPOSNED parse_ubx_nav_relposned(const std::vector<uint8_t>& payload);

// Funksjon for å lese data fra GPS2
void read_gps2(const std::string& port, DWORD baud_rate);

#endif // GPS2_H