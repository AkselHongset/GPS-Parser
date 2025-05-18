#include "Network.h"
#include "Display.h"
#include <iostream>
#include <sstream>
#include <iomanip>

// Funksjon for å beregne NMEA-sjekksum
std::string calculate_nmea_checksum(const std::string& sentence) {
    uint8_t checksum = 0;
    for (size_t i = 0; i < sentence.length(); ++i) {
        checksum ^= sentence[i];
    }
    std::stringstream ss;
    ss << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << static_cast<int>(checksum);
    return ss.str();
}

void send_to_agopengps(const Settings& settings) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed: " << WSAGetLastError() << std::endl;
        return;
    }

    SOCKET udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udpSocket == INVALID_SOCKET) {
        std::cerr << "Failed to create UDP socket: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return;
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(settings.udp_port);
    if (inet_pton(AF_INET, settings.udp_ip.c_str(), &serverAddr.sin_addr) <= 0) {
        std::cerr << "Invalid IP address: " << settings.udp_ip << std::endl;
        closesocket(udpSocket);
        WSACleanup();
        return;
    }
    // Set socket to non-blocking
    u_long mode = 1;
    ioctlsocket(udpSocket, FIONBIO, &mode);

    while (running) {
        std::string paogi_message;
        {
            std::lock_guard<std::mutex> lock(data_mutex);
            if (latest_gga.valid && latest_relposned.valid && latest_gga.gps_qual > 0) {
                // Convert to DDMM.MMMM
                double lat = latest_gga.latitude;
                double lat_ddmm = (int)lat * 100 + (lat - (int)lat) * 60;
                double lon = latest_gga.longitude;
                double lon_ddmm = (int)lon * 100 + (lon - (int)lon) * 60;

                std::stringstream ss;
                ss << "$PAOGI,"
                    << latest_gga.timestamp << ","
                    << std::fixed << std::setprecision(4) << lat_ddmm << "," << latest_gga.lat_dir << ","
                    << std::fixed << std::setprecision(4) << lon_ddmm << "," << latest_gga.lon_dir << ","
                    << latest_gga.gps_qual << ","
                    << latest_gga.num_sats << ","
                    << std::fixed << std::setprecision(1) << latest_gga.hdop << ","
                    << std::fixed << std::setprecision(1) << latest_gga.altitude << ","
                    << "0.0," // Age of differential
                    << "0.0," // Speed
                    << std::fixed << std::setprecision(1) << latest_relposned.relPosHeading << "," // Heading
                    << "0.0," // Roll
                    << "0.0," // Pitch
                    << std::fixed << std::setprecision(1) << latest_relposned.relPosHeading << "," // Yaw
                    << std::fixed << std::setprecision(3) << latest_relposned.relPosLength << "," // Distance
                    << "T"; // Tilt
                paogi_message = ss.str();
                paogi_message += "*" + calculate_nmea_checksum(paogi_message.substr(1)) + "\r\n";
            }
        }

        if (!paogi_message.empty()) {
            int len = static_cast<int>(paogi_message.length());
            int bytesSent = sendto(udpSocket, paogi_message.c_str(), len, 0,
                (sockaddr*)&serverAddr, sizeof(serverAddr));
            if (bytesSent == SOCKET_ERROR) {
                std::cerr << "Failed to send PAOGI: " << WSAGetLastError() << std::endl;
            }
        }

        Sleep(100); // 10 Hz update rate
    }

    closesocket(udpSocket);
    WSACleanup();
}

void fetch_rtcm_udp(HANDLE gps1_handle) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed: " << WSAGetLastError() << std::endl;
        return;
    }

    SOCKET udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udpSocket == INVALID_SOCKET) {
        std::cerr << "Failed to create UDP socket: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return;
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(2233);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(udpSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "Bind failed: " << WSAGetLastError() << std::endl;
        closesocket(udpSocket);
        WSACleanup();
        return;
    }

    // Set socket to non-blocking
    u_long mode = 1;
    ioctlsocket(udpSocket, FIONBIO, &mode);

    std::cout << "Listening for RTCM corrections on UDP port 2233..." << std::endl;

    char buffer[2048]; // Increased buffer size
    sockaddr_in senderAddr;
    int senderAddrSize = sizeof(senderAddr);
    while (running) {
        int bytesReceived = recvfrom(udpSocket, buffer, sizeof(buffer), 0,
            (sockaddr*)&senderAddr, &senderAddrSize);
        if (bytesReceived > 0) {
            if (gps1_handle != INVALID_HANDLE_VALUE) {
                DWORD bytesWritten;
                if (WriteFile(gps1_handle, buffer, bytesReceived, &bytesWritten, nullptr)) {
                    update_rtcm_timestamp();
                }
                else {
                    std::cerr << "Failed to write RTCM to GPS1: " << GetLastError() << std::endl;
                }
            }
            else {
                std::cerr << "Invalid GPS1 handle for RTCM data" << std::endl;
            }
        }
        else if (bytesReceived == SOCKET_ERROR) {
            int error = WSAGetLastError();
            if (error != WSAEWOULDBLOCK) {
                std::cerr << "Error receiving UDP data: " << error << std::endl;
            }
        }
        Sleep(10); // Prevent tight loop in non-blocking mode
    }

    closesocket(udpSocket);
    WSACleanup();
}