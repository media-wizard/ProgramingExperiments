#include <iostream>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <cstdint>
#include <cerrno>

// We define minimal required ioctl numbers manually to ensure independence from varying header states
#define BINDER_VERSION _IOWR('b', 9, struct binder_version)
#define BINDER_WRITE_READ _IOWR('b', 1, struct binder_write_read_generic)

#define BINDER_MMAP_SIZE (1024 * 1024)
constexpr uint32_t PING_TRANSACTION = 0x5F504E47; // 'p' 'i' 'n' 'g'
constexpr uint32_t BC_TRANSACTION_V7 = 0x40246200; // Hardcoded BC_TRANSACTION command ID for v7
constexpr uint32_t BC_TRANSACTION_V8 = 0x40406200; // Hardcoded BC_TRANSACTION command ID for v8
constexpr uint32_t BR_REPLY = 0x80047201;

// Base structure needed to query the driver version
struct binder_version {
    int32_t protocol_version;
};

// Generic container structure to satisfy ioctl signatures
struct binder_write_read_generic {
    uint64_t write_size;
    uint64_t write_consumed;
    uint64_t write_buffer;
    uint64_t read_size;
    uint64_t read_consumed;
    uint64_t read_buffer;
};

// --- BINDER VERSION 7 STRUCTURES (32-bit Types) ---
struct binder_transaction_data_v7 {
    uint32_t handle;
    uint32_t cookie;
    uint32_t code;
    uint32_t flags;
    int32_t  sender_pid;
    int32_t  sender_euid;
    uint32_t data_size;
    uint32_t offsets_size;
    uint32_t data_ptr;
    uint32_t offsets_ptr;
};

struct binder_write_read_v7 {
    uint32_t write_size;
    uint32_t write_consumed;
    uint32_t write_buffer;
    uint32_t read_size;
    uint32_t read_consumed;
    uint32_t read_buffer;
};

// --- BINDER VERSION 8 STRUCTURES (64-bit Types) ---
struct binder_transaction_data_v8 {
    uint64_t handle;
    uint64_t cookie;
    uint32_t code;
    uint32_t flags;
    int32_t  sender_pid;
    int32_t  sender_euid;
    uint64_t data_size;
    uint64_t offsets_size;
    uint64_t data_ptr;
    uint64_t offsets_ptr;
};

struct binder_write_read_v8 {
    uint64_t write_size;
    uint64_t write_consumed;
    uint64_t write_buffer;
    uint64_t read_size;
    uint64_t read_consumed;
    uint64_t read_buffer;
};

// Execution logic specifically tailored for Legacy Version 7 protocol
bool run_ping_v7(int binder_fd) {
    std::cout << "[*] Executing Version 7 (32-bit Layout) payload pathway..." << std::endl;
    
    binder_transaction_data_v7 tr{};
    tr.handle = 0; // servicemanager
    tr.code = PING_TRANSACTION;
    tr.flags = 0x10; // TF_ACCEPT_FDS
    
    uint32_t write_buffer[128];
    write_buffer[0] = BC_TRANSACTION_V7;
    std::memcpy(&write_buffer[1], &tr, sizeof(tr));

    uint32_t read_buffer[128];
    binder_write_read_v7 bwr{};
    bwr.write_size = sizeof(uint32_t) + sizeof(tr);
    bwr.write_buffer = reinterpret_cast<uintptr_t>(write_buffer);
    bwr.read_size = sizeof(read_buffer);
    bwr.read_buffer = reinterpret_cast<uintptr_t>(read_buffer);

    if (ioctl(binder_fd, _IOWR('b', 1, binder_write_read_v7), &bwr) < 0) {
        std::cerr << "[-] v7 ioctl transaction failed: " << strerror(errno) << std::endl;
        return false;
    }

    return (read_buffer[0] == BR_REPLY || read_buffer[1] == BR_REPLY);
}

// Execution logic specifically tailored for Modern Version 8 protocol
bool run_ping_v8(int binder_fd) {
    std::cout << "[*] Executing Version 8 (64-bit Layout) payload pathway..." << std::endl;

    binder_transaction_data_v8 tr{};
    tr.handle = 0; // servicemanager
    tr.code = PING_TRANSACTION;
    tr.flags = 0x10; // TF_ACCEPT_FDS

    uint32_t write_buffer[128];
    write_buffer[0] = BC_TRANSACTION_V8;
    std::memcpy(&write_buffer[1], &tr, sizeof(tr));

    uint32_t read_buffer[128];
    binder_write_read_v8 bwr{};
    bwr.write_size = sizeof(uint32_t) + sizeof(tr);
    bwr.write_buffer = reinterpret_cast<uintptr_t>(write_buffer);
    bwr.read_size = sizeof(read_buffer);
    bwr.read_buffer = reinterpret_cast<uintptr_t>(read_buffer);

    if (ioctl(binder_fd, _IOWR('b', 1, binder_write_read_v8), &bwr) < 0) {
        std::cerr << "[-] v8 ioctl transaction failed: " << strerror(errno) << std::endl;
        return false;
    }

    return (read_buffer[0] == BR_REPLY || read_buffer[1] == BR_REPLY);
}

int main() {
    int binder_fd = open("/dev/binder", O_RDWR | O_CLOEXEC);
    if (binder_fd < 0) {
        std::cerr << "[-] Failed to open /dev/binder: " << strerror(errno) << std::endl;
        return 1;
    }

    struct binder_version version{};
    if (ioctl(binder_fd, BINDER_VERSION, &version) < 0) {
        std::cerr << "[-] Failed to negotiate binder version: " << strerror(errno) << std::endl;
        close(binder_fd);
        return 1;
    }

    std::cout << "[+] Detected Driver Version Protocol: " << version.protocol_version << std::endl;

    void* mapped_mem = mmap(nullptr, BINDER_MMAP_SIZE, PROT_READ, MAP_PRIVATE, binder_fd, 0);
    if (mapped_mem == MAP_FAILED) {
        std::cerr << "[-] Shared memory mmap failed: " << strerror(errno) << std::endl;
        close(binder_fd);
        return 1;
    }

    bool success = false;
    if (version.protocol_version == 7) {
        success = run_ping_v7(binder_fd);
    } else if (version.protocol_version == 8) {
        success = run_ping_v8(binder_fd);
    } else {
        std::cerr << "[-] Driver returned an unknown protocol validation version." << std::endl;
    }

    if (success) {
        std::cout << "[SUCCESS] Ping verified! servicemanager is alive." << std::endl;
    } else {
        std::cout << "[FAILURE] Target servicemanager could not be verified." << std::endl;
    }

    munmap(mapped_mem, BINDER_MMAP_SIZE);
    close(binder_fd);
    return success ? 0 : 1;
}
