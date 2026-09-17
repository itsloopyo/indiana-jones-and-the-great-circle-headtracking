// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <thread>
#include <utility>

namespace gc_ht::idtech {

// The worker behind both engine-singleton resolvers. It runs `sweep` on its own
// thread whenever nothing is published, and publishes whatever that returns.
//
// A sweep reads a gigabyte and more of committed memory, which is nothing once
// per map load and a visible hitch if it happens on the render thread. So the
// render thread only ever reads the published pointer, and clearing that
// pointer is what asks for another sweep.
//
// Held here rather than written out per resolver: the two differ only in what
// they sweep for and what makes a candidate live, and a second copy of the stop
// flag, the publish and the teardown is a second place for them to be got
// subtly wrong.
//
// The owner of one of these must be immortal. HeadTrackingMod is deliberately
// leaked and GameState is never reset, which is what makes that true today.
//
// DESTROYING ONE CALLS std::terminate. There is no destructor here, so the
// implicit one destroys a joinable std::thread, and that is what std::thread's
// destructor does. That is the intended enforcement rather than an oversight:
// the alternative that was here before - detach, then destroy - let the worker
// go on reading destroyed atomics and calling through a destroyed std::function
// for the rest of a sweep, which takes seconds and fails silently. A loud abort
// beats that. Joining instead would block teardown for those same seconds.
//
// So anyone adding a teardown path has to add a wakeup and a join here FIRST.
// The worker loops forever by design and checks nothing that would let it stop.
class ResolverThread {
public:
    void Start(std::function<void*()> sweep) {
        m_sweep = std::move(sweep);
        m_thread = std::thread(&ResolverThread::Worker, this);
    }

    void* Published() const { return m_object.load(); }
    void Clear() { m_object.store(nullptr); }

private:
    // How long the worker waits between sweeps while it has nothing to publish.
    static constexpr std::chrono::seconds kSweepInterval{3};

    void Worker() {
        for (;;) {
            if (m_object.load() == nullptr) {
                if (void* found = m_sweep()) m_object.store(found);
            }
            std::this_thread::sleep_for(kSweepInterval);
        }
    }

    std::function<void*()> m_sweep;
    std::atomic<void*> m_object{nullptr};
    std::thread m_thread;
};

}  // namespace gc_ht::idtech
