#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include "sweet_fb_pending_surfaces.h"
#include "sweet_fb_copy_rows.h"

struct FakeSurface { unsigned id; unsigned references; unsigned releaseCalls; };
static FakeSurface *known[6];
static void *retain_fake(uint32_t id) {
    if (id >= 6 || known[id] == NULL) return NULL;
    ++known[id]->references;
    return known[id];
}
static void release_fake(void *p) {
    FakeSurface *surface = (FakeSurface *)p;
    assert(surface->references > 0);
    --surface->references;
    ++surface->releaseCalls;
}

static void test_pending_surface_lifetime(void)
{
    FakeSurface a{1,1,0}, b{2,1,0}, c{3,1,0}, d{4,1,0}, e{5,1,0};
    known[1] = &a; known[2] = &b; known[3] = &c; known[4] = &d; known[5] = &e;
    sweet_fb_pending_surfaces pending, incoming, replaced, taken;
    sweet_fb_pending_surfaces_init(&pending);
    sweet_fb_pending_surfaces_init(&incoming);
    sweet_fb_pending_surfaces_init(&replaced);
    sweet_fb_pending_surfaces_init(&taken);

    const uint32_t ids0[4] = {1, 2, 3, 4};
    sweet_fb_pending_surfaces_capture(&incoming, ids0, 0x5, retain_fake);
    assert(incoming.object[0] == &a && incoming.object[1] == NULL &&
        incoming.object[2] == &c && incoming.object[3] == NULL);
    assert(a.references == 2 && b.references == 1 && c.references == 2 && d.references == 1);
    sweet_fb_pending_surfaces_replace(&pending, &incoming, &replaced);
    assert(incoming.object[0] == NULL && replaced.object[0] == NULL);
    release_fake(&a); release_fake(&c); // caller drops its references
    assert(a.references == 1 && c.references == 1); // queue keeps both alive

    const uint32_t ids1[4] = {0, 2, 0, 0};
    sweet_fb_pending_surfaces_capture(&incoming, ids1, 0x2, retain_fake);
    release_fake(&b);
    sweet_fb_pending_surfaces_replace(&pending, &incoming, &replaced);
    assert(replaced.object[0] == &a && replaced.object[2] == &c);
    sweet_fb_pending_surfaces_release(&replaced, release_fake);
    assert(a.references == 0 && c.references == 0 && b.references == 1);

    const uint32_t ids2[4] = {0, 0, 0, 4};
    sweet_fb_pending_surfaces_capture(&incoming, ids2, 0x8, retain_fake);
    release_fake(&d);
    sweet_fb_pending_surfaces_replace(&pending, &incoming, &replaced);
    sweet_fb_pending_surfaces_release(&replaced, release_fake);
    assert(b.references == 0 && d.references == 1); // caller release; pending holds latest

    sweet_fb_pending_surfaces_take(&pending, &taken);
    assert(pending.object[3] == NULL && taken.object[3] == &d);
    assert(d.references == 1); // detached presentation owns it
    /* Simulate row copy and display register update, then drop the refs. */
    sweet_fb_pending_surfaces_release(&taken, release_fake);
    assert(d.references == 0);
    const uint32_t ids3[4] = {5, 0, 0, 0};
    sweet_fb_pending_surfaces_capture(&incoming, ids3, 1, retain_fake);
    release_fake(&e);
    sweet_fb_pending_surfaces_replace(&pending, &incoming, &replaced);
    sweet_fb_pending_surfaces_release(&pending, release_fake); // service stop cleanup
    assert(e.references == 0);
}

