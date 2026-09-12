#include "cpu1_vision_status.hpp"
#include "paths.hpp"
#include <cassert>
#include <iostream>
#include <map>
#include <set>

static std::set<std::string> files;
static std::string status;
static std::string ai_status;
namespace vpl::hardware::paths {
bool exists(const std::string &path) { return files.count(path) != 0; }
std::string read(const std::string &path) {
    if (path == "/sys/class/misc/tdvp-vision/status") return status;
    assert(path == "/sys/class/misc/tdvp-ai/status");
    return ai_status;
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
        assert(state["kpu_acceptance_state"] == "unavailable");
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

    files.insert("/sys/class/misc/tdvp-vision/device/driver");
    files.insert("/dev/tdvp-ai");
    files.insert("/sys/class/misc/tdvp-ai/device/driver");
    status = good;
    const std::string ai_good = "ai_abi=1\nbackend=cpu1-ai2d,fft,kpu-kws\nkpu_jobs=kws-reference\n"
        "fft_jobs=available\nstate=idle\nerror=0\nowner_error=0\nclient_open=0\npending=0\ndetached=0\n"
        "submitted=268\naccepted=268\ncompleted=268\nkpu_stage=7\nkpu_starts=2772\nkpu_completions=2772\n";
    ai_status = ai_good;
    append_cpu1_vision_state(&state);
    assert(state["kpu_available"] == "1" && state["kpu_runtime"] == "cpu1-rtsmart-kws-reference");
    assert(state["cpu1_ai_available"] == "1" && state["cpu1_ai_state"] == "idle");
    assert(state["cpu1_ai_completed"] == "268" && state["cpu1_ai_ai2d_available"] == "1");
    assert(state["cpu1_ai_fft_available"] == "1" && state["kpu_reference_runtime_available"] == "1");
    assert(state["kpu_acceptance"] == "unverified" && state["kpu_acceptance_state"] == "cpu1-reference-unverified");
    assert(state["kpu_acceptance_passed"] == "0" && state["kpu_functional"] == "0");
    assert(state["kpu_gnne_device"] == "0" && state["kpu_kernel_ready"] == "0");
    for (const char *owner : {"pending", "stale", "fault", "unavailable"}) {
        status = record(owner, "idle");
        append_cpu1_vision_state(&state);
        assert(state["cpu1_ai_status_valid"] == "1" && state["cpu1_ai_available"] == "0");
        assert(state["kpu_available"] == "0" && state["kpu_acceptance_state"] == owner);
    }
    status = good;
    for (const auto &[before, after] : std::map<std::string, std::string> {
            {"state=idle", "state=alien"}, {"ai_abi=1", "ai_abi=2"},
            {"backend=cpu1-ai2d,fft,kpu-kws", "backend=linux-gnne"},
            {"kpu_jobs=kws-reference", "kpu_jobs=unavailable"},
            {"fft_jobs=available", "fft_jobs=unavailable"},
            {"error=0", "error=5"}, {"owner_error=0", "owner_error=+1"},
            {"client_open=0", "client_open=2"}, {"pending=0", "pending=2"},
            {"detached=0", "detached=1"}, {"accepted=268", "accepted=269"},
            {"completed=268", "completed=269"}, {"submitted=268", "submitted=-1"},
            {"submitted=268", "submitted=18446744073709551616"},
            {"submitted=268", "submitted=268x"}, {"state=idle", "state=fault"},
            {"state=idle", "state=running"}, {"state=idle", "state=result"},
            {"error=0", "error=-5000"}, {"owner_error=0", "owner_error=-5000"}}) {
        ai_status = ai_good;
        ai_status.replace(ai_status.find(before), before.size(), after);
        append_cpu1_vision_state(&state);
        assert(state["cpu1_ai_status_valid"] == "0" && state["kpu_available"] == "0");
        assert(state["cpu1_ai_completed"] == "unknown" && state["cpu1_ai_active"] == "0");
        assert(state["camera_available"] == "1");
    }
    for (const char *field : {"ai_abi=1", "backend=cpu1-ai2d,fft,kpu-kws", "kpu_jobs=kws-reference",
            "fft_jobs=available", "state=idle", "error=0", "owner_error=0", "client_open=0",
            "pending=0", "detached=0", "submitted=268", "accepted=268", "completed=268"}) {
        ai_status = ai_good;
        ai_status.erase(ai_status.find(field), std::string(field).size() + 1);
        append_cpu1_vision_state(&state);
        assert(state["cpu1_ai_status_valid"] == "0" && state["kpu_available"] == "0");
        ai_status = ai_good + field + "\n";
        append_cpu1_vision_state(&state);
        assert(state["cpu1_ai_status_valid"] == "0" && state["kpu_available"] == "0");
    }
    for (const auto &bad : {std::string {}, std::string(4097, 'x'), ai_good + "malformed\n", ai_good + "empty=\n"}) {
        ai_status = bad;
        append_cpu1_vision_state(&state);
        assert(state["cpu1_ai_available"] == "0" && state["cpu1_ai_state"] == "status-unavailable");
    }
    for (const char *phase : {"pending", "running", "result", "fault"}) {
        ai_status = ai_good;
        ai_status.replace(ai_status.find("state=idle"), 10, std::string("state=") + phase);
        const bool active = std::string(phase) == "running";
        const bool has_result = std::string(phase) == "result";
        if (active || has_result) {
            ai_status.replace(ai_status.find("pending=0"), 9, "pending=1");
            ai_status.replace(ai_status.find("client_open=0"), 13, "client_open=1");
        }
        if (active) ai_status.replace(ai_status.find("completed=268"), 13, "completed=267");
        if (std::string(phase) == "fault") ai_status.replace(ai_status.find("error=0"), 7, "error=-5");
        append_cpu1_vision_state(&state);
        assert(state["cpu1_ai_status_valid"] == "1" && state["cpu1_ai_state"] == phase);
        assert(state["cpu1_ai_active"] == (active ? "1" : "0"));
        assert(state["kpu_available"] == (active || has_result ? "1" : "0"));
        assert(state["kpu_active"] == "0" && state["kpu_acceptance_passed"] == "0");
        if (active) {
            ai_status.replace(ai_status.find("client_open=1"), 13, "client_open=0");
            ai_status.replace(ai_status.find("detached=0"), 10, "detached=1");
            append_cpu1_vision_state(&state);
            assert(state["cpu1_ai_status_valid"] == "1" && state["cpu1_ai_detached"] == "1");
        }
    }
    ai_status = ai_good;
    ai_status.replace(ai_status.find("owner_error=0"), 13, "owner_error=-11");
    append_cpu1_vision_state(&state);
    assert(state["kpu_available"] == "0" && state["cpu1_ai_owner_error"] == "-11");
    for (const char *missing : {"/dev/tdvp-ai", "/sys/class/misc/tdvp-ai/device/driver",
            "/dev/tdvp-vision", "/sys/class/misc/tdvp-vision/device/driver"}) {
        ai_status = ai_good;
        files.erase(missing);
        append_cpu1_vision_state(&state);
        assert(state["kpu_available"] == "0");
        files.insert(missing);
    }
    ai_status = ai_good;
    append_cpu1_vision_state(&state);
    assert(state["kpu_available"] == "1" && state["cpu1_ai_error"] == "0");
    std::cout << "CPU1 AI status: PASS real endpoint/capability, busy/result/fault, fail-closed schema, recovery, telemetry != acceptance\n";
    std::cout << "CPU1 hardware status: PASS real bridge schema, stale/fault/malformed data, old-marker refusal; no stream writes\n";
}
