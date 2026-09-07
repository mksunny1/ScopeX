#ifndef SCOPE_HPP
#define SCOPE_HPP

#include <iostream>
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <cstring>
#include <cassert>
#include <memory>

enum class MemoryIntent : uint8_t {
    InlineStack,
    HeapProxy
};

struct AllocationHeader {
    uint32_t magic;       // 0xDEADC0DE validation token
    uint32_t size;        // Total payload size (padded)
    uint32_t generation;  // Version tracker to completely prevent stale lookups
    MemoryIntent intent;  // Stack vs Heap tracking
    bool isDead;          // Hole reuse marker
};

struct CheckedHandle {
    size_t blockIndex;    // Which memory block it lives in
    size_t offsetId;      // Byte offset within that specific block
    uint32_t generation;  // Version signature
};

// Represents a single fixed-size segment of hardware memory
struct MemoryBlock {
    std::vector<uint8_t> buffer;
    size_t writeOffset = 0;
    
    MemoryBlock(size_t size) : buffer(size), writeOffset(0) {}
};

class Scope {
protected:
    std::vector<std::unique_ptr<MemoryBlock>> blocks; // Chained list of memory pages
    size_t blockSize;
    
    // Free-list hole index tracking
    std::vector<CheckedHandle> freeListHoles;

    // Helper to align allocations to 8-byte boundaries to completely prevent small unusable holes
    size_t align(size_t size) {
        return (size + 7) & ~7;
    }

    AllocationHeader* getHeader(size_t blockIdx, size_t offset) {
        if (blockIdx >= blocks.size()) return nullptr;
        MemoryBlock& block = *blocks[blockIdx];
        if (offset + sizeof(AllocationHeader) > block.writeOffset) return nullptr;
        AllocationHeader* header = reinterpret_cast<AllocationHeader*>(&block.buffer[offset]);
        if (header->magic != 0xDEADC0DE) return nullptr;
        return header;
    }

public:
    // Initialize with a standard block/page size (e.g., 64KB per chunk)
    Scope(size_t defaultBlockSize) : blockSize(defaultBlockSize) {
        blocks.push_back(std::make_unique<MemoryBlock>(blockSize));
    }

    virtual ~Scope() {
        clearAllInternal();
    }

    // THE BREAKTHROUGH: Infinitely scalable allocation loop
    CheckedHandle allocateRaw(size_t dataSize, MemoryIntent intent, bool checkGeneration) {
        size_t paddedDataSize = align(dataSize);
        size_t totalNeeded = sizeof(AllocationHeader) + paddedDataSize;

        // 1. FREE-LIST HOLE REUSE CHECK
        if (checkGeneration) {
            for (auto it = freeListHoles.begin(); it != freeListHoles.end(); ++it) {
                AllocationHeader* holeHeader = getHeader(it->blockIndex, it->offsetId);
                if (holeHeader && holeHeader->isDead && holeHeader->size >= paddedDataSize) {
                    holeHeader->isDead = false;
                    holeHeader->intent = intent;
                    holeHeader->generation++; // Invalidate previous handle state
                    
                    CheckedHandle reusedHandle = *it;
                    reusedHandle.generation = holeHeader->generation;
                    freeListHoles.erase(it);
                    return reusedHandle;
                }
            }
        }

        // 2. CHECK ACTIVE BLOCK CAPACITY
        size_t activeBlockIdx = blocks.size() - 1;
        if (blocks[activeBlockIdx]->writeOffset + totalNeeded > blockSize) {
            // CURRENT BLOCK FULL: Dynamically append a brand new block segment.
            // Absolute zero relocation of old blocks ensures no pointer breaks!
            blocks.push_back(std::make_unique<MemoryBlock>(blockSize));
            activeBlockIdx = blocks.size() - 1;
            
            if (totalNeeded > blockSize) {
                throw std::runtime_error("Scope Limit Exception: Data size exceeds max page bounds!");
            }
        }

        // 3. PUNCH DATA INLINE INTO ACTIVE PAGE
        MemoryBlock& activeBlock = *blocks[activeBlockIdx];
        size_t targetedOffset = activeBlock.writeOffset;

        AllocationHeader* header = reinterpret_cast<AllocationHeader*>(&activeBlock.buffer[targetedOffset]);
        header->magic = 0xDEADC0DE;
        header->size = static_cast<uint32_t>(paddedDataSize);
        header->generation = checkGeneration ? 1 : 0; // 0 indicates a pure high-speed CONST allocation
        header->intent = intent;
        header->isDead = false;

        activeBlock.writeOffset += totalNeeded;

        return CheckedHandle{ activeBlockIdx, targetedOffset, header->generation };
    }

