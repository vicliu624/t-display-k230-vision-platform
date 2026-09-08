// SPDX-License-Identifier: GPL-2.0
/* CPU0 copies only. Part of the boot-pinned vision module, never a second
 * accelerator owner. One outstanding request/result, no mmap or pointer ioctl.
 * AI work/client copies have a separate mutex/work item from owner heartbeat.
 */
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/ktime.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>
#include "tdvp_cpu1_ai_linux.h"
#include "tdvp_ai_abi.h"
#include "tdvp_cpu1_vision_layout.h"
#include "tdvp_vision_abi.h"
#include "tdvp_vision_owner.h"

#define AI_TICK msecs_to_jiffies(20)
#define AI_WATCHDOG_MS 5000U

struct tdvp_ai_linux {
    struct miscdevice misc;
    struct mutex lock;
    struct delayed_work work;
    wait_queue_head_t wait;
    struct tdvp_ai_control __iomem *control;
    void __iomem *input_map, *output_map;
    void *input, *record;
    struct tdvp_ai_request request;
    u64 owner_cookie, peer_cookie, client_cookie, clients;
    u64 submitted, accepted, completed, heartbeat, peer_heartbeat, peer_seen, started;
    int owner_status, fault;
    bool initialized, peer_ready, opened, pending, detached, ready, writable, dead;
};

static int ai_fault(struct tdvp_ai_linux *ai, int error)
{
    if (!ai->fault) WRITE_ONCE(ai->fault, error < 0 ? error : -EIO);
    WRITE_ONCE(ai->ready, false);
    WRITE_ONCE(ai->writable, false);
    return ai->fault; /* No acknowledgement or reclaim after uncertain state. */
}

static void ai_ack(struct tdvp_ai_linux *ai)
{
    /* Caller has validated the exact response and copied or discarded it.
     * This never acknowledges a running/unknown job, including on close. */
    if (!ai->pending || !ai->ready || ai->fault) return;
    mb(); writeq(ai->submitted, &ai->control->linux_side.released); wmb();
    ai->pending = ai->detached = false;
    WRITE_ONCE(ai->ready, false);
    WRITE_ONCE(ai->writable, false); /* Wait for CPU1 to consume the ack. */
}

