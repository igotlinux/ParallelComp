// mpi_avx_scan.cpp
// Compile: mpicxx -O3 -mavx2 -std=c++17 mpi_avx_scan.cpp -o mpi_avx_scan
// Usage: mpirun -np <num_processes> -hostfile hostfile ./mpi_avx_scan signatures.txt input.bin out.json

#include <mpi.h>
#include <immintrin.h>
#include <vector>
#include <string>
#include <fstream>
#include <iostream>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <unordered_map>
#include <sys/sysinfo.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

const int TAG_WORK = 1;
const int TAG_RESULT = 2;
const int TAG_HEARTBEAT = 3;
const int TAG_REASSIGN = 4;
const int TAG_RAM_INFO = 5;

struct WorkChunk {
    uint64_t start;
    uint64_t length;
    int assigned_to;
    bool completed;
};

struct NodeInfo {
    int rank;
    uint64_t available_ram_mb;
    bool is_alive;
    double last_heartbeat;
};

// Get available RAM in MB
uint64_t get_available_ram_mb() {
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        return (info.freeram * info.mem_unit) / (1024 * 1024);
    }
    return 1024; // Default 1GB if can't determine
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

void master_process(int world_size, const std::string &filepath, const std::string &sigfile, const std::string &outpath) {
    std::cout << "[MASTER] Starting master process\n";
    
    // Get file size
    uint64_t filesize = get_file_size(filepath);
    std::cout << "[MASTER] File size: " << filesize / (1024*1024) << " MB\n";
    
    // Collect RAM information from all workers
    std::vector<NodeInfo> nodes(world_size);
    nodes[0] = {0, get_available_ram_mb(), true, MPI_Wtime()};
    
    std::cout << "[MASTER] RAM: " << nodes[0].available_ram_mb << " MB\n";
    
    for (int i = 1; i < world_size; i++) {
        uint64_t worker_ram;
        MPI_Recv(&worker_ram, 1, MPI_UINT64_T, i, TAG_RAM_INFO, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        nodes[i] = {i, worker_ram, true, MPI_Wtime()};
        std::cout << "[MASTER] Worker " << i << " RAM: " << worker_ram << " MB\n";
    }
    
    // Calculate total RAM
    uint64_t total_ram = 0;
    for (const auto &node : nodes) {
        total_ram += node.available_ram_mb;
    }
    
    // Create work chunks based on RAM distribution
    const uint64_t CHUNK_SIZE_MB = 100; // 100MB chunks
    const uint64_t CHUNK_SIZE_BYTES = CHUNK_SIZE_MB * 1024 * 1024;
    
    std::vector<WorkChunk> chunks;
    uint64_t offset = 0;
    int chunk_id = 0;
    
    while (offset < filesize) {
        uint64_t chunk_len = std::min(CHUNK_SIZE_BYTES, filesize - offset);
        chunks.push_back({offset, chunk_len, -1, false});
        offset += chunk_len;
        chunk_id++;
    }
    
    std::cout << "[MASTER] Created " << chunks.size() << " chunks of ~" << CHUNK_SIZE_MB << " MB each\n";
    
    // Distribute chunks based on RAM proportion
    std::vector<int> chunks_per_node(world_size, 0);
    for (size_t i = 0; i < chunks.size(); i++) {
        // Find node with least assigned chunks relative to its RAM
        int best_node = 0;
        double best_ratio = (double)chunks_per_node[0] / nodes[0].available_ram_mb;
        
        for (int n = 1; n < world_size; n++) {
            if (nodes[n].is_alive) {
                double ratio = (double)chunks_per_node[n] / nodes[n].available_ram_mb;
                if (ratio < best_ratio) {
                    best_ratio = ratio;
                    best_node = n;
                }
            }
        }
        
        chunks[i].assigned_to = best_node;
        chunks_per_node[best_node]++;
    }
    
    std::cout << "[MASTER] Work distribution:\n";
    for (int i = 0; i < world_size; i++) {
        std::cout << "  Node " << i << ": " << chunks_per_node[i] << " chunks ("
                  << (chunks_per_node[i] * CHUNK_SIZE_MB) << " MB)\n";
    }
    
    // Send work to workers
    int next_chunk_to_assign = 0;
    int completed_chunks = 0;
    std::unordered_map<int, std::vector<int>> worker_assigned_chunks; // worker -> chunk indices
    
    // Initial assignment
    for (int i = 1; i < world_size; i++) {
        if (next_chunk_to_assign < chunks.size() && chunks[next_chunk_to_assign].assigned_to == i) {
            uint64_t work_data[2] = {chunks[next_chunk_to_assign].start, chunks[next_chunk_to_assign].length};
            MPI_Send(work_data, 2, MPI_UINT64_T, i, TAG_WORK, MPI_COMM_WORLD);
            worker_assigned_chunks[i].push_back(next_chunk_to_assign);
            std::cout << "[MASTER] Assigned chunk " << next_chunk_to_assign << " to worker " << i << "\n";
            next_chunk_to_assign++;
        }
    }
    
    // Collect results and handle failures
    json final_result;
    auto sigs = read_signatures(sigfile);
    
    for (auto &sig : sigs) {
        final_result[sig] = json::array();
    }
    
    while (completed_chunks < chunks.size()) {
        MPI_Status status;
        int flag;
        
        // Check for incoming messages
        MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &flag, &status);
        
        if (flag) {
            int source = status.MPI_SOURCE;
            int tag = status.MPI_TAG;
            
            if (tag == TAG_RESULT) {
                // Receive result size first
                int result_size;
                MPI_Recv(&result_size, 1, MPI_INT, source, TAG_RESULT, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                
                // Receive JSON result
                std::vector<char> result_buffer(result_size);
                MPI_Recv(result_buffer.data(), result_size, MPI_CHAR, source, TAG_RESULT, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                
                std::string result_str(result_buffer.begin(), result_buffer.end());
                json chunk_result = json::parse(result_str);
                
                // Merge results
                for (auto &sig : sigs) {
                    if (chunk_result.contains(sig) && chunk_result[sig].is_array()) {
                        for (auto &match : chunk_result[sig]) {
                            final_result[sig].push_back(match);
                        }
                    }
                }
                
                completed_chunks++;
                std::cout << "[MASTER] Received result from worker " << source 
                          << " (" << completed_chunks << "/" << chunks.size() << " completed)\n";
                
                // Assign next chunk to this worker
                while (next_chunk_to_assign < chunks.size()) {
                    if (chunks[next_chunk_to_assign].assigned_to == source) {
                        uint64_t work_data[2] = {chunks[next_chunk_to_assign].start, chunks[next_chunk_to_assign].length};
                        MPI_Send(work_data, 2, MPI_UINT64_T, source, TAG_WORK, MPI_COMM_WORLD);
                        worker_assigned_chunks[source].push_back(next_chunk_to_assign);
                        std::cout << "[MASTER] Assigned chunk " << next_chunk_to_assign << " to worker " << source << "\n";
                        next_chunk_to_assign++;
                        break;
                    }
                    next_chunk_to_assign++;
                }
            } else if (tag == TAG_HEARTBEAT) {
                int dummy;
                MPI_Recv(&dummy, 1, MPI_INT, source, TAG_HEARTBEAT, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                nodes[source].last_heartbeat = MPI_Wtime();
            }
        }
        
        // Check for failed workers (no heartbeat for 10 seconds)
        double current_time = MPI_Wtime();
        for (int i = 1; i < world_size; i++) {
            if (nodes[i].is_alive && (current_time - nodes[i].last_heartbeat) > 10.0) {
                std::cout << "[MASTER] Worker " << i << " FAILED! Reassigning work...\n";
                nodes[i].is_alive = false;
                
                // Reassign incomplete chunks from failed worker
                for (int chunk_idx : worker_assigned_chunks[i]) {
                    if (!chunks[chunk_idx].completed) {
                        // Find another alive worker with similar RAM
                        int reassign_to = -1;
                        for (int j = 1; j < world_size; j++) {
                            if (nodes[j].is_alive && j != i) {
                                reassign_to = j;
                                break;
                            }
                        }
                        
                        if (reassign_to != -1) {
                            uint64_t work_data[2] = {chunks[chunk_idx].start, chunks[chunk_idx].length};
                            MPI_Send(work_data, 2, MPI_UINT64_T, reassign_to, TAG_WORK, MPI_COMM_WORLD);
                            chunks[chunk_idx].assigned_to = reassign_to;
                            worker_assigned_chunks[reassign_to].push_back(chunk_idx);
                            std::cout << "[MASTER] Reassigned chunk " << chunk_idx << " from worker " 
                                      << i << " to worker " << reassign_to << "\n";
                        }
                    }
                }
            }
        }
    }
    
    // Signal workers to stop
    for (int i = 1; i < world_size; i++) {
        uint64_t stop_signal[2] = {0, 0};
        MPI_Send(stop_signal, 2, MPI_UINT64_T, i, TAG_WORK, MPI_COMM_WORLD);
    }
    
    // Add metadata
    final_result["meta"] = {
        {"filesize", filesize},
        {"num_chunks", chunks.size()},
        {"num_workers", world_size - 1}
    };
    
    // Write output
    std::ofstream of(outpath);
    of << final_result.dump(2);
    std::cout << "[MASTER] Results written to " << outpath << "\n";
}

void worker_process(int rank, const std::string &filepath, const std::string &sigfile) {
    std::cout << "[Worker " << rank << "] Starting worker process\n";
    
    // Send RAM info to master
    uint64_t my_ram = get_available_ram_mb();
    MPI_Send(&my_ram, 1, MPI_UINT64_T, 0, TAG_RAM_INFO, MPI_COMM_WORLD);
    
    auto sigs = read_signatures(sigfile);
    std::cout << "[Worker " << rank << "] Loaded " << sigs.size() << " signatures\n";
    
    while (true) {
        // Receive work
        uint64_t work_data[2];
        MPI_Status status;
        MPI_Recv(work_data, 2, MPI_UINT64_T, 0, TAG_WORK, MPI_COMM_WORLD, &status);
        
        uint64_t start = work_data[0];
        uint64_t length = work_data[1];
        
        // Check for stop signal
        if (start == 0 && length == 0) {
            std::cout << "[Worker " << rank << "] Received stop signal\n";
            break;
        }
        
        std::cout << "[Worker " << rank << "] Processing chunk: offset=" << start 
                  << ", length=" << length / (1024*1024) << " MB\n";
        
        // Read chunk
        auto buf = read_chunk(filepath, start, length);
        
        // Scan for signatures
        json result;
        for (const auto &sig : sigs) {
            auto matches = find_pattern_avx2(buf, start, sig);
            if (!matches.empty()) {
                result[sig] = matches;
            }
        }
        
        // Send result back
        std::string result_str = result.dump();
        int result_size = result_str.size();
        MPI_Send(&result_size, 1, MPI_INT, 0, TAG_RESULT, MPI_COMM_WORLD);
        MPI_Send(result_str.data(), result_size, MPI_CHAR, 0, TAG_RESULT, MPI_COMM_WORLD);
        
        std::cout << "[Worker " << rank << "] Sent results back to master\n";
        
        // Send heartbeat
        int heartbeat = 1;
        MPI_Send(&heartbeat, 1, MPI_INT, 0, TAG_HEARTBEAT, MPI_COMM_WORLD);
    }
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
    
    if (world_rank == 0) {
        master_process(world_size, infile, sigfile, outpath);
    } else {
        worker_process(world_rank, infile, sigfile);
    }
    
    double end_time = MPI_Wtime();
    
    if (world_rank == 0) {
        std::cout << "\n[MASTER] Total execution time: " << (end_time - start_time) << " seconds\n";
    }
    
    MPI_Finalize();
    return 0;
}
