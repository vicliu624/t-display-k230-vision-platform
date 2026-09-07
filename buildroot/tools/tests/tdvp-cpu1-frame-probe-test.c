/* SPDX-License-Identifier: MIT */
/* Execute the actual standalone reader with mocked time and file/device I/O. */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include <assert.h>
#include <fcntl.h>
#include <poll.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
static int probe_test_clock(clockid_t, struct timespec *);
static int probe_test_poll(struct pollfd *, nfds_t, int);
static ssize_t probe_test_read(int, void *, size_t);
static int probe_test_open(const char *, int, ...);
static int probe_test_close(int);
static int probe_test_fstat(int, struct stat *);
static ssize_t probe_test_write(int, const void *, size_t);
#define clock_gettime probe_test_clock
#define poll probe_test_poll
#define read probe_test_read
#define open probe_test_open
#define close probe_test_close
#define fstat probe_test_fstat
#define write probe_test_write
#define main frame_probe_cli_main
#include "../tdvp-cpu1-frame-probe.c"
#undef main
#undef write
#undef fstat
#undef close
#undef open
#undef read
#undef poll
#undef clock_gettime

enum { NORMAL, POLL_TIMEOUT, POLL_EINTR, READ_EAGAIN, ERROR_EVENT, HUP_EVENT,
       INVALID_EVENT, NO_EVENT, READ_ERROR, EOF_READ, SHORT_READ, CLOCK_ERROR,
       CLOCK_BACKWARD, LATE_READY, ENDLESS_EINTR, ENDLESS_EAGAIN, LARGE_READ };
static unsigned int mode, poll_calls, read_calls, clock_calls, delivered;
static int previous_timeout;
static uint64_t mock_ms;
static unsigned int cli_case, stream_opened, stream_closed, output_opened, output_closed, write_calls;
static size_t output_bytes;

static void probe_test_frame(void *record, uint64_t sequence, uint64_t pts)
{
    struct tdvp_vision_frame_header header = {
        .sequence=sequence, .pts=pts, .width=1920, .height=1080,
        .stride=1920, .bytes=FRAME_BYTES, .fourcc=TDVP_VISION_NV12,
    };
    uint8_t *pixels = (uint8_t *)record + sizeof(header);
    memcpy(record, &header, sizeof(header));
    for (size_t i=0; i<FRAME_BYTES; ++i) pixels[i]=(uint8_t)i;
}

static void probe_test_reset(unsigned int next)
{
    mode=next; poll_calls=read_calls=clock_calls=delivered=0;
    mock_ms=1000; previous_timeout=120000;
    cli_case=stream_opened=stream_closed=output_opened=output_closed=write_calls=0;
    output_bytes=0;
}

static int probe_test_open(const char *path, int flags, ...)
{
    if (!strcmp(path,"/dev/tdvp-vision")) {
        assert(flags==(O_RDONLY|O_NONBLOCK|O_CLOEXEC) && !stream_opened);
        if (cli_case==3) { errno=EBUSY; return -1; }
        stream_opened=1; return 123;
    }
    assert(!strcmp(path,"new.nv12") && stream_closed && !output_opened);
    assert(flags==(O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC));
    va_list args;
    va_start(args,flags); assert(va_arg(args,int)==0600); va_end(args);
    if (cli_case==1) { errno=EEXIST; return -1; }
    output_opened=1; return 456;
}

static int probe_test_close(int fd)
{
    if (fd==123) { assert(stream_opened && !stream_closed); stream_closed=1; }
    else { assert(fd==456 && output_opened && !output_closed); output_closed=1; }
    if ((cli_case==6 && fd==456) || (cli_case==7 && fd==123)) { errno=EIO; return -1; }
    return 0;
}

static int probe_test_fstat(int fd, struct stat *info)
{
    assert(fd==123 && stream_opened && !stream_closed);
    memset(info,0,sizeof(*info));
    info->st_mode=cli_case==4 ? S_IFREG : S_IFCHR;
    return 0;
}

static ssize_t probe_test_write(int fd, const void *data, size_t bytes)
{
    assert(fd==456 && output_opened && !output_closed && stream_closed);
    ++write_calls;
    if (cli_case==2) { errno=ENOSPC; return -1; }
    if (write_calls==2) { errno=EINTR; return -1; }
    if (write_calls==1 && bytes>17) bytes=17; /* Exercise partial writes. */
    for (size_t i=0;i<bytes;++i) assert(((const uint8_t *)data)[i]==(uint8_t)(output_bytes+i));
    output_bytes+=bytes;
    return (ssize_t)bytes;
}

static int probe_test_clock(clockid_t id, struct timespec *now)
{
    uint64_t value=mock_ms;
    assert(id==CLOCK_MONOTONIC);
    ++clock_calls;
    if (mode==CLOCK_ERROR && clock_calls==3) { errno=EIO; return -1; }
    if (mode==CLOCK_BACKWARD && clock_calls==3) value-=2;
    *now=(struct timespec){.tv_sec=(time_t)(value/1000), .tv_nsec=(long)(value%1000)*1000000};
    return 0;
}

static int probe_test_poll(struct pollfd *request, nfds_t count, int timeout)
{
    assert(count==1 && request->fd==123 && request->events==POLLIN);
    assert(timeout>0 && timeout<=previous_timeout);
    previous_timeout=timeout;
    ++poll_calls; ++mock_ms;
    assert(poll_calls<=100); /* Catch unbounded retries in the actual reader. */
    if (mode==POLL_TIMEOUT) { mock_ms+=(unsigned int)timeout; return 0; }
    if ((mode==POLL_EINTR && poll_calls==1) || mode==ENDLESS_EINTR) { errno=EINTR; return -1; }
    request->revents=POLLIN;
    if (mode==ERROR_EVENT) request->revents|=POLLERR;
    if (mode==HUP_EVENT) request->revents|=POLLHUP;
    if (mode==INVALID_EVENT) request->revents=POLLNVAL;
    if (mode==NO_EVENT) request->revents=0;
    if (mode==LATE_READY) mock_ms+=(unsigned int)timeout;
    return 1;
}

