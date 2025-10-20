// scalar_scan.cpp
// Compile: g++ -O3 -std=c++17 scalar_scan.cpp -o scalar_scan
// Usage: ./scalar_scan signatures.txt input.bin start length out.json

#include <vector>
#include <string>
#include <fstream>
#include <iostream>
#include <cstdint>
#include <cstring>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

std::vector<std::string> read_signatures(const std::string &path) {
    std::ifstream f(path);
    std::string s;
    std::vector<std::string> out;
    while (std::getline(f,s)) if(!s.empty()) out.push_back(s);
    return out;
}

std::vector<char> read_chunk(const std::string &path, uint64_t start, uint64_t len) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    f.seekg(0, std::ios::end);
    uint64_t filesize = f.tellg();
    if (start >= filesize) return {};
    uint64_t toread = std::min(len, filesize - start);
    std::vector<char> buf(toread);
    f.seekg(start, std::ios::beg);
    f.read(buf.data(), toread);
    return buf;
}

std::vector<uint64_t> find_pattern_scalar(const std::vector<char>& buf, uint64_t global_offset, const std::string &pat) {
    std::vector<uint64_t> matches;
    size_t n = buf.size();
    if (pat.empty() || n == 0) return matches;
    size_t patlen = pat.size();
    for (size_t i = 0; i + patlen <= n; ++i) {
        if (buf[i] == pat[0]) {
            if (memcmp(buf.data()+i, pat.data(), patlen) == 0) matches.push_back(global_offset + i);
        }
    }
    return matches;
}

int main(int argc, char** argv) {
    if (argc != 6) {
        std::cerr << "Usage: " << argv[0] << " signatures.txt input.bin start length out.json\n";
        return 1;
    }
    std::string sigfile = argv[1];
    std::string infile = argv[2];
    uint64_t start = std::stoull(argv[3]);
    uint64_t length = std::stoull(argv[4]);
    std::string outpath = argv[5];

    auto sigs = read_signatures(sigfile);
    auto buf = read_chunk(infile, start, length);
    uint64_t scanned = buf.size();

    json out;
    for (auto &s : sigs) {
        auto matches = find_pattern_scalar(buf, start, s);
        if (!matches.empty()) out[s] = matches;
    }
    out["meta"] = { {"start", start}, {"length", scanned} };
    std::ofstream of(outpath);
    of << out.dump(2);
    std::cout << "Scanned " << scanned << " bytes from offset " << start << ", wrote " << outpath << "\n";
    return 0;
}
