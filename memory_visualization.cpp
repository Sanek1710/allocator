#include "memory_visualization.hpp"
#include <vector>
#include <fstream>
#include <cstdint>
#include <algorithm>

#include <iostream>

namespace {
    struct Color {
        uint8_t b, g, r;  // BMP uses BGR format
        Color(uint8_t red, uint8_t green, uint8_t blue) : b(blue), g(green), r(red) {}

        static Color free_block(size_t size, size_t min_size) {
            // Vibrant blue gradient based on block size
            int level = __builtin_ctz(size) - __builtin_ctz(min_size);
            uint8_t blue = static_cast<uint8_t>(100 + (155 * level) / 32);
            return Color(50, 50, 200 + blue/4);  // More saturated blue
        }

        static Color allocated_block(double fragmentation) {
            // Vibrant gradient from green (low fragmentation) to red (high fragmentation)
            return Color(
                static_cast<uint8_t>(200 * fragmentation),  // Red
                static_cast<uint8_t>(200 * (1 - fragmentation)),  // Green
                50                                         // Blue
            );
        }

        static Color header() {
            return Color(180, 180, 180);  // Lighter gray for headers
        }
    };

    // BMP file header (14 bytes)
    #pragma pack(push, 1)
    struct BMPFileHeader {
        uint16_t file_type{0x4D42};  // BM
        uint32_t file_size{0};
        uint16_t reserved1{0};
        uint16_t reserved2{0};
        uint32_t offset_data{0};
    };

    // BMP info header (40 bytes)
    struct BMPInfoHeader {
        uint32_t size{0};
        int32_t width{0};
        int32_t height{0};
        uint16_t planes{1};
        uint16_t bit_count{24};
        uint32_t compression{0};
        uint32_t size_image{0};
        int32_t x_pixels_per_meter{0};
        int32_t y_pixels_per_meter{0};
        uint32_t colors_used{0};
        uint32_t colors_important{0};
    };
    #pragma pack(pop)

    void write_bmp(const std::string& filename, const std::vector<std::vector<Color>>& image) {
        int32_t width = static_cast<int32_t>(image[0].size());
        int32_t height = static_cast<int32_t>(image.size());
        
        // Row padding for 4 byte alignment
        uint32_t padding = (4 - (width * 3) % 4) % 4;
        uint32_t row_size = width * 3 + padding;

        BMPFileHeader file_header;
        BMPInfoHeader info_header;

        // Set up headers
        info_header.size = sizeof(BMPInfoHeader);
        info_header.width = width;
        info_header.height = height;
        info_header.size_image = row_size * height;

        file_header.offset_data = sizeof(BMPFileHeader) + sizeof(BMPInfoHeader);
        file_header.file_size = file_header.offset_data + info_header.size_image;

        std::ofstream file(filename, std::ios::binary);
        if (!file) return;

        // Write headers
        file.write(reinterpret_cast<const char*>(&file_header), sizeof(file_header));
        file.write(reinterpret_cast<const char*>(&info_header), sizeof(info_header));

        // Write image data (bottom-up)
        std::vector<uint8_t> padding_data(padding, 0);
        for (int y = height - 1; y >= 0; --y) {
            file.write(reinterpret_cast<const char*>(image[y].data()), width * sizeof(Color));
            if (padding > 0) {
                file.write(reinterpret_cast<const char*>(padding_data.data()), padding);
            }
        }
    }

    void write_history_bmp(const std::string& filename, 
                          const std::vector<MemoryStateTracker::MemoryState>& history) {
        if (history.empty()) return;

        const auto& first_state = history[0];
        size_t width = first_state.total_size / 16;  // Divide by minimum block size (16 bytes)
        size_t states = history.size();
        size_t height = states * (MemoryStateTracker::LINE_HEIGHT + MemoryStateTracker::LINE_GAP);  // Total height including gaps
        std::vector<std::vector<Color>> image(height, std::vector<Color>(width, Color(0, 0, 0)));  // Black background

        // Draw each state
        for (size_t state_idx = 0; state_idx < states; ++state_idx) {
            const auto& state = history[state_idx];
            size_t y_start = state_idx * (MemoryStateTracker::LINE_HEIGHT + MemoryStateTracker::LINE_GAP);
            size_t y_end = y_start + MemoryStateTracker::LINE_HEIGHT;

            // Draw blocks
            for (size_t i = 0; i < state.blocks.size(); ++i) {
                size_t addr = state.blocks[i].first;
                size_t size = state.blocks[i].second;

                size_t start_x = addr / 16;  // Convert byte address to block index
                size_t end_x = (addr + size) / 16;
                if (start_x >= width) continue;
                if (end_x > width) end_x = width;

                Color color = state.is_free[i] 
                    ? Color::free_block(size, 16)  // Use MIN_BLOCK_SIZE
                    : Color::allocated_block(state.fragmentation[i]);

                for (size_t y = y_start; y < y_end; ++y) {
                    for (size_t x = start_x; x < end_x; ++x) {
                        image[y][x] = color;
                    }
                }
            }
        }

        write_bmp(filename, image);
    }
}

void MemoryStateTracker::track_state(const MemoryAllocator& alloc) {
    MemoryState state;
    state.total_size = alloc.total_size;

    for (const auto& pair : alloc.blocks) {
        state.blocks.emplace_back(pair.first, pair.second.size);
        state.is_free.push_back(pair.second.is_free);
        double frag = pair.second.is_free ? 0.0 :
            static_cast<double>(pair.second.size - pair.second.allocated) / pair.second.size;
        state.fragmentation.push_back(frag);
    }

    history.push_back(std::move(state));
}

void MemoryStateTracker::track_state(const TLSFAllocator& alloc) {
    MemoryState state;
    state.total_size = alloc.total_size;

    char* current_addr = reinterpret_cast<char*>(alloc.first_block);
    char* end_addr = current_addr + alloc.total_size;
    
    while (current_addr + sizeof(TLSFAllocator::Block) <= end_addr) {
        auto* block = reinterpret_cast<TLSFAllocator::Block*>(current_addr);
        if (block->size == 0 || block->size > alloc.total_size) break;

        size_t addr = current_addr - reinterpret_cast<char*>(alloc.first_block);
        state.blocks.emplace_back(addr, block->size);
        state.is_free.push_back(block->is_free);
        double frag = block->is_free ? 0.0 :
            static_cast<double>(block->size - block->allocated) / block->size;
        state.fragmentation.push_back(frag);

        char* next_addr = current_addr + sizeof(TLSFAllocator::Block) + block->size;
        if (next_addr <= current_addr || next_addr > end_addr) break;
        current_addr = next_addr;
    }

    history.push_back(std::move(state));
}

void MemoryStateTracker::save_history(const std::string& filename) {
    write_history_bmp(filename, history);
} 