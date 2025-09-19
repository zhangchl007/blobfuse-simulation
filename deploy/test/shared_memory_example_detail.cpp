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
#include <atomic>
#include <mutex>
#include <cerrno>
#include <cstdint>
#include <memory>
#include <sstream>
#include <cstring>

// Using namespace for chrono
using namespace std::chrono;

// === Added logging macros and utility ===
#ifndef LOGGING_MACROS_ADDED
#define LOGGING_MACROS_ADDED
#define LOG_ERROR  std::cerr << "[ERROR] "
#define LOG_INFO   std::cout << "[INFO] "
#define SPD_LOG_INFO(fmt, ...)  do { std::cout << "[INFO] " << fmt << FormatArgs(__VA_ARGS__) << std::endl; } while(0)

inline std::string FormatArgs() { return ""; }
template<typename T, typename... R>
std::string FormatArgs(T&& v, R&&... rest) {
    std::ostringstream oss;
    oss << " " << v << FormatArgs(std::forward<R>(rest)...);
    return oss.str();
}
#endif

// === Page touching helper (pre-fault pages) ===
static void TouchPages(const char* base, std::size_t bytes) {
    static const std::size_t kPage = 4096;
    for (std::size_t off = 0; off < bytes; off += kPage) {
        volatile char c = base[off];
        (void)c;
    }
}

// === Structures for FrozenHashMapImpl (separate from ItemFeatureHandlerV2) ===
struct FrozenHeader {
    char     magic[8];       // expect "STRATEGY"
    uint32_t version;
    uint32_t model_cnt;
    uint32_t bucket_cnt;
    uint32_t entry_cnt;
    uint32_t val_pool_sz;
};

struct Model {
    uint32_t model_id;
    uint32_t version;
};

struct Entry {
    uint32_t key_hash;
    uint32_t value_offset;
    uint32_t value_size;
};

class FrozenHashMapImpl {
public:
    bool Build(const std::string& file);

private:
    std::string file_path_;
    std::unique_ptr<boost::interprocess::file_mapping> fmap_;
    std::unique_ptr<boost::interprocess::mapped_region> region_;

    const char*         base_    = nullptr;
    const FrozenHeader* hdr_     = nullptr;
    const Model*        models_  = nullptr;
    const uint32_t*     bucket_  = nullptr;
    const Entry*        entries_ = nullptr;
    const char*         val_pool_= nullptr;

    uint32_t mask_ = 0;
    uint32_t size_ = 0;
};

bool FrozenHashMapImpl::Build(const std::string& file) {
    auto begin = std::chrono::system_clock::now();
    file_path_ = file;

    try {
        fmap_   = std::make_unique<boost::interprocess::file_mapping>(file.c_str(), boost::interprocess::read_only);
        region_ = std::make_unique<boost::interprocess::mapped_region>(*fmap_, boost::interprocess::read_only);
    } catch (const std::exception& ex) {
        LOG_ERROR << "mmap file failed: " << file << ", error: " << ex.what() << std::endl;
        return false;
    }

    base_ = static_cast<const char*>(region_->get_address());
    if (region_->get_size() < sizeof(FrozenHeader)) {
        LOG_ERROR << "file too small for header: " << file << std::endl;
        return false;
    }

    hdr_ = reinterpret_cast<const FrozenHeader*>(base_);
    if (std::strncmp(hdr_->magic, "STRATEGY", 8) != 0 || hdr_->version != 1) {
        LOG_ERROR << "bad header in file: " << file << ", magic: "
                  << std::string(hdr_->magic, 8) << ", version: " << hdr_->version << std::endl;
        return false;
    }

    std::size_t offset = sizeof(FrozenHeader);
    models_ = reinterpret_cast<const Model*>(base_ + offset);
    offset += sizeof(Model) * hdr_->model_cnt;

    bucket_ = reinterpret_cast<const uint32_t*>(base_ + offset);
    offset += sizeof(uint32_t) * hdr_->bucket_cnt;

    entries_ = reinterpret_cast<const Entry*>(base_ + offset);
    offset += sizeof(Entry) * hdr_->entry_cnt;

    val_pool_ = reinterpret_cast<const char*>(base_ + offset);
    mask_ = hdr_->bucket_cnt ? (hdr_->bucket_cnt - 1) : 0;
    size_ = hdr_->entry_cnt;

    madvise(const_cast<char*>(base_), region_->get_size(), MADV_WILLNEED);
    TouchPages(base_, region_->get_size());

    std::stringstream ss;
    ss << "load model:";
    for (uint32_t i = 0; i < hdr_->model_cnt; ++i) {
        auto m = models_[i];
        if (m.model_id == 0 || m.version == 0) {
            LOG_ERROR << "bad model in file: " << file << ", model_id: " << m.model_id
                      << ", version: " << m.version << std::endl;
            return false;
        }
        ss << " <" << m.model_id << ":" << m.version << ">";
    }

    auto cost = std::chrono::duration<double>(std::chrono::system_clock::now() - begin).count();
    SPD_LOG_INFO(" {} success", ss.str());
    SPD_LOG_INFO(" kv file: {}, entry count: {}, bucket count: {}, value pool size: {}, successfully !, cost: {:.2f}s",
                 file, hdr_->entry_cnt, hdr_->bucket_cnt, hdr_->val_pool_sz, cost);

    return true;
}