static void test_padded_stride_offset_and_guards(void)
{
    uint8_t source[64];
    uint8_t destination[80];
    memset(source, 0xee, sizeof(source));
    memset(destination, 0xa5, sizeof(destination));
    /* Source starts at byte 7; its 3-pixel rows are padded to 16 bytes. */
    for (unsigned y = 0; y < 3; ++y)
        for (unsigned x = 0; x < 3 * 4; ++x)
            source[7 + y * 16 + x] = (uint8_t)(y * 32 + x);
    assert(sweet_fb_copy_bgra_rows(destination + 4, 64, 20, source, sizeof(source),
        7, 16, 1, 1, 2, 1, 2, 2));
    for (unsigned y = 0; y < 2; ++y)
        assert(memcmp(destination + 4 + (1 + y) * 20 + 2 * 4,
            source + 7 + (1 + y) * 16 + 1 * 4, 8) == 0);
    assert(destination[0] == 0xa5 && destination[3] == 0xa5);
    assert(destination[4 + 64] == 0xa5 && destination[79] == 0xa5);
    for (unsigned i = 0; i < sizeof(destination); ++i) {
        if ((i >= 4 + 20 + 8 && i < 4 + 20 + 16) ||
            (i >= 4 + 40 + 8 && i < 4 + 40 + 16)) continue;
        assert(destination[i] == 0xa5);
    }
}

static void test_format_gate(void)
{
    assert(sweet_fb_copy_format_supported(0x42475241, 4, 1, 1));
    assert(!sweet_fb_copy_format_supported(0x41524742, 4, 1, 1));
    assert(!sweet_fb_copy_format_supported(0x42475241, 3, 1, 1));
    assert(!sweet_fb_copy_format_supported(0x42475241, 4, 2, 1));
    assert(!sweet_fb_copy_format_supported(0x42475241, 4, 1, 2));
}

static void test_rejects_out_of_range_without_writes(void)
{
    uint8_t source[32], destination[32];
    memset(source, 0x44, sizeof(source));
    memset(destination, 0xa5, sizeof(destination));
    assert(!sweet_fb_copy_bgra_rows(destination, sizeof(destination), 16,
        source, sizeof(source), 0, 16, 0, 1, 0, 1, 2, 2));
    assert(!sweet_fb_copy_bgra_rows(destination, sizeof(destination), 16,
        source, sizeof(source), UINT64_MAX - 3, 16, 0, 0, 0, 0, 1, 1));
    /* Bounds in the backing allocation are insufficient: each row must fit. */
    assert(!sweet_fb_copy_bgra_rows(destination, sizeof(destination), 16,
        source, sizeof(source), 0, 16, 3, 0, 0, 0, 2, 1));
    assert(!sweet_fb_copy_bgra_rows(destination, sizeof(destination), 16,
        source, sizeof(source), 0, 16, 0, 0, 3, 0, 2, 1));
    assert(!sweet_fb_copy_bgra_rows(destination, sizeof(destination), 16,
        source, sizeof(source), 0, 16, UINT32_MAX, 0, 0, 0, 1, 1));
    assert(!sweet_fb_copy_bgra_rows(destination, sizeof(destination), 16,
        source, sizeof(source), 0, 16, 0, 0, UINT32_MAX, 0, 1, 1));
    for (uint8_t b : destination) assert(b == 0xa5);

    /* A span ending exactly at each row boundary remains valid. */
    assert(sweet_fb_copy_bgra_rows(destination, sizeof(destination), 16,
        source, sizeof(source), 0, 16, 2, 0, 2, 0, 2, 2));
}

