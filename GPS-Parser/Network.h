#ifndef NETWORK_H
#define NETWORK_H

#include "Common.h"
#include <ws2tcpip.h> // For inet_pton
#include <string>

#pragma comment(lib, "ws2_32.lib") // Linker Winsock-biblioteket

// Funksjon for å beregne NMEA-sjekksum
std::string calculate_nmea_checksum(const std::string& sentence);

// Funksjon for å sende data til AgOpenGPS over UDP
void send_to_agopengps(const Settings& settings);

// Funksjon for å hente RTCM-korreksjoner fra rtk2go.com og sende til GPS1
void fetch_rtcm_corrections(const Settings& settings, HANDLE gps1_handle);

#endif // NETWORK_H