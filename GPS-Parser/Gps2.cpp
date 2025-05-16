#include "Gps2.h"
#include <iostream>

UBXNAVRELPOSNED parse_ubx_nav_relposned(const std::vector<uint8_t>& payload) {
    UBXNAVRELPOSNED relpos;
    if (payload.size() >= 40) { // UBX-NAV-RELPOSNED payload er 40 bytes
        try {
            // Ekstrakt felt fra payload (lille-endian)
            int32_t relPosN = *reinterpret_cast<const int32_t*>(&payload[8]); // cm
            int32_t relPosE = *reinterpret_cast<const int32_t*>(&payload[12]); // cm
            int32_t relPosD = *reinterpret_cast<const int32_t*>(&payload[16]); // cm
            int32_t relPosLength = *reinterpret_cast<const int32_t*>(&payload[20]); // cm
            int32_t relPosHeading = *reinterpret_cast<const int32_t*>(&payload[24]); // deg * 1e-5
            int8_t relPosHPN = *reinterpret_cast<const int8_t*>(&payload[28]); // 0.1 mm
            int8_t relPosHPE = *reinterpret_cast<const int8_t*>(&payload[29]); // 0.1 mm
            int8_t relPosHPD = *reinterpret_cast<const int8_t*>(&payload[30]); // 0.1 mm
            int8_t relPosHPLength = *reinterpret_cast<const int8_t*>(&payload[31]); // 0.1 mm
            uint32_t flags = *reinterpret_cast<const uint32_t*>(&payload[36]);

            relpos.relPosN = relPosN / 100.0 + relPosHPN * 0.0001; // m
            relpos.relPosE = relPosE / 100.0 + relPosHPE * 0.0001; // m
            relpos.relPosD = relPosD / 100.0 + relPosHPD * 0.0001; // m
            relpos.relPosLength = relPosLength / 100.0 + relPosHPLength * 0.0001; // m
            relpos.relPosHeading = relPosHeading / 100000.0; // deg
            relpos.carrSoln = (flags >> 6) & 0x03; // Bits 6-7
            relpos.isMoving = (flags & (1 << 3)) != 0; // Bit 3
            relpos.valid = true;
        }
        catch (...) {
            relpos.valid = false;
        }
    }
    return relpos;
}

void read_gps2(const std::string& port, DWORD baud_rate) {
    HANDLE hSerial = CreateFileA(("\\\\.\\" + port).c_str(), GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hSerial == INVALID_HANDLE_VALUE) {
        std::cerr << "Failed to open " << port << ": " << GetLastError() << std::endl;
        return;
    }

    DCB dcbSerialParams = { 0 };
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
    if (!GetCommState(hSerial, &dcbSerialParams)) {
        std::cerr << "Failed to get COMM state for " << port << std::endl;
        CloseHandle(hSerial);
        return;
    }

    dcbSerialParams.BaudRate = baud_rate;
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity = NOPARITY;
    if (!SetCommState(hSerial, &dcbSerialParams)) {
        std::cerr << "Failed to set COMM state for " << port << std::endl;
        CloseHandle(hSerial);
        return;
    }

    COMMTIMEOUTS timeouts = { 0 };
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 50;
    timeouts.ReadTotalTimeoutMultiplier = 10;
    if (!SetCommTimeouts(hSerial, &timeouts)) {
        std::cerr << "Failed to set timeouts for " << port << std::endl;
        CloseHandle(hSerial);
        return;
    }

    std::cout << "Connected to " << port << std::endl;

    std::vector<uint8_t> buffer;
    uint8_t data[256];
    DWORD bytesRead;
    while (true) {
        if (ReadFile(hSerial, data, sizeof(data), &bytesRead, NULL)) {
            if (bytesRead > 0) {
                buffer.insert(buffer.end(), data, data + bytesRead);
                while (buffer.size() >= 2 && !(buffer[0] == 0xB5 && buffer[1] == 0x62)) {
                    buffer.erase(buffer.begin());
                }
                if (buffer.size() >= 8) { // Minimum UBX header + lengde
                    uint16_t payload_length = (buffer[5] << 8) | buffer[4];
                    if (buffer.size() >= payload_length + 8) { // Full melding
                        if (buffer[2] == 0x01 && buffer[3] == 0x3C) { // NAV-RELPOSNED
                            std::vector<uint8_t> payload(buffer.begin() + 6, buffer.begin() + 6 + payload_length);
                            UBXNAVRELPOSNED relpos = parse_ubx_nav_relposned(payload);
                            if (relpos.valid) {
                                std::lock_guard<std::mutex> lock(data_mutex);
                                latest_relposned = relpos;
                            }
                        }
                        buffer.erase(buffer.begin(), buffer.begin() + payload_length + 8);
                    }
                }
            }
        }
        else {
            std::cerr << "Error reading from " << port << ": " << GetLastError() << std::endl;
            Sleep(1000);
        }
    }

    CloseHandle(hSerial);
}