const char* kMmfItemFeatureMapName = "ItemFeatureMap";
const char* kMmfItemFeatureVecName = "ItemFeatureVec";
const uint64_t kMinimumFileSize = 4096ULL * 1000ULL;
const size_t kBlockSize = 4096;

class ItemFeatureHandlerV2 {
public:
    bool Update(const std::string& file);
    void Reserve(size_t size);
    void Set(const std::string& key, std::pair<const char*, size_t> value);
    void StartContinuousUpdate(const std::string& file, int update_interval_ms);
    bool WriteToSharedMemory(const std::string& shared_memory_file); // public (locks)
    bool ReadFromSharedMemory(const std::string& shared_memory_file);

public:
    std::atomic<bool> running_{true};

private:
    struct alignas(4096) Header {
        size_t map_size;
        size_t vec_size;
        char   padding[4080];
    };

    std::unordered_map<std::string, std::vector<char>> data_map_;
    std::vector<std::pair<std::string, std::vector<char>>> data_vec_;
    std::mutex data_mutex_;

    bool DependencyCheck(const std::string& file, std::string* updating_file);
    size_t SerializeSizeUnlocked() const;
    size_t CalculateRequiredSize(); // wrapper
    bool EnsureFileSize(const std::string& file, size_t size);
    bool WriteToSharedMemoryUnlocked(const std::string& shared_memory_file); // core without locking
};

bool ItemFeatureHandlerV2::DependencyCheck(const std::string& file, std::string* updating_file) {
    (void)updating_file;
    std::cout << "Dependency check for " << file << std::endl;
    return true;
}

void ItemFeatureHandlerV2::Reserve(size_t size) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    data_map_.reserve(size);
    data_vec_.reserve(size);
    std::cout << "Reserved space for " << size << " elements." << std::endl;
}

void ItemFeatureHandlerV2::Set(const std::string& key, std::pair<const char*, size_t> value) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    std::vector<char> data(value.first, value.first + value.second);
    data_map_[key] = data;
    data_vec_.emplace_back(key, data);
    std::cout << "Stored key: " << key << " size: " << value.second << std::endl;
}

// Compute serialized payload size (header + entries aligned to kBlockSize).
size_t ItemFeatureHandlerV2::SerializeSizeUnlocked() const {
    size_t total = sizeof(Header);
    for (const auto& kv : data_map_) {
        // Layout per entry:
        // [size_t key_size][key bytes][size_t value_size][value bytes]
        size_t entry = sizeof(size_t) + kv.first.size()
                     + sizeof(size_t) + kv.second.size();
        // Align each entry start to kBlockSize (mirrors writer logic)
        total = ((total + kBlockSize - 1) / kBlockSize) * kBlockSize;
        total += entry;
    }
    total = ((total + kBlockSize - 1) / kBlockSize) * kBlockSize;
    if (total < kMinimumFileSize) total = kMinimumFileSize;
    return total;
}

