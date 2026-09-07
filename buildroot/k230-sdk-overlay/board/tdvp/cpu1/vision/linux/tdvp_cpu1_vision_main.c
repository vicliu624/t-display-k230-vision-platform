// SPDX-License-Identifier: GPL-2.0
/* CPU0 asynchronous copy bridge. Not a camera, ISP, KPU or GPU driver.
 * One reader; read() returns one header + packed NV12 record, poll() signals
 * availability. No mmap/ioctl accepting physical addresses from userspace.
 */
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/kref.h>
#include <linux/ktime.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>
#include "tdvp_vision_abi.h"
#include "tdvp_cpu1_vision_layout.h"
#include "tdvp_cpu1_owner.h"
#include "tdvp_vision_observer.h"

#define VISION_FRAME_BYTES (1920U * 1080U * 3U / 2U)
#define VISION_WATCHDOG (10 * HZ)
#define VISION_TICK msecs_to_jiffies(50)

struct vision_device {
    struct tdvp_linux_owner owner;
    struct tdvp_vision_observer observer;
    struct miscdevice misc;
    struct kref ref;
    struct mutex lock;
    struct delayed_work work;
    wait_queue_head_t wait;
    struct tdvp_vision_control __iomem *control;
    void __iomem *slots;
    void *record;
    u64 epoch, released, heartbeat, peer_heartbeat;
    u64 frames_delivered;
    unsigned long peer_seen;
    bool opened, dead, ready;
    int fault;
};

static void vision_free(struct kref *ref)
{
    struct vision_device *vision = container_of(ref, struct vision_device, ref);
    kvfree(vision->record);
    kfree(vision);
}

/* Called with the mutex held, before touching or acknowledging a frame. */
static void vision_refresh(struct vision_device *vision)
{
    struct tdvp_vision_producer __iomem *producer = &vision->control->producer;
    u64 published, heartbeat;

    if (vision->fault)
        return;
    if (readl(&producer->magic) != TDVP_VISION_MAGIC ||
        readl(&producer->version) != TDVP_VISION_ABI_VERSION ||
        readq(&producer->epoch) != vision->epoch) {
        vision->fault = -EPIPE;
        return;
    }
    heartbeat = readq(&producer->heartbeat);
    if (heartbeat != vision->peer_heartbeat) {
        vision->peer_heartbeat = heartbeat;
        vision->peer_seen = jiffies;
    } else if (time_after(jiffies, vision->peer_seen + VISION_WATCHDOG)) {
        vision->fault = -ETIMEDOUT;
        return;
    }
    if (readl(&producer->state) == TDVP_VISION_STATE_FAULT) {
        vision->fault = -EIO;
        return;
    }
    published = readq(&producer->published);
    if (published < vision->released || published - vision->released > TDVP_VISION_SLOT_COUNT) {
        vision->fault = -EPROTO;
        return;
    }
    WRITE_ONCE(vision->ready, published > vision->released);
}

static void vision_tick(struct work_struct *work)
{
    struct vision_device *vision = container_of(to_delayed_work(work), struct vision_device, work);
    int ownership;

    mutex_lock(&vision->lock);
    if (!vision->dead) {
        ownership = tdvp_linux_owner_poll(&vision->owner);
        if (ownership && ownership != -EAGAIN)
            vision->fault = ownership;
        if (!ownership) {
            struct tdvp_vision_producer sample;

            /* Telemetry never acknowledges a slot or starts a camera. Check
             * the epoch around the copy; counters are sampled statistics,
             * not a transactionally consistent frame descriptor. */
            memcpy_fromio(&sample, &vision->control->producer, sizeof(sample));
            rmb();
            if (sample.epoch != readq(&vision->control->producer.epoch))
                sample.epoch = 0;
            tdvp_vision_observe(&vision->observer, &sample,
                               vision->owner.session.observed_cookie, ktime_to_ms(ktime_get()));
        }
        if (vision->opened) {
            vision_refresh(vision);
            if (vision->fault)
                writel(TDVP_VISION_CMD_STOP, &vision->control->consumer.command);
            else
                writeq(++vision->heartbeat, &vision->control->consumer.heartbeat);
        }
        if (vision->ready || vision->fault)
            wake_up_interruptible(&vision->wait);
        schedule_delayed_work(&vision->work, VISION_TICK);
    }
    mutex_unlock(&vision->lock);
}

