#include "tlsf_allocator.hpp"
#include <stdexcept>
#include <cmath>

TLSFAllocator::TLSFAllocator(size_t size) 
    : total_size(size), allocated_size(0), first_block(nullptr) {
    
    // Initialize segregated lists and bitmaps
    segregated_lists.resize(FL_INDEX_COUNT);
    for (auto& sl : segregated_lists) {
        sl.resize(SL_INDEX_COUNT, nullptr);
    }
    fl_bitmap.resize((FL_INDEX_COUNT + 63) / 64, 0);
    sl_bitmap.resize(FL_INDEX_COUNT, 0);

    // Create initial block with space for header
    size_t total_block_size = size + sizeof(Block);
    char* memory = new char[total_block_size];
    first_block = reinterpret_cast<Block*>(memory);
    first_block->size = size;  // Size is just the usable space
    first_block->allocated = 0;
    first_block->is_free = true;
    first_block->prev_physical = nullptr;
    first_block->next_free = nullptr;
    first_block->prev_free = nullptr;

    // Insert into free lists
    mapping_insert(size, first_block);
}

std::pair<int, int> TLSFAllocator::mapping_indexes(size_t size) const {
    // Ensure minimum size
    if (size < MIN_BLOCK_SIZE) size = MIN_BLOCK_SIZE;
    
    // Calculate first level index (power of 2)
    int fl = fls(size);
    
    // Adjust for minimum block size
    fl = fl - __builtin_ctz(MIN_BLOCK_SIZE);
    if (fl < 0) fl = 0;
    
    // Calculate second level index
    size_t sl_size = 1ULL << (fl + __builtin_ctz(MIN_BLOCK_SIZE));
    size_t sl_mask = sl_size - 1;
    int sl = (size & sl_mask) ? ((size & sl_mask) * SL_INDEX_COUNT) / sl_size : 0;
    
    // Ensure indices are within bounds
    if (fl >= FL_INDEX_COUNT) {
        fl = FL_INDEX_COUNT - 1;
        sl = SL_INDEX_COUNT - 1;
    } else if (sl >= SL_INDEX_COUNT) {
        sl = SL_INDEX_COUNT - 1;
    }
    
    return {fl, sl};
}

void TLSFAllocator::mapping_insert(size_t size, Block* block) {
    if (!block) return;
    auto [fl, sl] = mapping_indexes(size);
    
    if (fl >= FL_INDEX_COUNT || sl >= SL_INDEX_COUNT) return;

    // Insert into segregated list
    block->next_free = segregated_lists[fl][sl];
    block->prev_free = nullptr;
    if (segregated_lists[fl][sl]) {
        segregated_lists[fl][sl]->prev_free = block;
    }
    segregated_lists[fl][sl] = block;

    // Set bitmap bits
    fl_bitmap[fl / 64] |= 1ULL << (fl % 64);
    sl_bitmap[fl] |= 1ULL << sl;
}

void TLSFAllocator::mapping_remove(size_t size, Block* block) {
    if (!block) return;
    auto [fl, sl] = mapping_indexes(size);
    
    if (fl >= FL_INDEX_COUNT || sl >= SL_INDEX_COUNT) return;

    // Remove from segregated list
    if (block->prev_free) {
        block->prev_free->next_free = block->next_free;
    } else if (segregated_lists[fl][sl] == block) {
        segregated_lists[fl][sl] = block->next_free;
    }
    
    if (block->next_free) {
        block->next_free->prev_free = block->prev_free;
    }

    // Clear block's free list pointers
    block->next_free = nullptr;
    block->prev_free = nullptr;

    // Update bitmap if list becomes empty
    if (!segregated_lists[fl][sl]) {
        sl_bitmap[fl] &= ~(1ULL << sl);
        if (!sl_bitmap[fl]) {
            fl_bitmap[fl / 64] &= ~(1ULL << (fl % 64));
        }
    }
}

