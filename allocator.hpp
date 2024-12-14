#pragma once
#include <cstddef>
#include <map>

class MemoryAllocator {
private:
  static constexpr size_t MIN_BLOCK_SIZE =
      16; // Minimum block size (power of 2)

  struct MemoryBlock {
    size_t size;      // Total size of block (power of 2)
    size_t allocated; // Actually allocated size (0 if free)
    bool is_free;
  };

  size_t total_size;                    // Total simulated memory size
  size_t allocated_size;                // Currently allocated size
  size_t next_address;                  // Next simulated memory address
  std::map<size_t, MemoryBlock> blocks; // Map of address -> block info

  // Fast bit operations for power of 2 calculations
  static inline size_t next_power_2(size_t x) {
    x--;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    x |= x >> 32;
    x++;
    return x;
  }

  static inline bool is_power_of_2(size_t x) { return x && !(x & (x - 1)); }
  static inline size_t calculate_block_size(size_t size) {
    return std::max(next_power_2(size), MIN_BLOCK_SIZE);
  }

  // Helper to find the last allocated block's address
  size_t find_last_allocated_address() const;

public:
  explicit MemoryAllocator(size_t size);
  size_t alloc(size_t size);
  void dealloc(size_t address);

  // Memory statistics - all const for use in production
  size_t get_total_space() const { return total_size; }
  size_t get_allocated_space() const { return allocated_size; }
  size_t get_free_space() const { return total_size - allocated_size; }
  double get_internal_fragmentation() const;
  double get_external_fragmentation() const;

  // New method that only considers fragmentation up to the last allocated block
  double get_trimmed_external_fragmentation() const;
};