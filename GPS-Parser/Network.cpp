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
                double lat = latest_gga.latitude * 100.0; // Konverter fra DD.MMMM til DDMM.MMMM
                double lon = latest_gga.longitude * 100.0;

                std::stringstream ss;
                ss << "$PAOGI,"
                    << latest_gga.timestamp << "," // Tidsfelt
                    << std::fixed << std::setprecision(7) << lat << "," << latest_gga.lat_dir << "," // DDMM.MMMM
                    << std::fixed << std::setprecision(7) << lon << "," << latest_gga.lon_dir << "," // DDMM.MMMM
                    << latest_gga.gps_qual << "," // Fix quality
                    << latest_gga.num_sats << "," // Satellites
                    << std::fixed << std::setprecision(1) << latest_gga.hdop << "," // HDOP
                    << std::fixed << std::setprecision(1) << latest_gga.altitude << "," // Altitude
                    << "0.0," // Speed (hardkodet for nå)
                    << "0.0," // Roll
                    << "0.0," // Pitch
                    << "0,"   // Unknown1
                    << "0.0," // Unknown2
                    << std::fixed << std::setprecision(1) << latest_relposned.relPosHeading << "," // Heading
                    << "T";   // Heading type
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

        Sleep(100); // Send hvert 100 ms (10 Hz)
    }

    closesocket(udpSocket);
    WSACleanup();
}

void fetch_rtcm_corrections(const Settings& settings, HANDLE gps1_handle) {
    // Initialiser Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed: " << WSAGetLastError() << std::endl;
        return;
    }

    // Opprett TCP-socket
    SOCKET ntripSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ntripSocket == INVALID_SOCKET) {
        std::cerr << "Failed to create NTRIP socket: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return;
    }

    // Sett opp socket til ikke-blokkerende modus for å håndtere timeouts
    u_long mode = 1; // 1 for ikke-blokkerende
    if (ioctlsocket(ntripSocket, FIONBIO, &mode) != 0) {
        std::cerr << "Failed to set socket to non-blocking: " << WSAGetLastError() << std::endl;
        closesocket(ntripSocket);
        WSACleanup();
        return;
    }

    // Sett opp serveradresse
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(settings.ntrip_port);
    if (inet_pton(AF_INET, settings.ntrip_host.c_str(), &serverAddr.sin_addr) <= 0) {
        std::cerr << "Invalid NTRIP host address: " << settings.ntrip_host << std::endl;
        closesocket(ntripSocket);
        WSACleanup();
        return;
    }

    // Koble til rtk2go.com
    if (connect(ntripSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        if (WSAGetLastError() != WSAEWOULDBLOCK) {
            std::cerr << "Failed to connect to " << settings.ntrip_host << ":" << settings.ntrip_port
                << ": " << WSAGetLastError() << std::endl;
            closesocket(ntripSocket);
            WSACleanup();
            return;
        }
    }

    // Bygg NTRIP-forespørsel
    std::string auth = settings.ntrip_username + ":" + settings.ntrip_password;
    std::string auth_b64;
    const std::string base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (size_t i = 0; i < auth.length(); i += 3) {
        uint32_t triplet = (auth[i] << 16) + (i + 1 < auth.length() ? auth[i + 1] << 8 : 0) +
            (i + 2 < auth.length() ? auth[i + 2] : 0);
        auth_b64 += base64_chars[(triplet >> 18) & 0x3F];
        auth_b64 += base64_chars[(triplet >> 12) & 0x3F];
        auth_b64 += (i + 1 < auth.length() ? base64_chars[(triplet >> 6) & 0x3F] : '=');
        auth_b64 += (i + 2 < auth.length() ? base64_chars[triplet & 0x3F] : '=');
    }

    std::string request = "GET /" + settings.ntrip_mountpoint + " HTTP/1.1\r\n"
        "Host: " + settings.ntrip_host + "\r\n"
        "NTRIP-Version: NTRIP/2.0\r\n"
        "User-Agent: NTRIP Client\r\n";
    if (!settings.ntrip_username.empty()) {
        request += "Authorization: Basic " + auth_b64 + "\r\n";
    }
    request += "Connection: close\r\n\r\n";

    // Send NTRIP-forespørsel
    int len = static_cast<int>(request.length());
    if (send(ntripSocket, request.c_str(), len, 0) == SOCKET_ERROR) {
        if (WSAGetLastError() != WSAEWOULDBLOCK) {
            std::cerr << "Failed to send NTRIP request: " << WSAGetLastError() << std::endl;
            closesocket(ntripSocket);
            WSACleanup();
            return;
        }
    }

    std::cout << "Connected to rtk2go.com, waiting for RTCM data..." << std::endl;

    // Les respons og RTCM-data med timeout
    char buffer[1024];
    int bytesReceived;
    bool header_received = false;
    std::string header;
    fd_set readSet;
    struct timeval timeout;
    timeout.tv_sec = 10; // 10 sekunders timeout
    timeout.tv_usec = 0;

    while (true) {
        FD_ZERO(&readSet);
        FD_SET(ntripSocket, &readSet);

        int selectResult = select(0, &readSet, nullptr, nullptr, &timeout);
        if (selectResult == SOCKET_ERROR) {
            std::cerr << "Select failed: " << WSAGetLastError() << std::endl;
            break;
        }
        else if (selectResult == 0) {
            std::cerr << "Timeout waiting for RTCM data from rtk2go.com" << std::endl;
            break;
        }

        if (FD_ISSET(ntripSocket, &readSet)) {
            bytesReceived = recv(ntripSocket, buffer, sizeof(buffer), 0);
            if (bytesReceived > 0) {
                if (!header_received) {
                    header.append(buffer, bytesReceived);
                    size_t pos = header.find("\r\n\r\n");
                    if (pos != std::string::npos) {
                        header_received = true;
                        // Logg hele headeren for debugging
                        std::cout << "NTRIP response header: " << header.substr(0, pos) << std::endl;
                        // Sjekk om responsen er OK
                        if (header.find("ICY 200 OK") == std::string::npos) {
                            std::cerr << "NTRIP server responded with error: " << header.substr(0, pos) << std::endl;
                            break;
                        }
                        std::cout << "Received NTRIP header, starting RTCM stream..." << std::endl;
                        // Skriv gjenværende data etter headeren til GPS1
                        if (pos + 4 < header.length()) {
                            DWORD bytesWritten;
                            DWORD lenToWrite = static_cast<DWORD>(header.length() - (pos + 4));
                            WriteFile(gps1_handle, header.c_str() + pos + 4, lenToWrite, &bytesWritten, nullptr);
                            std::cout << "Sent " << bytesWritten << " bytes of RTCM data to GPS1" << std::endl;
                        }
                    }
                }
                else {
                    // Skriv RTCM-data direkte til GPS1
                    DWORD bytesWritten;
                    DWORD lenToWrite = static_cast<DWORD>(bytesReceived);
                    WriteFile(gps1_handle, buffer, lenToWrite, &bytesWritten, nullptr);
                    std::cout << "Sent " << bytesWritten << " bytes of RTCM data to GPS1" << std::endl;
                }
            }
            else if (bytesReceived == 0) {
                std::cout << "NTRIP server closed connection" << std::endl;
                break;
            }
            else {
                std::cerr << "Error receiving RTCM data: " << WSAGetLastError() << std::endl;
                break;
            }
        }
    }

    closesocket(ntripSocket);
    WSACleanup();
}