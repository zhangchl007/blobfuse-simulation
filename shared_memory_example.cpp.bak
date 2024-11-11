// Example: Incorporating changes from shared_memory_example.cpp.new

#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/managed_mapped_file.hpp>
#include <boost/interprocess/permissions.hpp>
#include <chrono>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <unordered_map>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <iostream>
#include <atomic>
#include <mutex>
#include <cerrno>
#include <cstdint>  // Include for uint64_t
#include <memory>  // Add for std::align

using namespace std::chrono;  // Add this after the includes

const char* kMmfItemFeatureMapName = "ItemFeatureMap";
const char* kMmfItemFeatureVecName = "ItemFeatureVec";
const uint64_t kMinimumFileSize = 4096ULL * 1000ULL;  // Use uint64_t to avoid overflow
const size_t kBlockSize = 4096;  // Filesystem block size

class ItemFeatureHandlerV2 {
public:
    bool Update(const std::string& file);
    void Reserve(size_t size);
    void Set(const std::string& key, std::pair<const char*, size_t> value);
    void StartContinuousUpdate(const std::string& file, int update_interval_ms);
    bool WriteToSharedMemory(const std::string& shared_memory_file);
    bool ReadFromSharedMemory(const std::string& shared_memory_file);

public:
    std::atomic<bool> running_{true};

private:
    struct alignas(4096) Header {  // Align to 4KB boundary
        size_t map_size;
        size_t vec_size;
        char padding[4080];  // Pad to 4KB
    };

    std::unordered_map<std::string, std::vector<char>> data_map_;
    std::vector<std::pair<std::string, std::vector<char>>> data_vec_;
    std::mutex data_mutex_;  // Mutex for thread-safe access

    bool DependencyCheck(const std::string& file, std::string* updating_file);
    size_t CalculateRequiredSize();
    bool EnsureFileSize(const std::string& file, size_t size);
};

bool ItemFeatureHandlerV2::DependencyCheck(const std::string& file, std::string* updating_file) {
    std::cout << "Dependency check for " << file << std::endl;
    return true;  // Simulated success
}

void ItemFeatureHandlerV2::Reserve(size_t size) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    data_map_.reserve(size);
    data_vec_.reserve(size);
    std::cout << "Reserved space for " << size << " elements in data_map_ and data_vec_." << std::endl;
}

void ItemFeatureHandlerV2::Set(const std::string& key, std::pair<const char*, size_t> value) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    std::vector<char> data(value.first, value.first + value.second);
    data_map_[key] = data;
    data_vec_.emplace_back(key, data);
    std::cout << "Stored data for key: " << key << " with size: " << value.second << std::endl;
}

// Calculate required size for shared memory file based on data size
size_t ItemFeatureHandlerV2::CalculateRequiredSize() {
    std::lock_guard<std::mutex> lock(data_mutex_);

    size_t size = 0;
    for (const auto& pair : data_map_) {
        size += sizeof(pair.first) + pair.first.size() + sizeof(pair.second) + pair.second.size();
    }
    for (const auto& pair : data_vec_) {
        size += sizeof(pair.first) + pair.first.size() + sizeof(pair.second) + pair.second.size();
    }

    // Ensure minimum file size to avoid small allocations
    return std::max(size, kMinimumFileSize);
}

// Ensure file is at least the specified size
bool ItemFeatureHandlerV2::EnsureFileSize(const std::string& file, size_t size) {
    // Round up size to 4KB alignment
    size = (size + 4095) & ~4095ULL;
    
    int fd = open(file.c_str(), O_RDWR | O_CREAT | O_DIRECT, 0644);  // Add O_DIRECT flag
    if (fd == -1) {
        std::cerr << "Failed to open file: " << file << " Error: " << strerror(errno) << std::endl;
        return false;
    }

    int res = posix_fallocate(fd, 0, size);
    close(fd);

    if (res != 0) {
        std::cerr << "Failed to allocate space for file: " << file << " Error: " << strerror(res) << std::endl;
        return false;
    }

    return true;
}