/* AI mutex only. Must never call the ownership state machine or take its lock. */
static int ai_refresh(struct tdvp_ai_linux *ai)
{
    struct tdvp_ai_cpu1_line __iomem *peer = &ai->control->cpu1_side;
    struct tdvp_ai_response *response = ai->record;
    u64 now = ktime_to_ms(ktime_get()), heartbeat, accepted, completed;
    u32 state;
    int owner_status = smp_load_acquire(&ai->owner_status);
    if (ai->fault) return ai->fault;
    if (owner_status) {
        if (!ai->initialized && owner_status == -EAGAIN) return -EAGAIN;
        return ai_fault(ai, owner_status);
    }
    if (!ai->initialized) {
        struct tdvp_ai_linux_line line = {0};
        if (!ai->owner_cookie || !ai->peer_cookie) return ai_fault(ai, -EPROTO);
        line.version = TDVP_AI_VERSION; line.bytes = sizeof(struct tdvp_ai_control);
        line.owner_cookie = ai->owner_cookie; line.peer_cookie = ai->peer_cookie;
        line.heartbeat = ++ai->heartbeat;
        writel(0, &ai->control->linux_side.magic); wmb();
        memcpy_toio(&ai->control->linux_side, &line, sizeof(line)); wmb();
        writel(TDVP_AI_MAGIC, &ai->control->linux_side.magic); wmb();
        ai->peer_seen = now;
        ai->initialized = true;
    }
    if (readl(&peer->magic) != TDVP_AI_MAGIC || readq(&peer->owner_cookie) != ai->owner_cookie ||
        readq(&peer->peer_cookie) != ai->peer_cookie) {
        if (!ai->peer_ready && now >= ai->peer_seen && now - ai->peer_seen < AI_WATCHDOG_MS) return -EAGAIN;
        return ai_fault(ai, -EPIPE);
    }
    if (readl(&peer->version) != TDVP_AI_VERSION || readl(&peer->bytes) != sizeof(struct tdvp_ai_control) ||
        readl(&peer->capabilities) != TDVP_AI_CAP_AI2D) return ai_fault(ai, -EPROTO);
    ai->peer_ready = true;
    state = readl(&peer->state);
    if (state == TDVP_AI_FAULT) {
        int error = (s32)readl(&peer->fault);
        return ai_fault(ai, error >= -4095 && error < 0 ? error : -EPROTO);
    }
    if (state < TDVP_AI_STATE_IDLE || state > TDVP_AI_RESULT || readl(&peer->fault)) return ai_fault(ai, -EPROTO);
    heartbeat = readq(&peer->heartbeat);
    if (!heartbeat || heartbeat < ai->peer_heartbeat || now < ai->peer_seen) return ai_fault(ai, -EPROTO);
    if (heartbeat != ai->peer_heartbeat) { ai->peer_heartbeat = heartbeat; ai->peer_seen = now; }
    else if (now - ai->peer_seen >= AI_WATCHDOG_MS) return ai_fault(ai, -ETIMEDOUT);
    /* CPU1 publishes accepted before completed. Read the commit counter first
     * so a fast completion cannot be paired with an older accepted sample. */
    completed = readq(&peer->completed); rmb(); accepted = readq(&peer->accepted);
    if (accepted < ai->accepted || completed < ai->completed || completed > accepted || accepted > ai->submitted)
        return ai_fault(ai, -EPROTO);
    ai->accepted = accepted; ai->completed = completed;
    if (ai->pending && !ai->ready && completed == ai->submitted) {
        unsigned int i;
        u32 expected = ai->request.output_width * ai->request.output_height * 3U;
        rmb(); memcpy_fromio(response, &ai->control->response, sizeof(*response)); rmb();
        state = readl(&peer->state); /* RESULT is stable until our later ack. */
        if (response->owner_cookie != ai->owner_cookie || response->peer_cookie != ai->peer_cookie ||
            response->client_cookie != ai->request.client_cookie || response->id != ai->submitted ||
            response->operation != ai->request.operation || response->format != ai->request.format ||
            response->output_width != ai->request.output_width || response->output_height != ai->request.output_height ||
            response->result > 0 || response->result < -4095 ||
            response->output_bytes > ai->request.output_capacity ||
            response->output_bytes != (response->result ? 0U : expected) ||
            response->duration_ms > ai->request.budget_ms || state != TDVP_AI_RESULT)
            return ai_fault(ai, -EPROTO);
        for (i = 0; i < ARRAY_SIZE(response->reserved); ++i)
            if (response->reserved[i]) return ai_fault(ai, -EPROTO);
        if (!ai->detached && response->output_bytes)
            memcpy_fromio((u8 *)ai->record + sizeof(*response), ai->output_map, response->output_bytes);
        WRITE_ONCE(ai->ready, true);
        if (ai->detached) ai_ack(ai);
    }
    if (ai->pending && !ai->ready && (now < ai->started ||
        now - ai->started >= (u64)ai->request.budget_ms + AI_WATCHDOG_MS)) return ai_fault(ai, -ETIMEDOUT);
    WRITE_ONCE(ai->writable, !ai->pending && state == TDVP_AI_STATE_IDLE &&
        completed == ai->submitted && accepted == ai->submitted);
    return 0;
}

static void ai_tick(struct work_struct *work)
{
    struct tdvp_ai_linux *ai = container_of(to_delayed_work(work), struct tdvp_ai_linux, work);
    if (READ_ONCE(ai->dead)) return;
    /* A client page fault must not tie up a workqueue thread or owner heartbeat. */
    if (mutex_trylock(&ai->lock)) {
        (void)ai_refresh(ai);
        if (ai->initialized && !ai->fault) writeq(++ai->heartbeat, &ai->control->linux_side.heartbeat);
        if (ai->ready || ai->writable || ai->fault) wake_up_interruptible(&ai->wait);
        mutex_unlock(&ai->lock);
    }
    if (!READ_ONCE(ai->dead)) schedule_delayed_work(&ai->work, AI_TICK);
}

static int ai_open(struct inode *inode, struct file *file)
{
    struct tdvp_ai_linux *ai = container_of(file->private_data, struct tdvp_ai_linux, misc);
    int result;
    if ((file->f_mode & (FMODE_READ | FMODE_WRITE)) != (FMODE_READ | FMODE_WRITE)) return -EINVAL;
    mutex_lock(&ai->lock);
    if (ai->dead) result = -ENODEV;
    else if (ai->opened || ai->pending) result = -EBUSY;
    else if ((result = ai_refresh(ai))) { }
    else if (ai->clients == U64_MAX) result = ai_fault(ai, -EOVERFLOW);
    else {
        ai->client_cookie = ++ai->clients;
        ai->opened = true;
        file->private_data = ai;
        result = 0;
    }
    mutex_unlock(&ai->lock);
    return result;
}

