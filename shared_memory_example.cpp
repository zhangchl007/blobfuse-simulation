#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/permissions.hpp>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <iostream>
#include <vector>
#include <cstring>
#include <cerrno>
#include <thread>
#include <chrono>

const size_t kBlockSize = 4096;  // Filesystem block size

bool EnsureFileSize(const std::string& file, size_t size) {
    int fd = open(file.c_str(), O_RDWR | O_CREAT, 0644);  // Removed O_DIRECT
    if (fd == -1) {
        std::cerr << "Failed to open file: " << file << " Error: " << strerror(errno) << std::endl;
        return false;
    }

    // Ensure the file size is a multiple of the block size
    size = ((size + kBlockSize - 1) / kBlockSize) * kBlockSize;

    int res = posix_fallocate(fd, 0, size);
    close(fd);

    if (res != 0) {
        std::cerr << "Failed to allocate space for file: " << file << " Error: " << strerror(res) << std::endl;
        return false;
    }

    return true;
}

void UseDirectIO(const std::string& file) {
    // Ensure the file size is a multiple of the block size
    size_t file_size = 65536;  // Example size
    if (!EnsureFileSize(file, file_size)) {
        return;
    }

    std::cout << "File size: " << file_size << std::endl;

    // Open the file without O_DIRECT
    int fd = open(file.c_str(), O_RDWR);
    if (fd == -1) {
        std::cerr << "Failed to open file: " << file << " Error: " << strerror(errno) << std::endl;
        return;
    }

    boost::interprocess::mapped_region region;
    try {
        // Create a file mapping
        boost::interprocess::file_mapping file_mapping(file.c_str(), boost::interprocess::read_write);

        // Map the file into memory
        region = boost::interprocess::mapped_region(file_mapping, boost::interprocess::read_write);

        // Use the mapped region
        char* addr = static_cast<char*>(region.get_address());
        std::memset(addr, 0, region.get_size());

        // Example: Write data to the mapped region
        std::strcpy(addr, "Hello, Boost Interprocess!");

    } catch (const std::exception& ex) {
        std::cerr << "Exception::interprocess_exception: " << ex.what() << std::endl;
    }

    // Loop to sync the region to the file periodically
    while (true) {
        region.flush();
        std::cout << "Region synced to file." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(5));  // Sync every 5 seconds
    }

    // Close the file descriptor
    close(fd);
}

int main() {
    std::string file = "/app/html/direct_io_example";
    UseDirectIO(file);
    return 0;
}