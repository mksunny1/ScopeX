#include <iostream>
#include <chrono>
#include <vector>
#include <memory>
#include <cstdint>
#include "scope.hpp"

// --- Cross-Platform Real Memory (RSS) Measurement ---
#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
size_t getMeasuredRSS() {
    PROCESS_MEMORY_COUNTERS info;
    GetProcessMemoryInfo(GetCurrentProcess(), &info, sizeof(info));
    return info.WorkingSetSize;
}
#elif defined(__APPLE__) || defined(__MACH__)
#include <mach/mach.h>
size_t getMeasuredRSS() {
    struct mach_task_basic_info info;
    mach_msg_type_number_t infoCount = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &infoCount) != KERN_SUCCESS)
        return 0;
    return info.resident_size;
}
#elif defined(__linux__)
#include <unistd.h>
#include <fstream>
size_t getMeasuredRSS() {
    std::ifstream statm("/proc/self/statm");
    long rss = 0;
    if (statm) {
        long size;
        statm >> size >> rss;
        rss *= sysconf(_SC_PAGESIZE);
    }
    return static_cast<size_t>(rss);
}
#else
size_t getMeasuredRSS() { return 0; }
#endif

// --- Workload Payloads ---
struct SmallEntity {
    uint64_t id;
    uint32_t val;
}; // 16 bytes

struct LargeEntity {
    uint8_t data[4096]; // 4 KB block
};

void runScenario1_SmallHighFrequency() {
    const size_t count = 2'000'000;
    std::cout << "\n--- Scenario 1: Small Object High Frequency (" << count << " items of 16B) ---\n";

    // 1A. Vanilla C++
    size_t memBefore1 = getMeasuredRSS();
    size_t memAfter1 = 0;
    std::chrono::duration<double, std::milli> time1;
    {
        auto start1 = std::chrono::high_resolution_clock::now();
        std::vector<std::unique_ptr<SmallEntity>> vanillaVec;
        vanillaVec.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            vanillaVec.push_back(std::make_unique<SmallEntity>(SmallEntity{i, static_cast<uint32_t>(i)}));
        }
        volatile uint64_t check = vanillaVec[count / 2]->id;
        (void)check;
        auto end1 = std::chrono::high_resolution_clock::now();
        time1 = end1 - start1;
        memAfter1 = getMeasuredRSS(); // measured while still live, after timing stops, before teardown
    }
    std::cout << "  [Vanilla unique_ptr] Time: " << time1.count() << " ms | Measured RSS Delta: ~" << (static_cast<int64_t>(memAfter1 - memBefore1) / 1024 / 1024) << " MB\n";

    // 1B. ScopeX
    // Chunk capacity is elements-per-chunk, not bytes -- sized to the
    // workload so this allocates one chunk up front, same intent as the
    // original "single region" test.
    size_t memBefore2 = getMeasuredRSS();
    size_t memAfter2 = 0;
    std::chrono::duration<double, std::milli> time2;
    {
        auto start2 = std::chrono::high_resolution_clock::now();
        Scope scope(count);
        std::vector<Handle<SmallEntity>> handles;
        handles.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            handles.push_back(scope.push<SmallEntity>(SmallEntity{i, static_cast<uint32_t>(i)}));
        }
        volatile uint64_t check = handles[count / 2]->id;
        (void)check;
        auto end2 = std::chrono::high_resolution_clock::now();
        time2 = end2 - start2;
        memAfter2 = getMeasuredRSS(); // measured while still live, after timing stops, before teardown
    }
    std::cout << "  [ScopeX Region Stack] Time: " << time2.count() << " ms | Measured RSS Delta: ~" << (static_cast<int64_t>(memAfter2 - memBefore2) / 1024 / 1024) << " MB\n";
}

