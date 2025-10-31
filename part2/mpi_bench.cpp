// mpi_bench.cpp
// Compile: mpicxx -O3 -mavx2 -std=c++17 mpi_bench.cpp -o mpi_bench
// Usage (example):
// mpirun -np 4 --hostfile hosts.txt ./mpi_bench signatures.txt big_sample_10GB.bin out_dir

#include <mpi.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <deque> 
#include <sstream>
#include <atomic>
#include <map>
#include <set>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "json.hpp" // nlohmann json single-header

using json = nlohmann::json;
using namespace std::chrono;

///////////////////////////////////////////////////////////////////////////////
// Constants and message tags
///////////////////////////////////////////////////////////////////////////////
enum Tags {
    TAG_RAM_REPORT = 1,
    TAG_REQUEST_TASK = 2,
    TAG_ASSIGN_TASK = 3,
    TAG_TASK_DONE = 4,
    TAG_HEARTBEAT = 5,
    TAG_NO_MORE_TASKS = 6
};

struct Task { uint64_t start; uint64_t length; int id; };

///////////////////////////////////////////////////////////////////////////////
// Utility
///////////////////////////////////////////////////////////////////////////////
uint64_t file_size_bytes(const std::string &path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return 0;
    return (uint64_t) st.st_size;
}

std::vector<std::string> read_signatures(const std::string &path) {
    std::ifstream f(path);
    std::string s;
    std::vector<std::string> out;
    while (std::getline(f,s)) if(!s.empty()) out.push_back(s);
    return out;
}

std::vector<char> read_chunk_stream(const std::string &path, uint64_t start, size_t len) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    f.seekg(0, std::ios::end);
    uint64_t filesize = f.tellg();
    if (start >= filesize) return {};
    uint64_t toread = std::min<uint64_t>(len, filesize - start);
    std::vector<char> buf(toread);
    f.seekg(start, std::ios::beg);
    f.read(buf.data(), toread);
    return buf;
}

///////////////////////////////////////////////////////////////////////////////
// CPU feature detection (simple)
///////////////////////////////////////////////////////////////////////////////
bool cpu_has_avx2() {
#if defined(__x86_64__) || defined(_M_X64)
    // naive check: try compile-time presence; at runtime check /proc/cpuinfo if available
    std::ifstream f("/proc/cpuinfo");
    if (!f) return false;
    std::string s; std::string all;
    while (std::getline(f, s)) all += s + "\n";
    return all.find("avx2") != std::string::npos;
#else
    return false;
#endif
}

///////////////////////////////////////////////////////////////////////////////
// Scalar pattern find (from your code)
///////////////////////////////////////////////////////////////////////////////
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

///////////////////////////////////////////////////////////////////////////////
// AVX2-accelerated find (from your code). Works if built with -mavx2; falls
// back to scalar if AVX2 not present or pattern length small.
///////////////////////////////////////////////////////////////////////////////
#if defined(__AVX2__) || defined(__SSE4_1__)
#include <immintrin.h>
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
#else
// If compiled without AVX2, fall back to scalar
std::vector<uint64_t> find_pattern_avx2(const std::vector<char>& buf, uint64_t global_offset, const std::string &pat) {
    return find_pattern_scalar(buf, global_offset, pat);
}
#endif

///////////////////////////////////////////////////////////////////////////////
// Worker scanning routine: streaming windows, uses AVX2 if available
///////////////////////////////////////////////////////////////////////////////
json scan_range_streaming(const std::string &sigfile, const std::string &infile,
                          uint64_t start, uint64_t length, uint64_t worker_id) {
    auto sigs = read_signatures(sigfile);
    const size_t WINDOW = 64 * 1024 * 1024; // 64 MB window, adjust if needed
    uint64_t offset = start;
    uint64_t remaining = length;
    std::map<std::string, std::vector<uint64_t>> found;

    bool has_avx2 = cpu_has_avx2();

    while (remaining > 0) {
        size_t toread = (size_t) std::min<uint64_t>(WINDOW, remaining);
        auto buf = read_chunk_stream(infile, offset, toread);
        if (buf.empty()) break;
        // For each pattern, find matches in this window
        for (const auto &p : sigs) {
            std::vector<uint64_t> matches;
            if (has_avx2) matches = find_pattern_avx2(buf, offset, p);
            else matches = find_pattern_scalar(buf, offset, p);
            if (!matches.empty()) {
                auto &v = found[p];
                v.insert(v.end(), matches.begin(), matches.end());
            }
        }
        offset += toread;
        remaining -= toread;
    }

    json out;
    for (auto &kv : found) out[kv.first] = kv.second;
    out["meta"] = { {"start", start}, {"length", (int)length}, {"worker", (int)worker_id} };
    return out;
}