TLSFAllocator::Block* TLSFAllocator::mapping_find(size_t size) {
    auto [fl, sl] = mapping_indexes(size);
    
    if (fl >= FL_INDEX_COUNT) return nullptr;

    // First try to find in the same fl bucket
    size_t sl_map = sl_bitmap[fl] & (~0ULL << sl);
    if (sl_map) {
        int sl_index = ffs(sl_map) - 1;
        if (sl_index >= 0 && sl_index < SL_INDEX_COUNT && 
            segregated_lists[fl][sl_index] &&
            segregated_lists[fl][sl_index]->size >= size) {
            return segregated_lists[fl][sl_index];
        }
    }

    // Try in higher fl buckets
    for (int curr_fl = fl + 1; curr_fl < FL_INDEX_COUNT; ++curr_fl) {
        if (sl_bitmap[curr_fl]) {
            int sl_index = ffs(sl_bitmap[curr_fl]) - 1;
            if (sl_index >= 0 && sl_index < SL_INDEX_COUNT && 
                segregated_lists[curr_fl][sl_index] &&
                segregated_lists[curr_fl][sl_index]->size >= size) {
                return segregated_lists[curr_fl][sl_index];
            }
        }
    }

    return nullptr;
}

TLSFAllocator::Block* TLSFAllocator::split_block(Block* block, size_t size) {
    if (!block || size >= block->size) return block;

    // Ensure minimum block size and alignment
    size = (size + 7) & ~7;  // Align to 8 bytes
    if (size < MIN_BLOCK_SIZE) size = MIN_BLOCK_SIZE;

    // Calculate remaining size after split
    size_t total_remaining = block->size - size;
    if (total_remaining < MIN_BLOCK_SIZE + sizeof(Block)) {
        return block;  // Not enough space to create a new block
    }

    // Find next physical block before splitting
    char* block_end = reinterpret_cast<char*>(block) + sizeof(Block) + block->size;
    Block* next_physical = nullptr;
    if (block_end < reinterpret_cast<char*>(first_block) + total_size) {
        next_physical = reinterpret_cast<Block*>(block_end);
    }

    // Create new block from remaining space
    Block* new_block = reinterpret_cast<Block*>(
        reinterpret_cast<char*>(block) + sizeof(Block) + size);
    
    // Initialize new block
    new_block->size = total_remaining - sizeof(Block);
    new_block->allocated = 0;
    new_block->is_free = true;
    new_block->prev_physical = block;
    new_block->next_free = nullptr;
    new_block->prev_free = nullptr;

    // Update original block size
    block->size = size;

    // Fix physical links
    if (next_physical && next_physical->prev_physical == block) {
        next_physical->prev_physical = new_block;
    }

    // Insert new block into free lists
    mapping_insert(new_block->size, new_block);

    return block;
}

void TLSFAllocator::merge_block(Block* block) {
    if (!block || !block->is_free) return;

    char* block_addr = reinterpret_cast<char*>(block);
    char* first_block_end = reinterpret_cast<char*>(first_block) + total_size;

    // Try to merge with next block if it exists and is valid
    char* next_addr = block_addr + sizeof(Block) + block->size;
    if (next_addr + sizeof(Block) <= first_block_end) {
        Block* next = reinterpret_cast<Block*>(next_addr);
        
        // Verify next block is valid and free
        if (next->size >= MIN_BLOCK_SIZE && 
            next->size <= (total_size - (next_addr - reinterpret_cast<char*>(first_block))) &&
            next->is_free) {
            
            // Find next's next before merging
            char* next_next_addr = next_addr + sizeof(Block) + next->size;
            Block* next_next = nullptr;
            if (next_next_addr + sizeof(Block) <= first_block_end) {
                next_next = reinterpret_cast<Block*>(next_next_addr);
                if (next_next->prev_physical != next) {
                    return;  // Physical chain is broken
                }
            }
            
            // Remove next from free lists
            mapping_remove(next->size, next);
            
            // Merge sizes
            size_t new_size = block->size + sizeof(Block) + next->size;
            if (new_size > total_size) return;  // Overflow check
            block->size = new_size;
            
            // Update physical links
            if (next_next) {
                next_next->prev_physical = block;
            }
        }
    }

    // Try to merge with previous block if it exists and is valid
    if (block->prev_physical) {
        Block* prev = block->prev_physical;
        char* prev_addr = reinterpret_cast<char*>(prev);
        
        // Verify previous block is valid and free
        if (prev_addr >= reinterpret_cast<char*>(first_block) &&
            prev->size >= MIN_BLOCK_SIZE && 
            prev->size <= (total_size - (prev_addr - reinterpret_cast<char*>(first_block))) &&
            prev_addr + sizeof(Block) + prev->size == block_addr &&
            prev->is_free) {
            
            // Find next block before merging
            char* next_addr = block_addr + sizeof(Block) + block->size;
            Block* next = nullptr;
            if (next_addr + sizeof(Block) <= first_block_end) {
                next = reinterpret_cast<Block*>(next_addr);
                if (next->prev_physical != block) {
                    return;  // Physical chain is broken
                }
            }
            
            // Remove both blocks from free lists
            mapping_remove(prev->size, prev);
            mapping_remove(block->size, block);

            // Merge sizes
            size_t new_size = prev->size + sizeof(Block) + block->size;
            if (new_size > total_size) return;  // Overflow check
            prev->size = new_size;

            // Update physical links
            if (next) {
                next->prev_physical = prev;
            }

            // Insert merged block back into free lists
            mapping_insert(prev->size, prev);
            return;
        }
    }

    // If no merging occurred, insert this block into free lists
    mapping_insert(block->size, block);
}

