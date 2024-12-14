#include "allocator.hpp"
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <ostream>
#include <random>
#include <vector>

using Clock = std::chrono::high_resolution_clock;

void print_mem_state(const MemoryAllocator &alloc) {
  std::cout << "Memory State:\n"
            << "  Memory                 : " << alloc.get_allocated_space()
            << " / " << alloc.get_total_space() << " ("
            << (alloc.get_allocated_space() * 100.0 / alloc.get_total_space())
            << "% used)\n"
            << "  Internal frag          : " << std::fixed
            << std::setprecision(3) << alloc.get_internal_fragmentation()
            << "\n"
            << "  External frag (total)  : "
            << alloc.get_external_fragmentation() << "\n"
            << "  External frag (trimmed): "
            << alloc.get_trimmed_external_fragmentation() << "\n\n";
}

void stress_test(MemoryAllocator &alloc, size_t operations) {
  std::vector<size_t> addresses;
  addresses.reserve(operations / 2); // Reserve to avoid reallocation

  std::mt19937_64 rng(
      std::chrono::steady_clock::now().time_since_epoch().count());
  std::uniform_int_distribution<size_t> size_dist(1, 1024);
  std::uniform_int_distribution<size_t> op_dist(
      0, 99); // For deciding alloc/dealloc

  size_t allocs = 0, deallocs = 0;
  auto start = Clock::now();

  for (size_t i = 0; i < operations; ++i) {
    if (addresses.empty() || op_dist(rng) < 51) { // 50% chance to allocate
      try {
        size_t size = size_dist(rng);
        size_t addr = alloc.alloc(size);
        addresses.push_back(addr);
        ++allocs;
      } catch (const std::bad_alloc &) {
        // Memory full, force some deallocations
        while (!addresses.empty() && op_dist(rng) < 50) {
          alloc.dealloc(addresses.back());
          addresses.pop_back();
          ++deallocs;
        }
      }
    } else if (!addresses.empty()) {
      size_t index =
          std::uniform_int_distribution<size_t>(0, addresses.size() - 1)(rng);
      alloc.dealloc(addresses[index]);
      addresses[index] = addresses.back();
      addresses.pop_back();
      ++deallocs;
    }

    if (i % (operations / 10) == 0) {
      std::cout << "Progress: " << (i * 100 / operations) << "%\n";
      print_mem_state(alloc);
    }
  }

  auto end = Clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  std::cout << "\nPerformance Results:\n"
            << "Time: " << duration.count() << "ms\n"
            << "Operations: " << operations << "\n"
            << "Allocations: " << allocs << "\n"
            << "Deallocations: " << deallocs << "\n"
            << "Ops/sec: " << (operations * 1000.0 / duration.count())
            << "\n\n";
  print_mem_state(alloc);
}

int main() {
  try {
    MemoryAllocator allocator(1024 * 1024); // 1MB
    stress_test(allocator, 100000);
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
  return 0;
}

int main1(int argc, char *argv[]) {

  MemoryAllocator allocator(2048); // 1MB

  std::vector<size_t> addrs;
  while (allocator.get_free_space() > 1024) {
    size_t addr = allocator.alloc(MemoryAllocator::MIN_BLOCK_SIZE);
    addrs.push_back(addr);
  }

  for (size_t i = 0; i < addrs.size(); ++ ++i) {
    allocator.dealloc(addrs[i]);
  }
  print_mem_state(allocator);

  return 0;
}