#include "Gps1.h"
#include <iostream>
#include <sstream>

// Global HANDLE for GPS1
HANDLE gps1_handle = INVALID_HANDLE_VALUE;

NMEAGGA parse_gga(const std::string& sentence) {
    NMEAGGA gga;
    if (!sentence.empty() && (sentence.find("$GPGGA") == 0 || sentence.find("$GNGGA") == 0)) {
        std::vector<std::string> fields;
        std::stringstream ss(sentence);
        std::string field;
        while (std::getline(ss, field, ',')) {
            fields.push_back(field);
        }
        if (fields.size() >= 10) { // Minimum felt for GGA
            try {
                gga.timestamp = fields[1];
                gga.latitude = fields[2].empty() ? 0.0 : std::stod(fields[2]) / 100.0; // DDMM.MMMM
                gga.lat_dir = fields[3].empty() ? ' ' : fields[3][0];
                gga.longitude = fields[4].empty() ? 0.0 : std::stod(fields[4]) / 100.0; // DDDMM.MMMM
                gga.lon_dir = fields[5].empty() ? ' ' : fields[5][0];
                gga.gps_qual = fields[6].empty() ? 0 : std::stoi(fields[6]);
                gga.num_sats = fields[7].empty() ? 0 : std::stoi(fields[7]);
                gga.altitude = fields[9].empty() ? 0.0 : std::stod(fields[9]);
                gga.valid = true;
            }
            catch (...) {
                gga.valid = false;
            }
        }
    }
    return gga;
}

void read_gps1(const std::string& port, DWORD baud_rate) {
    gps1_handle = CreateFileA(("\\\\.\\" + port).c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (gps1_handle == INVALID_HANDLE_VALUE) {
        std::cerr << "Failed to open " << port << ": " << GetLastError() << std::endl;
        return;
    }

    DCB dcbSerialParams = { 0 };
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
    if (!GetCommState(gps1_handle, &dcbSerialParams)) {
        std::cerr << "Failed to get COMM state for " << port << std::endl;
        CloseHandle(gps1_handle);
        gps1_handle = INVALID_HANDLE_VALUE;
        return;
    }

    dcbSerialParams.BaudRate = baud_rate;
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity = NOPARITY;
    if (!SetCommState(gps1_handle, &dcbSerialParams)) {
        std::cerr << "Failed to set COMM state for " << port << std::endl;
        CloseHandle(gps1_handle);
        gps1_handle = INVALID_HANDLE_VALUE;
        return;
    }

    COMMTIMEOUTS timeouts = { 0 };
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 50;
    timeouts.ReadTotalTimeoutMultiplier = 10;
    if (!SetCommTimeouts(gps1_handle, &timeouts)) {
        std::cerr << "Failed to set timeouts for " << port << std::endl;
        CloseHandle(gps1_handle);
        gps1_handle = INVALID_HANDLE_VALUE;
        return;
    }

    std::cout << "Connected to " << port << std::endl;

    std::string buffer;
    char data[256];
    DWORD bytesRead;
    while (true) {
        if (ReadFile(gps1_handle, data, sizeof(data) - 1, &bytesRead, NULL)) {
            if (bytesRead > 0) {
                data[bytesRead] = '\0';
                buffer += data;
                size_t pos;
                while ((pos = buffer.find('\n')) != std::string::npos) {
                    std::string line = buffer.substr(0, pos);
                    buffer.erase(0, pos + 1);
                    if (line.find("$GPGGA") == 0 || line.find("$GNGGA") == 0) {
                        NMEAGGA gga = parse_gga(line);
                        if (gga.valid) {
                            std::lock_guard<std::mutex> lock(data_mutex);
                            latest_gga = gga;
                        }
                    }
                }
            }
        }
        else {
            std::cerr << "Error reading from " << port << ": " << GetLastError() << std::endl;
            Sleep(1000);
        }
    }

    CloseHandle(gps1_handle);
    gps1_handle = INVALID_HANDLE_VALUE;
}