size_t TLSFAllocator::alloc(size_t size) {
    if (!size) return 0;

    // Add header size and align
    size_t required_size = (size + 7) & ~7;  // Align size to 8 bytes
    if (required_size < MIN_BLOCK_SIZE) required_size = MIN_BLOCK_SIZE;

    // Find suitable block
    Block* block = mapping_find(required_size);
    if (!block) {
        throw std::bad_alloc();
    }

    // Remove from free lists
    mapping_remove(block->size, block);

    // Split if too large
    block = split_block(block, required_size);

    // Mark as allocated
    block->is_free = false;
    block->allocated = size;
    allocated_size += size;

    return reinterpret_cast<char*>(block) - reinterpret_cast<char*>(first_block);
}

void TLSFAllocator::dealloc(size_t address) {
    if (address == 0) return;
    
    // Calculate and verify block address
    char* block_addr = reinterpret_cast<char*>(first_block) + address;
    if (block_addr < reinterpret_cast<char*>(first_block) || 
        block_addr >= reinterpret_cast<char*>(first_block) + total_size) {
        throw std::invalid_argument("Invalid deallocation address");
    }

    Block* block = reinterpret_cast<Block*>(block_addr);
    
    // Verify block is within bounds and valid
    char* block_end = block_addr + sizeof(Block) + block->size;
    if (block_end > reinterpret_cast<char*>(first_block) + total_size ||
        block->size < MIN_BLOCK_SIZE || block->size > total_size ||
        block->allocated > block->size) {
        throw std::invalid_argument("Invalid block detected");
    }

    if (block->is_free) {
        throw std::invalid_argument("Double free detected");
    }

    allocated_size -= block->allocated;
    block->is_free = true;
    block->allocated = 0;
    block->next_free = nullptr;
    block->prev_free = nullptr;

    merge_block(block);
}

size_t TLSFAllocator::find_last_allocated_address() const {
    size_t last_addr = 0;
    char* current_addr = reinterpret_cast<char*>(first_block);
    char* end_addr = reinterpret_cast<char*>(first_block) + total_size;
    
    while (current_addr + sizeof(Block) <= end_addr) {
        Block* current = reinterpret_cast<Block*>(current_addr);
        if (current->size == 0 || current->size > total_size) break;
        
        if (!current->is_free) {
            last_addr = current_addr - reinterpret_cast<char*>(first_block) + 
                       sizeof(Block) + current->size;
        }
        
        char* next_addr = current_addr + sizeof(Block) + current->size;
        if (next_addr <= current_addr || next_addr > end_addr) break;
        current_addr = next_addr;
    }
    
    return last_addr;
}

double TLSFAllocator::get_internal_fragmentation() const {
    if (!allocated_size) return 0.0;

    size_t total_wasted = 0;
    char* current_addr = reinterpret_cast<char*>(first_block);
    char* end_addr = reinterpret_cast<char*>(first_block) + total_size;
    
    while (current_addr + sizeof(Block) <= end_addr) {
        Block* current = reinterpret_cast<Block*>(current_addr);
        if (current->size == 0 || current->size > total_size) break;
        
        if (!current->is_free) {
            total_wasted += current->size - current->allocated;
        }
        
        char* next_addr = current_addr + sizeof(Block) + current->size;
        if (next_addr <= current_addr || next_addr > end_addr) break;
        current_addr = next_addr;
    }

    return static_cast<double>(total_wasted) / allocated_size;
}

