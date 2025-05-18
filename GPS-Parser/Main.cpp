#include "Common.h"
#include "Display.h"
#include "Gps1.h"
#include "Gps2.h"
#include "Network.h"
#include <iostream>
#include <thread>
#include <vector>
#include <stdexcept>

// Funksjon for å validere COM-port
void validate_com_port(const std::string& port) {
    HANDLE hSerial = CreateFileA(("\\\\.\\" + port).c_str(), GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hSerial == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("Failed to open COM port " + port + ": Error " + std::to_string(GetLastError()));
    }
    CloseHandle(hSerial);
}

// Funksjon for å lese innstillinger fra settings.ini
Settings load_settings() {
    Settings settings;
    char buffer[256];

    if (!GetPrivateProfileStringA("GPS", "GPS1_Port", "", buffer, sizeof(buffer), ".\\settings.ini")) {
        throw std::runtime_error("Failed to read GPS1_Port from settings.ini");
    }
    settings.gps1_port = buffer;
    if (settings.gps1_port.empty()) {
        throw std::runtime_error("GPS1_Port is empty in settings.ini");
    }

    if (!GetPrivateProfileStringA("GPS", "GPS2_Port", "", buffer, sizeof(buffer), ".\\settings.ini")) {
        throw std::runtime_error("Failed to read GPS2_Port from settings.ini");
    }
    settings.gps2_port = buffer;
    if (settings.gps2_port.empty()) {
        throw std::runtime_error("GPS2_Port is empty in settings.ini");
    }

    if (!GetPrivateProfileStringA("GPS", "BaudRate", "", buffer, sizeof(buffer), ".\\settings.ini")) {
        throw std::runtime_error("Failed to read BaudRate from settings.ini");
    }
    try {
        int baud = std::stoi(buffer);
        switch (baud) {
        case 9600: settings.baud_rate = CBR_9600; break;
        case 19200: settings.baud_rate = CBR_19200; break;
        case 38400: settings.baud_rate = CBR_38400; break;
        case 57600: settings.baud_rate = CBR_57600; break;
        case 115200: settings.baud_rate = CBR_115200; break;
        default:
            throw std::runtime_error("Invalid BaudRate: " + std::string(buffer));
        }
    }
    catch (const std::exception&) {
        throw std::runtime_error("Invalid BaudRate format in settings.ini: " + std::string(buffer));
    }

    if (!GetPrivateProfileStringA("GPS", "DisplayIntervalMs", "", buffer, sizeof(buffer), ".\\settings.ini")) {
        throw std::runtime_error("Failed to read DisplayIntervalMs from settings.ini");
    }
    try {
        int interval = std::stoi(buffer);
        if (interval <= 0) {
            throw std::runtime_error("DisplayIntervalMs must be positive: " + std::string(buffer));
        }
        settings.display_interval_ms = static_cast<DWORD>(interval);
    }
    catch (const std::exception&) {
        throw std::runtime_error("Invalid DisplayIntervalMs format in settings.ini: " + std::string(buffer));
    }

    if (!GetPrivateProfileStringA("GPS", "AntennaSeparation", "2.0", buffer, sizeof(buffer), ".\\settings.ini")) {
        throw std::runtime_error("Failed to read AntennaSeparation from settings.ini");
    }
    try {
        settings.antenna_separation = std::stod(buffer);
        if (settings.antenna_separation <= 0) {
            throw std::runtime_error("AntennaSeparation must be positive: " + std::string(buffer));
        }
    }
    catch (const std::exception&) {
        throw std::runtime_error("Invalid AntennaSeparation format in settings.ini: " + std::string(buffer));
    }

    if (!GetPrivateProfileStringA("UDP", "IP", "192.168.1.100", buffer, sizeof(buffer), ".\\settings.ini")) {
        throw std::runtime_error("Failed to read UDP IP from settings.ini");
    }
    settings.udp_ip = buffer;
    if (settings.udp_ip.empty()) {
        throw std::runtime_error("UDP IP is empty in settings.ini");
    }

    if (!GetPrivateProfileStringA("UDP", "Port", "9999", buffer, sizeof(buffer), ".\\settings.ini")) {
        throw std::runtime_error("Failed to read UDP Port from settings.ini");
    }
    try {
        settings.udp_port = std::stoi(buffer);
        if (settings.udp_port <= 0) {
            throw std::runtime_error("UDP Port must be positive: " + std::string(buffer));
        }
    }
    catch (const std::exception&) {
        throw std::runtime_error("Invalid UDP Port format in settings.ini: " + std::string(buffer));
    }

    if (!GetPrivateProfileStringA("UDP", "RTCM_UDPPort", "2233", buffer, sizeof(buffer), ".\\settings.ini")) {
        throw std::runtime_error("Failed to read RTCM_UDPPort from settings.ini");
    }
    try {
        settings.rtcm_udp_port = std::stoi(buffer);
        if (settings.rtcm_udp_port <= 0) {
            throw std::runtime_error("RTCM_UDPPort must be positive: " + std::string(buffer));
        }
    }
    catch (const std::exception&) {
        throw std::runtime_error("Invalid RTCM_UDPPort format in settings.ini: " + std::string(buffer));
    }

    return settings;
}

