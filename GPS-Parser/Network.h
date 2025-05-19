#ifndef NETWORK_H
#define NETWORK_H

#include "Common.h"
#include <ws2tcpip.h>
#include <string>
#include <queue>
#include <mutex>

#pragma comment(lib, "ws2_32.lib")

// Deklarer globale variabler som extern
extern std::queue<std::vector<char>> rtcm_queue;
extern std::mutex rtcm_queue_mutex;

// Funksjoner
std::string calculate_nmea_checksum(const std::string& sentence);
void send_to_agopengps(const Settings& settings);
void fetch_rtcm_udp(HANDLE gps1_handle, int rtcm_port);
void write_rtcm_to_gps1(HANDLE gps1_handle);

#endif // NETWORK_H