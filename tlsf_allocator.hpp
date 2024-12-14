#pragma once
#include <cstddef>
#include <vector>

class TLSFAllocator {
public:
    static constexpr size_t MIN_BLOCK_SIZE = 16;
    static constexpr size_t MAX_BLOCK_SIZE = 1U << 31;
    static constexpr size_t SL_INDEX_COUNT = 32;  // Second level divisions
    static constexpr size_t FL_INDEX_COUNT = 32;  // First level (power of 2) count

    explicit TLSFAllocator(size_t size);
    size_t alloc(size_t size);
    size_t align_alloc(size_t size);
    void dealloc(size_t address);

    // Memory statistics
    size_t get_total_space() const { return total_size; }
    size_t get_allocated_space() const { return allocated_size; }
    size_t get_free_space() const { return total_size - allocated_size; }
    double get_internal_fragmentation() const;
    double get_external_fragmentation() const {
        return calculate_external_fragmentation();
    }
    double get_trimmed_external_fragmentation() const {
        return calculate_external_fragmentation(find_last_allocated_address());
    }

private:
    struct Block {
        size_t size;          // Includes header size
        size_t allocated;     // Actual allocated size (0 if free)
        bool is_free;
        Block* prev_physical; // Previous block in memory
        Block* next_free;     // Next free block in list
        Block* prev_free;     // Previous free block in list
    };

    // Bitmap operations
    static inline size_t fls(size_t x) { return x ? sizeof(size_t) * 8 - __builtin_clzll(x) : 0; }
    static inline size_t ffs(size_t x) { return __builtin_ffsll(x); }
    
    // Mapping functions
    void mapping_insert(size_t size, Block* block);
    void mapping_remove(size_t size, Block* block);
    Block* mapping_find(size_t size);
    std::pair<int, int> mapping_indexes(size_t size) const;

    // Helper functions
    size_t find_last_allocated_address() const;
    double calculate_external_fragmentation(size_t max_address = 0) const;
    Block* get_suitable_block(size_t size);
    void remove_free_block(Block* block);
    void insert_free_block(Block* block);
    Block* split_block(Block* block, size_t size);
    void merge_block(Block* block);

    size_t total_size;
    size_t allocated_size;
    Block* first_block;
    std::vector<std::vector<Block*>> segregated_lists;
    std::vector<size_t> fl_bitmap;
    std::vector<size_t> sl_bitmap;
}; 