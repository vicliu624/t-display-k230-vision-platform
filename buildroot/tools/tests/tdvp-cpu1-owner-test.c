/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "tdvp_vision_owner_io.h"

static struct tdvp_owner_session linux_owner, cpu1;
static struct tdvp_owner_control wire;
static unsigned int starts;
static unsigned long long now;

static void tick(void)
{
    struct tdvp_owner_record peer;
    int stable, result;
    now += 50;
    stable = tdvp_owner_snapshot(&wire.cpu1_side, &peer);
    (void)tdvp_owner_linux_step(&linux_owner, stable ? &peer : NULL, now);
    tdvp_owner_publish(&wire.linux_side, &linux_owner.own);
    stable = tdvp_owner_snapshot(&wire.linux_side, &peer);
    result = tdvp_owner_cpu1_step(&cpu1, stable ? &peer : NULL, now);
    if (result == 1) ++starts;
    if (cpu1.publish) tdvp_owner_publish(&wire.cpu1_side, &cpu1.own);
}

static void setup(void)
{
    now = 1000; starts = 0;
    memset(&wire, 0xa5, sizeof(wire));
    tdvp_owner_linux_init(&linux_owner, 123, now);
    tdvp_owner_cpu1_init(&cpu1, 456, now);
    tdvp_owner_publish(&wire.linux_side, &linux_owner.own);
}

static void connect(void)
{
    setup();
    for (unsigned int i = 0; i < 8; ++i) tick();
    assert(starts == 1 && cpu1.own.state == TDVP_OWNER_STARTING);
    assert(linux_owner.own.state == TDVP_OWNER_GRANT);
    assert(cpu1.own.peer_cookie == 123 && linux_owner.own.peer_cookie == 456);
    assert(tdvp_owner_cpu1_ready(&cpu1) == 0);
    assert(tdvp_owner_cpu1_ready(&cpu1) == -1);
    tick(); tick();
    assert(linux_owner.peer_ready);
}

int main(void)
{
    struct tdvp_owner_record peer, before;
    connect();
    for (unsigned int i = 0; i < 10000; ++i) tick();
    assert(starts == 1 && cpu1.own.state == TDVP_OWNER_READY);

    /* No offer and stationary old OFFER/GRANT must never publish CPU1 data. */
    for (unsigned int mode = 0; mode < 3; ++mode) {
        setup();
        if (mode == 0) memset(&wire.linux_side, 0, sizeof(peer));
        if (mode == 2) wire.linux_side.state = TDVP_OWNER_GRANT;
        before = wire.cpu1_side;
        peer = wire.linux_side;
        for (now = 1050; now <= 61000; now += 50)
            (void)tdvp_owner_cpu1_step(&cpu1, &peer, now);
        assert(!cpu1.publish && cpu1.own.state == TDVP_OWNER_FAULT);
        assert(!memcmp(&before, &wire.cpu1_side, sizeof(before)));
    }
    /* A stale HELLO cannot get a grant from a new Linux boot. */
    setup();
    peer = cpu1.own; peer.state = TDVP_OWNER_HELLO; peer.peer_cookie = 122;
    for (now = 1050; now <= 61000; now += 50) {
        ++peer.heartbeat;
        (void)tdvp_owner_linux_step(&linux_owner, &peer, now);
    }
    assert(linux_owner.own.state == TDVP_OWNER_FAULT && !linux_owner.own.peer_cookie);

    /* Mutating any connected identity/contract field latches a fault. */
    for (unsigned int bad = 0; bad < 10; ++bad) {
        connect(); peer = linux_owner.own;
        switch (bad) {
        case 0: ++peer.magic; break;
        case 1: ++peer.version; break;
        case 2: ++peer.bytes; break;
        case 3: ++peer.contract; break;
        case 4: ++peer.cookie; break;
        case 5: ++peer.peer_cookie; break;
        case 6: peer.state = TDVP_OWNER_OFFER; break;
        case 7: peer.fault = 1; break;
        case 8: peer.heartbeat = 1; break;
        case 9: peer.state = TDVP_OWNER_FAULT; break;
        }
        assert(tdvp_owner_cpu1_step(&cpu1, &peer, now + 50) == -1);
        peer = linux_owner.own; ++peer.heartbeat;
        assert(tdvp_owner_cpu1_step(&cpu1, &peer, now + 100) == -1);
        assert(tdvp_owner_cpu1_ready(&cpu1) == -1);
    }
    connect();
    assert(tdvp_owner_cpu1_step(&cpu1, NULL, now + TDVP_OWNER_PEER_MS) == -1);
    connect();
    assert(tdvp_owner_linux_step(&linux_owner, NULL, now + TDVP_OWNER_PEER_MS) == -1);
    connect(); peer = cpu1.own; peer.state = TDVP_OWNER_STARTING;
    assert(tdvp_owner_linux_step(&linux_owner, &peer, now + 50) == -1);
    connect();
    assert(tdvp_owner_cpu1_step(&cpu1, NULL, now - 1) == -1);
    connect(); cpu1.own.heartbeat = ~(tdvp_v_u64)0;
    assert(tdvp_owner_cpu1_step(&cpu1, NULL, now + 50) == -1);
    connect(); peer = cpu1.own; ++peer.cookie;
    assert(tdvp_owner_linux_step(&linux_owner, &peer, now + 50) == -1);
    setup();
    for (unsigned int i = 0; i < 1300; ++i) tick();
    assert(cpu1.own.state == TDVP_OWNER_FAULT && linux_owner.own.state == TDVP_OWNER_FAULT);

    /* In-progress writes are not snapshots; wrap preserves even publication. */
    setup(); wire.linux_side.sequence = 7;
    assert(!tdvp_owner_snapshot(&wire.linux_side, &peer));
    wire.linux_side.sequence = 0xfffffffeU;
    tdvp_owner_publish(&wire.linux_side, &linux_owner.own);
    assert(tdvp_owner_snapshot(&wire.linux_side, &peer) && peer.sequence == 0);
    assert(peer.cookie == linux_owner.own.cookie);
    puts("CPU1 ownership policy: PASS live handshake, stale RAM, identities, timeouts, no regrant, publication and overflow");
    return 0;
}
