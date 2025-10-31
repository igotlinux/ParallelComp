// mpi_avx_scan.cpp - FULLY FIXED with proper RAM detection and robust fault tolerance
// Compile: mpicxx -O3 -mavx2 -std=c++17 -pthread mpi_avx_scan.cpp -o mpi_avx_scan
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
#include <map>
#include <sys/sysinfo.h>
#include <unistd.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

const int TAG_WORK = 1;
const int TAG_RESULT = 2;
const int TAG_HEARTBEAT = 3;
const int TAG_RAM_INFO = 5;

const double HEARTBEAT_INTERVAL = 1.0;  // Send heartbeat every 1 second
const double HEARTBEAT_TIMEOUT = 5.0;   // Consider dead after 5 seconds without heartbeat

struct WorkChunk
{
    uint64_t start;
    uint64_t length;
    int assigned_to;
    bool completed;
    bool in_progress;
};

struct NodeInfo
{
    int rank;
    uint64_t available_ram_mb;
    bool is_alive;
    double last_heartbeat;
};

// FIXED: Get TOTAL RAM, not just free RAM
uint64_t get_available_ram_mb()
{
    struct sysinfo info;
    if (sysinfo(&info) == 0)
    {
        // Return TOTAL RAM, not just free RAM
        uint64_t total_ram_mb = (info.totalram * info.mem_unit) / (1024 * 1024);
        std::cout << "[DEBUG] Total RAM: " << total_ram_mb << " MB, Free RAM: " 
                  << ((info.freeram * info.mem_unit) / (1024 * 1024)) << " MB\n";
        return total_ram_mb;
    }
    return 1024;
}

std::vector<std::string> read_signatures(const std::string &path)
{
    std::ifstream f(path);
    std::string s;
    std::vector<std::string> out;
    while (std::getline(f, s))
    {
        if (!s.empty())
            out.push_back(s);
    }
    return out;
}

uint64_t get_file_size(const std::string &path)
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f)
        return 0;
    return f.tellg();
}

std::vector<char> read_chunk(const std::string &path, uint64_t start, uint64_t len)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return {};
    f.seekg(0, std::ios::end);
    uint64_t filesize = f.tellg();
    if (start >= filesize)
        return {};
    uint64_t toread = std::min(len, filesize - start);
    std::vector<char> buf(toread);
    f.seekg(start, std::ios::beg);
    f.read(buf.data(), toread);
    return buf;
}

std::vector<uint64_t> find_pattern_avx2(const std::vector<char> &buf, uint64_t global_offset, const std::string &pat)
{
    std::vector<uint64_t> matches;
    size_t n = buf.size();
    if (pat.empty() || n == 0)
        return matches;
    size_t patlen = pat.size();

    if (patlen == 1)
    {
        char c = pat[0];
        for (size_t i = 0; i < n; ++i)
        {
            if (buf[i] == c)
                matches.push_back(global_offset + i);
        }
        return matches;
    }

    const char first = pat[0];
    size_t i = 0;
    __m256i firstv = _mm256_set1_epi8(first);

    for (; i + 32 <= n; i += 32)
    {
        __m256i block = _mm256_loadu_si256((const __m256i *)(buf.data() + i));
        __m256i cmp = _mm256_cmpeq_epi8(block, firstv);
        unsigned int mask = (unsigned int)_mm256_movemask_epi8(cmp);

        while (mask)
        {
            unsigned int bit = __builtin_ctz(mask);
            size_t pos = i + bit;
            if (pos + patlen <= n)
            {
                if (memcmp(buf.data() + pos, pat.data(), patlen) == 0)
                {
                    matches.push_back(global_offset + pos);
                }
            }
            mask &= mask - 1;
        }
    }

    for (; i < n; ++i)
    {
        if (buf[i] == first)
        {
            if (i + patlen <= n)
            {
                if (memcmp(buf.data() + i, pat.data(), patlen) == 0)
                {
                    matches.push_back(global_offset + i);
                }
            }
        }
    }
    return matches;
}

json process_chunk(const std::string &filepath, uint64_t start, uint64_t length, 
                   const std::vector<std::string> &sigs, int rank)
{
    auto buf = read_chunk(filepath, start, length);
    
    json result;
    for (const auto &sig : sigs)
    {
        auto matches = find_pattern_avx2(buf, start, sig);
        if (!matches.empty())
        {
            result[sig] = matches;
        }
    }
    
    return result;
}

