#pragma once
#include <cstddef>
#include <map>

class MemoryAllocator {
private:
    struct MemoryBlock {
        size_t size;
        bool is_free;
    };

    size_t total_size;        // Total simulated memory size
    size_t allocated_size;    // Currently allocated size
    size_t next_address;      // Next simulated memory address
    std::map<size_t, MemoryBlock> blocks;  // Map of address -> block info

public:
    explicit MemoryAllocator(size_t size);

    size_t alloc(size_t size);  // Returns simulated memory address
    void dealloc(size_t address);

    // Memory statistics
    size_t get_total_space() const { return total_size; }
    size_t get_allocated_space() const { return allocated_size; }
    size_t get_free_space() const { return total_size - allocated_size; }
}; 