static ssize_t ai_write(struct file *file, const char __user *buffer, size_t count, loff_t *offset)
{
    struct tdvp_ai_linux *ai = file->private_data;
    struct tdvp_ai_request request;
    int result;
    if (count < sizeof(request) || count > sizeof(request) + TDVP_AI_BUFFER_BYTES) return -EMSGSIZE;
    if (copy_from_user(&request, buffer, sizeof(request))) return -EFAULT;
    result = tdvp_ai_validate_request(&request);
    if (result) return result;
    if (request.owner_cookie || request.peer_cookie || request.client_cookie || request.id) return -EINVAL;
    if (count != sizeof(request) + request.input_bytes) return -EMSGSIZE;
    if (mutex_lock_interruptible(&ai->lock)) return -ERESTARTSYS;
    if ((result = ai_refresh(ai))) goto out;
    if (!ai->writable || ai->pending) { result = -EAGAIN; goto out; }
    if (ai->submitted == U64_MAX) { result = ai_fault(ai, -EOVERFLOW); goto out; }
    if (copy_from_user(ai->input, buffer + sizeof(request), request.input_bytes)) { result = -EFAULT; goto out; }
    /* A userfault can outlast an ownership transition; check again before any
     * publication. The owner work item never needs this mutex. */
    if ((result = ai_refresh(ai))) goto out;
    request.owner_cookie = ai->owner_cookie; request.peer_cookie = ai->peer_cookie;
    request.client_cookie = ai->client_cookie; request.id = ai->submitted + 1;
    memcpy_toio(ai->input_map, ai->input, request.input_bytes);
    memcpy_toio(&ai->control->request, &request, sizeof(request)); wmb();
    if (smp_load_acquire(&ai->owner_status)) { result = ai_fault(ai, -EPIPE); goto out; }
    ai->request = request; ai->submitted = request.id;
    ai->started = ktime_to_ms(ktime_get());
    ai->pending = true; ai->detached = false;
    WRITE_ONCE(ai->ready, false); WRITE_ONCE(ai->writable, false);
    writeq(ai->submitted, &ai->control->linux_side.submitted); wmb();
    result = count;
out:
    mutex_unlock(&ai->lock);
    return result;
}

static ssize_t ai_read(struct file *file, char __user *buffer, size_t count, loff_t *offset)
{
    struct tdvp_ai_linux *ai = file->private_data;
    struct tdvp_ai_response *response = ai->record;
    int result;
    size_t bytes;
    for (;;) {
        if (mutex_lock_interruptible(&ai->lock)) return -ERESTARTSYS;
        result = ai_refresh(ai);
        if (result && result != -EAGAIN) goto out;
        if (ai->ready) break;
        if (!ai->pending || (file->f_flags & O_NONBLOCK)) { result = -EAGAIN; goto out; }
        mutex_unlock(&ai->lock);
        result = wait_event_interruptible(ai->wait, READ_ONCE(ai->ready) || READ_ONCE(ai->fault));
        if (result) return result;
    }
    bytes = sizeof(*response) + response->output_bytes;
    if (count < bytes) { result = -EMSGSIZE; goto out; }
    if (copy_to_user(buffer, ai->record, bytes)) { result = -EFAULT; goto out; }
    /* The record is a private CPU0 copy. Still require the same owner before
     * releasing the shared lease; a failed user copy never reaches this point. */
    if (smp_load_acquire(&ai->owner_status)) { result = ai_fault(ai, -EPIPE); goto out; }
    ai_ack(ai);
    result = bytes;
out:
    mutex_unlock(&ai->lock);
    return result;
}

static __poll_t ai_poll(struct file *file, poll_table *wait)
{
    struct tdvp_ai_linux *ai = file->private_data;
    __poll_t events = 0;
    poll_wait(file, &ai->wait, wait);
    if (READ_ONCE(ai->fault)) return EPOLLERR;
    if (READ_ONCE(ai->ready)) events |= EPOLLIN | EPOLLRDNORM;
    if (READ_ONCE(ai->writable)) events |= EPOLLOUT | EPOLLWRNORM;
    return events;
}

static int ai_release(struct inode *inode, struct file *file)
{
    struct tdvp_ai_linux *ai = file->private_data;
    mutex_lock(&ai->lock);
    ai->opened = false;
    if (ai->pending) {
        ai->detached = true;
        (void)ai_refresh(ai);
        if (ai->ready) ai_ack(ai);
    }
    mutex_unlock(&ai->lock);
    return 0;
}

static const struct file_operations ai_fops = {
    .owner = THIS_MODULE, .open = ai_open, .write = ai_write, .read = ai_read,
    .poll = ai_poll, .release = ai_release, .llseek = no_llseek,
};