static ssize_t probe_test_read(int fd, void *record, size_t bytes)
{
    assert(fd==123 && bytes==RECORD_BYTES);
    ++read_calls;
    if ((mode==READ_EAGAIN && read_calls==1) || mode==ENDLESS_EAGAIN) { errno=EAGAIN; return -1; }
    if (mode==READ_ERROR) { errno=EIO; return -1; }
    if (mode==EOF_READ) return 0;
    probe_test_frame(record, 40+delivered, 100000+33333*delivered);
    ++delivered;
    if (mode==SHORT_READ) return (ssize_t)RECORD_BYTES-1;
    if (mode==LARGE_READ) return (ssize_t)RECORD_BYTES+1;
    return (ssize_t)RECORD_BYTES;
}

int main(void)
{
    void *record=malloc(RECORD_BYTES);
    struct frame_probe_evidence evidence, before;
    struct tdvp_vision_frame_header *header=record;
    const int errors[] = {0, -ETIMEDOUT, 0, 0, -EIO, -EIO, -EIO, -EPROTO,
                          -EIO, -EPIPE, -EPROTO, -EIO, -EIO, -ETIMEDOUT,
                          -ETIMEDOUT, -ETIMEDOUT, -EPROTO};
    const char *bad_numbers[] = {"", "0", "-1", "+1", " 1", "1 ", "1x", "301", "0x30", "999999999999999999999999"};
    assert(record);
    for (unsigned int i=0; i<sizeof(errors)/sizeof(errors[0]); ++i) {
        probe_test_reset(i); evidence=(struct frame_probe_evidence){0};
        assert(frame_probe_run(123, 3, 10, record, &evidence)==errors[i]);
        if (!errors[i]) {
            assert(evidence.frames==3 && evidence.bytes==3ULL*FRAME_BYTES);
            assert(evidence.first_sequence==40 && evidence.sequence==42);
            assert(evidence.first_pts==100000 && evidence.pts==166666);
            assert(evidence.luma_min==0 && evidence.luma_max==255);
            assert(evidence.hash==UINT64_C(0x7093890ab731e525));
        }
        if (i==ERROR_EVENT || i==HUP_EVENT || i==INVALID_EVENT || i==LATE_READY)
            assert(!read_calls && !evidence.frames);
    }
    for (unsigned int mutation=0; mutation<15; ++mutation) {
        probe_test_frame(record, 7, 1000);
        evidence=(struct frame_probe_evidence){0};
        assert(!frame_probe_validate(record, RECORD_BYTES, &evidence));
        before=evidence;
        probe_test_frame(record, 8, 2000);
        switch (mutation) {
        case 0: header->sequence=0; break;
        case 1: header->sequence=7; break;
        case 2: header->sequence=9; break;
        case 3: header->pts=1000; break;
        case 4: header->pts=999; break;
        case 5: header->width=640; break;
        case 6: header->height=480; break;
        case 7: header->stride=2048; break;
        case 8: header->bytes=UINT32_MAX; break;
        case 9: header->fourcc=0; break;
        case 10: header->reserved[0]=1; break;
        case 11: header->reserved[6]=1; break;
        case 12: evidence.sequence=UINT64_MAX; break;
        default: break;
        }
        assert(frame_probe_validate(record, mutation==13 ? RECORD_BYTES-1 : mutation==14 ? 0 : RECORD_BYTES, &evidence)==-EPROTO);
        assert(evidence.frames==before.frames && evidence.bytes==before.bytes && evidence.hash==before.hash);
    }
    for (size_t i=0; i<sizeof(bad_numbers)/sizeof(bad_numbers[0]); ++i)
        assert(!frame_probe_number(bad_numbers[i], 300));
    assert(frame_probe_number("030",300)==30 && frame_probe_number("300",300)==300);
    probe_test_reset(NORMAL); evidence=(struct frame_probe_evidence){0};
    assert(frame_probe_run(123, 0, 10, record, &evidence)==-EINVAL);
    assert(frame_probe_run(123, 301, 10, record, &evidence)==-EINVAL);
    assert(frame_probe_run(123, 3, 0, record, &evidence)==-EINVAL);
    assert(frame_probe_run(123, 3, 120001, record, &evidence)==-EINVAL);
    assert(!clock_calls && !poll_calls && !read_calls);
    char *args[]={"frame-probe","3","100","new.nv12",NULL};
    for (unsigned int scenario=0;scenario<8;++scenario) {
        probe_test_reset(scenario==5 ? POLL_TIMEOUT : NORMAL); cli_case=scenario;
        assert(frame_probe_cli_main(4,args)==(scenario==0 ? 0 : 1));
        if (scenario==3) assert(!stream_opened && !stream_closed);
        else assert(stream_opened && stream_closed);
        if (scenario==1 || scenario==3 || scenario==4 || scenario==5 || scenario==7)
            assert(!output_opened && !write_calls);
        else assert(output_opened && output_closed);
        if (scenario==0 || scenario==6) assert(output_bytes==FRAME_BYTES);
    }
    probe_test_reset(NORMAL);
    assert(frame_probe_cli_main(3,args)==0 && stream_closed && !output_opened);
    free(record);
    puts("CPU1 frame probe: PASS actual parser/reader, 17 I/O/time cases, 15 malformed records, nine CLI/file lifecycles, bounded retries and strict limits; not hardware acceptance");
    return 0;
}