static int vision_open(struct inode *inode, struct file *file)
{
    struct vision_device *vision = container_of(file->private_data, struct vision_device, misc);
    struct tdvp_vision_producer __iomem *producer = &vision->control->producer;
    int result = 0;

    if (!(file->f_mode & FMODE_READ))
        return -EINVAL;
    mutex_lock(&vision->lock);
    if (vision->dead) {
        result = -ENODEV;
        goto out;
    }
    if (vision->opened) {
        result = -EBUSY;
        goto out;
    }
    result = tdvp_linux_owner_status(&vision->owner);
    if (result)
        goto out;
    /* READY precedes worker launch. Refuse stale RAM from an earlier boot,
     * and wait until this boot's worker publishes its ownership cookie.
     */
    if (readq(&producer->epoch) != vision->owner.session.observed_cookie) {
        result = -EAGAIN;
        goto out;
    }
    if (readl(&producer->magic) != TDVP_VISION_MAGIC ||
        readl(&producer->version) != TDVP_VISION_ABI_VERSION ||
        readl(&producer->control_bytes) != sizeof(struct tdvp_vision_control) ||
        readl(&producer->slot_count) != TDVP_VISION_SLOT_COUNT ||
        readl(&producer->slot_bytes) != TDVP_VISION_SLOT_BYTES) {
        result = -EPROTO;
        goto out;
    }
    if (readl(&producer->state) == TDVP_VISION_STATE_FAULT) {
        result = -EIO;
        goto out;
    }
    vision->epoch = readq(&producer->epoch);
    if (!vision->epoch) {
        result = -EPROTO;
        goto out;
    }
    vision->released = readq(&producer->published);
    vision->peer_heartbeat = readq(&producer->heartbeat);
    vision->peer_seen = jiffies;
    vision->heartbeat = 1;
    vision->fault = 0;
    vision->ready = false;
    vision->opened = true;
    /* New exclusive reader discards old records. No old read can still be
     * copying: release() completes before opened becomes false. */
    writel(TDVP_VISION_CMD_STOP, &vision->control->consumer.command);
    writeq(vision->released, &vision->control->consumer.released);
    writeq(vision->heartbeat, &vision->control->consumer.heartbeat);
    wmb();
    writeq(vision->epoch, &vision->control->consumer.epoch);
    wmb();
    writel(TDVP_VISION_CMD_RUN, &vision->control->consumer.command);
    kref_get(&vision->ref);
    file->private_data = vision;
out:
    mutex_unlock(&vision->lock);
    return result;
}

static ssize_t vision_read(struct file *file, char __user *buffer, size_t count, loff_t *offset)
{
    struct vision_device *vision = file->private_data;
    struct tdvp_vision_frame_header *header = vision->record;
    size_t bytes = sizeof(*header) + VISION_FRAME_BYTES;
    u64 sequence;
    unsigned int slot;
    int result;

    if (count < bytes)
        return -EMSGSIZE;
    for (;;) {
        if (mutex_lock_interruptible(&vision->lock))
            return -ERESTARTSYS;
        if (vision->dead) {
            result = -ENODEV;
            goto unlock;
        }
        vision_refresh(vision);
        if (vision->fault) {
            result = vision->fault;
            goto unlock;
        }
        if (vision->ready)
            break;
        mutex_unlock(&vision->lock);
        if (file->f_flags & O_NONBLOCK)
            return -EAGAIN;
        result = wait_event_interruptible(vision->wait, READ_ONCE(vision->ready) ||
                                          READ_ONCE(vision->fault) || READ_ONCE(vision->dead));
        if (result)
            return result;
    }
    sequence = vision->released + 1;
    slot = (sequence - 1) % TDVP_VISION_SLOT_COUNT;
    rmb();
    memcpy_fromio(header, &vision->control->frame[slot], sizeof(*header));
    if (header->sequence != sequence || header->width != 1920 || header->height != 1080 ||
        header->stride != 1920 || header->bytes != VISION_FRAME_BYTES ||
        header->fourcc != TDVP_VISION_NV12) {
        result = vision->fault = -EPROTO;
        goto unlock;
    }
    memcpy_fromio((u8 *)vision->record + sizeof(*header),
                  vision->slots + slot * TDVP_VISION_SLOT_BYTES, VISION_FRAME_BYTES);
    /* CPU1 cannot reuse this slot yet. Failed copy_to_user keeps the record
     * leased, so retry is safe and no partial user copy acknowledges it. */
    if (copy_to_user(buffer, vision->record, bytes)) {
        result = -EFAULT;
        goto unlock;
    }
    mb();
    writeq(sequence, &vision->control->consumer.released);
    vision->released = sequence;
    if (vision->frames_delivered != ~0ULL)
        ++vision->frames_delivered;
    vision_refresh(vision);
    result = bytes;
unlock:
    mutex_unlock(&vision->lock);
    return result;
}

