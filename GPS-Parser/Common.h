#ifndef COMMON_H
#define COMMON_H

#include <windows.h>
#include <string>
#include <mutex>
#include <vector>

// Struktur for å lagre NMEA GGA-data
struct NMEAGGA {
    std::string timestamp;
    double latitude;
    char lat_dir;
    double longitude;
    char lon_dir;
    int gps_qual;
    int num_sats;
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
    uint8_t carrSoln; // Carrier solution status
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
};

// Globale variabler
extern NMEAGGA latest_gga;
extern UBXNAVRELPOSNED latest_relposned;
extern std::mutex data_mutex;

#endif // COMMON_H