bool ItemFeatureHandlerV2::Update(const std::string& file) {
    std::string updating_file(file);

    if (!DependencyCheck(file, &updating_file)) {
        std::cerr << "Dependency check failed, stop updating file " << file << std::endl;
        return false;
    }

    std::cout << "Begin to update file: " << updating_file << std::endl;

    auto start_time = system_clock::now();

    // Check if file exists and create if necessary
    if (access(updating_file.c_str(), F_OK) == -1) {
        int fd = open(updating_file.c_str(), O_CREAT | O_RDWR, 0644);
        if (fd == -1) {
            std::cerr << "Failed to create file: " << updating_file << std::endl;
            return false;
        }
        close(fd);
        std::cout << "File created: " << updating_file << std::endl;
    }

    // Set file permissions
    if (chmod(updating_file.c_str(), 0644) == -1) {
        std::cerr << "Failed to set file permissions: " << updating_file << std::endl;
        return false;
    }

    try {
        // Lock the data while we're working with it
        std::lock_guard<std::mutex> lock(data_mutex_);

        std::cout << "Current data sizes - Map: " << data_map_.size() 
                  << ", Vec: " << data_vec_.size() << std::endl;

        // Calculate required size
        size_t required_size = CalculateRequiredSize();
        
        if (!EnsureFileSize(updating_file, required_size)) {
            std::cerr << "Failed to ensure file size" << std::endl;
            return false;
        }

        // Write directly to the file
        if (!WriteToSharedMemory(updating_file)) {
            std::cerr << "Failed to write to shared memory file" << std::endl;
            return false;
        }

        auto load_duration = std::chrono::duration<double>(system_clock::now() - start_time).count();
        std::cout << "File update completed in " << load_duration << " seconds" << std::endl;

        return true;
    }
    catch (const std::exception& ex) {
        std::cerr << "Exception in Update: " << ex.what() << std::endl;
        return false;
    }
}

bool ItemFeatureHandlerV2::WriteToSharedMemory(const std::string& shared_memory_file) {
    std::lock_guard<std::mutex> lock(data_mutex_);

    try {
        std::cout << "Writing to shared memory file: " << shared_memory_file << std::endl;
        std::cout << "Current data sizes - Map: " << data_map_.size() 
                  << ", Vec: " << data_vec_.size() << std::endl;

        // Calculate total size needed
        size_t required_size = sizeof(Header);
        for (const auto& pair : data_map_) {
            required_size += sizeof(size_t) * 2;
            required_size += pair.first.size();
            required_size += pair.second.size();
        }
        
        // Round up to block size
        required_size = ((required_size + kBlockSize - 1) / kBlockSize) * kBlockSize;
        required_size = std::max(required_size, kMinimumFileSize);

        // Step 1: Create and allocate the file with O_DIRECT
        int fd = open(shared_memory_file.c_str(), O_RDWR | O_CREAT | O_DIRECT, 0644);
        if (fd == -1) {
            std::cerr << "Failed to open file with O_DIRECT: " << strerror(errno) << std::endl;
            return false;
        }

        // Allocate space
        int res = posix_fallocate(fd, 0, required_size);
        close(fd);
        if (res != 0) {
            std::cerr << "Failed to allocate space: " << strerror(res) << std::endl;
            return false;
        }

        // Step 2: Create file mapping without O_DIRECT
        boost::interprocess::file_mapping file_mapping(
            shared_memory_file.c_str(),
            boost::interprocess::read_write
        );

        // Map the region
        boost::interprocess::mapped_region region(
            file_mapping,
            boost::interprocess::read_write
        );

        // Get the address
        char* addr = static_cast<char*>(region.get_address());
        
        // Clear the memory
        std::memset(addr, 0, region.get_size());

        // Write header
        Header header{data_map_.size(), data_vec_.size()};
        std::memcpy(addr, &header, sizeof(Header));
        size_t offset = sizeof(Header);

        // Write map data
        for (const auto& pair : data_map_) {
            // Align to block boundary
            offset = ((offset + kBlockSize - 1) / kBlockSize) * kBlockSize;
            char* current_pos = addr + offset;

            // Write key size
            size_t key_size = pair.first.size();
            std::memcpy(current_pos, &key_size, sizeof(size_t));
            current_pos += sizeof(size_t);
            offset += sizeof(size_t);

            // Write key
            std::memcpy(current_pos, pair.first.c_str(), key_size);
            current_pos += key_size;
            offset += key_size;

            // Write value size
            size_t value_size = pair.second.size();
            std::memcpy(current_pos, &value_size, sizeof(size_t));
            current_pos += sizeof(size_t);
            offset += sizeof(size_t);

            // Write value
            std::memcpy(current_pos, pair.second.data(), value_size);
            offset += value_size;
        }

        // Ensure data is written to disk
        region.flush();

        std::cout << "Successfully written to shared memory file: " << shared_memory_file << "\n"
                  << "Total size: " << required_size << " bytes\n"
                  << "Map entries: " << header.map_size << "\n"
                  << "Vec entries: " << header.vec_size << std::endl;

        return true;
    }
    catch (const std::exception& ex) {
        std::cerr << "Exception in WriteToSharedMemory: " << ex.what() << std::endl;
        return false;
    }
}

