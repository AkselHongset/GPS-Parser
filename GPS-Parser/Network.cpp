#define _USE_MATH_DEFINES // Må være før cmath
#include "Network.h"
#include "Display.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <queue>
#include <thread>
#include <chrono>

// Definer globale variabler for RTCM-kø
std::queue<std::vector<char>> rtcm_queue;
std::mutex rtcm_queue_mutex;

// Variabler for å spore datarater (kun for intern bruk, ikke logget)
static std::chrono::system_clock::time_point last_log_time = std::chrono::system_clock::now();
static size_t total_bytes_received = 0;
static size_t total_bytes_written = 0;

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

// Funksjon for å sende data til AgOpenGPS
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
    u_long mode = 1;
    ioctlsocket(udpSocket, FIONBIO, &mode);

    while (running) {
        std::string paogi_message;
        {
            std::lock_guard<std::mutex> lock(data_mutex);
            if (latest_gga.valid && latest_relposned.valid && latest_gga.gps_qual > 0) {
                double lat = latest_gga.latitude;
                double lat_ddmm = (int)lat * 100 + (lat - (int)lat) * 60;
                double lon = latest_gga.longitude;
                double lon_ddmm = (int)lon * 100 + (lon - (int)lon) * 60;

                double roll = 0.0;
                if (settings.antenna_separation > 0.0) {
                    roll = atan2(latest_relposned.relPosD, settings.antenna_separation) * 180.0 / M_PI;
                }

                std::stringstream ss;
                ss << "$PAOGI,"
                    << latest_gga.timestamp << ","
                    << std::fixed << std::setprecision(4) << lat_ddmm << "," << latest_gga.lat_dir << ","
                    << std::fixed << std::setprecision(4) << lon_ddmm << "," << latest_gga.lon_dir << ","
                    << latest_gga.gps_qual << ","
                    << latest_gga.num_sats << ","
                    << std::fixed << std::setprecision(1) << latest_gga.hdop << ","
                    << std::fixed << std::setprecision(1) << latest_gga.altitude << ","
                    << "0.0,"
                    << std::fixed << std::setprecision(1) << latest_gga.speed_knots << ","
                    << std::fixed << std::setprecision(1) << latest_relposned.relPosHeading << ","
                    << std::fixed << std::setprecision(1) << roll << ","
                    << "0.0,"
                    << std::fixed << std::setprecision(1) << latest_relposned.relPosHeading << ","
                    << std::fixed << std::setprecision(3) << latest_relposned.relPosLength << ","
                    << "T";
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

        if (latest_gga.valid && !latest_gga.raw_sentence.empty()) {
            std::string gga_message = latest_gga.raw_sentence;
            if (gga_message.back() != '\n') {
                gga_message += "\r\n";
            }
            int len = static_cast<int>(gga_message.length());
            int bytesSent = sendto(udpSocket, gga_message.c_str(), len, 0,
                (sockaddr*)&serverAddr, sizeof(serverAddr));
            if (bytesSent == SOCKET_ERROR) {
                std::cerr << "Failed to send GGA: " << WSAGetLastError() << std::endl;
            }
        }

        if (latest_gga.valid && !latest_gga.raw_vtg_sentence.empty()) {
            std::string vtg_message = latest_gga.raw_vtg_sentence;
            if (vtg_message.back() != '\n') {
                vtg_message += "\r\n";
            }
            int len = static_cast<int>(vtg_message.length());
            int bytesSent = sendto(udpSocket, vtg_message.c_str(), len, 0,
                (sockaddr*)&serverAddr, sizeof(serverAddr));
            if (bytesSent == SOCKET_ERROR) {
                std::cerr << "Failed to send VTG: " << WSAGetLastError() << std::endl;
            }
        }

        Sleep(100);
    }

    closesocket(udpSocket);
    WSACleanup();
}

// Funksjon for å skrive RTCM-data fra køen til GPS1
void write_rtcm_to_gps1(HANDLE gps1_handle) {
    // Øk trådprioritet for å prioritere skriving
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

    while (running) {
        std::vector<char> batch_data;
        size_t batch_size = 0;
        {
            std::lock_guard<std::mutex> lock(rtcm_queue_mutex);
            // Sikkerhetsfunksjon: Tøm køen hvis den når 100 pakker
            if (rtcm_queue.size() >= 100) {
                std::cerr << "WARNING: RTCM queue reached 100 packets, clearing queue to prevent overflow" << std::endl;
                while (!rtcm_queue.empty()) {
                    rtcm_queue.pop();
                }
                continue; // Hopp til neste iterasjon
            }
            // Samle opptil 2048 bytes (eller flere pakker) for batch-skriving
            while (!rtcm_queue.empty() && batch_size < 2048) {
                auto& data = rtcm_queue.front();
                batch_data.insert(batch_data.end(), data.begin(), data.end());
                batch_size += data.size();
                rtcm_queue.pop();
            }
        }
        if (!batch_data.empty() && gps1_handle != INVALID_HANDLE_VALUE) {
            // Tøm utgangsbuffer før skriving
            if (!PurgeComm(gps1_handle, PURGE_TXCLEAR)) {
                std::cerr << "Failed to purge TX buffer: " << GetLastError() << std::endl;
            }
            auto start_time = std::chrono::steady_clock::now();
            DWORD bytesWritten;
            if (WriteFile(gps1_handle, batch_data.data(), batch_data.size(), &bytesWritten, nullptr)) {
                update_rtcm_timestamp();
                total_bytes_written += bytesWritten;
                auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start_time).count();
                if (bytesWritten != batch_data.size()) {
                    std::cerr << "Incomplete RTCM write to GPS1: " << bytesWritten << " of " << batch_data.size()
                        << " bytes, took " << duration_ms << " ms" << std::endl;
                }
                else if (duration_ms > 10) {
                    std::cerr << "Slow RTCM write to GPS1: " << bytesWritten << " bytes, took " << duration_ms << " ms" << std::endl;
                }
            }
            else {
                DWORD error = GetLastError();
                std::cerr << "Failed to write RTCM to GPS1: Error " << error << ", took "
                    << std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - start_time).count() << " ms" << std::endl;
                // Prøv å tømme feil og fortsett
                DWORD errors;
                COMSTAT comStat;
                if (ClearCommError(gps1_handle, &errors, &comStat)) {
                    std::cout << "Cleared COM port error: Errors = " << errors << ", Buffer = "
                        << comStat.cbInQue << " in, " << comStat.cbOutQue << " out" << std::endl;
                }
                Sleep(10); // Kort pause for å gi porten tid til å gjenopprette
            }
        }
        // Kort pause hvis køen er tom
        if (batch_data.empty()) {
            Sleep(5);
        }
    }
}