int main() {
    try {
        Settings settings = load_settings();

        std::cout << "Validating COM ports..." << std::endl;
        validate_com_port(settings.gps1_port);
        std::cout << "COM port " << settings.gps1_port << " is available" << std::endl;
        validate_com_port(settings.gps2_port);
        std::cout << "COM port " << settings.gps2_port << " is available" << std::endl;

        gps1_handle = CreateFileA(("\\\\.\\" + settings.gps1_port).c_str(),
            GENERIC_READ | GENERIC_WRITE, 0, NULL,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (gps1_handle == INVALID_HANDLE_VALUE) {
            throw std::runtime_error("Failed to open GPS1 port " + settings.gps1_port + ": Error " + std::to_string(GetLastError()));
        }

        DCB dcbSerialParams = { 0 };
        dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
        if (!GetCommState(gps1_handle, &dcbSerialParams)) {
            CloseHandle(gps1_handle);
            throw std::runtime_error("Failed to get COMM state for " + settings.gps1_port);
        }
        dcbSerialParams.BaudRate = settings.baud_rate;
        dcbSerialParams.ByteSize = 8;
        dcbSerialParams.StopBits = ONESTOPBIT;
        dcbSerialParams.Parity = NOPARITY;
        if (!SetCommState(gps1_handle, &dcbSerialParams)) {
            CloseHandle(gps1_handle);
            throw std::runtime_error("Failed to set COMM state for " + settings.gps1_port);
        }

        COMMTIMEOUTS timeouts = { 0 };
        timeouts.ReadIntervalTimeout = 50;
        timeouts.ReadTotalTimeoutConstant = 50;
        timeouts.ReadTotalTimeoutMultiplier = 10;
        if (!SetCommTimeouts(gps1_handle, &timeouts)) {
            CloseHandle(gps1_handle);
            throw std::runtime_error("Failed to set timeouts for " + settings.gps1_port);
        }

        std::vector<std::thread> threads;
        threads.emplace_back(read_gps1, settings.gps1_port, settings.baud_rate);
        threads.emplace_back(read_gps2, settings.gps2_port, settings.baud_rate);
        threads.emplace_back(display_data, settings.display_interval_ms);
        threads.emplace_back(send_to_agopengps, settings);
        // Use lambda without capturing gps1_handle, as it's global
        threads.emplace_back([rtcm_port = settings.rtcm_udp_port]() {
            fetch_rtcm_udp(gps1_handle, rtcm_port);
            });

        std::cout << "Press Enter to exit..." << std::endl;
        std::cin.get();
        running = false;

        for (auto& t : threads) {
            if (t.joinable()) {
                t.join();
            }
        }

        if (gps1_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(gps1_handle);
        }
        if (gps2_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(gps2_handle);
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        running = false;
        Sleep(1000);
        if (gps1_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(gps1_handle);
        }
        if (gps2_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(gps2_handle);
        }
        std::cerr << "Press Enter to exit..." << std::endl;
        std::cin.get();
        return 1;
    }

    return 0;
}