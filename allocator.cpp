#include "allocator.hpp"
#include "memory_visualization.hpp"
#include <array>
#include <iomanip>
#include <iostream>
#include <ostream>
#include <stdexcept>
#include <vector>

MemoryAllocator::MemoryAllocator(size_t size)
    : total_size(next_power_2(size)), allocated_size(0), next_address(0) {
  blocks[next_address] = {total_size, 0, true};
}

size_t MemoryAllocator::find_last_allocated_address() const {
  size_t last_addr = next_address; // Start of memory
  for (const auto &pair : blocks) {
    if (!pair.second.is_free) {
      last_addr = pair.first + pair.second.size;
    }
  }
  return last_addr;
}

size_t MemoryAllocator::alloc(size_t size) {
  if (!size)
    return 0;

  size_t block_size = calculate_block_size(size);

  // Find and split in one pass
  for (auto &pair : blocks) {
    auto addr = pair.first;
    auto &block = pair.second;

    if (block.is_free && block.size >= block_size) {
      // Split only if necessary
      while (block.size > block_size && block.size > MIN_BLOCK_SIZE) {
        size_t new_size = block.size >> 1; // Divide by 2
        blocks[addr + new_size] = {new_size, 0, true};
        block.size = new_size;
      }

      block.is_free = false;
      block.allocated = size;
      allocated_size += size;
      return addr;
    }
  }

  throw std::bad_alloc();
}

void MemoryAllocator::dealloc(size_t address) {
  auto it = blocks.find(address);
  if (it == blocks.end() || it->second.is_free) {
    throw std::invalid_argument("Invalid deallocation");
  }

  allocated_size -= it->second.allocated;
  it->second.is_free = true;
  it->second.allocated = 0;

  // Merge buddies if possible
  bool merged;
  do {
    merged = false;
    size_t size = it->second.size;
    size_t addr = it->first;
    size_t buddy_addr;

    // Calculate buddy address based on address alignment
    if (addr & size) {
      buddy_addr = addr - size; // We're the right buddy
    } else {
      buddy_addr = addr + size; // We're the left buddy
    }

    auto buddy = blocks.find(buddy_addr);
    if (buddy != blocks.end() && buddy->second.is_free &&
        buddy->second.size == size) {

      if (addr > buddy_addr) {
        // Merge into left buddy
        buddy->second.size <<= 1; // Multiply by 2
        blocks.erase(it);
        it = buddy;
      } else {
        // Merge into current block
        it->second.size <<= 1;
        blocks.erase(buddy);
      }
      merged = true;
    }
  } while (merged && it->second.size < total_size);
}

double MemoryAllocator::get_internal_fragmentation() const {
  if (!allocated_size)
    return 0.0;

  size_t total_wasted = 0;
  for (const auto &pair : blocks) {
    const auto &block = pair.second;
    if (!block.is_free) {
      total_wasted += block.size - block.allocated;
    }
  }

  return static_cast<double>(total_wasted) / allocated_size;
}

double
MemoryAllocator::calculate_external_fragmentation(size_t max_address) const {
  if (blocks.empty() || allocated_size == 0)
    return 0.0;

  size_t total_free = 0;

  // Calculate total free space
  for (const auto &pair : blocks) {
    if (max_address && pair.first >= max_address)
      break;
    if (pair.second.is_free) {
      total_free += pair.second.size;
    }
  }

  if (total_free == 0)
    return 0.0;

  // Initialize array for block counts
  std::array<size_t, BLOCK_SIZES_COUNT> actual_blocks{};

  // First pass: count blocks of each size
  for (const auto &pair : blocks) {
    if (max_address && pair.first >= max_address)
      break;
    if (pair.second.is_free) {
      size_t block_size = pair.second.size;
      size_t index = get_block_size_index(block_size);
      actual_blocks[index] += 1;
    }
  }

  // Second pass: calculate actual blocks for each size
  for (size_t i = 0; i < BLOCK_SIZES_COUNT - 1; ++i) {
    for (size_t j = i + 1; j < BLOCK_SIZES_COUNT; ++j) {
      if (actual_blocks[j] > 0) {
        actual_blocks[i] += actual_blocks[j] * (1U << (j - i));
      }
    }
  }

  // Third pass: calculate weighted sum and total weight
  double weighted_sum = 0.0;
  size_t total_weight = 0;

  for (size_t i = 0; i < BLOCK_SIZES_COUNT; ++i) {
    size_t block_size = MIN_BLOCK_SIZE << i;
    if (block_size > total_free)
      break;

    size_t potential_blocks = total_free / block_size;
    if (potential_blocks == 0)
      continue;

    weighted_sum += static_cast<double>(actual_blocks[i]) / potential_blocks;
    total_weight += 1;
  }

#ifdef PRINT_EXT_FRAG
  std::cout << "\nBlock size | Actual blocks | Potential blocks | Rate\n";
  std::cout << "-----------------------------------------------\n";
  for (size_t i = 0; i < BLOCK_SIZES_COUNT; ++i) {
    size_t block_size = MIN_BLOCK_SIZE << i;
    if (block_size > total_free)
      break;

    size_t potential_blocks = total_free / block_size;
    if (potential_blocks == 0)
      continue;

    double rate = static_cast<double>(actual_blocks[i]) / potential_blocks;
    std::cout << std::setw(9) << block_size << " | " << std::setw(13)
              << actual_blocks[i] << " | " << std::setw(15) << potential_blocks
              << " | " << std::fixed << std::setprecision(3) << rate << "\n";
  }
  std::cout << "\n";
#endif

  return total_weight > 0 ? 1.0 - (weighted_sum / total_weight) : 0.0;
}

size_t MemoryAllocator::align_alloc(size_t size) {
  if (!size)
    return 0;

  size_t block_size = calculate_block_size(size);

  // Find and split in one pass
  for (auto &pair : blocks) {
    auto addr = pair.first;
    auto &block = pair.second;

    // Skip if block is not free or too small
    if (!block.is_free || block.size < block_size)
      continue;

    // Calculate next grid position within this block
    size_t grid_pos = ((addr + block_size - 1) / block_size) * block_size;
    size_t offset = grid_pos - addr;

    // Check if we can fit block at grid position
    if (offset + block_size <= block.size) {
      // Split until we reach grid position
      while (offset > 0) {
        size_t new_size = block.size >> 1;
        blocks[addr + new_size] = {new_size, 0, true};
        block.size = new_size;
        addr += new_size;
        offset -= new_size;
      }

      // Split down to required size if needed
      while (block.size > block_size && block.size > MIN_BLOCK_SIZE) {
        size_t new_size = block.size >> 1;
        blocks[addr + new_size] = {new_size, 0, true};
        block.size = new_size;
      }

      block.is_free = false;
      block.allocated = size;
      allocated_size += size;
      return addr;
    }
  }

  throw std::bad_alloc();
}