// Add this method to verify the data was written correctly
bool ItemFeatureHandlerV2::ReadFromSharedMemory(const std::string& shared_memory_file) {
    try {
        // Create file mapping
        boost::interprocess::file_mapping file(
            shared_memory_file.c_str(), 
            boost::interprocess::read_only
        );

        // Map the region
        boost::interprocess::mapped_region region(
            file, 
            boost::interprocess::read_only
        );

        const char* mapped_data = static_cast<const char*>(region.get_address());

        // Read header
        const Header* header = reinterpret_cast<const Header*>(mapped_data);
        size_t offset = sizeof(Header);

        std::cout << "Reading from shared memory file:\n"
                  << "Map entries: " << header->map_size << "\n"
                  << "Vec entries: " << header->vec_size << std::endl;

        // Read map entries
        for (size_t i = 0; i < header->map_size; ++i) {
            // Align offset to 512 bytes
            offset = ((offset + 511) / 512) * 512;

            // Read key size
            size_t key_size;
            std::memcpy(&key_size, mapped_data + offset, sizeof(size_t));
            offset += sizeof(size_t);

            // Read key
            std::string key(mapped_data + offset, key_size);
            offset += key_size;

            // Read value size
            size_t value_size;
            std::memcpy(&value_size, mapped_data + offset, sizeof(size_t));
            offset += sizeof(size_t);

            // Skip value data but print info
            std::cout << "Entry " << i << ": Key=" << key 
                     << ", Value size=" << value_size << " bytes" << std::endl;
            offset += value_size;
        }

        return true;
    }
    catch (const std::exception& ex) {
        std::cerr << "Exception in ReadFromSharedMemory: " << ex.what() << std::endl;
        return false;
    }
}

void ItemFeatureHandlerV2::StartContinuousUpdate(const std::string& file, int update_interval_ms) {
    while (running_) {
        if (!Update(file)) {
            std::cerr << "Update failed, stopping continuous updates." << std::endl;
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(update_interval_ms));
    }
}

int main() {
    ItemFeatureHandlerV2 handler;

    // Add test data
    std::string test_value1 = "test_value_data_1";
    std::string test_value2 = "test_value_data_2";
    
    std::cout << "\n=== Adding test data ===" << std::endl;
    handler.Set("test_key_1", {test_value1.c_str(), test_value1.size()});
    handler.Set("test_key_2", {test_value2.c_str(), test_value2.size()});

    const std::string shared_memory_path = "/app/html/file_backed_shared_memory";

    std::cout << "\n=== Initial write to shared memory ===" << std::endl;
    if (!handler.WriteToSharedMemory(shared_memory_path)) {
        std::cerr << "Initial write failed!" << std::endl;
        return 1;
    }

    std::cout << "\n=== Reading back data ===" << std::endl;
    if (!handler.ReadFromSharedMemory(shared_memory_path)) {
        std::cerr << "Initial read failed!" << std::endl;
        return 1;
    }

    std::cout << "\n=== Starting continuous update ===" << std::endl;
    std::thread updater([&handler, &shared_memory_path]() {
        handler.StartContinuousUpdate(shared_memory_path, 5000);
    });

    // Wait for a few updates
    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::cout << "\n=== Verifying after updates ===" << std::endl;
    handler.ReadFromSharedMemory(shared_memory_path);

    // Clean up
    std::cout << "\n=== Cleaning up ===" << std::endl;
    handler.running_ = false;
    updater.join();

    return 0;
}
