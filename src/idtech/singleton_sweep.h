// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gc_ht::idtech {

// One pass over the process's committed, writable memory, offering every
// 4-byte-aligned position to `accept` and answering with the address of the
// first it takes.
//
// Both engine objects this mod needs are ECS components: plain structs with no
// vtable, heap allocated, with nothing in the image pointing at them. So there
// is no pointer to follow and no vtable to key on - the only handle on them
// from inside the process is what their own fields look like, and `accept` is
// that test.
//
// A template rather than a std::function, and given a pointer into a slice
// rather than an address, for two reasons that are both about the scale of the
// walk. The game commits several gigabytes, so the predicate runs a few hundred
// million times and has to inline; and reading through a raw pointer that
// VirtualQuery called committed still races a region being freed underneath it,
// so each slice is copied out with ReadProcessMemory first and the predicate
// then works on a buffer that cannot go away. Whatever prefix that call managed
// is scanned even when it reports a partial copy - those bytes are valid and
// the object may be among them.
//
// `accept` is called as `accept(const unsigned char* p, std::uintptr_t address)`
// and may read `window` bytes from `p`. Positions within `window` of the end of
// a slice are not offered, so a predicate never has to guard its own tail - and
// slices overlap by `window` so an object straddling a slice boundary is still
// seen.
//
// The real address is passed alongside the buffer pointer because the predicate
// cannot work it out: it is looking at a COPY, so the pointer it holds says
// nothing about where in the process the bytes came from. A predicate that
// wants to skip everything below some address - one enumerating every match
// rather than taking the first - has no other way to know.
//
// `cutShort` is set when VIRTUALQUERY refused mid-walk, and only then, so the
// sweep covered a prefix of the address space rather than all of it. That has to
// be distinguishable from an honest miss: both answer nullptr, but "the sweep
// ran and the object is not there" and "the sweep stopped a third of the way
// through" need different fixes, and a caller that cannot tell them apart
// reports the first forever.
template <typename Accept>
void* SweepCommittedMemory(Accept accept, std::size_t window, bool* cutShort = nullptr) {
    // 4 MiB at a time. Large enough that the per-slice overhead disappears,
    // small enough that the transient commit is not itself a problem.
    constexpr std::size_t kSlice = 4u << 20;

    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    auto address = reinterpret_cast<std::uintptr_t>(info.lpMinimumApplicationAddress);
    const auto limit = reinterpret_cast<std::uintptr_t>(info.lpMaximumApplicationAddress);

    std::vector<unsigned char> slice(kSlice);

    // The staging buffer is itself committed, writable, and inside the range
    // being walked, so the sweep reaches it. Two things go wrong if it is not
    // excluded. One slice read has its source overlapping its own destination,
    // which ReadProcessMemory does not define. Worse, the buffer holds a COPY of
    // whatever region is being scanned, so the copy of the real block matches the
    // predicate byte for byte - and the address handed back then points into a
    // std::vector that is freed when this function returns. SafeRead goes on
    // succeeding on that freed heap page, so nothing ever asks for another sweep
    // and the gate reads its flags out of recycled allocator memory for the rest
    // of the session.
    const auto selfLow = reinterpret_cast<std::uintptr_t>(slice.data());
    const auto selfHigh = selfLow + kSlice;

    while (address < limit) {
        MEMORY_BASIC_INFORMATION region{};
        if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &region, sizeof(region)) == 0) {
            if (cutShort != nullptr) *cutShort = true;
            return nullptr;
        }
        const auto base = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
        const std::uintptr_t next = base + region.RegionSize;
        if (base < selfHigh && next > selfLow) {
            if (next <= address) break;
            address = next;
            continue;
        }

        // Masked rather than compared whole, so a readable page is still
        // recognised when the allocator has combined the access with
        // PAGE_NOCACHE or PAGE_WRITECOMBINE. PAGE_GUARD survives the mask on
        // purpose: touching a guard page raises.
        //
        // Writable only. Both targets are live game state the engine rewrites
        // every frame, so read-only pages and every module's .text would be
        // pulled through the predicate for nothing.
        const DWORD access = region.Protect & ~static_cast<DWORD>(PAGE_NOCACHE | PAGE_WRITECOMBINE);
        const bool readable = region.State == MEM_COMMIT &&
                              (access == PAGE_READWRITE || access == PAGE_EXECUTE_READWRITE);
        if (readable && region.RegionSize > window) {
            const std::size_t step = kSlice - window;
            for (std::size_t done = 0; done + window < region.RegionSize; done += step) {
                std::size_t want = region.RegionSize - done;
                if (want > kSlice) want = kSlice;
                // Zeroed here, and that initialiser is what makes the loop below
                // safe: the kernel writes the copied count on success and on a
                // partial copy, and does not touch it if it never got that far,
                // so an unwritten `got` has to already read zero.
                SIZE_T got = 0;
                ReadProcessMemory(GetCurrentProcess(),
                                  reinterpret_cast<LPCVOID>(base + done), slice.data(), want, &got);
                if (got <= window) continue;
                const std::size_t end = static_cast<std::size_t>(got) - window;
                for (std::size_t i = 0; i < end; i += 4) {
                    const std::uintptr_t candidate = base + done + i;
                    if (accept(slice.data() + i, candidate)) {
                        return reinterpret_cast<void*>(candidate);
                    }
                }
            }
        }
        if (next <= address) break;
        address = next;
    }
    return nullptr;
}

}  // namespace gc_ht::idtech
