#include <iostream>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <cstdint>
#include <cerrno>

// Core Android Kernel Binder definitions 
#include <linux/android/binder.h>

#define BINDER_MMAP_SIZE (1024 * 1024) // 1MB required scratchpad memory space

// PING_TRANSACTION is traditionally defined as a fallback check code in IPC frameworks
// It translates to the integer representation of ('p' << 24 | 'i' << 16 | 'n' << 8 | 'g') -> 1599295559
constexpr uint32_t PING_TRANSACTION = 0x5F504E47; 

int main() {
    // 1. Open the binder driver
    int binder_fd = open("/dev/binder", O_RDWR | O_CLOEXEC);
    if (binder_fd < 0) {
        std::cerr << "[-] Failed to open /dev/binder: " << strerror(errno) << std::endl;
        return 1;
    }
    std::cout << "[+] Successfully opened /dev/binder" << std::endl;

    // 2. Query and validate binder protocol version (Supports 7 and 8)
    struct binder_version version;
    if (ioctl(binder_fd, BINDER_VERSION, &version) < 0) {
        std::cerr << "[-] Failed to get binder version: " << strerror(errno) << std::endl;
        close(binder_fd);
        return 1;
    }
    std::cout << "[+] Binder Kernel Protocol Version: " << version.protocol_version << std::endl;

    if (version.protocol_version != 7 && version.protocol_version != 8) {
        std::cerr << "[-] Unsupported binder version detected!" << std::endl;
        close(binder_fd);
        return 1;
    }

    // 3. Perform mmap (the kernel driver requires a mapped space to handle transaction transactions)
    void* mapped_mem = mmap(nullptr, BINDER_MMAP_SIZE, PROT_READ, MAP_PRIVATE, binder_fd, 0);
    if (mapped_mem == MAP_FAILED) {
        std::cerr << "[-] mmap failed: " << strerror(errno) << std::endl;
        close(binder_fd);
        return 1;
    }

    // 4. Construct the Raw Transaction Data Payload
    struct binder_transaction_data tr;
    std::memset(&tr, 0, sizeof(tr));
    
    tr.target.handle = 0;          // 0 is universally the handle for servicemanager
    tr.code = PING_TRANSACTION;    // Check command
    tr.flags = TF_ACCEPT_FDS;      // Basic transactional flag
    tr.data_size = 0;              // Ping has no data payload bytes
    tr.offsets_size = 0;           // No object offsets passed

    // 5. Package transaction into the Binder write-read structure
    // We send BC_TRANSACTION command followed by the transaction payload packet
    uint32_t write_buffer[256];
    write_buffer[0] = BC_TRANSACTION;
    std::memcpy(&write_buffer[1], &tr, sizeof(tr));

    struct binder_write_read bwr;
    std::memset(&bwr, 0, sizeof(bwr));
    bwr.write_size = sizeof(uint32_t) + sizeof(tr);
    bwr.write_consumed = 0;
    bwr.write_buffer = reinterpret_cast<binder_uintptr_t>(write_buffer);

    // Provide a read buffer space to intercept the driver's response code (like BR_REPLY)
    uint32_t read_buffer[256];
    bwr.read_size = sizeof(read_buffer);
    bwr.read_consumed = 0;
    bwr.read_buffer = reinterpret_cast<binder_uintptr_t>(read_buffer);

    // 6. Execute the ioctl transaction
    std::cout << "[*] Sending Ping Transaction payload to Handle 0 (servicemanager)..." << std::endl;
    if (ioctl(binder_fd, BINDER_WRITE_READ, &bwr) < 0) {
        std::cerr << "[-] IOCTL BINDER_WRITE_READ failed: " << strerror(errno) << std::endl;
        munmap(mapped_mem, BINDER_MMAP_SIZE);
        close(binder_fd);
        return 1;
    }

    // 7. Parse the incoming response loop from the kernel driver
    bool reached_servicemanager = false;
    uintptr_t ptr = reinterpret_cast<uintptr_t>(read_buffer);
    uintptr_t end = ptr + bwr.read_consumed;

    while (ptr < end) {
        uint32_t cmd = *reinterpret_cast<uint32_t*>(ptr);
        ptr += sizeof(uint32_t);

        if (cmd == BR_REPLY) {
            std::cout << "[+] Received BR_REPLY from driver!" << std::endl;
            reached_servicemanager = true;
            break;
        } else if (cmd == BR_DEAD_REPLY || cmd == BR_FAILED_REPLY) {
            std::cerr << "[-] Received failure code from driver: " << cmd << std::endl;
            break;
        } else if (cmd == BR_NOOP || cmd == BR_OK) {
            continue;
        } else if (cmd == BR_TRANSACTION_COMPLETE) {
            std::cout << "[+] Transaction safely handed off to Binder kernel layer." << std::endl;
        } else {
            // Some protocol codes append payload arguments that we must skip over
            // e.g., BR_SPAWN_LOOPER takes no arguments, BR_TRANSACTION takes struct binder_transaction_data
            break; 
        }
    }

    // 8. Output findings and clean up resources
    if (reached_servicemanager) {
        std::cout << "\n[SUCCESS] servicemanager is alive and accessible!" << std::endl;
    } else {
        std::cout << "\n[FAILURE] servicemanager could not be verified or is unresponsive." << std::endl;
    }

    munmap(mapped_mem, BINDER_MMAP_SIZE);
    close(binder_fd);
    return reached_servicemanager ? 0 : 1;
}
