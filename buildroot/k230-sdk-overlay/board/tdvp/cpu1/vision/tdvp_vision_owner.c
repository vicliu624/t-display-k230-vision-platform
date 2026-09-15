/* SPDX-License-Identifier: MIT */
/* Pure policy: adapters own MMIO ordering, time and resource preparation. */
#include "tdvp_vision_owner.h"

void tdvp_owner_fail(struct tdvp_owner_session *session, tdvp_v_u32 fault)
{
    if (session->own.state != TDVP_OWNER_FAULT) {
        session->own.fault = fault;
        session->own.state = TDVP_OWNER_FAULT;
    }
}

static void owner_init(struct tdvp_owner_session *session, tdvp_v_u64 cookie,
                       tdvp_v_u64 now, unsigned int state, unsigned int publish)
{
    *session = (struct tdvp_owner_session){0};
    session->own = (struct tdvp_owner_record){
        .magic = TDVP_OWNER_MAGIC, .version = TDVP_OWNER_VERSION,
        .bytes = sizeof(struct tdvp_owner_record), .contract = TDVP_OWNER_CONTRACT,
        .cookie = cookie, .heartbeat = 1, .state = state
    };
    session->started = session->phase_started = session->last_now = now;
    session->publish = publish;
    if (!cookie) tdvp_owner_fail(session, TDVP_OWNER_ERR_PROTOCOL);
}

void tdvp_owner_cpu1_init(struct tdvp_owner_session *session, tdvp_v_u64 cookie, tdvp_v_u64 now)
{
    owner_init(session, cookie, now, TDVP_OWNER_WAIT, 0);
}

void tdvp_owner_linux_init(struct tdvp_owner_session *session, tdvp_v_u64 cookie, tdvp_v_u64 now)
{
    owner_init(session, cookie, now, TDVP_OWNER_OFFER, 1);
}

static int owner_tick(struct tdvp_owner_session *session, tdvp_v_u64 now)
{
    if (now < session->last_now) tdvp_owner_fail(session, TDVP_OWNER_ERR_CLOCK);
    session->last_now = now;
    if (session->own.heartbeat == ~(tdvp_v_u64)0)
        tdvp_owner_fail(session, TDVP_OWNER_ERR_OVERFLOW);
    else
        ++session->own.heartbeat;
    return session->own.state == TDVP_OWNER_FAULT ? -1 : 0;
}

static int owner_valid(const struct tdvp_owner_record *peer)
{
    return peer && peer->magic == TDVP_OWNER_MAGIC && peer->version == TDVP_OWNER_VERSION &&
           peer->bytes == sizeof(*peer) && peer->contract == TDVP_OWNER_CONTRACT &&
           peer->cookie && peer->heartbeat && !(peer->sequence & 1U) && !peer->fault;
}

/* Two observations of the SAME cookie with strictly advancing heartbeat.
 * A static RAM image, including a complete old successful handshake, is not
 * permission to touch a device. Never replace a cookie after connecting.
 */
static int owner_observe(struct tdvp_owner_session *session, const struct tdvp_owner_record *peer,
                         tdvp_v_u64 now)
{
    if (session->observed_cookie != peer->cookie) {
        session->observed_cookie = peer->cookie;
        session->observed_heartbeat = peer->heartbeat;
        return 0;
    }
    if (peer->heartbeat <= session->observed_heartbeat) return 0;
    session->observed_heartbeat = peer->heartbeat;
    session->peer_seen = now;
    return 1;
}

static int owner_live(struct tdvp_owner_session *session, const struct tdvp_owner_record *peer,
                      tdvp_v_u64 now, tdvp_v_u64 timeout)
{
    if (peer) {
        if (!owner_valid(peer) || peer->cookie != session->observed_cookie ||
            peer->heartbeat < session->observed_heartbeat) {
            tdvp_owner_fail(session, TDVP_OWNER_ERR_PROTOCOL);
            return 0;
        }
        if (peer->heartbeat > session->observed_heartbeat) {
            session->observed_heartbeat = peer->heartbeat;
            session->peer_seen = now;
        }
    }
    if (now - session->peer_seen >= timeout) {
        tdvp_owner_fail(session, TDVP_OWNER_ERR_TIMEOUT);
        return 0;
    }
    return 1;
}

