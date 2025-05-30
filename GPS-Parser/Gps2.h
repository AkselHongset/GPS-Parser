#ifndef GPS2_H
#define GPS2_H

#include "Common.h"
#include <vector>
#include <string>

// Funksjon for å parse UBX-NAV-RELPOSNED meldinger
UBXNAVRELPOSNED parse_ubx_nav_relposned(const std::vector<uint8_t>& payload);

// Funksjon for å parse UBX-NAV-VELNED meldinger
void parse_ubx_nav_velned(const std::vector<uint8_t>& payload, UBXNAVRELPOSNED& relpos);

// Funksjon for å lese data fra GPS2
void read_gps2(const std::string& port, DWORD baud_rate);

// Global HANDLE for GPS2
extern HANDLE gps2_handle;

#endif // GPS2_H