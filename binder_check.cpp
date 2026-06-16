#include <iostream>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cstdint>
#include <linux/android/binder.h>

// --- Pure Legacy 32-bit Architecture Layouts ---
#pragma pack(push, 4)
struct binder_write_read_v7 {
    uint32_t write_size;       
    uint32_t write_consumed;   
    uint32_t write_buffer;     
    uint32_t read_size;        
    uint32_t read_consumed;    
    uint32_t read_buffer;      
};

struct binder_transaction_data_v7 {
    union {
        uint32_t handle;
        uint32_t ptr;          
    } target;
    uint32_t cookie;           
    uint32_t code; 
    uint32_t flags;
    int32_t  sender_pid;
    int32_t  sender_euid;
    uint32_t data_size;        
    uint32_t offsets_size;     
    union {
        struct {
            uint32_t buffer;   
            uint32_t offsets;  
        } ptr;
        uint8_t buf[8];
    } data;
};
#pragma pack(pop)

// --- Configuration Constants ---
#define BINDER_MMAP_SIZE_V7  (128 * 1024)
#define BINDER_MMAP_SIZE_V8  (1024 * 1024) 
#define BINDER_VERSION       _IOWR('b', 9, struct binder_version)
#define BINDER_WRITE_READ_V7 _IOWR('b', 1, struct binder_write_read_v7)
#define BC_TRANSACTION_V7    _IOW('c', 0, struct binder_transaction_data_v7)

constexpr uint32_t BR_REPLY_V7       = 0x80247201; 
constexpr uint32_t BR_REPLY_V8       = 0x80287203;
constexpr uint32_t BR_REPLY_V7_ACTUAL = 0x7206; 
constexpr uint32_t BR_TRANSACTION_COMPLETE_V7 = 0x720c;
constexpr uint32_t BR_OK_V7          = 0x7205;
constexpr uint32_t PING_TRANSACTION  = 0x5F504E47; // '_PNG'

// --- Unified Payload Aggregator Struct ---
struct BinderTransaction {
    std::vector<uint32_t> write_payload;
    std::vector<uint32_t> read_payload;
    unsigned long ioctl_command = 0;
};

// Helper: Generates v7 structural packets
BinderTransaction prepare_v7_transaction() {
    BinderTransaction tx;
    tx.ioctl_command = BINDER_WRITE_READ_V7;

    binder_transaction_data_v7 txn{};
    txn.target.handle = 0;          
    txn.code = PING_TRANSACTION;    
    txn.flags = 0;                  
    txn.data_size = 0;              
    txn.offsets_size = 0;

    size_t tx_words = sizeof(txn) / sizeof(uint32_t);
    tx.write_payload.reserve(1 + tx_words);
    tx.write_payload.push_back(BC_TRANSACTION_V7);
    
    const auto* raw_ptr = reinterpret_cast<const uint32_t*>(&txn);
    tx.write_payload.insert(tx.write_payload.end(), raw_ptr, raw_ptr + tx_words);
    tx.read_payload.resize(256, 0);
    return tx;
}

// Helper: Generates v8 (Current System Context) structural packets
BinderTransaction prepare_v8_transaction() {
    BinderTransaction tx;
    tx.ioctl_command = BINDER_WRITE_READ;

    struct binder_transaction_data txn{};
    std::memset(&txn, 0, sizeof(txn));
    txn.target.handle = 0;          
    txn.code = PING_TRANSACTION;    
    txn.flags = TF_ACCEPT_FDS;      
    txn.data_size = 0;              
    txn.offsets_size = 0;

    size_t tx_words = sizeof(txn) / sizeof(uint32_t);
    tx.write_payload.reserve(1 + tx_words);
    tx.write_payload.push_back(BC_TRANSACTION);

    const auto* raw_ptr = reinterpret_cast<const uint32_t*>(&txn);
    tx.write_payload.insert(tx.write_payload.end(), raw_ptr, raw_ptr + tx_words);
    tx.read_payload.resize(256, 0);
    return tx;
}