double TLSFAllocator::calculate_external_fragmentation(size_t max_address) const {
    if (!allocated_size) return 0.0;

    // Count free blocks by size class
    std::vector<size_t> free_blocks(FL_INDEX_COUNT, 0);
    size_t total_free = 0;
    size_t largest_free = 0;
    char* current_addr = reinterpret_cast<char*>(first_block);
    char* end_addr = reinterpret_cast<char*>(first_block) + total_size;
    
    while (current_addr + sizeof(Block) <= end_addr) {
        Block* current = reinterpret_cast<Block*>(current_addr);
        if (current->size == 0 || current->size > total_size) break;
        
        size_t addr = current_addr - reinterpret_cast<char*>(first_block);
        if (max_address && addr >= max_address) break;

        if (current->is_free) {
            auto [fl, _] = mapping_indexes(current->size);
            if (fl < FL_INDEX_COUNT) {
                free_blocks[fl]++;
                total_free += current->size;
                largest_free = std::max(largest_free, current->size);
            }
        }

        char* next_addr = current_addr + sizeof(Block) + current->size;
        if (next_addr <= current_addr || next_addr > end_addr) break;
        current_addr = next_addr;
    }

    if (total_free == 0) return 0.0;

    // Calculate fragmentation for each size class
    double weighted_sum = 0.0;
    size_t total_weight = 0;

    // Only consider sizes up to the largest free block
    size_t max_fl = fls(largest_free);
    if (max_fl >= FL_INDEX_COUNT) max_fl = FL_INDEX_COUNT - 1;

    for (size_t i = 0; i <= max_fl; ++i) {
        size_t block_size = MIN_BLOCK_SIZE << i;
        if (block_size > largest_free) break;

        size_t potential_blocks = total_free / block_size;
        if (potential_blocks == 0) continue;

        size_t actual_blocks = free_blocks[i];
        for (size_t j = i + 1; j <= max_fl; ++j) {
            if (free_blocks[j] > 0) {
                actual_blocks += free_blocks[j] * (1ULL << (j - i));
            }
        }

        double ratio = static_cast<double>(actual_blocks) / potential_blocks;
        if (ratio > 1.0) ratio = 1.0;

        weighted_sum += block_size * ratio;
        total_weight += block_size;
    }

    return total_weight > 0 ? 1.0 - (weighted_sum / total_weight) : 0.0;
}

size_t TLSFAllocator::align_alloc(size_t size) {
    if (!size) return 0;

    // Calculate required size with alignment
    size_t required_size = (size + 7) & ~7;  // Align size to 8 bytes
    if (required_size < MIN_BLOCK_SIZE) required_size = MIN_BLOCK_SIZE;

    // Find suitable block
    Block* block = mapping_find(required_size);
    if (!block) {
        throw std::bad_alloc();
    }

    // Remove from free lists
    mapping_remove(block->size, block);

    // Calculate aligned position within block
    size_t block_addr = reinterpret_cast<char*>(block) - reinterpret_cast<char*>(first_block);
    size_t data_addr = block_addr + sizeof(Block);
    size_t aligned_addr = (data_addr + required_size - 1) & ~(required_size - 1);
    size_t offset = aligned_addr - data_addr;

    // If we need to split for alignment
    if (offset > 0 && offset >= MIN_BLOCK_SIZE + sizeof(Block)) {
        // Create a small block for the front piece
        Block* front = block;
        front->size = offset - sizeof(Block);
        front->is_free = true;
        front->allocated = 0;
        mapping_insert(front->size, front);

        // Create the aligned block
        block = reinterpret_cast<Block*>(reinterpret_cast<char*>(first_block) + aligned_addr - sizeof(Block));
        block->size = required_size;
        block->is_free = false;
        block->allocated = 0;
        block->prev_physical = front;
        block->next_free = nullptr;
        block->prev_free = nullptr;
    }

    // Split remaining space if possible
    block = split_block(block, required_size);

    // Mark as allocated
    block->is_free = false;
    block->allocated = size;
    allocated_size += size;

    return reinterpret_cast<char*>(block) - reinterpret_cast<char*>(first_block);
}