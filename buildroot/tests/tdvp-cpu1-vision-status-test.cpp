#include "cpu1_vision_status.hpp"
#include "paths.hpp"
#include <cassert>
#include <iostream>
#include <map>
#include <set>

static std::set<std::string> files;
static std::string status;
namespace vpl::hardware::paths {
bool exists(const std::string &path) { return files.count(path) != 0; }
std::string read(const std::string &path) {
    assert(path == "/sys/class/misc/tdvp-vision/status");
    return status;
}
// No write/open/run stubs: inventory must not start a stream or touch a mailbox.
}
static std::string record(const std::string &owner, const std::string &stream)
{
    return "status_version=1\nresource_owner=cpu1\nownership_contract=2\nownership_state=" + owner +
        "\nownership_error=" + (owner == "ready" ? "0" : "-11") + "\nvision_state=" + stream +
        "\nvision_error=0\nreader_open=1\nframes_delivered=12\ncaptured=14\npublished=13\ndropped=1\n";
}
int main()
{
    using namespace vpl::hardware;
    State state;
    append_cpu1_vision_state(nullptr);
    files = {"/dev/k230-gnne", "/dev/k230-ai2d", "/root/app/ai2d_kpu/ai2d_kpu.elf",
        "/run/vicliu-pocket-linux-hardware/kpu-acceptance.pass",
        "/run/vicliu-pocket-linux-hardware/acceptance/camera.pass"};
    append_cpu1_vision_state(&state);
    assert(state["camera_available"] == "0" && state["kpu_available"] == "0");
    assert(state["kpu_acceptance_passed"] == "0" && state["camera_functional"] == "0");
    files.insert("/dev/tdvp-vision");
    files.insert("/sys/class/misc/tdvp-vision/device/driver");
    for (const char *owner : {"pending", "stale", "fault", "unavailable"}) {
        status = record(owner, "running");
        append_cpu1_vision_state(&state);
        assert(state["camera_available"] == "0" && state["cpu1_ai_initialized"] == "0");
    }
    for (const char *stream : {"idle", "starting", "running", "stopping"}) {
        status = record("ready", stream);
        append_cpu1_vision_state(&state);
        assert(state["camera_available"] == "1" && state["cpu1_ai_initialized"] == "1");
        assert(state["camera_frames_delivered"] == "12" && state["camera_captured"] == "14");
        assert(state["camera_functional"] == "0" && state["kpu_available"] == "0");
        assert(state["kpu_acceptance_state"] == "cpu1-model-unconfigured");
    }
    for (const char *stream : {"pending", "fault", "stale", "protocol-error"}) {
        status = record("ready", stream);
        append_cpu1_vision_state(&state);
        assert(state["camera_available"] == "0" && state["camera_stream_running"] == "0");
    }
    const std::string good = record("ready", "running");
    for (const char *field : {"status_version=1", "resource_owner=cpu1", "ownership_contract=2",
            "ownership_state=ready", "ownership_error=0", "vision_state=running", "vision_error=0",
            "reader_open=1", "frames_delivered=12", "captured=14", "published=13", "dropped=1"}) {
        status = good;
        status.erase(status.find(field), std::string(field).size() + 1);
        append_cpu1_vision_state(&state);
        assert(state["camera_available"] == "0" && state["cpu1_ai_initialized"] == "0");
    }
    for (const char *extra : {"status_version=1\n", "ownership_state=fault\n", "captured=18446744073709551616\n"}) {
        status = good + extra;
        append_cpu1_vision_state(&state);
        assert(state["camera_available"] == "0");
    }
    for (const char *number : {"-1", "18446744073709551616", "3x", "0x12", "+2"}) {
        status = good;
        status.replace(status.find("captured=14"), 11, std::string("captured=") + number);
        append_cpu1_vision_state(&state);
        assert(state["camera_available"] == "0");
    }
    status = good;
    status.replace(status.find("vision_error=0"), 14, "vision_error=-5");
    append_cpu1_vision_state(&state);
    assert(state["camera_available"] == "0");
    status = good;
    files.erase("/sys/class/misc/tdvp-vision/device/driver");
    append_cpu1_vision_state(&state);
    assert(state["cpu1_ai_initialized"] == "0" && state["camera_available"] == "0");
    std::cout << "CPU1 hardware status: PASS real bridge schema, stale/fault/malformed data, old-marker refusal; no stream writes\n";
}
