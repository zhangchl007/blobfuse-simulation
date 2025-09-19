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
#include <sys/mman.h>
#include <sstream>
#include <atomic>
#include <sys/stat.h>
#include <csignal>
#include <fcntl.h>
#include <cstdio>
#include <sys/types.h>
#include <unistd.h>   // for close()

namespace bip = boost::interprocess;

// === Logging (轻量) ===
#ifndef SIMPLE_LOG_MACROS
#define SIMPLE_LOG_MACROS
#define LOG_ERROR  std::cerr << "[ERROR] "
#define LOG_INFO   std::cout << "[INFO] "
inline std::string FormatArgs() { return ""; }
template<typename T, typename... R>
std::string FormatArgs(T&& v, R&&... rest) {
    std::ostringstream oss;
    oss << " " << v << FormatArgs(std::forward<R>(rest)...);
    return oss.str();
}
#define SPD_LOG_INFO(fmt, ...)  do { std::cout << "[INFO]" << fmt << FormatArgs(__VA_ARGS__) << std::endl; } while(0)
#endif

// === Page touch (预热页) ===
static void TouchPages(const char* base, std::size_t bytes) {
    static const std::size_t kPage = 4096;
    for (std::size_t off = 0; off < bytes; off += kPage) {
        volatile char c = base[off];
        (void)c;
    }
}

// === 冻结文件结构 ===
struct FrozenHeader {
    char     magic[8];
    uint32_t version;
    uint32_t model_cnt;
    uint32_t bucket_cnt;
    uint32_t entry_cnt;
    uint32_t val_pool_sz;
};
using Header = FrozenHeader;  // 兼容引用代码
struct Model { uint32_t model_id; uint32_t version; };
struct Entry { uint32_t key_hash; uint32_t value_offset; uint32_t value_size; };

// === Loader ===
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#endif

// 移除 MMapFile，直接使用 boost::interprocess
class FrozenHashMapImpl {
public:
    bool Build(const std::string& file) {
        auto begin = std::chrono::system_clock::now();
        file_path_ = file;

        try {
            fmap_   = std::make_unique<bip::file_mapping>(file.c_str(), bip::read_only);
            region_ = std::make_unique<bip::mapped_region>(*fmap_, bip::read_only);
        } catch (const std::exception& ex) {
            LOG_ERROR << "boost mmap failed: " << file << " err=" << ex.what() << std::endl;
            return false;
        }

        base_ = static_cast<const char*>(region_->get_address());
        std::size_t fsz = region_->get_size();
        if (fsz < sizeof(Header)) {
            LOG_ERROR << "file too small: " << file << std::endl;
            return false;
        }

        hdr_ = reinterpret_cast<const Header*>(base_);
        if (std::strncmp(hdr_->magic, "STRATEGY", 8) != 0 || hdr_->version != 1) {
            LOG_ERROR << "bad header in file: " << file
                      << ", magic: " << std::string(hdr_->magic, 8)
                      << ", version: " << hdr_->version << std::endl;
            return false;
        }

        models_   = reinterpret_cast<const Model*>(base_ + sizeof(Header));
        bucket_   = reinterpret_cast<const uint32_t*>(models_ + hdr_->model_cnt);
        entries_  = reinterpret_cast<const Entry*>(bucket_ + hdr_->bucket_cnt);
        val_pool_ = reinterpret_cast<const char*>(entries_ + hdr_->entry_cnt);
        mask_     = hdr_->bucket_cnt ? (hdr_->bucket_cnt - 1) : 0;
        size_     = hdr_->entry_cnt;

        PrefetchAndTouch(fsz);

        std::stringstream ss;
        ss << "load model:";
        for (uint32_t i = 0; i < hdr_->model_cnt; ++i) {
            auto m = models_[i];
            if (m.model_id == 0 || m.version == 0) {
                LOG_ERROR << "bad model in file: " << file
                          << ", model_id: " << m.model_id
                          << ", version: " << m.version << std::endl;
                return false;
            }
            ss << " <" << m.model_id << ":" << m.version << ">";
        }

        double cost = std::chrono::duration<double>(
            std::chrono::system_clock::now() - begin).count();
        SPD_LOG_INFO(" {} success", ss.str());
        SPD_LOG_INFO(" kv file: {}, entry count: {}, bucket count: {}, value pool size: {}, successfully !, cost: {:.2f}s",
                     file, hdr_->entry_cnt, hdr_->bucket_cnt, hdr_->val_pool_sz, cost);
        return true;
    }

private:
    void PrefetchAndTouch(std::size_t sz) {
        if (!base_ || sz == 0) return;
        ::madvise(const_cast<char*>(base_), sz, MADV_WILLNEED);
#ifdef MADV_POPULATE_READ
        ::madvise(const_cast<char*>(base_), sz, MADV_POPULATE_READ);
#endif
        TouchPages(base_, sz);
    }

private:
    std::string file_path_;
    std::unique_ptr<bip::file_mapping>   fmap_;
    std::unique_ptr<bip::mapped_region>  region_;