    void clearAllInternal() {
        // Iterate across all blocks to clean up any registered Heap Proxies safely
        for (size_t i = 0; i < blocks.size(); ++i) {
            size_t scan = 0;
            MemoryBlock& block = *blocks[i];
            while (scan < block.writeOffset) {
                AllocationHeader* header = reinterpret_cast<AllocationHeader*>(&block.buffer[scan]);
                if (!header || header->magic != 0xDEADC0DE) break;

                if (header->intent == MemoryIntent::HeapProxy && !header->isDead) {
                    uint8_t* targetPtr = &block.buffer[scan + sizeof(AllocationHeader)];
                    void* actualHeapObject = *reinterpret_cast<void**>(targetPtr);
                    if (actualHeapObject) {
                        std::free(actualHeapObject); // Free the proxied allocation on user's behalf
                    }
                }
                scan += sizeof(AllocationHeader) + header->size;
            }
        }
        
        // Deallocate all secondary blocks, shrinking memory footprint back to one single page
        blocks.clear();
        blocks.push_back(std::make_unique<MemoryBlock>(blockSize));
        freeListHoles.clear();
    }

    void freeHandle(CheckedHandle handle) {
        AllocationHeader* header = getHeader(handle.blockIndex, handle.offsetId);
        if (header && header->generation == handle.generation && !header->isDead) {
            header->isDead = true;
            if (header->generation > 0) header->generation++; // Invalidate matching references
            
            if (header->intent == MemoryIntent::HeapProxy) {
                uint8_t* targetPtr = &blocks[handle.blockIndex]->buffer[handle.offsetId + sizeof(AllocationHeader)];
                void* actualHeapObject = *reinterpret_cast<void**>(targetPtr);
                std::free(actualHeapObject);
            }
            
            if (header->generation > 0) {
                freeListHoles.push_back(handle);
            }
        }
    }

    uint8_t* resolveHandle(CheckedHandle handle) {
        AllocationHeader* header = getHeader(handle.blockIndex, handle.offsetId);
        if (!header) throw std::runtime_error("Access Violation: Block address completely missing!");
        
        // Pure high-speed const bypass verification check completely if generation tracking is disabled (0)
        if (header->generation > 0 && (header->generation != handle.generation || header->isDead)) {
            throw std::runtime_error("Security Blocked: Handle version mismatch! This resource was changed or cleared.");
        }

        return &blocks[handle.blockIndex]->buffer[handle.offsetId + sizeof(AllocationHeader)];
    }
};

// ============================================================================
// THE CONVENIENCE INTEL LAYER: CONST VS MUT SEPARATE HOOKS
// ============================================================================
class ManagedScope : public Scope {
public:
    ManagedScope(size_t defaultBlockSize) : Scope(defaultBlockSize) {}

    // --- TRACK MUTABLE MODES (PAY-FOR-USE PAYLOADS) ---
    template <typename T>
    CheckedHandle pushMut() {
        return allocateRaw(sizeof(T), MemoryIntent::InlineStack, true); // Active version validation
    }

    template <typename T, typename... Args>
    CheckedHandle pushHeapMut(Args&&... args) {
        T* heapInstance = new T(std::forward<Args>(args)...);
        CheckedHandle h = allocateRaw(sizeof(void*), MemoryIntent::HeapProxy, true);
        std::memcpy(resolveHandle(h), &heapInstance, sizeof(void*));
        return h;
    }

    // --- PURE HIGH-SPEED CONST MODES (ZERO PERFORMANCE overhead) ---
    template <typename T>
    CheckedHandle pushConst(const T& value) {
        CheckedHandle h = allocateRaw(sizeof(T), MemoryIntent::InlineStack, false); // Skips checking setup
        std::memcpy(resolveHandle(h), &value, sizeof(T));
        return h;
    }

    template <typename T>
    T* access(CheckedHandle handle) {
        uint8_t* resolved = resolveHandle(handle);
        AllocationHeader* h = getHeader(handle.blockIndex, handle.offsetId);
        if (h->intent == MemoryIntent::HeapProxy) {
            return *reinterpret_cast<T**>(resolved);
        }
        return reinterpret_cast<T*>(resolved);
    }

    void wipe() {
        clearAllInternal();
    }
};

#endif
