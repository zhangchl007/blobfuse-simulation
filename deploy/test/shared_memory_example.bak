#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <string>
#include <ctime>

// Namespace alias for convenience
namespace bip = boost::interprocess;

// Constants
const size_t FILE_SIZE = 2L * 1024 * 1024 * 1024; // 2GB
const std::string FILE_PATH = "/mnt/blobfuse/test_file.dat";

// Function to create and initialize the 2GB file
void create_large_file() {
    std::ofstream file(FILE_PATH, std::ios::binary);
    if (!file) {
        std::cerr << "Error: Could not create the file." << std::endl;
        return;
    }

    // Write a large file with null bytes
    std::vector<char> buffer(1024 * 1024, 0); // 1MB buffer
    for (size_t i = 0; i < FILE_SIZE / buffer.size(); ++i) {
        file.write(buffer.data(), buffer.size());
    }

    file.close();
    std::cout << "File created successfully." << std::endl;
}

// Function to get the current timestamp as a string
std::string get_current_timestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    return std::string(std::ctime(&now_c));
}

// Function to write data to the mapped region and sync periodically
void write_to_mapped_region() {
    try {
        bip::file_mapping fmap(FILE_PATH.c_str(), bip::read_write);
        bip::mapped_region region(fmap, bip::read_write);

        char* mem = static_cast<char*>(region.get_address());
        size_t offset = 0;

        while (true) {
            std::string timestamp = get_current_timestamp();
            std::string data = timestamp + ": New data added to the file.\n";
            
            if (offset + data.size() > region.get_size()) {
                offset = 0; // Wrap around if out of bounds
            }

            std::memcpy(mem + offset, data.c_str(), data.size());
            offset += data.size();

            region.flush(); // Sync the mapped region to the file
            std::cout << "Data written and synced: " << data << std::endl;

            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
}

// Function to simulate shared memory reading
void read_shared_memory() {
    try {
        bip::file_mapping fmap(FILE_PATH.c_str(), bip::read_only);
        bip::mapped_region region(fmap, bip::read_only);

        char* mem = static_cast<char*>(region.get_address());

        while (true) {
            std::cout << "Reading shared memory data: " << std::string(mem, 100) << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(3));
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
}

int main(int argc, char* argv[]) {
    std::string mode;
    if (argc > 1) {
        mode = argv[1];
    }

    if (mode == "writer") {
        create_large_file();
        write_to_mapped_region();
    } else if (mode == "reader") {
        read_shared_memory();
    } else {
        std::cerr << "Error: Specify mode as either 'writer' or 'reader'." << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
