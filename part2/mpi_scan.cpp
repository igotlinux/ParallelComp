#include <mpi.h>
#include <fstream>
#include <iostream>
#include <vector>
#include <string>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

std::vector<char> read_chunk(const std::string &file, uint64_t start, uint64_t len) {
    std::ifstream f(file, std::ios::binary);
    if (!f) return {};
    f.seekg(0, std::ios::end);
    uint64_t filesize = f.tellg();
    if (start >= filesize) return {};
    len = std::min(len, filesize - start);
    std::vector<char> buf(len);
    f.seekg(start);
    f.read(buf.data(), len);
    return buf;
}

int main(int argc, char** argv) {
    MPI_Init(&argc,&argv);
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD,&rank);
    MPI_Comm_size(MPI_COMM_WORLD,&size);

    if(argc<4){
        if(rank==0) std::cerr<<"Usage: "<<argv[0]<<" input_file chunk_size num_chunks\n";
        MPI_Finalize();
        return 1;
    }

    std::string infile = argv[1];
    uint64_t chunk_size = std::stoull(argv[2]);
    uint64_t total_chunks = std::stoull(argv[3]);

    if(rank==0){
        // Master
        json merged;
        uint64_t next_chunk = 0;
        int num_workers = size - 1;

        // Send initial chunks to workers
        for(int w=1; w<=num_workers && next_chunk<total_chunks; ++w){
            uint64_t start = next_chunk * chunk_size;
            MPI_Send(&start,1,MPI_UINT64_T,w,0,MPI_COMM_WORLD);
            MPI_Send(&chunk_size,1,MPI_UINT64_T,w,0,MPI_COMM_WORLD);
            next_chunk++;
        }

        int finished_workers = 0;
        while(finished_workers < num_workers){
            MPI_Status status;
            MPI_Probe(MPI_ANY_SOURCE,MPI_ANY_TAG,MPI_COMM_WORLD,&status);
            int worker = status.MPI_SOURCE;

            uint64_t msg_size;
            MPI_Recv(&msg_size,1,MPI_UINT64_T,worker,0,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
            std::vector<char> buf(msg_size);
            MPI_Recv(buf.data(), msg_size, MPI_CHAR, worker,0,MPI_COMM_WORLD,MPI_STATUS_IGNORE);

            json chunk_json = json::parse(buf.begin(),buf.end());
            merged.update(chunk_json);

            // Send next chunk or terminate
            if(next_chunk < total_chunks){
                uint64_t start = next_chunk * chunk_size;
                MPI_Send(&start,1,MPI_UINT64_T,worker,0,MPI_COMM_WORLD);
                MPI_Send(&chunk_size,1,MPI_UINT64_T,worker,0,MPI_COMM_WORLD);
                next_chunk++;
            } else {
                uint64_t term = UINT64_MAX;
                MPI_Send(&term,1,MPI_UINT64_T,worker,0,MPI_COMM_WORLD);
                MPI_Send(&term,1,MPI_UINT64_T,worker,0,MPI_COMM_WORLD);
                finished_workers++;
            }
std::cout << "Rank " << rank << " started\n" << std::flush;

        }


        std::ofstream out("merged.json");
        out << merged.dump(2);
        std::cout<<"Master: merged.json written\n";

    } else {
        // Worker
        while(true){
            uint64_t start,len;
            MPI_Recv(&start,1,MPI_UINT64_T,0,0,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
            MPI_Recv(&len,1,MPI_UINT64_T,0,0,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
            if(start==UINT64_MAX) break;

            auto buf = read_chunk(infile,start,len);
            json out;
            out["chunk_start"] = start;
            out["chunk_size"] = buf.size();

            std::string out_str = out.dump();
            uint64_t msg_size = out_str.size();
            MPI_Send(&msg_size,1,MPI_UINT64_T,0,0,MPI_COMM_WORLD);
            MPI_Send(out_str.data(), msg_size, MPI_CHAR,0,0,MPI_COMM_WORLD);

            std::cout<<"Rank "<<rank<<" processed chunk "<<start<<"\n"<<std::flush;
        }
    }

    MPI_Finalize();
    return 0;
}
