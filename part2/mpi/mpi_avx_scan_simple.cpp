// mpi_avx_scan_simple.cpp - Simplified version for testing
// Compile: mpicxx -O3 -mavx2 -std=c++17 mpi_avx_scan_simple.cpp -o mpi_avx_scan
// Usage: mpirun -np <num_processes> ./mpi_avx_scan signatures.txt input.bin out.json

#include <mpi.h>
#include <immintrin.h>
#include <vector>
#include <string>
#include <fstream>
#include <iostream>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <sys/sysinfo.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// Get available RAM in MB
uint64_t get_available_ram_mb() {
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        return (info.freeram * info.mem_unit) / (1024 * 1024);
    }
    return 1024; // Default 1GB
}

std::vector<std::string> read_signatures(const std::string &path) {
    std::ifstream f(path);
    std::string s;
    std::vector<std::string> out;
    while (std::getline(f, s)) {
        if (!s.empty()) out.push_back(s);
    }
    return out;
}

uint64_t get_file_size(const std::string &path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return 0;
    return f.tellg();
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

std::vector<uint64_t> find_pattern_avx2(const std::vector<char>& buf, uint64_t global_offset, const std::string &pat) {
    std::vector<uint64_t> matches;
    size_t n = buf.size();
    if (pat.empty() || n == 0) return matches;
    size_t patlen = pat.size();
    
    if (patlen == 1) {
        char c = pat[0];
        for (size_t i = 0; i < n; ++i) {
            if (buf[i] == c) matches.push_back(global_offset + i);
        }
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
                if (memcmp(buf.data() + pos, pat.data(), patlen) == 0) {
                    matches.push_back(global_offset + pos);
                }
            }
            mask &= mask - 1;
        }
    }
    
    for (; i < n; ++i) {
        if (buf[i] == first) {
            if (i + patlen <= n) {
                if (memcmp(buf.data() + i, pat.data(), patlen) == 0) {
                    matches.push_back(global_offset + i);
                }
            }
        }
    }
    return matches;
}

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    
    int world_rank, world_size;
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);
    
    if (argc != 4) {
        if (world_rank == 0) {
            std::cerr << "Usage: mpirun -np <N> " << argv[0] << " signatures.txt input.bin out.json\n";
        }
        MPI_Finalize();
        return 1;
    }
    
    std::string sigfile = argv[1];
    std::string infile = argv[2];
    std::string outpath = argv[3];
    
    double start_time = MPI_Wtime();
    
    // All processes read signatures
    auto sigs = read_signatures(sigfile);
    
    if (world_rank == 0) {
        std::cout << "[MASTER] Starting MPI scan with " << world_size << " processes\n";
        std::cout << "[MASTER] Signatures loaded: " << sigs.size() << "\n";
    }
    
    // Get file size (master only)
    uint64_t filesize = 0;
    if (world_rank == 0) {
        filesize = get_file_size(infile);
        std::cout << "[MASTER] File size: " << filesize / (1024*1024) << " MB\n";
    }
    
    // Broadcast file size to all processes
    MPI_Bcast(&filesize, 1, MPI_UINT64_T, 0, MPI_COMM_WORLD);
    
    // Collect RAM info from all processes
    uint64_t my_ram = get_available_ram_mb();
    std::vector<uint64_t> all_rams(world_size);
    MPI_Gather(&my_ram, 1, MPI_UINT64_T, all_rams.data(), 1, MPI_UINT64_T, 0, MPI_COMM_WORLD);
    
    if (world_rank == 0) {
        uint64_t total_ram = 0;
        for (int i = 0; i < world_size; i++) {
            std::cout << "[MASTER] Process " << i << " RAM: " << all_rams[i] << " MB\n";
            total_ram += all_rams[i];
        }
        std::cout << "[MASTER] Total RAM: " << total_ram << " MB\n";
    }
    
    // Simple equal division (can be enhanced with RAM-based allocation)
    const uint64_t CHUNK_SIZE = 100 * 1024 * 1024; // 100MB chunks
    uint64_t num_chunks = (filesize + CHUNK_SIZE - 1) / CHUNK_SIZE;
    uint64_t chunks_per_process = (num_chunks + world_size - 1) / world_size;
    
    if (world_rank == 0) {
        std::cout << "[MASTER] Total chunks: " << num_chunks << " (~100MB each)\n";
        std::cout << "[MASTER] Chunks per process: ~" << chunks_per_process << "\n";
    }
    
    // Calculate this process's work range
    uint64_t my_start_chunk = world_rank * chunks_per_process;
    uint64_t my_end_chunk = std::min(my_start_chunk + chunks_per_process, num_chunks);
    
    std::cout << "[Process " << world_rank << "] Processing chunks " 
              << my_start_chunk << " to " << my_end_chunk - 1 << "\n";
    
    // Process assigned chunks
    json my_results;
    for (auto &sig : sigs) {
        my_results[sig] = json::array();
    }
    
    for (uint64_t chunk_id = my_start_chunk; chunk_id < my_end_chunk; chunk_id++) {
        uint64_t chunk_start = chunk_id * CHUNK_SIZE;
        uint64_t chunk_len = std::min(CHUNK_SIZE, filesize - chunk_start);
        
        if (chunk_len == 0) break;
        
        std::cout << "[Process " << world_rank << "] Reading chunk " << chunk_id 
                  << " (offset=" << chunk_start << ", len=" << chunk_len / (1024*1024) << "MB)\n";
        
        auto buf = read_chunk(infile, chunk_start, chunk_len);
        
        if (buf.empty()) {
            std::cerr << "[Process " << world_rank << "] Error reading chunk " << chunk_id << "\n";
            continue;
        }
        
        // Scan for all signatures
        for (const auto &sig : sigs) {
            auto matches = find_pattern_avx2(buf, chunk_start, sig);
            for (auto match : matches) {
                my_results[sig].push_back(match);
            }
        }
        
        std::cout << "[Process " << world_rank << "] Completed chunk " << chunk_id << "\n";
    }
    
    // Gather results to master
    std::string my_results_str = my_results.dump();
    int my_results_size = my_results_str.size();
    
    std::cout << "[Process " << world_rank << "] Sending results (" << my_results_size << " bytes)\n";
    
    if (world_rank == 0) {
        // Master collects all results
        json final_results;
        for (auto &sig : sigs) {
            final_results[sig] = json::array();
        }
        
        // Add master's own results
        for (auto &sig : sigs) {
            if (my_results.contains(sig) && my_results[sig].is_array()) {
                for (auto &match : my_results[sig]) {
                    final_results[sig].push_back(match);
                }
            }
        }
        
        // Receive from all workers
        for (int i = 1; i < world_size; i++) {
            int worker_results_size;
            MPI_Recv(&worker_results_size, 1, MPI_INT, i, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            
            std::vector<char> worker_results_buffer(worker_results_size);
            MPI_Recv(worker_results_buffer.data(), worker_results_size, MPI_CHAR, i, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            
            std::string worker_results_str(worker_results_buffer.begin(), worker_results_buffer.end());
            json worker_results = json::parse(worker_results_str);
            
            std::cout << "[MASTER] Received results from process " << i << "\n";
            
            // Merge results
            for (auto &sig : sigs) {
                if (worker_results.contains(sig) && worker_results[sig].is_array()) {
                    for (auto &match : worker_results[sig]) {
                        final_results[sig].push_back(match);
                    }
                }
            }
        }
        
        // Sort results
        for (auto &sig : sigs) {
            if (final_results[sig].is_array()) {
                std::vector<uint64_t> matches = final_results[sig];
                std::sort(matches.begin(), matches.end());
                final_results[sig] = matches;
            }
        }
        
        // Add metadata
        final_results["meta"] = {
            {"filesize", filesize},
            {"num_chunks", num_chunks},
            {"num_processes", world_size}
        };
        
        // Write output
        std::ofstream of(outpath);
        of << final_results.dump(2);
        
        double end_time = MPI_Wtime();
        
        std::cout << "\n[MASTER] ======= Summary =======\n";
        std::cout << "[MASTER] Total execution time: " << (end_time - start_time) << " seconds\n";
        std::cout << "[MASTER] Throughput: " << (filesize / (1024.0 * 1024.0)) / (end_time - start_time) << " MB/s\n";
        std::cout << "[MASTER] Results written to: " << outpath << "\n";
        
        // Print signature counts
        std::cout << "[MASTER] Signature matches:\n";
        int total_matches = 0;
        for (auto &sig : sigs) {
            int count = final_results[sig].size();
            total_matches += count;
            std::cout << "[MASTER]   " << sig << ": " << count << " matches\n";
        }
        std::cout << "[MASTER] Total matches: " << total_matches << "\n";
        
    } else {
        // Workers send their results to master
        MPI_Send(&my_results_size, 1, MPI_INT, 0, 0, MPI_COMM_WORLD);
        MPI_Send(my_results_str.data(), my_results_size, MPI_CHAR, 0, 0, MPI_COMM_WORLD);
    }
    
    MPI_Finalize();
    return 0;
}
