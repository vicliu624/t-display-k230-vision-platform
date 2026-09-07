// SPDX-License-Identifier: GPL-2.0
/* CPU0 asynchronous copy bridge. Not a camera, ISP, KPU or GPU driver.
 * One reader; read() returns one header + packed NV12 record, poll() signals
 * availability. No mmap/ioctl accepting physical addresses from userspace.
 */
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/kref.h>
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

#define VISION_FRAME_BYTES (1920U * 1080U * 3U / 2U)
#define VISION_WATCHDOG (10 * HZ)
#define VISION_TICK msecs_to_jiffies(50)

struct vision_device {
    struct miscdevice misc;
    struct kref ref;
    struct mutex lock;
    struct delayed_work work;
    wait_queue_head_t wait;
    struct tdvp_vision_control __iomem *control;
    void __iomem *slots;
    void *record;
    u64 epoch, released, heartbeat, peer_heartbeat;
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

    mutex_lock(&vision->lock);
    if (!vision->dead && vision->opened) {
        vision_refresh(vision);
        if (vision->fault)
            writel(TDVP_VISION_CMD_STOP, &vision->control->consumer.command);
        else
            writeq(++vision->heartbeat, &vision->control->consumer.heartbeat);
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
    schedule_delayed_work(&vision->work, VISION_TICK);
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

    /* Keep the exclusive-open gate held while cancelling the old heartbeat. */
    cancel_delayed_work_sync(&vision->work);
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

static int vision_probe(struct platform_device *pdev)
{
    struct device_node *memory;
    struct resource reserved;
    struct vision_device *vision;
    int result;

    memory = of_parse_phandle(pdev->dev.of_node, "memory-region", 0);
    if (!memory)
        return -EINVAL;
    result = of_address_to_resource(memory, 0, &reserved);
    if (!of_property_read_bool(memory, "no-map"))
        result = -EINVAL;
    of_node_put(memory);
    if (result)
        return result;
    if (reserved.start != TDVP_VISION_SHARED_BASE || resource_size(&reserved) != TDVP_VISION_SHARED_SIZE)
        return -EINVAL;
    vision = kzalloc(sizeof(*vision), GFP_KERNEL);
    if (!vision)
        return -ENOMEM;
    kref_init(&vision->ref);
    mutex_init(&vision->lock);
    init_waitqueue_head(&vision->wait);
    INIT_DELAYED_WORK(&vision->work, vision_tick);
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
    result = misc_register(&vision->misc);
    if (result)
        goto failed;
    platform_set_drvdata(pdev, vision);
    dev_info(&pdev->dev, "CPU1 asynchronous frame bridge; no camera/ISP ownership\n");
    return 0;
failed:
    kref_put(&vision->ref, vision_free);
    return result;
}

static int vision_remove(struct platform_device *pdev)
{
    struct vision_device *vision = platform_get_drvdata(pdev);

    mutex_lock(&vision->lock);
    vision->dead = true;
    writel(TDVP_VISION_CMD_STOP, &vision->control->consumer.command);
    mutex_unlock(&vision->lock);
    wake_up_interruptible(&vision->wait);
    misc_deregister(&vision->misc);
    cancel_delayed_work_sync(&vision->work);
    /* Existing fds retain the object, but dead prevents further I/O after
     * devm unmaps the physical windows on return from remove(). */
    kref_put(&vision->ref, vision_free);
    return 0;
}

static const struct of_device_id vision_match[] = {
    { .compatible = "tdvp,cpu1-vision-v1" },
    {},
};
MODULE_DEVICE_TABLE(of, vision_match);
static struct platform_driver vision_driver = {
    .probe = vision_probe,
    .remove = vision_remove,
    .driver = { .name = "tdvp-cpu1-vision", .of_match_table = vision_match },
};
module_platform_driver(vision_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TDVP CPU1 vision asynchronous copy bridge");