///////////////////////////////////////////////////////////////////////////////
// Master: create tasks based on available RAM (proportional), but actually
// we will create many small tasks to enable fine-grained reassign.
///////////////////////////////////////////////////////////////////////////////
std::vector<Task> create_tasks_proportional(uint64_t filesize,
                                             const std::map<int, uint64_t> &usable_ram_map,
                                             uint64_t min_task_size = 32*1024*1024, // 32MB
                                             uint64_t max_task_size = 512*1024*1024 // 512MB
                                             ) {
    // Sum usable RAM
    uint64_t total_ram = 0;
    for (auto &p : usable_ram_map) total_ram += p.second;
    if (total_ram == 0) total_ram = usable_ram_map.size() * (64ULL<<20); // fallback

    // We'll create tasks by computing an ideal "task size per RAM unit"
    // But to improve fault tolerance we create many smaller tasks.
    // Strategy: compute proportional sizes, then split into fixed SUBTASK_SIZE chunks.
    // Choose SUBTASK_SIZE relative to average usable RAM but bounded by min/max.
    // Compute average usable RAM per node
    double avg_ram = (double)total_ram / std::max<size_t>(1, usable_ram_map.size());
    uint64_t subtask = std::clamp<uint64_t>((uint64_t)(avg_ram/4), min_task_size, max_task_size);
    // But ensure subtask divides the file into many pieces
    if (subtask == 0) subtask = min_task_size;

    std::vector<Task> tasks;
    uint64_t id = 0;
    uint64_t offset = 0;
    while (offset < filesize) {
        uint64_t this_len = std::min<uint64_t>(subtask, filesize - offset);
        tasks.push_back({offset, this_len, (int)id++});
        offset += this_len;
    }
    // Optionally shuffle tasks a little to distribute load
    //std::shuffle(tasks.begin(), tasks.end(), std::mt19937{std::random_device{}()});
    return tasks;
}

