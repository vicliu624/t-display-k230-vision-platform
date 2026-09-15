#define _DEFAULT_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/* Native Linux/target replay of the production executable. Only openpty() is
 * used; this harness never opens /dev/ttyS1 or any physical radio transport. */
struct exchange { const char *command; const char *response; };
#define IDENTITY {"AT+VER?", "+VER:K230_NRF52840_AT,2026-08-19\r\nOK\r\n"}
#define STATUS {"AT+STATUS?", "+STATUS:READY,COUNT=0,GATT=0,SVC=0,CHR=0,DESC=0,MESH_ADV=1,MESH_CONN=0,MESH_QUEUE=0\r\nOK\r\n"}

static long long now_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) return -1;
    return (long long)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static int same_termios(const struct termios *a, const struct termios *b)
{
    return a->c_iflag == b->c_iflag && a->c_oflag == b->c_oflag &&
        a->c_cflag == b->c_cflag && a->c_lflag == b->c_lflag &&
        memcmp(a->c_cc, b->c_cc, NCCS) == 0 &&
        cfgetispeed(a) == cfgetispeed(b) && cfgetospeed(a) == cfgetospeed(b);
}

static int write_response(int fd, const char *response, long long deadline)
{
    size_t remaining = strlen(response);
    while (remaining) {
        if (now_ms() < 0 || now_ms() >= deadline) return -1;
        ssize_t n = write(fd, response, remaining);
        if (n > 0) { response += n; remaining -= (size_t)n; continue; }
        if (n < 0 && errno != EINTR && errno != EAGAIN) return -1;
        struct pollfd p = {fd, POLLOUT, 0};
        if (poll(&p, 1, 20) < 0 && errno != EINTR) return -1;
    }
    return 0;
}

