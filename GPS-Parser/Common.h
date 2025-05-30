#ifndef COMMON_H
#define COMMON_H

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <string>
#include <mutex>
#include <vector>
#include <atomic>
#include <condition_variable>

// Struktur for å lagre NMEA GGA-data
struct NMEAGGA {
    std::string timestamp;
    double latitude;
    char lat_dir;
    double longitude;
    char lon_dir;
    int gps_qual;
    int num_sats;
    double hdop;
    double altitude;
    bool valid;
    NMEAGGA();
};

// Struktur for å lagre UBX-NAV-RELPOSNED-data
struct UBXNAVRELPOSNED {
    double relPosN; // m
    double relPosE; // m
    double relPosD; // m
    double relPosLength; // m
    double relPosHeading; // deg
    double speed_knots; // knots, fra UBX-NAV-VELNED
    uint8_t carrSoln;
    bool isMoving;
    bool valid;
    UBXNAVRELPOSNED();
};

// Struktur for å lagre innstillinger
struct Settings {
    std::string gps1_port;
    std::string gps2_port;
    DWORD baud_rate;
    DWORD display_interval_ms;
    std::string udp_ip;
    int udp_port;
    int rtcm_udp_port;
    double antenna_separation;
};

// Globale variabler
extern NMEAGGA latest_gga;
extern UBXNAVRELPOSNED latest_relposned;
extern std::mutex data_mutex;
extern std::condition_variable data_cv;
extern std::atomic<bool> running;

#endif // COMMON_H