static __poll_t vision_poll(struct file *file, poll_table *wait)
{
    struct vision_device *vision = file->private_data;
    poll_wait(file, &vision->wait, wait);
    if (READ_ONCE(vision->dead))
        return EPOLLERR | EPOLLHUP;
    if (READ_ONCE(vision->fault))
        return EPOLLERR;
    return READ_ONCE(vision->ready) ? EPOLLIN | EPOLLRDNORM : 0;
}

static int vision_release(struct inode *inode, struct file *file)
{
    struct vision_device *vision = file->private_data;

    /* The boot ownership heartbeat must outlive every individual reader. */
    mutex_lock(&vision->lock);
    if (!vision->dead && readq(&vision->control->producer.epoch) == vision->epoch) {
        u64 published = readq(&vision->control->producer.published);

        writel(TDVP_VISION_CMD_STOP, &vision->control->consumer.command);
        wmb();
        /* No user copy remains at final fput. Release unread copied slots,
         * never the private CPU1 ISP buffer pool. */
        if (published >= vision->released && published - vision->released <= TDVP_VISION_SLOT_COUNT)
            writeq(published, &vision->control->consumer.released);
    }
    vision->opened = false;
    mutex_unlock(&vision->lock);
    kref_put(&vision->ref, vision_free);
    return 0;
}

static const struct file_operations vision_fops = {
    .owner = THIS_MODULE,
    .open = vision_open,
    .read = vision_read,
    .poll = vision_poll,
    .release = vision_release,
    .llseek = no_llseek,
};

/* One coherent Linux-side status record. Reading this file must not open the
 * stream, poll the ownership protocol (which writes OFFER/heartbeat), clear a
 * fault, or operate any hardware. Busy readers get EAGAIN, never an unbounded
 * status wait behind a stalled application's copy_to_user fault. */
static ssize_t status_show(struct device *dev, struct device_attribute *attr, char *buffer)
{
    struct miscdevice *misc = dev_get_drvdata(dev);
    struct vision_device *vision = container_of(misc, struct vision_device, misc);
    const struct tdvp_vision_producer *sample = &vision->observer.sample;
    const struct tdvp_owner_record *peer = &vision->owner.last_peer;
    const char *ownership_state;
    u64 now = ktime_to_ms(ktime_get());
    unsigned int stream_state;
    bool traced;
    int ownership;
    ssize_t bytes;

    if (!mutex_trylock(&vision->lock))
        return -EAGAIN;
    ownership = tdvp_linux_owner_status(&vision->owner);
    ownership_state = vision->dead ? "unavailable" :
        (ownership == -EAGAIN ? "pending" : (ownership ? "fault" : "ready"));
    if (!ownership && (now < vision->owner.session.peer_seen ||
        now - vision->owner.session.peer_seen >= TDVP_OWNER_PEER_MS))
        ownership_state = "stale";
    stream_state = tdvp_vision_observer_state(&vision->observer, now);
    traced = peer->reserved[0] == TDVP_STARTUP_TRACE_MAGIC &&
        peer->reserved[1] > TDVP_STARTUP_NONE && peer->reserved[1] <= TDVP_STARTUP_COMPLETE;
    bytes = sysfs_emit(buffer,
        "status_version=1\nresource_owner=cpu1\nownership_contract=2\n"
        "ownership_state=%s\nownership_error=%d\nvision_state=%s\nvision_error=%d\n"
        "reader_open=%u\nframes_delivered=%llu\ncaptured=%llu\npublished=%llu\ndropped=%llu\n"
        "startup_trace_version=%u\nstartup_stage=%s\nstartup_result=%d\n",
        ownership_state, ownership, tdvp_vision_observer_name(stream_state),
        vision->fault ? vision->fault : sample->fault, vision->opened,
        vision->frames_delivered, (unsigned long long)sample->captured,
        (unsigned long long)sample->published, (unsigned long long)sample->dropped,
        traced ? 1U : 0U, tdvp_startup_stage_name(traced ? peer->reserved[1] : 0U),
        traced ? (s32)peer->reserved[2] : 0);
    mutex_unlock(&vision->lock);
    return bytes;
}
static DEVICE_ATTR_RO(status);
static struct attribute *vision_attrs[] = { &dev_attr_status.attr, NULL };
ATTRIBUTE_GROUPS(vision);

