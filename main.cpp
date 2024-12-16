#include "allocator.hpp"
#include "memory_visualization.hpp"
#include "tlsf_allocator.hpp"
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <ostream>
#include <random>
#include <vector>

using Clock = std::chrono::high_resolution_clock;

template <typename Allocator> void print_mem_state(const Allocator &alloc) {
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

template <typename Allocator>
void stress_test(Allocator &alloc, size_t operations) {
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
      track_memory_state(alloc);
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
  track_memory_state(alloc); // Track final state
}

template <typename Allocator>
void stress_test_align(Allocator &alloc, size_t operations) {
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
        size_t addr = alloc.align_alloc(size);
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
      track_memory_state(alloc);
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
  track_memory_state(alloc); // Track final state
}

int test1() {
  try {
    clear_memory_history();
    MemoryAllocator allocator(1024 * 1024); // 1MB
    stress_test(allocator, 100000);
    save_memory_history("buddy_state.bmp");
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
  return 0;
}

int test2() {
  try {
    clear_memory_history();
    MemoryAllocator allocator(1024 * 1024); // 1MB
    stress_test_align(allocator, 100000);
    save_memory_history("buddy_state_aligned.bmp");
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
  return 0;
}

int test3() {
  try {
    clear_memory_history();
    TLSFAllocator allocator(1024 * 1024); // 1MB
    stress_test(allocator, 100000);
    save_memory_history("tlsf_state.bmp");
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
  return 0;
}

int test4() {
  try {
    clear_memory_history();
    TLSFAllocator allocator(1024 * 1024); // 1MB
    stress_test_align(allocator, 100000);
    save_memory_history("tlsf_state_aligned.bmp");
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
  return 0;
}

int main0() {
  std::cout << "\nRunning Buddy Allocator Tests:\n";
  test1();
  test2();

  std::cout << "\nRunning TLSF Allocator Tests:\n";
  test3();
  test4();
  return 0;
}

int main() {
  MemoryAllocator allocator(2048); // 2KB

  std::vector<size_t> addrs;
  clear_memory_history();
  
  std::mt19937_64 rng(std::chrono::steady_clock::now().time_since_epoch().count());  // Fixed seed for reproducibility
  std::uniform_int_distribution<size_t> size_dist(4, 64);  // Random sizes between 4 and 64 bytes
  std::uniform_int_distribution<size_t> op_dist(0, 99);    // For deciding alloc/dealloc

  // Perform 100 random operations
  for (int i = 0; i < 100; i++) {
    try {
      if (addrs.empty() || op_dist(rng) < 70) {  // 70% chance to allocate
        size_t size = size_dist(rng);
        size_t addr = allocator.alloc(size);
        addrs.push_back(addr);
      } else {  // 30% chance to deallocate
        size_t idx = std::uniform_int_distribution<size_t>(0, addrs.size() - 1)(rng);
        allocator.dealloc(addrs[idx]);
        addrs[idx] = addrs.back();
        addrs.pop_back();
      }
      track_memory_state(allocator);
    } catch (const std::bad_alloc&) {
      // If allocation fails, force some deallocations
      while (!addrs.empty() && op_dist(rng) < 50) {
        size_t idx = std::uniform_int_distribution<size_t>(0, addrs.size() - 1)(rng);
        allocator.dealloc(addrs[idx]);
        addrs[idx] = addrs.back();
        addrs.pop_back();
        track_memory_state(allocator);
      }
    }
  }

  print_mem_state(allocator);
  save_memory_history("buddy_random.bmp");
  return 0;
}

int main3() {
  MemoryAllocator allocator(2048); // 2KB

  std::vector<size_t> addrs;
  clear_memory_history();
  
  std::mt19937_64 rng(42);  // Fixed seed for reproducibility
  std::uniform_int_distribution<size_t> size_dist(4, 64);  // Random sizes between 4 and 64 bytes
  std::uniform_int_distribution<size_t> op_dist(0, 99);    // For deciding alloc/dealloc

  // Perform 100 random operations
  for (int i = 0; i < 100; i++) {
    try {
      if (addrs.empty() || op_dist(rng) < 70) {  // 70% chance to allocate
        size_t size = size_dist(rng);
        size_t addr = allocator.alloc(size);
        addrs.push_back(addr);
      } else {  // 30% chance to deallocate
        size_t idx = std::uniform_int_distribution<size_t>(0, addrs.size() - 1)(rng);
        allocator.dealloc(addrs[idx]);
        addrs[idx] = addrs.back();
        addrs.pop_back();
      }
      track_memory_state(allocator);
    } catch (const std::bad_alloc&) {
      // If allocation fails, force some deallocations
      while (!addrs.empty() && op_dist(rng) < 50) {
        size_t idx = std::uniform_int_distribution<size_t>(0, addrs.size() - 1)(rng);
        allocator.dealloc(addrs[idx]);
        addrs[idx] = addrs.back();
        addrs.pop_back();
        track_memory_state(allocator);
      }
    }
  }

  print_mem_state(allocator);
  save_memory_history("buddy_random.bmp");
  return 0;
}

int main2(int argc, char *argv[]) {
  MemoryAllocator allocator(2048); // 1KB

  std::vector<size_t> addrs;
  clear_memory_history();
  int flip = 0;
  while (addrs.size() < 64) {
    size_t addr = allocator.alloc(flip ? 4 : 12);
    addrs.push_back(addr);
    track_memory_state(allocator);
    flip = !flip;
  }

  for (size_t i = 0; i < addrs.size(); ++ ++i) {
    allocator.dealloc(addrs[i]);
    track_memory_state(allocator);
  }
  allocator.alloc(31);
  allocator.alloc(40);
  allocator.alloc(48);
  allocator.alloc(56);
  allocator.alloc(17);
  track_memory_state(allocator);

  print_mem_state(allocator);
  save_memory_history("buddy_state.bmp");

  return 0;
}