static int run_case(const char *binary, const char *name, const char *const *commands,
                    const struct exchange *script, size_t script_size,
                    int expected_exit, int terminate_on_query, const char *required_output)
{
    int master = -1, slave = -1, output[2] = {-1, -1};
    struct termios original, restored;
    char path[128], wire[2048] = {0}, captured[32768] = {0};
    size_t wire_size = 0, captured_size = 0, step = 0;
    pid_t child = -1;
    int status = 0, reaped = 0, failed = 0;
    long long started = now_ms();
    if (started < 0 || openpty(&master, &slave, path, NULL, NULL) ||
        tcgetattr(slave, &original) || pipe(output)) {
        perror("native PTY setup"); failed = 1; goto cleanup;
    }
    if (fcntl(master, F_SETFL, O_NONBLOCK) || fcntl(output[0], F_SETFL, O_NONBLOCK)) {
        perror("native PTY nonblocking"); failed = 1; goto cleanup;
    }
    child = fork();
    if (child < 0) { perror("fork target client"); failed = 1; goto cleanup; }
    if (!child) {
        if (dup2(output[1], STDOUT_FILENO) < 0 || dup2(output[1], STDERR_FILENO) < 0) _exit(126);
        close(master); close(slave); close(output[0]); close(output[1]);
        char *args[32] = {(char *)binary, "--device", path, "--timeout-ms", "250"};
        size_t count = 5;
        for (size_t i = 0; commands && commands[i]; ++i) {
            if (count >= 31) _exit(126);
            args[count++] = (char *)commands[i];
        }
        args[count] = NULL;
        alarm(8); /* Exec retains a hard outer guard independent of client logic. */
        execv(binary, args);
        _exit(127);
    }
    close(output[1]); output[1] = -1;
    const long long deadline = started + 10000;
    while (!reaped && !failed) {
        if (now_ms() < 0 || now_ms() >= deadline) { failed = 1; break; }
        struct pollfd p[2] = {{master, POLLIN, 0}, {output[0], POLLIN, 0}};
        if (poll(p, 2, 20) < 0 && errno != EINTR) { failed = 1; break; }
        if (p[0].revents & POLLIN) {
            ssize_t n = read(master, wire + wire_size, sizeof(wire) - wire_size - 1);
            if (n > 0) wire_size += (size_t)n;
            else if (n < 0 && errno != EAGAIN && errno != EINTR) failed = 1;
            wire[wire_size] = 0;
            char *end;
            while (!failed && (end = strstr(wire, "\r\n"))) {
                *end = 0;
                if (step >= script_size || strcmp(wire, script[step].command)) {
                    fprintf(stderr, "%s: unexpected UART command %s\n", name, wire);
                    failed = 1; break;
                }
                if (terminate_on_query) {
                    if (kill(child, SIGTERM)) failed = 1;
                } else if (write_response(master, script[step].response, deadline)) failed = 1;
                ++step;
                size_t consumed = (size_t)(end - wire) + 2;
                memmove(wire, wire + consumed, wire_size - consumed);
                wire_size -= consumed;
                wire[wire_size] = 0;
            }
            if (wire_size >= sizeof(wire) - 1) failed = 1;
        }
        if (p[1].revents & POLLIN) {
            ssize_t n = read(output[0], captured + captured_size, sizeof(captured) - captured_size - 1);
            if (n > 0) captured_size += (size_t)n;
            else if (n < 0 && errno != EAGAIN && errno != EINTR) failed = 1;
            if (captured_size >= sizeof(captured) - 1) failed = 1;
        }
        pid_t done = waitpid(child, &status, WNOHANG);
        if (done == child) reaped = 1;
        else if (done < 0 && errno != EINTR) failed = 1;
    }
    if (reaped) {
        ssize_t n;
        while (captured_size < sizeof(captured) - 1 &&
               (n = read(output[0], captured + captured_size, sizeof(captured) - captured_size - 1)) > 0)
            captured_size += (size_t)n;
        captured[captured_size] = 0;
        if (!WIFEXITED(status) || WEXITSTATUS(status) != expected_exit ||
            step != script_size || wire_size || !strstr(captured, required_output)) failed = 1;
        if (!expected_exit && strstr(captured, "\"error\":")) failed = 1;
        int exclusive = 1;
        if (tcgetattr(slave, &restored) || !same_termios(&original, &restored) ||
            ioctl(slave, TIOCGEXCL, &exclusive) || exclusive) failed = 1;
    }
cleanup:
    if (child > 0 && !reaped) {
        (void)kill(child, SIGKILL);
        while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    }
    if (master >= 0) close(master);
    if (slave >= 0) close(slave);
    if (output[0] >= 0) close(output[0]);
    if (output[1] >= 0) close(output[1]);
    if (failed) {
        fprintf(stderr, "FAIL %s (step %zu/%zu, status %d)\n%s\n", name, step, script_size, status, captured);
        return 1;
    }
    printf("PASS native %s; exact wire commands, termios restored, TIOCEXCL cleared\n", name);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 2 || argv[1][0] != '/') {
        fprintf(stderr, "usage: %s /absolute/path/to/tdvp-nrf52840\n", argv[0]);
        return 2;
    }
    static const char *const commands[] = {
        "scan 1", "connect 12:34:56:78:9A:BC", "services", "characteristics 0",
        "descriptors 0", "read 37", "write 37 AABB", "cccd 38 notify", "listen 1", "disconnect", NULL
    };
    static const struct exchange complete[] = {
        IDENTITY, STATUS,
        {"AT+SCAN=1", "OK\r\n+SCAN:0,AA:BB:CC:DD:EE:01,-40,0,first\r\n+SCAN:1,12:34:56:78:9A:BC,-51,1,chosen\r\n+SCAN:DONE,2\r\n"},
        {"AT+CONN=1", "OK\r\n+CONNECTED:0\r\n"},
        {"AT+GATTS?", "OK\r\n+GATTS:0,0x180F:1,1,50\r\n+GATTS:DONE,1,10A\r\n"},
        {"AT+GATTC=0", "OK\r\n+GATTC:0,0,0x2A19:1,36,37,40,18,38\r\n+GATTC:DONE,0,1,10A\r\n"},
        {"AT+GATTD=0", "+GATTD:0,0,0x2902:1,38\r\n+GATTD:DONE,0,1,10A\r\nOK\r\n"},
        {"AT+READ=37,0", "OK\r\n+NOTIFY:37,1,11\r\n+READ:37,0,2,AABB,0\r\n"},
        {"AT+WRITE=37,AABB,REQ", "+WRITE:37,2,0\r\nOK\r\n"},
        {"AT+WRITE=38,0100,REQ", "OK\r\n+WRITE:38,2,0\r\n+NOTIFY:37,1,22\r\n"},
        {"AT+DISC", "+DISCONNECTED:0,16\r\nOK\r\n"}
    };
    static const struct exchange silent[] = {{"AT+VER?", ""}};
    static const struct exchange wrong[] = {{"AT+VER?", "+VER:OTHER,1\r\nOK\r\n"}};
    static const struct exchange ack_only[] = {IDENTITY, STATUS, {"AT+SCAN=1", "OK\r\n"}};
    static const char *const scan[] = {"scan 1", "status", NULL};
    int failed = 0;
    failed |= run_case(argv[1], "full BLE protocol replay", commands, complete,
                       sizeof(complete)/sizeof(complete[0]), 0, 0, "\"completed\":\"disconnect\"");
    failed |= run_case(argv[1], "silent peer timeout", NULL, silent, 1, 1, 0, "timeout");
    failed |= run_case(argv[1], "wrong identity sends no BLE commands", scan, wrong, 1, 1, 0, "\"error\":");
    failed |= run_case(argv[1], "scan ACK without completion", scan, ack_only, 3, 1, 0, "timeout");
    failed |= run_case(argv[1], "SIGTERM cancellation", NULL, silent, 1, 1, 1, "cancelled");
    if (!failed) puts("5 native production-client scenarios PASS; PTY only, NOT physical UART/RF acceptance");
    return failed;
}
