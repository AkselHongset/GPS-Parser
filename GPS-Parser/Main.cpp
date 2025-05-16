#include "Common.h"
#include "Display.h"
#include "Gps1.h"
#include "Gps2.h"
#include <iostream>
#include <thread>
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

    // Les GPS1_Port
    if (!GetPrivateProfileStringA("GPS", "GPS1_Port", "", buffer, sizeof(buffer), ".\\settings.ini")) {
        throw std::runtime_error("Failed to read GPS1_Port from settings.ini");
    }
    settings.gps1_port = buffer;
    if (settings.gps1_port.empty()) {
        throw std::runtime_error("GPS1_Port is empty in settings.ini");
    }

    // Les GPS2_Port
    if (!GetPrivateProfileStringA("GPS", "GPS2_Port", "", buffer, sizeof(buffer), ".\\settings.ini")) {
        throw std::runtime_error("Failed to read GPS2_Port from settings.ini");
    }
    settings.gps2_port = buffer;
    if (settings.gps2_port.empty()) {
        throw std::runtime_error("GPS2_Port is empty in settings.ini");
    }

    // Les BaudRate
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
                throw std::runtime_error("Invalid BaudRate: " + std::string(buffer) + ". Must be 9600, 19200, 38400, 57600, or 115200");
        }
    } catch (const std::exception&) {
        throw std::runtime_error("Invalid BaudRate format in settings.ini: " + std::string(buffer));
    }

    // Les DisplayIntervalMs
    if (!GetPrivateProfileStringA("GPS", "DisplayIntervalMs", "", buffer, sizeof(buffer), ".\\settings.ini")) {
        throw std::runtime_error("Failed to read DisplayIntervalMs from settings.ini");
    }
    try {
        int interval = std::stoi(buffer);
        if (interval <= 0) {
            throw std::runtime_error("DisplayIntervalMs must be positive: " + std::string(buffer));
        }
        settings.display_interval_ms = static_cast<DWORD>(interval);
    } catch (const std::exception&) {
        throw std::runtime_error("Invalid DisplayIntervalMs format in settings.ini: " + std::string(buffer));
    }

    return settings;
}

int main() {
    try {
        // Last innstillinger fra settings.ini
        Settings settings = load_settings();

        // Valider COM-porter
        std::cout << "Validating COM ports..." << std::endl;
        validate_com_port(settings.gps1_port);
        std::cout << "COM port " << settings.gps1_port << " is available" << std::endl;
        validate_com_port(settings.gps2_port);
        std::cout << "COM port " << settings.gps2_port << " is available" << std::endl;

        // Start tråder med innstillinger fra INI-filen
        std::thread gps1_thread(read_gps1, settings.gps1_port, settings.baud_rate);
        std::thread gps2_thread(read_gps2, settings.gps2_port, settings.baud_rate);
        std::thread display_thread(display_data, settings.display_interval_ms);

        gps1_thread.detach();
        gps2_thread.detach();
        display_thread.detach();

        std::cout << "Press Enter to exit..." << std::endl;
        std::cin.get();
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        std::cerr << "Press Enter to exit..." << std::endl;
        std::cin.get();
        return 1;
    }

    return 0;
}