int tdvp_owner_cpu1_step(struct tdvp_owner_session *session, const struct tdvp_owner_record *peer,
                         tdvp_v_u64 now)
{
    if (owner_tick(session, now)) return -1;
    if (session->own.state == TDVP_OWNER_WAIT) {
        if (now - session->started >= TDVP_OWNER_BOOT_MS) {
            tdvp_owner_fail(session, TDVP_OWNER_ERR_TIMEOUT);
            return -1;
        }
        if (owner_valid(peer) && peer->state == TDVP_OWNER_OFFER && !peer->peer_cookie &&
            owner_observe(session, peer, now)) {
            session->own.peer_cookie = peer->cookie;
            session->own.state = TDVP_OWNER_HELLO;
            session->phase_started = now;
            session->publish = 1;
        }
        return 0;
    }
    if (!owner_live(session, peer, now, TDVP_OWNER_PEER_MS)) return -1;
    if (session->own.state != TDVP_OWNER_READY &&
        now - session->phase_started >= TDVP_OWNER_BOOT_MS) {
        tdvp_owner_fail(session, TDVP_OWNER_ERR_TIMEOUT);
        return -1;
    }
    if (!peer) return 0;
    if (session->own.state == TDVP_OWNER_HELLO && peer->state == TDVP_OWNER_OFFER && !peer->peer_cookie)
        return 0;
    if (peer->state != TDVP_OWNER_GRANT || peer->peer_cookie != session->own.cookie) {
        tdvp_owner_fail(session, TDVP_OWNER_ERR_PROTOCOL);
        return -1;
    }
    if (session->own.state == TDVP_OWNER_HELLO) {
        session->own.state = TDVP_OWNER_STARTING;
        session->phase_started = now;
        return 1;
    }
    return 0;
}

int tdvp_owner_cpu1_ready(struct tdvp_owner_session *session)
{
    if (session->own.state != TDVP_OWNER_STARTING) return -1;
    session->own.state = TDVP_OWNER_READY;
    return 0;
}

int tdvp_owner_linux_step(struct tdvp_owner_session *session, const struct tdvp_owner_record *peer,
                          tdvp_v_u64 now)
{
    if (owner_tick(session, now)) return -1;
    if (session->own.state == TDVP_OWNER_OFFER) {
        if (now - session->started >= TDVP_OWNER_BOOT_MS) {
            tdvp_owner_fail(session, TDVP_OWNER_ERR_TIMEOUT);
            return -1;
        }
        if (owner_valid(peer) && peer->state == TDVP_OWNER_HELLO &&
            peer->peer_cookie == session->own.cookie && owner_observe(session, peer, now)) {
            session->own.peer_cookie = peer->cookie;
            session->own.state = TDVP_OWNER_GRANT;
            session->phase_started = now;
        }
        return 0;
    }
    if (!owner_live(session, peer, now, session->peer_ready ? TDVP_OWNER_PEER_MS : TDVP_OWNER_BOOT_MS))
        return -1;
    if (!session->peer_ready && now - session->phase_started >= TDVP_OWNER_BOOT_MS) {
        tdvp_owner_fail(session, TDVP_OWNER_ERR_TIMEOUT);
        return -1;
    }
    if (!peer) return 0;
    if (peer->peer_cookie != session->own.cookie ||
        (peer->state != TDVP_OWNER_HELLO && peer->state != TDVP_OWNER_STARTING &&
         peer->state != TDVP_OWNER_READY) ||
        (session->peer_ready && peer->state != TDVP_OWNER_READY)) {
        tdvp_owner_fail(session, TDVP_OWNER_ERR_PROTOCOL);
        return -1;
    }
    if (peer->state == TDVP_OWNER_READY) session->peer_ready = 1;
    return 0;
}