static int vision_probe(struct platform_device *pdev)
{
    struct vision_device *vision;
    int result;

    vision = kzalloc(sizeof(*vision), GFP_KERNEL);
    if (!vision)
        return -ENOMEM;
    kref_init(&vision->ref);
    mutex_init(&vision->lock);
    init_waitqueue_head(&vision->wait);
    INIT_DELAYED_WORK(&vision->work, vision_tick);
    result = tdvp_linux_owner_prepare(&pdev->dev, &vision->owner);
    if (result)
        goto failed;
    vision->record = kvmalloc(sizeof(struct tdvp_vision_frame_header) + VISION_FRAME_BYTES, GFP_KERNEL);
    vision->slots = devm_ioremap(&pdev->dev, TDVP_VISION_SHARED_BASE,
                                TDVP_VISION_SLOT_COUNT * TDVP_VISION_SLOT_BYTES);
    vision->control = devm_ioremap(&pdev->dev, TDVP_VISION_CONTROL_BASE, TDVP_VISION_CONTROL_SIZE);
    if (!vision->record || !vision->slots || !vision->control) {
        result = -ENOMEM;
        goto failed;
    }
    vision->misc.minor = MISC_DYNAMIC_MINOR;
    vision->misc.name = "tdvp-vision";
    vision->misc.fops = &vision_fops;
    vision->misc.mode = 0600; /* image udev rule grants the video group */
    vision->misc.parent = &pdev->dev;
    vision->misc.groups = vision_groups;
    result = misc_register(&vision->misc);
    if (result)
        goto failed;
    platform_set_drvdata(pdev, vision);
    /* Fixed boot-time DT node; no hot-unbind, unloading, or in-place restart.
     * From OFFER onward even a failed handshake must retain all suppliers.
     */
    __module_get(THIS_MODULE);
    /* misc_register already exposes the read-only status file. Serialize
     * initial ownership publication with those readers and stream opens. */
    mutex_lock(&vision->lock);
    tdvp_linux_owner_start(&vision->owner);
    mutex_unlock(&vision->lock);
    schedule_delayed_work(&vision->work, VISION_TICK);
    dev_info(&pdev->dev, "CPU1 ownership OFFER; boot-lifetime resource holds, asynchronous frame bridge\n");
    return 0;
failed:
    tdvp_linux_owner_abort(&vision->owner);
    kref_put(&vision->ref, vision_free);
    return dev_err_probe(&pdev->dev, result, "CPU1 ownership preparation failed; no offer issued\n");
}

static int vision_pm_prepare(struct device *dev)
{
    struct vision_device *vision = dev_get_drvdata(dev);
    /* DPMS blanking is not system suspend. A cross-core quiesce protocol is
     * required before suspend/hibernation can take away clocks or memory.
     */
    return vision->owner.started ? -EBUSY : 0;
}
static const struct dev_pm_ops vision_pm_ops = { .prepare = vision_pm_prepare };

static const struct of_device_id vision_match[] = {
    { .compatible = "tdvp,cpu1-vision-v1" },
    {},
};
MODULE_DEVICE_TABLE(of, vision_match);
static struct platform_driver vision_driver = {
    .probe = vision_probe,
    .driver = { .name = "tdvp-cpu1-vision", .of_match_table = vision_match,
                .suppress_bind_attrs = true, .pm = &vision_pm_ops },
};
module_platform_driver(vision_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TDVP CPU1 vision asynchronous copy bridge");
