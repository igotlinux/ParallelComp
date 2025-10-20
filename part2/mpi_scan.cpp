#include <mpi.h>
#include <vector>
#include <string>
#include <fstream>
#include <iostream>
#include <cstdint>
#include <cstring>
#include <nlohmann/json.hpp>
#include <immintrin.h> // for AVX2
#include <algorithm>

using json = nlohmann::json;

// ----------- Signature reading ----------------
std::vector<std::string> read_signatures(const std::string &path) {
    std::ifstream f(path);
    std::string s;
    std::vector<std::string> out;
    while (std::getline(f,s)) if(!s.empty()) out.push_back(s);
    return out;
}

// ----------- Read chunk from file ----------------
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

// ----------- Scalar scan ----------------
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

// ----------- AVX2 scan ----------------
std::vector<uint64_t> find_pattern_avx2(const std::vector<char>& buf, uint64_t global_offset, const std::string &pat) {
    std::vector<uint64_t> matches;
    size_t n = buf.size();
    if (pat.empty() || n == 0) return matches;
    size_t patlen = pat.size();
    if (patlen == 1) {
        char c = pat[0];
        for (size_t i = 0; i < n; ++i) if (buf[i] == c) matches.push_back(global_offset + i);
        return matches;
    }
    const char first = pat[0];
    size_t i = 0;
    __m256i firstv = _mm256_set1_epi8(first);
    for (; i + 32 <= n; i += 32) {
        __m256i block = _mm256_loadu_si256((const __m256i*)(buf.data() + i));
        __m256i cmp = _mm256_cmpeq_epi8(block, firstv);
        unsigned int mask = (unsigned int)_mm256_movemask_epi8(cmp);
        while (mask) {
            unsigned int bit = __builtin_ctz(mask);
            size_t pos = i + bit;
            if (pos + patlen <= n) {
                if (memcmp(buf.data()+pos, pat.data(), patlen) == 0) matches.push_back(global_offset + pos);
            }
            mask &= mask - 1;
        }
    }
    for (; i < n; ++i) {
        if (buf[i] == first) {
            if (i + patlen <= n) {
                if (memcmp(buf.data()+i, pat.data(), patlen) == 0) matches.push_back(global_offset + i);
            }
        }
    }
    return matches;
}

// ----------- Main ----------------
int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (argc != 5) {
        if (rank==0) std::cerr << "Usage: " << argv[0] << " signatures.txt input.bin chunk_size use_avx(0/1)\n";
        MPI_Finalize();
        return 1;
    }

    std::string sigfile = argv[1];
    std::string infile = argv[2];
    uint64_t chunk_size = std::stoull(argv[3]);
    bool use_avx = std::stoi(argv[4]) != 0;

    auto sigs = read_signatures(sigfile);

    // --------- Master process -----------
    if (rank == 0) {
        std::ifstream f(infile, std::ios::binary | std::ios::ate);
        uint64_t filesize = f.tellg();
        uint64_t chunks = (filesize + chunk_size -1)/chunk_size;

        int next_chunk = 0;
        int active_workers = std::min(size-1, (int)chunks);
        json merged;

        // send initial chunks to workers
        for(int i=1;i<=active_workers;i++){
            uint64_t start = next_chunk * chunk_size;
            uint64_t len = std::min(chunk_size, filesize - start);
            MPI_Send(&start, 1, MPI_UINT64_T, i, 0, MPI_COMM_WORLD);
            MPI_Send(&len, 1, MPI_UINT64_T, i, 0, MPI_COMM_WORLD);
            next_chunk++;
        }

        while(next_chunk < chunks){
            MPI_Status status;
            // receive json string size
            int worker = 0;
            MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
            worker = status.MPI_SOURCE;

            int msg_size;
            MPI_Get_count(&status, MPI_CHAR, &msg_size);
            std::vector<char> buf(msg_size);
            MPI_Recv(buf.data(), msg_size, MPI_CHAR, worker, MPI_ANY_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            // parse JSON and merge
            json chunk_json = json::parse(buf.begin(), buf.end());
            for(auto &[k,v]: chunk_json.items()){
                if(k=="meta") continue;
                merged[k].insert(merged[k].end(), v.begin(), v.end());
            }

            // send next chunk
            uint64_t start = next_chunk * chunk_size;
            uint64_t len = std::min(chunk_size, filesize - start);
            MPI_Send(&start, 1, MPI_UINT64_T, worker, 0, MPI_COMM_WORLD);
            MPI_Send(&len, 1, MPI_UINT64_T, worker, 0, MPI_COMM_WORLD);
            next_chunk++;
        }

        // collect remaining results
        for(int i=1;i<=active_workers;i++){
            MPI_Status status;
            MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
            int worker = status.MPI_SOURCE;
            int msg_size;
            MPI_Get_count(&status, MPI_CHAR, &msg_size);
            std::vector<char> buf(msg_size);
            MPI_Recv(buf.data(), msg_size, MPI_CHAR, worker, MPI_ANY_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            json chunk_json = json::parse(buf.begin(), buf.end());
            for(auto &[k,v]: chunk_json.items()){
                if(k=="meta") continue;
                merged[k].insert(merged[k].end(), v.begin(), v.end());
            }

            // send termination signal (-1)
            uint64_t term = UINT64_MAX;
            MPI_Send(&term,1,MPI_UINT64_T,worker,0,MPI_COMM_WORLD);
        }

        merged["meta"] = { {"start", 0}, {"length", (int)filesize} };
        std::ofstream of("merged_out.json");
        of << merged.dump(2);
        std::cout << "Master: Merged JSON written to merged_out.json\n";

    } else {
        // --------- Worker process -----------
        while(true){
            uint64_t start,len;
            MPI_Recv(&start,1,MPI_UINT64_T,0,0,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
            MPI_Recv(&len,1,MPI_UINT64_T,0,0,MPI_COMM_WORLD,MPI_STATUS_IGNORE);

            if(start == UINT64_MAX) break; // termination signal

            auto buf = read_chunk(infile,start,len);
            json out;
            for(auto &s:sigs){
                std::vector<uint64_t> matches;
                if(use_avx) matches = find_pattern_avx2(buf,start,s);
                else matches = find_pattern_scalar(buf,start,s);
                if(!matches.empty()) out[s]=matches;
            }
            out["meta"] = { {"start",start}, {"length",(int)buf.size()} };

            // send json as string
            std::string out_str = out.dump();
            MPI_Send(out_str.data(), out_str.size(), MPI_CHAR, 0, 0, MPI_COMM_WORLD);
        }
    }

    MPI_Finalize();
    return 0;
}