static ssize_t status_show(struct device *dev, struct device_attribute *attr, char *buffer)
{
    struct miscdevice *misc = dev_get_drvdata(dev);
    struct tdvp_ai_linux *ai = container_of(misc, struct tdvp_ai_linux, misc);
    ssize_t result;
    if (!mutex_trylock(&ai->lock)) return -EAGAIN;
    result = sysfs_emit(buffer, "ai_abi=1\nbackend=cpu1-ai2d\nkpu_jobs=unavailable\nfft_jobs=unavailable\n"
        "state=%s\nerror=%d\nowner_error=%d\nclient_open=%u\npending=%u\ndetached=%u\n"
        "submitted=%llu\naccepted=%llu\ncompleted=%llu\n",
        ai->fault ? "fault" : !ai->peer_ready ? "pending" : ai->ready ? "result" : ai->pending ? "running" : "idle",
        ai->fault, smp_load_acquire(&ai->owner_status), ai->opened, ai->pending, ai->detached,
        ai->submitted, ai->accepted, ai->completed);
    mutex_unlock(&ai->lock);
    return result;
}
static DEVICE_ATTR_RO(status);
static struct attribute *ai_attrs[] = { &dev_attr_status.attr, NULL };
ATTRIBUTE_GROUPS(ai);

struct tdvp_ai_linux *tdvp_ai_linux_create(struct device *parent, void __iomem *vision_control)
{
    struct tdvp_ai_linux *ai;
    int result;
    static_assert(TDVP_VISION_SHARED_BASE + TDVP_VISION_SLOT_COUNT * TDVP_VISION_SLOT_BYTES == TDVP_AI_INPUT_BASE);
    static_assert(TDVP_OWNER_BASE + TDVP_OWNER_WINDOW <= TDVP_AI_CONTROL_BASE);
    static_assert(TDVP_AI_CONTROL_BASE + TDVP_AI_WINDOW <= TDVP_VISION_CONTROL_BASE + TDVP_VISION_CONTROL_SIZE);
    ai = kzalloc(sizeof(*ai), GFP_KERNEL);
    if (!ai) return ERR_PTR(-ENOMEM);
    ai->owner_status = -EAGAIN;
    mutex_init(&ai->lock); init_waitqueue_head(&ai->wait); INIT_DELAYED_WORK(&ai->work, ai_tick);
    ai->control = vision_control + (TDVP_AI_CONTROL_BASE - TDVP_VISION_CONTROL_BASE);
    ai->input_map = devm_ioremap(parent, TDVP_AI_INPUT_BASE, TDVP_AI_BUFFER_BYTES);
    ai->output_map = devm_ioremap(parent, TDVP_AI_OUTPUT_BASE, TDVP_AI_BUFFER_BYTES);
    ai->input = kvzalloc(TDVP_AI_BUFFER_BYTES, GFP_KERNEL);
    ai->record = kvzalloc(sizeof(struct tdvp_ai_response) + TDVP_AI_BUFFER_BYTES, GFP_KERNEL);
    if (!ai->input_map || !ai->output_map || !ai->input || !ai->record) { result = -ENOMEM; goto failed; }
    ai->misc.minor = MISC_DYNAMIC_MINOR; ai->misc.name = "tdvp-ai"; ai->misc.fops = &ai_fops;
    ai->misc.mode = 0600; ai->misc.parent = parent; ai->misc.groups = ai_groups;
    result = misc_register(&ai->misc);
    if (result) goto failed;
    schedule_delayed_work(&ai->work, AI_TICK);
    return ai;
failed:
    kvfree(ai->input); kvfree(ai->record); kfree(ai);
    return ERR_PTR(result);
}

void tdvp_ai_linux_abort(struct tdvp_ai_linux *ai)
{
    if (IS_ERR_OR_NULL(ai)) return;
    WRITE_ONCE(ai->dead, true); cancel_delayed_work_sync(&ai->work);
    misc_deregister(&ai->misc);
    kvfree(ai->input); kvfree(ai->record); kfree(ai);
}

void tdvp_ai_linux_owner_update(struct tdvp_ai_linux *ai, int status, u64 owner_cookie, u64 peer_cookie)
{
    if (!ai) return;
    /* These cookies are immutable after the first release publication. */
    if (!status) {
        if (!owner_cookie || !peer_cookie ||
            (ai->owner_cookie && (ai->owner_cookie != owner_cookie || ai->peer_cookie != peer_cookie))) status = -EPIPE;
        else if (!ai->owner_cookie) { ai->owner_cookie = owner_cookie; ai->peer_cookie = peer_cookie; }
    }
    smp_store_release(&ai->owner_status, status);
}
