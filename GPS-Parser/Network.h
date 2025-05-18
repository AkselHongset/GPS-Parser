#ifndef NETWORK_H
#define NETWORK_H

#include "Common.h"
#include <ws2tcpip.h>
#include <string>

#pragma comment(lib, "ws2_32.lib")

// Funksjon for å beregne NMEA-sjekksum
std::string calculate_nmea_checksum(const std::string& sentence);

// Funksjon for å sende data til AgOpenGPS over UDP
void send_to_agopengps(const Settings& settings);

// Funksjon for å hente RTCM-korreksjoner fra UDP port 2233
void fetch_rtcm_udp(HANDLE gps1_handle);

#endif // NETWORK_H