void runScenario2_LargePayloadThroughput() {
    const size_t count = 20'000;
    std::cout << "\n--- Scenario 2: Large Payload Blocks (" << count << " items of 4KB) ---\n";

    // 2A. Vanilla C++
    size_t memBefore1 = getMeasuredRSS();
    size_t memAfter1 = 0;
    std::chrono::duration<double, std::milli> time1;
    {
        auto start1 = std::chrono::high_resolution_clock::now();
        std::vector<std::unique_ptr<LargeEntity>> vanillaVec;
        vanillaVec.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            vanillaVec.push_back(std::make_unique<LargeEntity>());
        }
        auto end1 = std::chrono::high_resolution_clock::now();
        time1 = end1 - start1;
        memAfter1 = getMeasuredRSS(); // measured while still live, after timing stops, before teardown
    }
    std::cout << "  [Vanilla unique_ptr] Time: " << time1.count() << " ms | Measured RSS Delta: ~" << (static_cast<int64_t>(memAfter1 - memBefore1) / 1024 / 1024) << " MB\n";

    // 2B. ScopeX
    // count elements at 4KB each (~80MB chunk) -- NOT the old 1024*1024*128
    // constant, which under elements-per-chunk semantics would try to
    // allocate 134M * 4KB in one shot.
    size_t memBefore2 = getMeasuredRSS();
    size_t memAfter2 = 0;
    std::chrono::duration<double, std::milli> time2;
    {
        auto start2 = std::chrono::high_resolution_clock::now();
        Scope scope(count);
        std::vector<Handle<LargeEntity>> handles;
        handles.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            handles.push_back(scope.push<LargeEntity>(LargeEntity{}));
        }
        auto end2 = std::chrono::high_resolution_clock::now();
        time2 = end2 - start2;
        memAfter2 = getMeasuredRSS(); // measured while still live, after timing stops, before teardown
    }
    std::cout << "  [ScopeX Region Stack] Time: " << time2.count() << " ms | Measured RSS Delta: ~" << (static_cast<int64_t>(memAfter2 - memBefore2) / 1024 / 1024) << " MB\n";
}

void runScenario3_ChurnAndTeardown() {
    const size_t count = 1'000'000;
    std::cout << "\n--- Scenario 3: Bulk Teardown / Cleanup & Churn (" << count << " items) ---\n";

    // 3A. Vanilla C++ Vector Teardown
    auto start1 = std::chrono::high_resolution_clock::now();
    {
        std::vector<std::unique_ptr<SmallEntity>> vanillaVec;
        vanillaVec.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            vanillaVec.push_back(std::make_unique<SmallEntity>(SmallEntity{i, 0}));
        }
        // Scope exit triggers destructor freeing 1M individual allocations
    }
    auto end1 = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> time1 = end1 - start1;
    std::cout << "  [Vanilla unique_ptr delete] Teardown Time: " << time1.count() << " ms\n";

    // 3B. ScopeX Region Exit Teardown
    // Handles are discarded here on purpose -- unlike moveTo, a discarded
    // push() handle doesn't affect the object's lifetime; Scope owns the
    // storage directly, and exit() destroys everything regardless of
    // whether any handle to it was ever kept.
    auto start2 = std::chrono::high_resolution_clock::now();
    {
        Scope scope(count);
        for (size_t i = 0; i < count; ++i) {
            scope.push<SmallEntity>(SmallEntity{i, 0});
        }
        scope.exit(); // destroys every live object across every pool this scope created
    }
    auto end2 = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> time2 = end2 - start2;
    std::cout << "  [ScopeX scope.exit()]        Teardown Time: " << time2.count() << " ms\n";
}

void runScenario4_MutableStateUpdate() {
    const size_t count = 1'000'000;
    std::cout << "\n--- Scenario 4: Mutable State Update & Resolution (" << count << " items) ---\n";

    // 4A. Vanilla C++
    auto start1 = std::chrono::high_resolution_clock::now();
    {
        std::vector<std::unique_ptr<SmallEntity>> vanillaVec;
        vanillaVec.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            vanillaVec.push_back(std::make_unique<SmallEntity>(SmallEntity{i, 10}));
        }
        for (auto& ptr : vanillaVec) {
            ptr->val += 5;
        }
    }
    auto end1 = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> time1 = end1 - start1;
    std::cout << "  [Vanilla unique_ptr mutation] Time: " << time1.count() << " ms\n";

    // 4B. ScopeX -- no epoch/generation tracking in the current design; a
    // Handle<T> resolves through a single cached pointer, same cost either
    // way, so this is just direct handle-cached mutation.
    auto start2 = std::chrono::high_resolution_clock::now();
    {
        Scope scope(count);
        std::vector<Handle<SmallEntity>> handles;
        handles.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            handles.push_back(scope.push<SmallEntity>(SmallEntity{i, 10}));
        }
        for (auto& h : handles) {
            h->val += 5;
        }
    }
    auto end2 = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> time2 = end2 - start2;
    std::cout << "  [ScopeX Mutable Handles]     Time: " << time2.count() << " ms\n";
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "      ScopeX Measured Time & Space Benchmark Suite      \n";
    std::cout << "========================================================\n";

    runScenario1_SmallHighFrequency();
    runScenario2_LargePayloadThroughput();
    runScenario3_ChurnAndTeardown();
    runScenario4_MutableStateUpdate();

    std::cout << "\n========================================================\n";
    std::cout << "               Benchmark Complete Successfully          \n";
    std::cout << "========================================================\n";
    return 0;
}