void master_process(int world_size, const std::string &filepath, const std::string &sigfile, const std::string &outpath)
{
    std::cout << "\n========================================\n";
    std::cout << "  MPI DISTRIBUTED SIGNATURE SCANNER\n";
    std::cout << "========================================\n\n";

    uint64_t filesize = get_file_size(filepath);
    std::cout << "[MASTER] File size: " << filesize / (1024 * 1024) << " MB ("
              << filesize / (1024 * 1024 * 1024) << " GB)\n";

    auto sigs = read_signatures(sigfile);
    std::cout << "[MASTER] Loaded " << sigs.size() << " signatures\n\n";

    // Collect RAM information
    std::vector<NodeInfo> nodes(world_size);
    nodes[0] = {0, get_available_ram_mb(), true, MPI_Wtime()};
    std::cout << "[MASTER] Master RAM: " << nodes[0].available_ram_mb << " MB ("
              << nodes[0].available_ram_mb / 1024.0 << " GB)\n";

    for (int i = 1; i < world_size; i++)
    {
        uint64_t worker_ram;
        MPI_Recv(&worker_ram, 1, MPI_UINT64_T, i, TAG_RAM_INFO, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        nodes[i] = {i, worker_ram, true, MPI_Wtime()};
        std::cout << "[MASTER] Worker " << i << " RAM: " << worker_ram << " MB ("
                  << worker_ram / 1024.0 << " GB)\n";
    }

    // Create chunks
    const uint64_t CHUNK_SIZE_MB = 100;
    const uint64_t CHUNK_SIZE_BYTES = CHUNK_SIZE_MB * 1024 * 1024;

    std::vector<WorkChunk> chunks;
    uint64_t offset = 0;

    while (offset < filesize)
    {
        uint64_t chunk_len = std::min(CHUNK_SIZE_BYTES, filesize - offset);
        chunks.push_back({offset, chunk_len, -1, false, false});
        offset += chunk_len;
    }

    std::cout << "\n[MASTER] Created " << chunks.size() << " chunks of " << CHUNK_SIZE_MB << " MB each\n";

    // RAM-BASED DISTRIBUTION
    uint64_t total_ram = 0;
    for (const auto &node : nodes)
    {
        total_ram += node.available_ram_mb;
    }

    std::cout << "[MASTER] Total cluster RAM: " << total_ram << " MB (" 
              << total_ram / 1024.0 << " GB)\n\n";
    std::cout << "--- RAM-BASED WORK DISTRIBUTION ---\n";

    int chunk_idx = 0;
    for (int node = 0; node < world_size; node++)
    {
        double ram_proportion = (double)nodes[node].available_ram_mb / total_ram;
        int chunks_for_node = (int)(chunks.size() * ram_proportion);
        
        if (node == world_size - 1)
        {
            chunks_for_node = chunks.size() - chunk_idx;
        }

        std::cout << "  Node " << node << ": " << chunks_for_node << " chunks ("
                  << (chunks_for_node * CHUNK_SIZE_MB) << " MB) - "
                  << (int)(ram_proportion * 100.0) << "% of work\n";

        for (int i = 0; i < chunks_for_node && chunk_idx < (int)chunks.size(); i++, chunk_idx++)
        {
            chunks[chunk_idx].assigned_to = node;
        }
    }
    std::cout << "========================================\n\n";

    // Initialize result structure
    json final_result;
    for (auto &sig : sigs)
    {
        final_result[sig] = json::array();
    }

    // Track work
    std::map<int, int> worker_current_chunk;                // map worker -> chunk index (currently processing)
    std::map<int, std::vector<int>> worker_pending_chunks; // map worker -> vector of chunk indices pending
    std::vector<int> master_pending_chunks;                // explicit queue for chunks assigned to master

    for (size_t ci = 0; ci < chunks.size(); ci++)
    {
        int assigned_node = chunks[ci].assigned_to;
        if (assigned_node == 0)
            master_pending_chunks.push_back(ci);
        else
            worker_pending_chunks[assigned_node].push_back(ci);
    }

    // Send initial work to workers (NON-BLOCKING to avoid deadlock)
    std::cout << "\n--- SENDING INITIAL WORK TO WORKERS ---\n";
    for (int i = 1; i < world_size; i++)
    {
        if (!worker_pending_chunks[i].empty())
        {
            int chunk_idx = worker_pending_chunks[i].front();
            worker_pending_chunks[i].erase(worker_pending_chunks[i].begin());
            
            uint64_t work_data[2] = {chunks[chunk_idx].start, chunks[chunk_idx].length};
            
            // Use immediate mode send to avoid blocking
            MPI_Request req;
            MPI_Isend(work_data, 2, MPI_UINT64_T, i, TAG_WORK, MPI_COMM_WORLD, &req);
            MPI_Request_free(&req);  // We don't need to track this
            
            worker_current_chunk[i] = chunk_idx;
            chunks[chunk_idx].in_progress = true;
            std::cout << "[ASSIGN] Chunk " << chunk_idx << " → Worker " << i << "\n";
        }
    }
    std::cout << "--- INITIAL ASSIGNMENTS COMPLETE ---\n";

    int completed_chunks = 0;
    double last_heartbeat_check = MPI_Wtime();
    double last_status_print = MPI_Wtime();
    size_t master_chunk_idx = 0;

    std::cout << "\n--- PROCESSING STARTED ---\n";
    
    // Give workers time to be ready
    usleep(100000);  // 100ms delay

    while (completed_chunks < (int)chunks.size())
    {
        MPI_Status status;
        int flag;

        // CRITICAL: Use non-blocking probe to check for messages
        MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &flag, &status);

        if (flag)
        {
            int source = status.MPI_SOURCE;
            int tag = status.MPI_TAG;

            if (tag == TAG_RESULT)
            {
                int result_size;
                MPI_Recv(&result_size, 1, MPI_INT, source, TAG_RESULT, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

                std::vector<char> result_buffer(result_size);
                MPI_Recv(result_buffer.data(), result_size, MPI_CHAR, source, TAG_RESULT, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

                std::string result_str(result_buffer.begin(), result_buffer.end());
                json chunk_result = json::parse(result_str);

                for (auto &sig : sigs)
                {
                    if (chunk_result.contains(sig) && chunk_result[sig].is_array())
                    {
                        for (auto &match : chunk_result[sig])
                        {
                            final_result[sig].push_back(match);
                        }
                    }
                }

                // Find which chunk this worker finished
                int completed_chunk_idx = -1;
                if (worker_current_chunk.count(source) > 0)
                {
                    completed_chunk_idx = worker_current_chunk[source];
                    worker_current_chunk.erase(source);
                }
                else
                {
                    // If worker_current_chunk doesn't have it, try to find by scanning chunks (defensive)
                    for (size_t k = 0; k < chunks.size(); ++k)
                    {
                        if (chunks[k].in_progress && chunks[k].assigned_to == source)
                        {
                            completed_chunk_idx = k;
                            break;
                        }
                    }
                }

                if (completed_chunk_idx >= 0)
                {
                    chunks[completed_chunk_idx].completed = true;
                    chunks[completed_chunk_idx].in_progress = false;
                    completed_chunks++;

                    std::cout << "[DONE] Worker " << source << " completed chunk " << completed_chunk_idx
                              << " (" << completed_chunks << "/" << chunks.size() << ")\n";
                }
                else
                {
                    // Unknown finished chunk; just increment completed_chunks defensively (shouldn't happen normally)
                    std::cout << "[WARN] Received result from worker " << source << " but couldn't map to chunk index\n";
                }

                // Assign next chunk to this worker if any pending
                if (!worker_pending_chunks[source].empty())
                {
                    int next_chunk = worker_pending_chunks[source].front();
                    worker_pending_chunks[source].erase(worker_pending_chunks[source].begin());
                    
                    chunks[next_chunk].in_progress = true;
                    chunks[next_chunk].assigned_to = source;
                    
                    uint64_t work_data[2] = {chunks[next_chunk].start, chunks[next_chunk].length};
                    
                    // Non-blocking send to avoid deadlock
                    MPI_Request req;
                    MPI_Isend(work_data, 2, MPI_UINT64_T, source, TAG_WORK, MPI_COMM_WORLD, &req);
                    MPI_Request_free(&req);
                    
                    worker_current_chunk[source] = next_chunk;
                    std::cout << "[ASSIGN] Chunk " << next_chunk << " → Worker " << source << "\n";
                }
            }
            else if (tag == TAG_HEARTBEAT)
            {
                int dummy;
                MPI_Recv(&dummy, 1, MPI_INT, source, TAG_HEARTBEAT, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                nodes[source].last_heartbeat = MPI_Wtime();
                
                // Only print heartbeat occasionally to avoid spam
                if (MPI_Wtime() - last_status_print > 5.0)
                {
                    std::cout << "[HEARTBEAT] Worker " << source << " alive\n";
                }
            }
        }
        else
        {
            // No incoming messages — allow master to do its own work
            // Prefer explicit master pending queue first
            if (!master_pending_chunks.empty())
            {
                int master_ci = master_pending_chunks.front();
                master_pending_chunks.erase(master_pending_chunks.begin());

                if (!chunks[master_ci].completed)
                {
                    std::cout << "[MASTER-WORK] Processing chunk " << master_ci << "\n";
                    json chunk_result = process_chunk(filepath, chunks[master_ci].start, chunks[master_ci].length, sigs, 0);
                    
                    for (auto &sig : sigs)
                    {
                        if (chunk_result.contains(sig) && chunk_result[sig].is_array())
                        {
                            for (auto &match : chunk_result[sig])
                            {
                                final_result[sig].push_back(match);
                            }
                        }
                    }

                    chunks[master_ci].completed = true;
                    chunks[master_ci].in_progress = false;
                    completed_chunks++;
                    std::cout << "[DONE] Master completed chunk " << master_ci 
                              << " (" << completed_chunks << "/" << chunks.size() << ")\n";
                }
            }
            else
            {
                // fallback: scan forward for next master-owned chunk
                while (master_chunk_idx < chunks.size() && (chunks[master_chunk_idx].assigned_to != 0 || chunks[master_chunk_idx].completed))
                    master_chunk_idx++;

                if (master_chunk_idx < chunks.size() && chunks[master_chunk_idx].assigned_to == 0 && !chunks[master_chunk_idx].completed)
                {
                    std::cout << "[MASTER-WORK] Processing chunk " << master_chunk_idx << "\n";
                    json chunk_result = process_chunk(filepath, chunks[master_chunk_idx].start, chunks[master_chunk_idx].length, sigs, 0);
                    
                    for (auto &sig : sigs)
                    {
                        if (chunk_result.contains(sig) && chunk_result[sig].is_array())
                        {
                            for (auto &match : chunk_result[sig])
                            {
                                final_result[sig].push_back(match);
                            }
                        }
                    }

                    chunks[master_chunk_idx].completed = true;
                    chunks[master_chunk_idx].in_progress = false;
                    completed_chunks++;
                    std::cout << "[DONE] Master completed chunk " << master_chunk_idx 
                              << " (" << completed_chunks << "/" << chunks.size() << ")\n";
                    master_chunk_idx++;
                }
                else
                {
                    // nothing to do right now for master; sleep briefly
                    usleep(10000);  // 10ms
                }
            }
        }

        // CRITICAL FAILURE DETECTION AND RECOVERY
        double current_time = MPI_Wtime();
        if (current_time - last_heartbeat_check > 1.0)  // Check every second
        {
            last_heartbeat_check = current_time;
            
            for (int i = 1; i < world_size; i++)
            {
                double time_since_heartbeat = current_time - nodes[i].last_heartbeat;
                
                if (nodes[i].is_alive && time_since_heartbeat > HEARTBEAT_TIMEOUT)
                {
                    std::cout << "\n╔════════════════════════════════════════╗\n";
                    std::cout << "║   ⚠️  WORKER FAILURE DETECTED  ⚠️      ║\n";
                    std::cout << "╚════════════════════════════════════════╝\n";
                    std::cout << "Worker " << i << " FAILED!\n";
                    std::cout << "Last heartbeat: " << time_since_heartbeat << " seconds ago\n";
                    std::cout << "Status: DISCONNECTED / CRASHED\n\n";
                    
                    nodes[i].is_alive = false;

                    // Reassign current chunk (in-progress)
                    if (worker_current_chunk.count(i) > 0)
                    {
                        int failed_chunk = worker_current_chunk[i];
                        if (!chunks[failed_chunk].completed)
                        {
                            chunks[failed_chunk].in_progress = false;

                            // Find another alive worker or give to master
                            int reassign_to = 0;  // default to master
                            for (int j = 1; j < world_size; j++)
                            {
                                if (nodes[j].is_alive && j != i)
                                {
                                    reassign_to = j;
                                    break;
                                }
                            }

                            // Update chunk bookkeeping
                            chunks[failed_chunk].assigned_to = reassign_to;

                            if (reassign_to == 0)
                            {
                                master_pending_chunks.insert(master_pending_chunks.begin(), failed_chunk);
                            }
                            else
                            {
                                // push front so it will be handled soon
                                worker_pending_chunks[reassign_to].insert(
                                    worker_pending_chunks[reassign_to].begin(), failed_chunk);

                                // if target worker is idle, immediately send
                                if (worker_current_chunk.count(reassign_to) == 0)
                                {
                                    int next_chunk = worker_pending_chunks[reassign_to].front();
                                    worker_pending_chunks[reassign_to].erase(worker_pending_chunks[reassign_to].begin());

                                    uint64_t work_data[2] = {chunks[next_chunk].start, chunks[next_chunk].length};
                                    MPI_Request req;
                                    MPI_Isend(work_data, 2, MPI_UINT64_T, reassign_to, TAG_WORK, MPI_COMM_WORLD, &req);
                                    MPI_Request_free(&req);

                                    worker_current_chunk[reassign_to] = next_chunk;
                                    chunks[next_chunk].in_progress = true;
                                    std::cout << "[ASSIGN-RECOVER] Chunk " << next_chunk << " → Worker " << reassign_to << " (immediate)\n";
                                }
                            }

                            std::cout << "→ Reassigning in-progress chunk " << failed_chunk 
                                      << " to " << (reassign_to == 0 ? "Master" : ("Worker " + std::to_string(reassign_to))) << "\n";
                        }
                        worker_current_chunk.erase(i);
                    }

                    // Reassign all pending chunks for the failed worker
                    if (!worker_pending_chunks[i].empty())
                    {
                        int reassigned_count = worker_pending_chunks[i].size();
                        std::cout << "→ Reassigning " << reassigned_count << " pending chunks\n";

                        // Build list of alive worker targets (include master as 0)
                        std::vector<int> alive_targets;
                        for (int j = 0; j < world_size; j++)
                        {
                            if (j == i) continue;
                            if (j == 0 || nodes[j].is_alive) alive_targets.push_back(j);
                        }

                        // Simple round-robin distribution across alive_targets
                        int idx = 0;
                        for (int chunk_idx_to_move : worker_pending_chunks[i])
                        {
                            int target = alive_targets[idx % alive_targets.size()];
                            idx++;

                            // Update chunk bookkeeping
                            chunks[chunk_idx_to_move].assigned_to = target;
                            chunks[chunk_idx_to_move].in_progress = false;

                            if (target == 0)
                            {
                                master_pending_chunks.push_back(chunk_idx_to_move);
                            }
                            else
                            {
                                worker_pending_chunks[target].push_back(chunk_idx_to_move);

                                // If target worker is idle, immediately send the chunk
                                if (worker_current_chunk.count(target) == 0)
                                {
                                    int next_chunk = worker_pending_chunks[target].front();
                                    worker_pending_chunks[target].erase(worker_pending_chunks[target].begin());

                                    uint64_t work_data[2] = {chunks[next_chunk].start, chunks[next_chunk].length};
                                    MPI_Request req;
                                    MPI_Isend(work_data, 2, MPI_UINT64_T, target, TAG_WORK, MPI_COMM_WORLD, &req);
                                    MPI_Request_free(&req);

                                    worker_current_chunk[target] = next_chunk;
                                    chunks[next_chunk].in_progress = true;
                                    std::cout << "[ASSIGN-RECOVER] Chunk " << next_chunk << " → Worker " << target << " (immediate)\n";
                                }
                            }
                        }

                        worker_pending_chunks[i].clear();

                        std::cout << "→ All work reassigned to remaining " << alive_targets.size() << " nodes\n";
                    }
                    
                    std::cout << "✓ Recovery complete. Continuing execution...\n";
                    std::cout << "════════════════════════════════════════\n\n";
                }
            }
        }
        
        // Print progress occasionally
        if (current_time - last_status_print > 10.0)
        {
            last_status_print = current_time;
            std::cout << "\n[PROGRESS] " << completed_chunks << "/" << chunks.size() 
                      << " chunks completed (" << (100.0 * completed_chunks / chunks.size()) << "%)\n\n";
        }
    }

    std::cout << "\n--- SIGNALING WORKERS TO STOP ---\n";
    // Signal workers to stop (non-blocking)
    for (int i = 1; i < world_size; i++)
    {
        if (!nodes[i].is_alive)
        {
            std::cout << "→ Worker " << i << " is dead; skipping stop signal\n";
            continue;
        }
        uint64_t stop_signal[2] = {0, 0};
        MPI_Request req;
        MPI_Isend(stop_signal, 2, MPI_UINT64_T, i, TAG_WORK, MPI_COMM_WORLD, &req);
        MPI_Request_free(&req);
        std::cout << "→ Stop signal sent to worker " << i << "\n";
    }
    
    // Give workers time to receive stop signal
    usleep(500000);  // 500ms

    final_result["meta"] = {
        {"filesize", filesize},
        {"num_chunks", chunks.size()},
        {"num_workers", world_size - 1},
        {"total_ram_mb", total_ram}};

    std::ofstream of(outpath);
    of << final_result.dump(2);
    std::cout << "\n[SUCCESS] Results written to " << outpath << "\n";
}

void worker_process(int rank, const std::string &filepath, const std::string &sigfile)
{
    std::cout << "[Worker " << rank << "] Starting up...\n";

    uint64_t my_ram = get_available_ram_mb();
    MPI_Send(&my_ram, 1, MPI_UINT64_T, 0, TAG_RAM_INFO, MPI_COMM_WORLD);

    auto sigs = read_signatures(sigfile);
    std::cout << "[Worker " << rank << "] Loaded " << sigs.size() << " signatures\n";
    std::cout << "[Worker " << rank << "] Ready for work. Heartbeat active.\n";

    double last_heartbeat = MPI_Wtime();

    while (true)
    {
        // Send periodic heartbeat
        double current_time = MPI_Wtime();
        if (current_time - last_heartbeat >= HEARTBEAT_INTERVAL)
        {
            int heartbeat = 1;
            MPI_Send(&heartbeat, 1, MPI_INT, 0, TAG_HEARTBEAT, MPI_COMM_WORLD);
            last_heartbeat = current_time;
        }

        // Check for work with timeout
        MPI_Status status;
        int flag;
        MPI_Iprobe(0, TAG_WORK, MPI_COMM_WORLD, &flag, &status);

        if (!flag)
        {
            usleep(100000);  // 100ms
            continue;
        }

        uint64_t work_data[2];
        MPI_Recv(work_data, 2, MPI_UINT64_T, 0, TAG_WORK, MPI_COMM_WORLD, &status);

        uint64_t start = work_data[0];
        uint64_t length = work_data[1];

        if (start == 0 && length == 0)
        {
            std::cout << "[Worker " << rank << "] Received stop signal. Shutting down.\n";
            break;
        }

        std::cout << "[Worker " << rank << "] Processing chunk: offset=" << start
                  << ", size=" << length / (1024 * 1024) << " MB\n";

        json result = process_chunk(filepath, start, length, sigs, rank);

        std::string result_str = result.dump();
        int result_size = result_str.size();
        MPI_Send(&result_size, 1, MPI_INT, 0, TAG_RESULT, MPI_COMM_WORLD);
        MPI_Send(result_str.data(), result_size, MPI_CHAR, 0, TAG_RESULT, MPI_COMM_WORLD);

        std::cout << "[Worker " << rank << "] ✓ Chunk completed and sent\n";
        
        // Send heartbeat after completing work
        int heartbeat = 1;
        MPI_Send(&heartbeat, 1, MPI_INT, 0, TAG_HEARTBEAT, MPI_COMM_WORLD);
        last_heartbeat = MPI_Wtime();
    }
}

int main(int argc, char **argv)
{
    MPI_Init(&argc, &argv);

    int world_rank, world_size;
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);

    if (argc != 4)
    {
        if (world_rank == 0)
        {
            std::cerr << "Usage: mpirun -np <N> " << argv[0] << " signatures.txt input.bin out.json\n";
        }
        MPI_Finalize();
        return 1;
    }

    std::string sigfile = argv[1];
    std::string infile = argv[2];
    std::string outpath = argv[3];

    double start_time = MPI_Wtime();

    if (world_rank == 0)
    {
        master_process(world_size, infile, sigfile, outpath);
    }
    else
    {
        worker_process(world_rank, infile, sigfile);
    }

    double end_time = MPI_Wtime();

    if (world_rank == 0)
    {
        double throughput = (get_file_size(infile) / (1024.0 * 1024.0)) / (end_time - start_time);
        std::cout << "\n========================================\n";
        std::cout << "  EXECUTION COMPLETE\n";
        std::cout << "========================================\n";
        std::cout << "Total time: " << (end_time - start_time) << " seconds\n";
        std::cout << "Throughput: " << throughput << " MB/s\n";
        std::cout << "========================================\n\n";
    }

    MPI_Finalize();
    return 0;
}