///////////////////////////////////////////////////////////////////////////////
// Master main loop: accepts messages, assigns tasks, receives results, detects
// unresponsive workers (heartbeat), requeues tasks as needed.
///////////////////////////////////////////////////////////////////////////////
int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int world_rank, world_size;
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);

    if (argc < 4) {
        if (world_rank == 0) {
            std::cerr << "Usage: mpirun -np N ./mpi_bench signatures.txt input.bin out_dir\n";
        }
        MPI_Finalize();
        return 1;
    }

    std::string sigfile = argv[1];
    std::string infile = argv[2];
    std::string outdir = argv[3];
    mkdir(outdir.c_str(), 0755);

    // Master
    if (world_rank == 0) {
        // Step A: ask each worker to send usable RAM & start-time heartbeat
        std::map<int, uint64_t> usable_ram_map; // rank -> usable bytes
        std::set<int> live_workers;
        for (int r = 1; r < world_size; ++r) live_workers.insert(r);

        // Wait for RAM reports (with timeout)
        std::cout << "[master] waiting for RAM reports from workers...\n";
        auto start_wait = steady_clock::now();
        double wait_seconds = 5.0;
        while ((steady_clock::now() - start_wait) < seconds((int)wait_seconds) && usable_ram_map.size() < (size_t)(world_size-1)) {
            MPI_Status st;
            int flag=0;
            MPI_Iprobe(MPI_ANY_SOURCE, TAG_RAM_REPORT, MPI_COMM_WORLD, &flag, &st);
            if (flag) {
                uint64_t ram;
                MPI_Recv(&ram, 1, MPI_UNSIGNED_LONG_LONG, st.MPI_SOURCE, TAG_RAM_REPORT, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                usable_ram_map[st.MPI_SOURCE] = ram;
                std::cout << "[master] got RAM report from rank " << st.MPI_SOURCE << ": " << (ram>>20) << " MiB\n";
            } else {
                std::this_thread::sleep_for(milliseconds(100));
            }
        }
        // For any missing workers, assign a conservative default
        for (int r = 1; r < world_size; ++r) if (!usable_ram_map.count(r)) {
            usable_ram_map[r] = 256ULL << 20; // default 256MB usable
            std::cout << "[master] no RAM report from " << r << ", using default 256MB\n";
        }

        // Create tasks from filesize and usable_ram_map
        uint64_t fsize = file_size_bytes(infile);
        if (fsize == 0) { std::cerr << "[master] input file not found or size 0\n"; MPI_Abort(MPI_COMM_WORLD, 1); }
        std::cout << "[master] input file size: " << (fsize>>20) << " MiB\n";

        auto tasks = create_tasks_proportional(fsize, usable_ram_map);
        std::deque<Task> task_queue(tasks.begin(), tasks.end());
        std::map<int, Task> assigned; // worker -> task
        std::map<int, steady_clock::time_point> last_heartbeat;
        std::map<int, bool> worker_alive;
        for (int r = 1; r < world_size; ++r) { worker_alive[r] = true; last_heartbeat[r] = steady_clock::now(); }

        std::vector<std::string> result_files;
        std::cout << "[master] created " << task_queue.size() << " tasks. entering dispatch loop...\n";

        const int HEARTBEAT_TIMEOUT_SEC = 30; // if no heartbeat in this many seconds consider worker stalled
        const int POLL_SLEEP_MS = 50;

        // dispatch loop
        while (!task_queue.empty() || !assigned.empty()) {
            // probe for messages
            MPI_Status st;
            int flag=0;
            MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &flag, &st);
            if (flag) {
                if (st.MPI_TAG == TAG_REQUEST_TASK) {
                    // worker requests a task
                    int dummy;
                    MPI_Recv(&dummy, 1, MPI_INT, st.MPI_SOURCE, TAG_REQUEST_TASK, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                    // if have a task, send task triple, else send NO_MORE_TASKS
                    if (!task_queue.empty()) {
                        Task t = task_queue.front();
                        task_queue.pop_front();
                        assigned[st.MPI_SOURCE] = t;
                        uint64_t msg[3] = { t.start, t.length, (uint64_t)t.id };
                        MPI_Send(msg, 3, MPI_UNSIGNED_LONG_LONG, st.MPI_SOURCE, TAG_ASSIGN_TASK, MPI_COMM_WORLD);
                        last_heartbeat[st.MPI_SOURCE] = steady_clock::now();
                        //console
                        std::cout << "[master] assigned task " << t.id << " to " << st.MPI_SOURCE << " (" << (t.length>>20) << " MiB)\n";
                    } else {
                        // no tasks left
                        int zero = 0;
                        MPI_Send(&zero, 1, MPI_INT, st.MPI_SOURCE, TAG_NO_MORE_TASKS, MPI_COMM_WORLD);
                    }
                } else if (st.MPI_TAG == TAG_TASK_DONE) {
                    // worker finished a task and will send string path length then path
                    int pathlen;
                    MPI_Recv(&pathlen, 1, MPI_INT, st.MPI_SOURCE, TAG_TASK_DONE, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                    std::string path(pathlen, '\0');
                    MPI_Recv(path.data(), pathlen, MPI_CHAR, st.MPI_SOURCE, TAG_TASK_DONE, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                    std::cout << "[master] worker " << st.MPI_SOURCE << " DONE -> " << path << "\n";
                    result_files.push_back(path);
                    // remove assignment
                    if (assigned.count(st.MPI_SOURCE)) assigned.erase(st.MPI_SOURCE);
                    last_heartbeat[st.MPI_SOURCE] = steady_clock::now();
                } else if (st.MPI_TAG == TAG_HEARTBEAT) {
                    // receive heartbeat (no payload)
                    int dummy;
                    MPI_Recv(&dummy, 1, MPI_INT, st.MPI_SOURCE, TAG_HEARTBEAT, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                    last_heartbeat[st.MPI_SOURCE] = steady_clock::now();
                    //std::cout << "[master] heartbeat from " << st.MPI_SOURCE << "\n";
                } else if (st.MPI_TAG == TAG_RAM_REPORT) {
                    uint64_t ram;
                    MPI_Recv(&ram, 1, MPI_UNSIGNED_LONG_LONG, st.MPI_SOURCE, TAG_RAM_REPORT, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                    std::cout << "[master] late RAM report from " << st.MPI_SOURCE << ": " << (ram>>20) << " MiB\n";
                    // ignore or update
                } else {
                    // unexpected tag
                    MPI_Recv(MPI_BOTTOM, 0, MPI_BYTE, st.MPI_SOURCE, st.MPI_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                }
            } else {
                // no incoming messages -> check heartbeats/timeouts
                for (auto &p : assigned) {
                    int worker = p.first;
                    auto tstart = last_heartbeat[worker];
                    auto now = steady_clock::now();
                    if (duration_cast<seconds>(now - tstart).count() > HEARTBEAT_TIMEOUT_SEC) {
                        // consider worker stalled, requeue its task
                        std::cout << "[master] worker " << worker << " heartbeat timeout; requeuing task " << p.second.id << "\n";
                        task_queue.push_front(p.second); // requeue at front
                        assigned.erase(worker);
                        worker_alive[worker] = false;
                        // note: we do not attempt to forcibly kill worker here; assume admin will handle or job will continue
                        break;
                    }
                }
                std::this_thread::sleep_for(milliseconds(POLL_SLEEP_MS));
            }
        } // dispatch loop

        // Merge results
        std::cout << "[master] all tasks done; merging " << result_files.size() << " partial results\n";
        // Merge logic: read each JSON and combine arrays by signature
        json merged;
        uint64_t total_scanned = 0;
        for (auto &p : result_files) {
            std::ifstream in(p);
            if (!in) continue;
            json j; in >> j;
            for (auto it = j.begin(); it != j.end(); ++it) {
                if (it.key() == "meta") {
                    if (it.value().contains("length")) total_scanned += (uint64_t)it.value()["length"].get<int>();
                    continue;
                }
                if (!merged.contains(it.key())) merged[it.key()] = json::array();
                for (auto &off : it.value()) merged[it.key()].push_back(off);
            }
        }
        merged["meta"] = { {"start", 0}, {"length", (int)total_scanned} };
        std::string final_path = outdir + "/out_merged.json";
        std::ofstream of(final_path);
        of << merged.dump(2);
        std::cout << "[master] wrote merged results -> " << final_path << "\n";

        // compute simple timing/throughput - (in this code we did not measure per-worker time; you can extend)
        // Send email summary by shelling out to send_email.py (same as your earlier script)
        // Build summary string similar to your bench_10gb.sh
        std::ostringstream summary;
        summary << "Distributed bench run (master)\\n";
        summary << "Input: " << infile << "\\n";
        summary << "Tasks: " << tasks.size() << ", partial files: " << result_files.size() << "\\n";
        summary << "Merged results: " << final_path << "\\n";

        // call external send_email.py if present
        if (access("send_email.py", X_OK) == 0) {
            std::string cmd = "python3 send_email.py \"omshingare120@gmail.com\" \"pcproject@coeptech.ac.in\" \"[Group-14] Distributed Bench Results\" \"" + summary.str() + "\"";
            std::cout << "[master] invoking: " << cmd << "\n";
            int rc = system(cmd.c_str());
            if (rc != 0) std::cout << "[master] send_email.py returned " << rc << "\n";
        } else {
            std::cout << "[master] send_email.py not found/executable; skipping email send\n";
        }

    } else {
        // Worker
        // 1) report usable RAM to master
        // compute usable RAM: read /proc/meminfo or use sysconf
        uint64_t mem_available = 0;
        std::ifstream memf("/proc/meminfo");
        if (memf) {
            std::string line;
            while (std::getline(memf, line)) {
                if (line.rfind("MemAvailable:", 0) == 0) {
                    std::istringstream iss(line);
                    std::string key; uint64_t kb;
                    iss >> key >> kb;
                    mem_available = kb * 1024ULL;
                    break;
                }
            }
        }
        if (mem_available == 0) {
            long pages = sysconf(_SC_PHYS_PAGES);
            long page_size = sysconf(_SC_PAGE_SIZE);
            if (pages > 0 && page_size > 0) mem_available = (uint64_t)pages * (uint64_t)page_size / 4; // heuristic
        }
        // safety factor: use only 70% of available for buffer sizing
        uint64_t usable = (uint64_t)(mem_available * 0.70);
        MPI_Send(&usable, 1, MPI_UNSIGNED_LONG_LONG, 0, TAG_RAM_REPORT, MPI_COMM_WORLD);

        // Worker main loop: request tasks, do scanning, send results, send heartbeat periodically.
        bool keep_working = true;
        const int HEARTBEAT_INTERVAL_SEC = 5;
        auto last_hb = steady_clock::now();

        while (keep_working) {
            // send request
            int dummy = 1;
            MPI_Send(&dummy, 1, MPI_INT, 0, TAG_REQUEST_TASK, MPI_COMM_WORLD);

            // wait for assignment or NO_MORE_TASKS
            MPI_Status st;
            // blocking recv but with a timeout implemented by probing
            bool received=false;
            while (!received) {
                int flag=0;
                MPI_Iprobe(0, MPI_ANY_TAG, MPI_COMM_WORLD, &flag, &st);
                if (flag) {
                    if (st.MPI_TAG == TAG_ASSIGN_TASK) {
                        uint64_t msg[3];
                        MPI_Recv(msg, 3, MPI_UNSIGNED_LONG_LONG, 0, TAG_ASSIGN_TASK, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                        Task t{msg[0], msg[1], (int)msg[2]};
                        // perform scan on range t
                        std::cout << "[worker " << world_rank << "] got task " << t.id << " start=" << t.start << " len=" << t.length << "\n";
                        auto start_time = steady_clock::now();
                        json out = scan_range_streaming(sigfile, infile, t.start, t.length, world_rank);
                        auto dur = duration_cast<milliseconds>(steady_clock::now() - start_time).count();
                        // write local partial file
                        std::ostringstream fname;
                        fname << outdir << "/out_rank" << world_rank << "_task" << t.id << ".json";
                        std::string path = fname.str();
                        std::ofstream of(path);
                        of << out.dump(2);
                        // notify master of completion: send length of path then path
                        int plen = (int)path.size();
                        MPI_Send(&plen, 1, MPI_INT, 0, TAG_TASK_DONE, MPI_COMM_WORLD);
                        MPI_Send(path.c_str(), plen, MPI_CHAR, 0, TAG_TASK_DONE, MPI_COMM_WORLD);
                        // record heartbeat
                        last_hb = steady_clock::now();
                        received = true;
                    } else if (st.MPI_TAG == TAG_NO_MORE_TASKS) {
                        int zero;
                        MPI_Recv(&zero, 1, MPI_INT, 0, TAG_NO_MORE_TASKS, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                        keep_working = false;
                        received = true;
                    } else {
                        // unexpected
                        MPI_Recv(MPI_BOTTOM, 0, MPI_BYTE, 0, st.MPI_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                    }
                } else {
                    // send heartbeat periodically
                    auto now = steady_clock::now();
                    if (duration_cast<seconds>(now - last_hb).count() >= HEARTBEAT_INTERVAL_SEC) {
                        int one = 1;
                        MPI_Send(&one, 1, MPI_INT, 0, TAG_HEARTBEAT, MPI_COMM_WORLD);
                        last_hb = now;
                    }
                    std::this_thread::sleep_for(milliseconds(100));
                }
            } // waiting for assignment
        } // worker loop
    }

    MPI_Finalize();
    return 0;
}
