#include "Network.h"
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
    // Initialiser Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed: " << WSAGetLastError() << std::endl;
        return;
    }

    // Opprett UDP-socket
    SOCKET udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udpSocket == INVALID_SOCKET) {
        std::cerr << "Failed to create UDP socket: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return;
    }

    // Sett opp serveradresse
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(settings.udp_port);
    if (inet_pton(AF_INET, settings.udp_ip.c_str(), &serverAddr.sin_addr) <= 0) {
        std::cerr << "Invalid IP address: " << settings.udp_ip << std::endl;
        closesocket(udpSocket);
        WSACleanup();
        return;
    }

    while (true) {
        std::string paogi_message;
        {
            std::lock_guard<std::mutex> lock(data_mutex);

            // Generer $PAOGI-melding hvis både GGA og RELPOSNED er gyldige, og vi har en fix
            if (latest_gga.valid && latest_relposned.valid && latest_gga.gps_qual > 0) {
                // Koordinater i DDMM.MMMM-format
                double lat = latest_gga.latitude * 100.0;
                double lon = latest_gga.longitude * 100.0;

                std::stringstream ss;
                ss << "$PAOGI,"
                    << latest_gga.timestamp << ","
                    << std::fixed << std::setprecision(7) << lat << "," << latest_gga.lat_dir << ","
                    << std::fixed << std::setprecision(7) << lon << "," << latest_gga.lon_dir << ","
                    << latest_gga.gps_qual << ","
                    << latest_gga.num_sats << ","
                    << std::fixed << std::setprecision(1) << latest_gga.hdop << ","
                    << std::fixed << std::setprecision(1) << latest_gga.altitude << ","
                    << "0.0,"
                    << "0.0,"
                    << "0.0,"
                    << "0,"
                    << "0.0,"
                    << std::fixed << std::setprecision(1) << latest_relposned.relPosHeading << ","
                    << "T";
                paogi_message = ss.str();
                paogi_message += "*" + calculate_nmea_checksum(paogi_message.substr(1)) + "\r\n";
            }
        }

        // Send meldingen hvis den er gyldig
        if (!paogi_message.empty()) {
            int len = static_cast<int>(paogi_message.length());
            sendto(udpSocket, paogi_message.c_str(), len, 0,
                (sockaddr*)&serverAddr, sizeof(serverAddr));
            std::cout << "Sent to AgOpenGPS: " << paogi_message;
        }

        Sleep(100);
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

    std::cout << "Listening for RTCM corrections on UDP port 2233..." << std::endl;

    char buffer[1024];
    sockaddr_in senderAddr;
    int senderAddrSize = sizeof(senderAddr);
    while (true) {
        int bytesReceived = recvfrom(udpSocket, buffer, sizeof(buffer), 0,
                                     (sockaddr*)&senderAddr, &senderAddrSize);
        if (bytesReceived > 0) {
            DWORD bytesWritten;
            WriteFile(gps1_handle, buffer, bytesReceived, &bytesWritten, nullptr);
            std::cout << "Received " << bytesReceived << " bytes of RTCM data via UDP, sent "
                      << bytesWritten << " to GPS1" << std::endl;
        } else if (bytesReceived == SOCKET_ERROR) {
            std::cerr << "Error receiving UDP data: " << WSAGetLastError() << std::endl;
            Sleep(1000);
        }
    }

    closesocket(udpSocket);
    WSACleanup();
}