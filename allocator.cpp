#include "allocator.hpp"
#include <stdexcept>

MemoryAllocator::MemoryAllocator(size_t size) 
    : total_size(size), allocated_size(0), next_address(1000) {  // Start at 1000 for easy reading
    // Initialize with one free block spanning the entire space
    blocks[next_address] = {size, true};
}

size_t MemoryAllocator::alloc(size_t size) {
    if (size == 0) {
        throw std::invalid_argument("Cannot allocate 0 bytes");
    }

    // Align size to 8 bytes (simulate memory alignment)
    size = (size + 7) & ~7;

    // Find a free block that's big enough
    for (auto& pair : blocks) {
        auto addr = pair.first;
        auto& block = pair.second;
        
        if (block.is_free && block.size >= size) {
            // If block is significantly larger, split it
            if (block.size > size + 16) {  // Only split if difference is worth it
                size_t new_addr = addr + size;
                blocks[new_addr] = {block.size - size, true};
                block.size = size;
            }
            
            block.is_free = false;
            allocated_size += block.size;
            return addr;
        }
    }
    
    throw std::bad_alloc();
}

void MemoryAllocator::dealloc(size_t address) {
    auto it = blocks.find(address);
    if (it == blocks.end()) {
        throw std::invalid_argument("Invalid address");
    }

    if (it->second.is_free) {
        throw std::runtime_error("Double free detected");
    }

    // Mark block as free
    it->second.is_free = true;
    allocated_size -= it->second.size;

    // Merge with next block if it's free
    auto next = std::next(it);
    if (next != blocks.end() && next->second.is_free) {
        it->second.size += next->second.size;
        blocks.erase(next);
    }

    // Merge with previous block if it's free
    if (it != blocks.begin()) {
        auto prev = std::prev(it);
        if (prev->second.is_free) {
            prev->second.size += it->second.size;
            blocks.erase(it);
        }
    }
} 