size_t ItemFeatureHandlerV2::CalculateRequiredSize() {
    std::lock_guard<std::mutex> lock(data_mutex_);
    return SerializeSizeUnlocked();
}

bool ItemFeatureHandlerV2::EnsureFileSize(const std::string& file, size_t size) {
    size = (size + 4095) & ~4095ULL;
    int fd = open(file.c_str(), O_RDWR | O_CREAT, 0644);
    if (fd == -1) {
        std::cerr << "Failed to open file: " << file << " Error: " << strerror(errno) << std::endl;
        return false;
    }
    int res = posix_fallocate(fd, 0, size);
    close(fd);
    if (res != 0) {
        std::cerr << "Failed to allocate space for file: " << file
                  << " Error: " << strerror(res) << std::endl;
        return false;
    }
    return true;
}

bool ItemFeatureHandlerV2::WriteToSharedMemory(const std::string& shared_memory_file) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    return WriteToSharedMemoryUnlocked(shared_memory_file);
}

bool ItemFeatureHandlerV2::WriteToSharedMemoryUnlocked(const std::string& shared_memory_file) {
    try {
        size_t required_size = SerializeSizeUnlocked();

        if (!EnsureFileSize(shared_memory_file, required_size)) {
            std::cerr << "EnsureFileSize failed" << std::endl;
            return false;
        }

        boost::interprocess::file_mapping file_mapping(
            shared_memory_file.c_str(),
            boost::interprocess::read_write
        );
        boost::interprocess::mapped_region region(
            file_mapping,
            boost::interprocess::read_write
        );
        if (region.get_size() < required_size) {
            std::cerr << "Region smaller than required_size" << std::endl;
            return false;
        }

        char* addr = static_cast<char*>(region.get_address());
        std::memset(addr, 0, required_size);

        Header header{};
        header.map_size = data_map_.size();
        header.vec_size = data_vec_.size();
        std::memcpy(addr, &header, sizeof(Header));

        size_t offset = sizeof(Header);
        for (const auto& kv : data_map_) {
            offset = ((offset + kBlockSize - 1) / kBlockSize) * kBlockSize;
            char* current = addr + offset;

            size_t key_size = kv.first.size();
            std::memcpy(current, &key_size, sizeof(size_t));
            current += sizeof(size_t);
            offset += sizeof(size_t);

            std::memcpy(current, kv.first.data(), key_size);
            current += key_size;
            offset += key_size;

            size_t value_size = kv.second.size();
            std::memcpy(current, &value_size, sizeof(size_t));
            current += sizeof(size_t);
            offset += sizeof(size_t);

            std::memcpy(current, kv.second.data(), value_size);
            offset += value_size;
        }

        region.flush();
        madvise(addr, required_size, MADV_WILLNEED);
        TouchPages(addr, required_size);

        std::cout << "Written file: " << shared_memory_file
                  << " map_entries=" << header.map_size
                  << " vec_entries=" << header.vec_size
                  << " bytes=" << required_size << std::endl;
        return true;
    } catch (const std::exception& ex) {
        std::cerr << "WriteToSharedMemory exception: " << ex.what() << std::endl;
        return false;
    }
}

bool ItemFeatureHandlerV2::Update(const std::string& file) {
    std::string updating_file(file);
    if (!DependencyCheck(file, &updating_file)) {
        std::cerr << "Dependency check failed for " << file << std::endl;
        return false;
    }

    std::cout << "Begin update: " << updating_file << std::endl;
    auto start_time = system_clock::now();

    if (access(updating_file.c_str(), F_OK) == -1) {
        int fd = open(updating_file.c_str(), O_CREAT | O_RDWR, 0644);
        if (fd == -1) {
            std::cerr << "Failed to create file: " << updating_file << std::endl;
            return false;
        }
        close(fd);
        std::cout << "File created: " << updating_file << std::endl;
    }

    if (chmod(updating_file.c_str(), 0644) == -1) {
        std::cerr << "chmod failed: " << updating_file << std::endl;
        return false;
    }

    try {
        std::lock_guard<std::mutex> lock(data_mutex_);
        size_t required_size = SerializeSizeUnlocked();
        if (!EnsureFileSize(updating_file, required_size)) return false;
        if (!WriteToSharedMemoryUnlocked(updating_file)) return false;

        auto load_duration = std::chrono::duration<double>(system_clock::now() - start_time).count();
        std::cout << "Update completed in " << load_duration << " seconds" << std::endl;
        return true;
    } catch (const std::exception& ex) {
        std::cerr << "Update exception: " << ex.what() << std::endl;
        return false;
    }
}

