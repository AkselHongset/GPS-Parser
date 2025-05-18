#include "Display.h"
#include <iostream>
#include <iomanip>
#include <chrono>

static std::chrono::system_clock::time_point last_rtcm_time;
static std::mutex rtcm_mutex;

void update_rtcm_timestamp() {
    std::lock_guard<std::mutex> lock(rtcm_mutex);
    last_rtcm_time = std::chrono::system_clock::now();
}

void display_data(DWORD interval_ms) {
    while (running) {
        system("cls");
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        tm time_info;
        localtime_s(&time_info, &time_t_now);
        std::cout << "GPS Data Display - " << std::put_time(&time_info, "%Y-%m-%d %H:%M:%S") << std::endl;
        std::cout << std::string(50, '-') << std::endl;

        {
            std::lock_guard<std::mutex> lock(data_mutex);
            std::cout << "GPS1 (NMEA GGA):" << std::endl;
            if (latest_gga.valid) {
                std::cout << "  Time: " << latest_gga.timestamp << std::endl;
                std::cout << "  Latitude: " << std::fixed << std::setprecision(7) << latest_gga.latitude << " " << latest_gga.lat_dir << std::endl;
                std::cout << "  Longitude: " << std::fixed << std::setprecision(7) << latest_gga.longitude << " " << latest_gga.lon_dir << std::endl;
                std::cout << "  Altitude: " << std::fixed << std::setprecision(1) << latest_gga.altitude << " M" << std::endl;
                std::cout << "  Fix Quality: " << latest_gga.gps_qual << " (";
                switch (latest_gga.gps_qual) {
                case 0: std::cout << "Invalid"; break;
                case 1: std::cout << "GPS fix"; break;
                case 2: std::cout << "DGPS fix"; break;
                case 4: std::cout << "RTK Fixed"; break;
                case 5: std::cout << "RTK Float"; break;
                default: std::cout << "Unknown";
                }
                std::cout << ")" << std::endl;
                std::cout << "  Number of Satellites: " << latest_gga.num_sats << std::endl;
            }
            else {
                std::cout << "  Waiting for GGA data..." << std::endl;
            }

            std::cout << std::string(50, '-') << std::endl;

            std::cout << "GPS2 (UBX-NAV-RELPOSNED):" << std::endl;
            if (latest_relposned.valid) {
                std::cout << "  Relative Position North: " << std::fixed << std::setprecision(3) << latest_relposned.relPosN << " m" << std::endl;
                std::cout << "  Relative Position East: " << std::fixed << std::setprecision(3) << latest_relposned.relPosE << " m" << std::endl;
                std::cout << "  Relative Position Down: " << std::fixed << std::setprecision(3) << latest_relposned.relPosD << " m" << std::endl;
                std::cout << "  Relative Position Length: " << std::fixed << std::setprecision(3) << latest_relposned.relPosLength << " m" << std::endl;
                std::cout << "  Relative Position Heading: " << std::fixed << std::setprecision(2) << latest_relposned.relPosHeading << " deg" << std::endl;
                std::cout << "  Carrier Solution: " << static_cast<int>(latest_relposned.carrSoln) << " (";
                switch (latest_relposned.carrSoln) {
                case 0: std::cout << "None"; break;
                case 1: std::cout << "Float"; break;
                case 2: std::cout << "Fixed"; break;
                default: std::cout << "Unknown";
                }
                std::cout << ")" << std::endl;
                std::cout << "  Is Moving Baseline: " << (latest_relposned.isMoving ? "Yes" : "No") << std::endl;
            }
            else {
                std::cout << "  Waiting for RELPOSNED data..." << std::endl;
            }

            std::cout << std::string(50, '-') << std::endl;

            std::cout << "RTCM Corrections:" << std::endl;
            {
                std::lock_guard<std::mutex> lock(rtcm_mutex);
                if (last_rtcm_time.time_since_epoch().count() == 0) {
                    std::cout << "  No RTCM data received" << std::endl;
                }
                else {
                    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_rtcm_time).count();
                    std::cout << "  Last received: " << elapsed << " seconds ago" << std::endl;
                }
            }
        }

        Sleep(interval_ms);
    }
}