    const char* base_{nullptr};
    const Header* hdr_{nullptr};
    const Model* models_{nullptr};
    const uint32_t* bucket_{nullptr};
    const Entry* entries_{nullptr};
    const char* val_pool_{nullptr};
    uint32_t mask_{0};
    uint32_t size_{0};
};

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

// === 工具函数 ===
static std::string GetEnvOrDefault(const char* k, const std::string& defv) {
    const char* v = std::getenv(k);
    return (v && *v) ? std::string(v) : defv;
}
static bool FileExistsNonEmpty(const std::string& path) {
    struct stat st{};
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0;
}
static bool AtomicWriteFile(const std::string& path, const std::string& content) {
    std::string tmp = path + ".tmp";
    std::ofstream ofs(tmp, std::ios::binary | std::ios::trunc);
    if (!ofs) return false;
    ofs.write(content.data(), content.size());
    ofs.close();
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}
static bool WriteManifest(const std::string& manifest_path, const std::string& target_path) {
    return AtomicWriteFile(manifest_path, target_path + "\n");
}
static bool ReadManifest(const std::string& manifest_path, std::string* out_target) {
    std::ifstream ifs(manifest_path);
    if (!ifs) return false;
    std::string line;
    while (std::getline(ifs, line)) {
        size_t a = line.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) continue;
        size_t b = line.find_last_not_of(" \t\r\n");
        *out_target = line.substr(a, b - a + 1);
        return true;
    }
    return false;
}

// === 生成大模型文件（简单 Header + 填充）===
static bool GenerateBigModelFile(const std::string& path,
                                 uint64_t total_bytes,
                                 uint32_t model_id = 1,
                                 uint32_t model_version = 1) {
    if (total_bytes < sizeof(FrozenHeader) + sizeof(Model)) {
        LOG_ERROR << "size too small: " << total_bytes << std::endl;
        return false;
    }
    int fd = ::open(path.c_str(), O_CREAT | O_RDWR, 0644);
    if (fd < 0) {
        LOG_ERROR << "open fail: " << path << " err=" << strerror(errno) << std::endl;
        return false;
    }
    if (posix_fallocate(fd, 0, (off_t)total_bytes) != 0) {
        LOG_ERROR << "posix_fallocate fail: " << path << std::endl;
        ::close(fd);
        return false;
    }
    void* addr = mmap(nullptr, total_bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
        LOG_ERROR << "mmap fail: " << path << " err=" << strerror(errno) << std::endl;
        ::close(fd);
        return false;
    }
    char* base = (char*)addr;
    std::memset(base, 0, total_bytes);
    FrozenHeader hdr{};
    std::memcpy(hdr.magic, "STRATEGY", 8);
    hdr.version = 1;
    hdr.model_cnt = 1;
    hdr.bucket_cnt = 0;
    hdr.entry_cnt = 0;
    hdr.val_pool_sz = (uint32_t)std::min<uint64_t>(UINT32_MAX,
                     total_bytes - sizeof(FrozenHeader) - sizeof(Model));
    std::memcpy(base, &hdr, sizeof(hdr));
    Model m{model_id, model_version};
    std::memcpy(base + sizeof(FrozenHeader), &m, sizeof(Model));
    msync(base, total_bytes, MS_SYNC);
    munmap(addr, total_bytes);
    ::close(fd);
    LOG_INFO << "Generated model file: " << path
             << " size=" << total_bytes
             << " model=" << model_id << ":" << model_version << std::endl;
    return true;
}