// --- Common Protocol Engine Core ---
bool execute_binder_ping(int binder_fd, int protocol_version) {
    BinderTransaction tx = (protocol_version == 7) ? prepare_v7_transaction() : prepare_v8_transaction();
    uint32_t bytes_consumed = 0;

    std::cout << "[*] Routing Ping via version " << protocol_version << " layout engine...\n";

    if (protocol_version == 7) {
        binder_write_read_v7 bwr{};
        bwr.write_size = tx.write_payload.size() * sizeof(uint32_t);
        bwr.write_consumed = 0;
        bwr.write_buffer = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(tx.write_payload.data()));
        bwr.read_size = tx.read_payload.size() * sizeof(uint32_t);
        bwr.read_consumed = 0;
        bwr.read_buffer = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(tx.read_payload.data()));

        if (ioctl(binder_fd, tx.ioctl_command, &bwr) < 0) {
            std::perror("[-] ioctl execution map allocation failed");
            return false;
        }
        bytes_consumed = bwr.read_consumed;
    } else {
        struct binder_write_read bwr{};
        std::memset(&bwr, 0, sizeof(bwr));
        bwr.write_size = tx.write_payload.size() * sizeof(uint32_t);
        bwr.write_consumed = 0;
        bwr.write_buffer = reinterpret_cast<binder_uintptr_t>(tx.write_payload.data());
        bwr.read_size = tx.read_payload.size() * sizeof(uint32_t);
        bwr.read_consumed = 0;
        bwr.read_buffer = reinterpret_cast<binder_uintptr_t>(tx.read_payload.data());

        if (ioctl(binder_fd, tx.ioctl_command, &bwr) < 0) {
            std::cerr << "[-] ioctl execution map allocation failed: " << std::strerror(errno) << "\n";
            return false;
        }
        bytes_consumed = bwr.read_consumed;
    }

    // --- Unified Protocol Response Token Parsing Loop ---
    std::cout << "[*] Driver returned " << bytes_consumed << " bytes of response telemetry.\n";
    
    const uint32_t* read_ptr = tx.read_payload.data();
    const uint32_t* read_end = read_ptr + (bytes_consumed / sizeof(uint32_t));
    bool service_manager_alive = false;

    while (read_ptr < read_end) {
        uint32_t token = *read_ptr++;
        std::cout << "[*] Intercepted response token: 0x" << std::hex << token << std::dec << "\n";

        if (token == BR_REPLY || token == BR_REPLY_V7_ACTUAL || token == BR_REPLY_V7 || token == BR_REPLY_V8) {
            std::cout << "[+] Explicit reply acknowledgement found!\n";
            service_manager_alive = true;
            break; 
        } 
        if (token == BR_DEAD_REPLY || token == BR_FAILED_REPLY) {
            std::cerr << "[-] Driver faulted payload execution target. Status: " << token << "\n";
            break;
        } 
        if (token == BR_TRANSACTION_COMPLETE || token == BR_TRANSACTION_COMPLETE_V7) {
            std::cout << "[+] Transaction safely handed off to Binder kernel layer.\n";
            continue;
        } 
        if (token == BR_NOOP || token == BR_OK || token == BR_OK_V7) {
            continue;
        } 
        
        // Safety Fallback for unexpected or structural multi-word response components
        std::cout << "[!] Structural bound reached or unhandled response code. Breaking parsing thread loop.\n";
        break;
    }

    return service_manager_alive;
}

bool isServiceManagerAvailable() {
    bool service_manager_alive = false;

    int binder_fd = open("/dev/binder", O_RDWR | O_CLOEXEC);
    if (binder_fd < 0) {
        std::perror("[-] Failed to open /dev/binder");
        return service_manager_alive;
    }
    std::cout << "[+] Successfully opened /dev/binder\n";

    binder_version version{};
    if (ioctl(binder_fd, BINDER_VERSION, &version) < 0) {
        std::perror("[-] Failed to extract device driver protocol revision metadata");
        close(binder_fd);
        return service_manager_alive;
    }
    std::cout << "[+] Binder protocol version detected: " << version.protocol_version << "\n";

    uint32_t binder_map_size = (version.protocol_version == 7) ? BINDER_MMAP_SIZE_V7 : BINDER_MMAP_SIZE_V8;
    void* mapped_mem = mmap(nullptr, binder_map_size, PROT_READ, MAP_PRIVATE, binder_fd, 0);
    if (mapped_mem == MAP_FAILED) {
        std::perror("[-] Shared address space context instantiation failed");
        close(binder_fd);
        return service_manager_alive;
    }
    std::cout << "[+] Memory mapped successfully\n";

    service_manager_alive = execute_binder_ping(binder_fd, version.protocol_version);

    munmap(mapped_mem, binder_map_size);
    close(binder_fd);
    return service_manager_alive;
}

int main() {
    bool service_manager_alive = isServiceManagerAvailable();
    std::cout << "\n--- Final Verdict ---\n";
    if (service_manager_alive) {
        std::cout << "[+] SUCCESS: Service Manager (handle 0) responded cleanly!\n";       
    } else {
        std::cout << "[-] FAILURE: Protocol architecture transaction validation failed.\n";
    }
    return 0;
}