static void test_concurrent_vsync_swap_stop_ordering(void)
{
    FakeSurface a{1,1,0}, b{2,1,0}, c{3,1,0};
    known[1] = &a; known[2] = &b; known[3] = &c;
    sweet_fb_swap_lifecycle state;
    sweet_fb_swap_lifecycle_init(&state);
    sweet_fb_pending_surfaces incoming, replaced, detached, afterStop;
    sweet_fb_pending_surfaces_init(&incoming);
    sweet_fb_pending_surfaces_init(&replaced);
    sweet_fb_pending_surfaces_init(&detached);
    sweet_fb_pending_surfaces_init(&afterStop);
    const uint32_t idsA[4] = {1,0,0,0};
    sweet_fb_pending_surfaces_capture(&incoming, idsA, 1, retain_fake);
    std::mutex loopMutex; // mirrors IOWorkLoop::runAction serialization
    std::mutex queueMutex;
    { std::lock_guard<std::mutex> lock(queueMutex); assert(sweet_fb_swap_lifecycle_queue(&state, &incoming, &replaced)); }
    release_fake(&a); // caller drops the frame reference immediately after enqueue
    std::condition_variable cv;
    bool detachedReady = false, allowPresentFinish = false;
    std::atomic<unsigned> rearmCount{0};
    std::atomic<bool> stopReturned{false};
    std::thread timer([&] {
        std::lock_guard<std::mutex> loop(loopMutex);
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            assert(sweet_fb_swap_lifecycle_take(&state, &detached));
        }
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            detachedReady = true; cv.notify_all();
            cv.wait(lock, [&] { return allowPresentFinish; });
        }
        /* Simulated panel copy and display update are complete here. */
        sweet_fb_pending_surfaces_release(&detached, release_fake);
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            if (sweet_fb_swap_lifecycle_can_rearm(&state)) ++rearmCount;
        }
    });
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        cv.wait(lock, [&] { return detachedReady; });
    }
    const uint32_t idsB[4] = {0,2,0,0};
    sweet_fb_pending_surfaces_capture(&incoming, idsB, 2, retain_fake);
    release_fake(&b); // caller may drop it while queued
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        assert(sweet_fb_swap_lifecycle_queue(&state, &incoming, &replaced));
    }
    assert(a.references == 1 && b.references == 1);

    std::thread stopper([&] {
        std::lock_guard<std::mutex> loop(loopMutex); // runAction waits for timer
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            sweet_fb_swap_lifecycle_stop(&state, &afterStop);
        }
        sweet_fb_pending_surfaces_release(&afterStop, release_fake);
        stopReturned = true;
    });
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        allowPresentFinish = true; cv.notify_all();
    }
    timer.join(); stopper.join();
    assert(stopReturned && rearmCount == 1);
    assert(a.references == 0 && b.references == 0);
    const uint32_t idsC[4] = {0,0,3,0};
    sweet_fb_pending_surfaces_capture(&incoming, idsC, 4, retain_fake);
    release_fake(&c);
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        assert(!sweet_fb_swap_lifecycle_queue(&state, &incoming, &replaced));
        assert(!sweet_fb_swap_lifecycle_can_rearm(&state));
    }
    sweet_fb_pending_surfaces_release(&replaced, release_fake);
    assert(c.references == 0);

    /* A callback queued after stop observes stopped and cannot detach or rearm. */
    {
        std::lock_guard<std::mutex> loop(loopMutex);
        std::lock_guard<std::mutex> lock(queueMutex);
        assert(!sweet_fb_swap_lifecycle_take(&state, &detached));
        assert(!sweet_fb_swap_lifecycle_can_rearm(&state));
    }
}

static void test_gpu_completion_requests_one_throttled_recopy(void)
{
    assert(!sweet_fb_should_flush(false, false, 1, 0, 6, 6));
    assert(!sweet_fb_should_flush(false, true, 0, 0, 6, 6));
    assert(!sweet_fb_should_flush(false, true, 1, 0, 5, 6));
    assert(sweet_fb_should_flush(false, true, 1, 0, 6, 6));
    assert(!sweet_fb_should_flush(false, true, 1, 1, 12, 6));
    assert(sweet_fb_should_flush(true, true, 1, 1, 12, 6));
    assert(!sweet_fb_should_flush(true, true, 1, 1, 12, 0));
}

int main()
{
    test_pending_surface_lifetime();
    test_padded_stride_offset_and_guards();
    test_format_gate();
    test_rejects_out_of_range_without_writes();
    test_concurrent_vsync_swap_stop_ordering();
    test_gpu_completion_requests_one_throttled_recopy();
    puts("presentation host tests: PASS");
    return 0;
}
