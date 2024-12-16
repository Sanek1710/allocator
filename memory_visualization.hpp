#pragma once
#include "allocator.hpp"
#include "tlsf_allocator.hpp"
#include <string>
#include <vector>

// Class to track memory state history
class MemoryStateTracker {
public:
    static constexpr size_t LINE_HEIGHT = 1;  // Height of each state line in pixels
    static constexpr size_t LINE_GAP = 0;     // Gap between lines in pixels

    struct MemoryState {
        size_t total_size;
        std::vector<std::pair<size_t, size_t>> blocks;  // (address, size)
        std::vector<bool> is_free;  // true if block is free
        std::vector<double> fragmentation;  // internal fragmentation per block
    };

    static MemoryStateTracker& instance() {
        static MemoryStateTracker tracker;
        return tracker;
    }

    void track_state(const MemoryAllocator& alloc);
    void track_state(const TLSFAllocator& alloc);
    void save_history(const std::string& filename);
    void clear() { history.clear(); }

private:
    std::vector<MemoryState> history;
};

// Helper functions to track states
inline void track_memory_state(const MemoryAllocator& alloc) {
    MemoryStateTracker::instance().track_state(alloc);
}

inline void track_memory_state(const TLSFAllocator& alloc) {
    MemoryStateTracker::instance().track_state(alloc);
}

inline void save_memory_history(const std::string& filename) {
    MemoryStateTracker::instance().save_history(filename);
}

inline void clear_memory_history() {
    MemoryStateTracker::instance().clear();
} 