bool ItemFeatureHandlerV2::ReadFromSharedMemory(const std::string& shared_memory_file) {
    try {
        boost::interprocess::file_mapping file(
            shared_memory_file.c_str(),
            boost::interprocess::read_only
        );
        boost::interprocess::mapped_region region(
            file,
            boost::interprocess::read_only
        );
        const char* mapped_data = static_cast<const char*>(region.get_address());

        madvise(const_cast<char*>(mapped_data), region.get_size(), MADV_WILLNEED);
        TouchPages(mapped_data, region.get_size());

        if (region.get_size() < sizeof(Header)) {
            std::cerr << "Region too small" << std::endl;
            return false;
        }

        const Header* header = reinterpret_cast<const Header*>(mapped_data);
        size_t offset = sizeof(Header);

        std::cout << "Reading shared memory file:\n"
                  << "Map entries: " << header->map_size << "\n"
                  << "Vec entries: " << header->vec_size << std::endl;

        for (size_t i = 0; i < header->map_size; ++i) {
            offset = ((offset + kBlockSize - 1) / kBlockSize) * kBlockSize;

            if (offset + sizeof(size_t) > region.get_size()) break;
            size_t key_size;
            std::memcpy(&key_size, mapped_data + offset, sizeof(size_t));
            offset += sizeof(size_t);
            if (offset + key_size + sizeof(size_t) > region.get_size()) break;

            std::string key(mapped_data + offset, key_size);
            offset += key_size;

            size_t value_size;
            std::memcpy(&value_size, mapped_data + offset, sizeof(size_t));
            offset += sizeof(size_t);
            if (offset + value_size > region.get_size()) break;

            std::cout << "Entry " << i << ": Key=" << key
                      << ", Value size=" << value_size << " bytes" << std::endl;
            offset += value_size;
        }
        return true;
    } catch (const std::exception& ex) {
        std::cerr << "ReadFromSharedMemory exception: " << ex.what() << std::endl;
        return false;
    }
}

void ItemFeatureHandlerV2::StartContinuousUpdate(const std::string& file, int update_interval_ms) {
    while (running_) {
        if (!Update(file)) {
            std::cerr << "Update failed, stopping." << std::endl;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(update_interval_ms));
    }
}

int main() {
    ItemFeatureHandlerV2 handler;
    std::string test_value1 = "test_value_data_1";
    std::string test_value2 = "test_value_data_2";
    handler.Set("test_key_1", {test_value1.c_str(), test_value1.size()});
    handler.Set("test_key_2", {test_value2.c_str(), test_value2.size()});

    const std::string shared_memory_path = "/app/html/file_backed_shared_memory";

    if (!handler.WriteToSharedMemory(shared_memory_path)) {
        std::cerr << "Initial write failed!" << std::endl;
        return 1;
    }

    std::cout << "\n=== Reading back data ===" << std::endl;
    if (!handler.ReadFromSharedMemory(shared_memory_path)) {
        std::cerr << "Initial read failed!" << std::endl;
        return 1;
    }

    FrozenHashMapImpl frozen_loader;
    // frozen_loader.Build("/app/html/frozen_kv_file");

    std::thread updater([&handler, &shared_memory_path]() {
        handler.StartContinuousUpdate(shared_memory_path, 5000);
    });

    std::this_thread::sleep_for(std::chrono::seconds(2));
    handler.ReadFromSharedMemory(shared_memory_path);
    handler.running_ = false;
    updater.join();
    return 0;
}