// === 监听 manifest 并热加载 ===
static void ManifestWatchLoop(const std::string& manifest,
                              int interval_sec,
                              std::atomic<bool>& running) {
    FrozenHashMapImpl loader;
    std::string current_target;
    time_t last_manifest_mtime = 0;
    time_t last_target_mtime = 0;

    while (running) {
        struct stat stm{};
        if (stat(manifest.c_str(), &stm) == 0) {
            if (stm.st_mtime != last_manifest_mtime) {
                last_manifest_mtime = stm.st_mtime;
                std::string new_target;
                if (ReadManifest(manifest, &new_target) && !new_target.empty()) {
                    if (new_target != current_target) {
                        LOG_INFO << "Manifest switch -> " << new_target << std::endl;
                        current_target = new_target;
                        if (FileExistsNonEmpty(current_target)) {
                            if (loader.Build(current_target)) {
                                struct stat stt{};
                                if (stat(current_target.c_str(), &stt) == 0)
                                    last_target_mtime = stt.st_mtime;
                            }
                        } else {
                            LOG_ERROR << "Target not ready: " << current_target << std::endl;
                        }
                    }
                }
            }
        } else {
            LOG_ERROR << "stat manifest fail: " << manifest << std::endl;
        }
        if (!current_target.empty()) {
            struct stat stt{};
            if (stat(current_target.c_str(), &stt) == 0) {
                if (stt.st_mtime != last_target_mtime) {
                    LOG_INFO << "Detected target update: " << current_target << std::endl;
                    if (loader.Build(current_target)) last_target_mtime = stt.st_mtime;
                }
            }
        }
        for (int i = 0; i < interval_sec * 10 && running; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

// === Writer 循环：生成 N 个版本并滚动 manifest ===
static int WriterLoop() {
    std::string base = GetEnvOrDefault("MODEL_BASE", "/mnt/blobfuse/frozen_kv");
    uint64_t size_bytes = std::strtoull(
        GetEnvOrDefault("FILE_SIZE_BYTES","2147483648").c_str(), nullptr, 10);
    int version_cnt = std::atoi(GetEnvOrDefault("VERSION_COUNT","5").c_str());
    int interval_sec = std::atoi(GetEnvOrDefault("VERSION_UPDATE_INTERVAL_SEC","5").c_str());
    int cycles = std::atoi(GetEnvOrDefault("CYCLES","0").c_str()); // 0 = infinite

    if (version_cnt <= 0) version_cnt = 5;
    LOG_INFO << "WriterLoop start base=" << base
             << " size=" << size_bytes
             << " versions=" << version_cnt
             << " interval=" << interval_sec
             << " cycles=" << cycles << std::endl;

    const std::string manifest = base + ".manifest";
    int cycle = 0;
    while (cycles == 0 || cycle < cycles) {
        for (int v = 1; v <= version_cnt; ++v) {
            std::ostringstream fname;
            fname << base << "_v" << v;
            // 简单生成（重写覆盖触发 mtime）
            if (!GenerateBigModelFile(fname.str(), size_bytes, 1000 + v, v)) {
                LOG_ERROR << "Generate file failed, abort." << std::endl;
                return EXIT_FAILURE;
            }
            if (!WriteManifest(manifest, fname.str())) {
                LOG_ERROR << "Write manifest failed" << std::endl;
                return EXIT_FAILURE;
            }
            LOG_INFO << "Manifest -> " << fname.str() << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(interval_sec));
        }
        ++cycle;
    }
    return EXIT_SUCCESS;
}

// === Reader Watch ===
static int ReaderWatch(const std::string& manifest, int interval_sec) {
    if (!FileExistsNonEmpty(manifest)) {
        LOG_INFO << "Waiting manifest: " << manifest << std::endl;
        int wait = 0;
        while (wait < 120 && !FileExistsNonEmpty(manifest)) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            ++wait;
        }
        if (!FileExistsNonEmpty(manifest)) {
            LOG_ERROR << "Manifest not ready: " << manifest << std::endl;
            return EXIT_FAILURE;
        }
    }
    std::atomic<bool> running(true);
    signal(SIGINT, [](int){});
    LOG_INFO << "Start watch manifest=" << manifest
             << " interval=" << interval_sec << "s" << std::endl;
    ManifestWatchLoop(manifest, interval_sec, running);
    return EXIT_SUCCESS;
}

int main(int argc, char* argv[]) {
    SPD_LOG_INFO(" starting args_count={} {}", argc, (argc>1?argv[1]:"(none)"));
    std::string mode = (argc > 1) ? argv[1] : "";
    if (mode == "writer-loop") {
        return WriterLoop();
    } else if (mode == "watch") {
        // Allow omission of manifest path -> derive from MODEL_BASE
        std::string manifest;
        if (argc >= 3) {
            manifest = argv[2];
        } else {
            std::string base = GetEnvOrDefault("MODEL_BASE", "/mnt/blobfuse/frozen_kv");
            manifest = base + ".manifest";
            SPD_LOG_INFO(" no manifest arg, fallback {}", manifest);
        }
        int interval = 5;
        if (argc >= 4) {
            interval = std::max(1, std::atoi(argv[3]));
        } else {
            interval = std::max(1, std::atoi(GetEnvOrDefault("WATCH_INTERVAL_SEC","5").c_str()));
        }
        SPD_LOG_INFO(" watch mode manifest={} interval={}s", manifest, interval);
        return ReaderWatch(manifest, interval);
    } else {
        std::cerr << "Usage:\n"
                  << "  " << argv[0] << " writer-loop\n"
                  << "  " << argv[0] << " watch [manifest_path] [interval_sec]\n";
        return EXIT_FAILURE;
    }
}
