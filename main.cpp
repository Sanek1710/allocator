#include "allocator.hpp"
#include <iostream>
#include <vector>

int main() {
    try {
        // Create an allocator with 1MB of memory
        MemoryAllocator allocator(1024 * 1024);

        std::cout << "Initial state:\n";
        std::cout << "Total space: " << allocator.get_total_space() << " bytes\n";
        std::cout << "Free space: " << allocator.get_free_space() << " bytes\n";
        std::cout << "Allocated space: " << allocator.get_allocated_space() << " bytes\n\n";

        // Store some allocations to free later
        std::vector<size_t> addresses;

        // Allocate different sized blocks
        std::cout << "Making allocations:\n";
        for (size_t size : {128, 256, 512, 1024}) {
            size_t addr = allocator.alloc(size);
            addresses.push_back(addr);
            std::cout << "Allocated " << size << " bytes at address " << addr << "\n";
            std::cout << "Free space: " << allocator.get_free_space() << " bytes\n";
        }
        std::cout << "\n";

        // Free some memory
        std::cout << "Freeing memory:\n";
        for (size_t addr : addresses) {
            allocator.dealloc(addr);
            std::cout << "Freed memory at address " << addr << "\n";
            std::cout << "Free space: " << allocator.get_free_space() << " bytes\n";
        }

        // Try to allocate a block larger than available memory
        std::cout << "\nTrying to allocate more than total memory...\n";
        try {
            allocator.alloc(2 * 1024 * 1024);
        } catch (const std::bad_alloc& e) {
            std::cout << "Allocation failed as expected\n";
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
} 