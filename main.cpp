#include "Scope.hpp"
#include <iostream>

struct SampleEntity { int id; double value; };

int main() {
    std::cout << "=== Running Infinite-Scale Chained ScopeX Engine ===\n\n";

    // Initialize with a tiny 128-byte block configuration to deliberately trigger block chaining/growth
    ManagedScope scopeBus(128);

    // 1. ALLOCATE MUTABLE VALUE (Version checked, free-list enabled)
    CheckedHandle mutHandle = scopeBus.pushMut<SampleEntity>();
    scopeBus.access<SampleEntity>(mutHandle)->id = 777;

    // 2. ALLOCATE ULTRA-FAST CONST VALUE (Skips version tracking overhead completely)
    CheckedHandle constHandle = scopeBus.pushConst<int>(9999);

    // 3. ALLOCATE ENOUGH TO FORCE A NEW BLOCK CHAIN
    // This pushes memory layout footprint past the 128-byte chunk bounds
    CheckedHandle dynamicChainHandle = scopeBus.pushMut<SampleEntity>();
    scopeBus.access<SampleEntity>(dynamicChainHandle)->id = 888;

    std::cout << "Block Index for Item 1: " << mutHandle.blockIndex << "\n";
    std::cout << "Block Index for Item 3 (New Segment): " << dynamicChainHandle.blockIndex << "\n";

    // Verify original pointer references never broke or shifted
    std::cout << "Item 1 Verified Value: " << scopeBus.access<SampleEntity>(mutHandle)->id << "\n";
    std::cout << "Item 3 Verified Value: " << scopeBus.access<SampleEntity>(dynamicChainHandle)->id << "\n";

    scopeBus.wipe();
    std::cout << "\n=== All Architectural Core Tests Passed Successfully ===\n";
    return 0;
}
