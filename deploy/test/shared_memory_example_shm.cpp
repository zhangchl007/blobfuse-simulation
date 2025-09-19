#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sys/types.h>
#include <sys/shm.h>
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <stdexcept>

const size_t LARGE_BLOCK_SIZE = 128 * 1024 * 1024; // 128MB

int main() {
    // Open file without O_DIRECT flag
    int fd = open("/app/html/direct_io_file", O_RDWR | O_CREAT, 0666);
    if (fd < 0) {
        perror("File open error");
        return -1;
    }

    // Allocate memory (no need for alignment since O_DIRECT is removed)
    void *aligned_mem = malloc(LARGE_BLOCK_SIZE);
    if (!aligned_mem) {
        perror("malloc failed");
        close(fd);
        return -1;
    }

    // Initialize Boost shared memory
    try {
        boost::interprocess::shared_memory_object shm(
            boost::interprocess::open_or_create, 
            "BoostSharedMemory", 
            boost::interprocess::read_write
        );
        // Set shared memory size to 128MB
        shm.truncate(LARGE_BLOCK_SIZE);
        boost::interprocess::mapped_region region(shm, boost::interprocess::read_write);
        std::cout << "Boost shared memory segment created successfully." << std::endl;
    } 
    catch (const boost::interprocess::interprocess_exception &ex) {
        std::cerr << "Caught interprocess_exception: " << ex.what() << std::endl;
        close(fd);
        free(aligned_mem);
        return -1;
    }

    // Open POSIX shared memory
    int shm_fd = shm_open("/my_large_shm", O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open failed");
        close(fd);
        free(aligned_mem);
        return -1;
    }

    // Set shared memory size to 128MB
    if (ftruncate(shm_fd, LARGE_BLOCK_SIZE) == -1) {
        perror("ftruncate failed");
        close(shm_fd);
        shm_unlink("/my_large_shm");
        close(fd);
        free(aligned_mem);
        return -1;
    }

    // Map shared memory into process's address space
    void *shared_mem = mmap(nullptr, LARGE_BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared_mem == MAP_FAILED) {
        perror("mmap failed");
        close(shm_fd);
        shm_unlink("/my_large_shm");
        close(fd);
        free(aligned_mem);
        return -1;
    }

    // Prepare data and copy it to the memory
    const char *data = "Standard I/O with shared memory";
    memset(aligned_mem, 0, LARGE_BLOCK_SIZE); // Clear buffer before use
    memcpy(aligned_mem, data, strlen(data) + 1);

    // Write data to file (Standard I/O)
    ssize_t written = pwrite(fd, aligned_mem, LARGE_BLOCK_SIZE, 0);
    if (written != LARGE_BLOCK_SIZE) {
        perror("pwrite failed");
    } else {
        std::cout << "Data written successfully with standard I/O." << std::endl;
    }

    // Copy data from allocated memory to shared memory for access by other processes
    memcpy(shared_mem, aligned_mem, LARGE_BLOCK_SIZE);
    std::cout << "Data copied to shared memory successfully." << std::endl;

    // Clean up
    munmap(shared_mem, LARGE_BLOCK_SIZE);
    close(shm_fd);
    shm_unlink("/my_large_shm");
    close(fd);
    free(aligned_mem);

    return 0;
}