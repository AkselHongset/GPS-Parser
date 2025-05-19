#include "Gps1.h"
#include <iostream>
#include <sstream>

// Global HANDLE for GPS1
HANDLE gps1_handle = INVALID_HANDLE_VALUE;

NMEAGGA parse_gga(const std::string& sentence) {
    NMEAGGA gga;
    gga.raw_sentence = sentence;
    if (!sentence.empty() && (sentence.find("$GPGGA") == 0 || sentence.find("$GNGGA") == 0)) {
        std::vector<std::string> fields;
        std::stringstream ss(sentence);
        std::string field;
        while (std::getline(ss, field, ',')) {
            fields.push_back(field);
        }
        if (fields.size() >= 10) {
            try {
                gga.timestamp = fields[1];
                if (!fields[2].empty()) {
                    double lat = std::stod(fields[2]);
                    int degrees = static_cast<int>(lat / 100);
                    double minutes = lat - (degrees * 100);
                    gga.latitude = degrees + minutes / 60.0;
                }
                else {
                    gga.latitude = 0.0;
                }
                gga.lat_dir = fields[3].empty() ? ' ' : fields[3][0];
                if (!fields[4].empty()) {
                    double lon = std::stod(fields[4]);
                    int degrees = static_cast<int>(lon / 100);
                    double minutes = lon - (degrees * 100);
                    gga.longitude = degrees + minutes / 60.0;
                }
                else {
                    gga.longitude = 0.0;
                }
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

// Parse VTG message to extract speed in knots and store raw sentence
double parse_vtg(const std::string& sentence, std::string& raw_vtg_sentence) {
    double speed_knots = 0.0;
    raw_vtg_sentence = sentence; // Store raw VTG sentence
    if (!sentence.empty() && (sentence.find("$GPVTG") == 0 || sentence.find("$GNVTG") == 0)) {
        std::vector<std::string> fields;
        std::stringstream ss(sentence);
        std::string field;
        while (std::getline(ss, field, ',')) {
            fields.push_back(field);
        }
        if (fields.size() >= 8) { // Field 7 is speed in knots
            try {
                if (!fields[7].empty()) {
                    speed_knots = std::stod(fields[7]);
                }
            }
            catch (...) {
                speed_knots = 0.0;
            }
        }
    }
    return speed_knots;
}

void read_gps1(const std::string& port, DWORD baud_rate) {
    std::cout << "Connected to " << port << std::endl;

    std::string buffer;
    char data[256];
    DWORD bytesRead;
    std::vector<std::string> lines; // Buffer for flere linjer
    while (running) {
        if (ReadFile(gps1_handle, data, sizeof(data) - 1, &bytesRead, NULL)) {
            if (bytesRead > 0) {
                data[bytesRead] = '\0';
                buffer += data;
                size_t pos;
                while ((pos = buffer.find('\n')) != std::string::npos) {
                    std::string line = buffer.substr(0, pos);
                    buffer.erase(0, pos + 1);
                    lines.push_back(line); // Samle linjer
                }
                // Oppdater latest_gga kun når flere linjer er samlet
                if (lines.size() >= 5) { // Prosesser hver 5. linje for å redusere låsing
                    std::lock_guard<std::mutex> lock(data_mutex);
                    for (const auto& line : lines) {
                        if (line.find("$GPGGA") == 0 || line.find("$GNGGA") == 0) {
                            NMEAGGA gga = parse_gga(line);
                            if (gga.valid) {
                                gga.speed_knots = latest_gga.speed_knots;
                                gga.raw_vtg_sentence = latest_gga.raw_vtg_sentence;
                                latest_gga = gga;
                            }
                        }
                        else if (line.find("$GPVTG") == 0 || line.find("$GNVTG") == 0) {
                            std::string raw_vtg_sentence;
                            double speed_knots = parse_vtg(line, raw_vtg_sentence);
                            latest_gga.speed_knots = speed_knots;
                            latest_gga.raw_vtg_sentence = raw_vtg_sentence;
                        }
                    }
                    lines.clear();
                }
            }
        }
        else {
            std::cerr << "Error reading from " << port << ": " << GetLastError() << std::endl;
            Sleep(1000);
        }
    }
}