// Funksjon for å hente RTCM-korreksjoner fra UDP-port
void fetch_rtcm_udp(HANDLE gps1_handle, int rtcm_port) {
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

    // Øk mottaksbufferstørrelsen
    int bufferSize = 65536; // 64 KB
    if (setsockopt(udpSocket, SOL_SOCKET, SO_RCVBUF, (char*)&bufferSize, sizeof(bufferSize)) == SOCKET_ERROR) {
        std::cerr << "Failed to set receive buffer size: " << WSAGetLastError() << std::endl;
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(rtcm_port);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(udpSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "Bind failed: " << WSAGetLastError() << std::endl;
        closesocket(udpSocket);
        WSACleanup();
        return;
    }

    u_long mode = 1;
    ioctlsocket(udpSocket, FIONBIO, &mode);

    std::cout << "Listening for RTCM corrections on UDP port " << rtcm_port << "..." << std::endl;

    // Start en separat tråd for å skrive RTCM-data til GPS1
    std::thread writer_thread(write_rtcm_to_gps1, gps1_handle);

    char buffer[2048];
    sockaddr_in senderAddr;
    int senderAddrSize = sizeof(senderAddr);
    while (running) {
        int bytesReceived = recvfrom(udpSocket, buffer, sizeof(buffer), 0,
            (sockaddr*)&senderAddr, &senderAddrSize);
        if (bytesReceived > 0) {
            // Ingen logging av pakkestørrelse
            std::vector<char> data(buffer, buffer + bytesReceived);
            total_bytes_received += bytesReceived;
            {
                std::lock_guard<std::mutex> lock(rtcm_queue_mutex);
                rtcm_queue.push(std::move(data));
                // Begrens køstørrelse for å unngå overdreven minnebruk
                if (rtcm_queue.size() > 1000) {
                    std::cerr << "RTCM queue overflow, dropping oldest packet" << std::endl;
                    rtcm_queue.pop();
                }
            }
        }
        else if (bytesReceived == SOCKET_ERROR) {
            int error = WSAGetLastError();
            if (error != WSAEWOULDBLOCK) {
                std::cerr << "Error receiving UDP data: " << error << std::endl;
            }
        }
        // Ingen Sleep her for å maksimere mottakshastighet
    }

    closesocket(udpSocket);
    WSACleanup();
    writer_thread.join();
}