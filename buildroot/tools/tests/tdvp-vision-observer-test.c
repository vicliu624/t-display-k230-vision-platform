#include "tdvp_vision_observer.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    struct tdvp_vision_observer observer = {0};
    struct tdvp_vision_producer sample = {
        .magic = TDVP_VISION_MAGIC, .version = TDVP_VISION_ABI_VERSION,
        .control_bytes = sizeof(struct tdvp_vision_control), .epoch = 123,
        .heartbeat = 1, .state = TDVP_VISION_STATE_IDLE,
        .slot_count = TDVP_VISION_SLOT_COUNT, .slot_bytes = TDVP_VISION_SLOT_BYTES,
    };
    assert(tdvp_vision_observer_state(&observer, 0) == TDVP_OBSERVER_PENDING);
    tdvp_vision_observe(&observer, &sample, 124, 10); // previous boot cannot look ready
    assert(tdvp_vision_observer_state(&observer, 10) == TDVP_OBSERVER_PENDING);
    tdvp_vision_observe(&observer, &sample, 123, 10);
    assert(tdvp_vision_observer_state(&observer, 10) == TDVP_OBSERVER_PENDING);
    assert(tdvp_vision_observer_state(&observer, 10010) == TDVP_OBSERVER_STALE);
    ++sample.heartbeat;
    tdvp_vision_observe(&observer, &sample, 123, 10020);
    assert(tdvp_vision_observer_state(&observer, 10020) == TDVP_VISION_STATE_IDLE);
    assert(tdvp_vision_observer_state(&observer, 20019) == TDVP_VISION_STATE_IDLE);
    assert(tdvp_vision_observer_state(&observer, 20020) == TDVP_OBSERVER_STALE);
    for (unsigned int state = TDVP_VISION_STATE_IDLE; state <= TDVP_VISION_STATE_FAULT; ++state) {
        sample.state = state;
        sample.fault = state == TDVP_VISION_STATE_FAULT ? -5 : 0;
        ++sample.heartbeat;
        tdvp_vision_observe(&observer, &sample, 123, 20030 + state);
        assert(tdvp_vision_observer_state(&observer, 20030 + state) == state);
    }
    --sample.heartbeat;
    tdvp_vision_observe(&observer, &sample, 123, 20040);
    assert(tdvp_vision_observer_state(&observer, 20040) == TDVP_OBSERVER_PROTOCOL);
    ++sample.heartbeat;
    tdvp_vision_observe(&observer, &sample, 123, 20050);
    assert(tdvp_vision_observer_state(&observer, 20050) == TDVP_OBSERVER_PROTOCOL);
    tdvp_vision_observe(&observer, NULL, 123, 20060);
    assert(tdvp_vision_observer_state(&observer, 20060) == TDVP_OBSERVER_PENDING);
    sample.state = TDVP_VISION_STATE_IDLE;
    sample.fault = 0;
    tdvp_vision_observe(&observer, &sample, 123, 20070);
    ++sample.heartbeat;
    tdvp_vision_observe(&observer, &sample, 123, 20080);
    assert(tdvp_vision_observer_state(&observer, 20079) == TDVP_OBSERVER_PROTOCOL);
    sample.magic = 0;
    tdvp_vision_observe(&observer, &sample, 123, 20090);
    assert(tdvp_vision_observer_state(&observer, 20090) == TDVP_OBSERVER_PROTOCOL);
    sample.magic = TDVP_VISION_MAGIC;
    sample.epoch = 456;
    sample.heartbeat = 1;
    tdvp_vision_observe(&observer, &sample, 456, 20100);
    ++sample.heartbeat;
    sample.captured = 100;
    sample.published = 3;
    sample.dropped = 97;
    const struct tdvp_vision_producer original = sample;
    tdvp_vision_observe(&observer, &sample, 456, 20110);
    assert(!memcmp(&sample, &original, sizeof(sample))); // no producer writes
    assert(observer.sample.captured == 100 && observer.sample.published == 3);
    assert(tdvp_vision_observer_state(&observer, 20110) == TDVP_VISION_STATE_IDLE);
    assert(!strcmp(tdvp_vision_observer_name(TDVP_OBSERVER_PROTOCOL), "protocol-error"));
    puts("CPU1 telemetry observer: PASS epoch, advancing/frozen heartbeat, startup, fault, rollback and read-only samples");
}
