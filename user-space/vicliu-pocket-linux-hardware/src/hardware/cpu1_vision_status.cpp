#include "cpu1_vision_status.hpp"
#include "paths.hpp"
#include <charconv>
#include <cstdint>
#include <sstream>

namespace vpl::hardware {
namespace {
template <typename T> bool number(const State &fields, const char *key, T *output)
{
    const auto found = fields.find(key);
    if (found == fields.end() || found->second.empty())
        return false;
    const auto &text = found->second;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), *output);
    return result.ec == std::errc {} && result.ptr == text.data() + text.size();
}

bool parse(const std::string &text, State *fields)
{
    if (text.empty() || text.size() > 4096)
        return false;
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        const auto equal = line.find('=');
        if (equal == std::string::npos || !equal || equal + 1 == line.size() ||
            !fields->emplace(line.substr(0, equal), line.substr(equal + 1)).second)
            return false;
    }
    if ((*fields)["status_version"] != "1" || (*fields)["resource_owner"] != "cpu1" ||
        (*fields)["ownership_contract"] != "2")
        return false;
    const auto &owner = (*fields)["ownership_state"];
    const auto &vision = (*fields)["vision_state"];
    if (owner != "ready" && owner != "pending" && owner != "stale" &&
        owner != "fault" && owner != "unavailable")
        return false;
    if (vision != "idle" && vision != "starting" && vision != "running" &&
        vision != "stopping" && vision != "pending" && vision != "stale" &&
        vision != "fault" && vision != "protocol-error")
        return false;
    int owner_error, vision_error;
    unsigned int reader;
    std::uint64_t counter;
    if (!number(*fields, "ownership_error", &owner_error) || (owner == "ready" && owner_error) ||
        !number(*fields, "vision_error", &vision_error) || !number(*fields, "reader_open", &reader) || reader > 1)
        return false;
    for (const char *key : {"frames_delivered", "captured", "published", "dropped"})
        if (!number(*fields, key, &counter))
            return false;
    return true;
}

bool parse_ai_status(const std::string &text, State *fields)
{
    if (text.empty() || text.size() > 4096)
        return false;
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        const auto equal = line.find('=');
        if (equal == std::string::npos || !equal || equal + 1 == line.size() ||
            !fields->emplace(line.substr(0, equal), line.substr(equal + 1)).second)
            return false;
    }
    // This is the paired image's read-only ABI, not a claim inferred from a
    // camera endpoint, an old Linux model file, or an acceptance marker.
    if ((*fields)["ai_abi"] != "1" || (*fields)["backend"] != "cpu1-ai2d,fft,kpu-kws" ||
        (*fields)["kpu_jobs"] != "kws-reference" || (*fields)["fft_jobs"] != "available")
        return false;
    const auto &phase = (*fields)["state"];
    if (phase != "idle" && phase != "pending" && phase != "running" &&
        phase != "result" && phase != "fault")
        return false;
    int error, owner_error;
    unsigned int opened, pending, detached;
    std::uint64_t submitted, accepted, completed;
    if (!number(*fields, "error", &error) || error > 0 || error < -4095 ||
        !number(*fields, "owner_error", &owner_error) || owner_error > 0 || owner_error < -4095 ||
        !number(*fields, "client_open", &opened) || opened > 1 ||
        !number(*fields, "pending", &pending) || pending > 1 ||
        !number(*fields, "detached", &detached) || detached > 1 ||
        !number(*fields, "submitted", &submitted) || !number(*fields, "accepted", &accepted) ||
        !number(*fields, "completed", &completed) || completed > accepted || accepted > submitted)
        return false;
    if ((phase == "fault") != (error != 0) || (detached && (!pending || opened)) ||
        (phase == "idle" && (pending || submitted != completed)) ||
        ((phase == "running" || phase == "result") && !pending) ||
        (phase == "result" && completed != submitted))
        return false;
    return true;
}

void append_cpu1_ai_state(State *state, bool ownership_ready, const std::string &owner)
{
    const bool endpoint = paths::exists("/dev/tdvp-ai");
    const bool driver = paths::exists("/sys/class/misc/tdvp-ai/device/driver");
    State fields;
    const bool valid = endpoint && driver &&
        parse_ai_status(paths::read("/sys/class/misc/tdvp-ai/status"), &fields);
    const std::string phase = valid ? fields["state"] :
        (endpoint && driver ? "status-unavailable" : "unavailable");
    const bool available = ownership_ready && valid && fields["error"] == "0" &&
        fields["owner_error"] == "0" && phase != "pending" && phase != "fault";
    (*state)["cpu1_ai_status_valid"] = valid ? "1" : "0";
    (*state)["cpu1_ai_available"] = available ? "1" : "0";
    (*state)["cpu1_ai_state"] = phase;
    (*state)["cpu1_ai_active"] = available && phase == "running" ? "1" : "0";
    (*state)["cpu1_ai_abi"] = valid ? fields["ai_abi"] : "unknown";
    for (const char *key : {"backend", "kpu_jobs", "fft_jobs", "error", "owner_error",
            "client_open", "pending", "detached", "submitted", "accepted", "completed"})
        (*state)[std::string("cpu1_ai_") + key] = valid ? fields[key] : "unknown";
    (*state)["cpu1_ai_ai2d_available"] = available ? "1" : "0";
    (*state)["cpu1_ai_fft_available"] = available ? "1" : "0";

    (*state)["kpu_owner"] = "cpu1";
    (*state)["kpu_transport"] = endpoint ? "1" : "0";
    (*state)["kpu_driver"] = driver ? "1" : "0";
    (*state)["kpu_runtime"] = valid ? "cpu1-rtsmart-kws-reference" : "";
    (*state)["kpu_available"] = available ? "1" : "0";
    (*state)["kpu_reference_runtime_available"] = available ? "1" : "0";
    (*state)["kpu_acceptance"] = available ? "unverified" : "unavailable";
    (*state)["kpu_acceptance_state"] = available ? "cpu1-reference-unverified" :
        (!ownership_ready ? owner : phase == "idle" || phase == "running" || phase == "result"
            ? "owner-not-ready" : phase);
    // Shared AI job state does not identify which accelerator is currently
    // executing. Report that activity above, not as fabricated KPU activity.
    // Runtime counters are telemetry, never numerical/physical acceptance.
    // Linux GNNE/AI2D devices and the retired Linux acceptance service stay off.
    for (const char *key : {"kpu_functional", "kpu_active", "kpu_gnne_device", "kpu_ai2d_device",
            "kpu_kernel_ready", "kpu_acceptance_service_active", "kpu_acceptance_passed",
            "kpu_acceptance_skipped"})
        (*state)[key] = "0";
}
}

void append_cpu1_vision_state(State *state)
{
    if (!state)
        return;
    const bool endpoint = paths::exists("/dev/tdvp-vision");
    const bool driver = paths::exists("/sys/class/misc/tdvp-vision/device/driver");
    State fields;
    const bool valid = endpoint && driver && parse(paths::read("/sys/class/misc/tdvp-vision/status"), &fields);
    const bool initialized = valid && fields["ownership_state"] == "ready";
    const std::string owner = valid ? fields["ownership_state"] :
        (endpoint && driver ? "status-unavailable" : "unavailable");
    const std::string stream = valid ? fields["vision_state"] : "unavailable";
    const bool streaming_runtime = stream == "idle" || stream == "starting" || stream == "running" || stream == "stopping";
    const bool available = initialized && streaming_runtime && fields["vision_error"] == "0";
    (*state)["cpu1_resource_owner"] = "cpu1"; // configured policy; live ownership is separate
    (*state)["cpu1_resource_ownership_state"] = owner;
    (*state)["cpu1_ai_initialized"] = initialized ? "1" : "0";
    (*state)["cpu1_vision_state"] = stream;
    (*state)["cpu1_vision_error"] = valid ? fields["vision_error"] : "unknown";
    (*state)["camera_owner"] = "cpu1";
    (*state)["camera_transport"] = endpoint ? "1" : "0";
    (*state)["camera_driver"] = driver ? "1" : "0";
    (*state)["camera_runtime"] = initialized && streaming_runtime ? "1" : "0";
    (*state)["camera_available"] = available ? "1" : "0";
    (*state)["camera_stream_running"] = available && stream == "running" && fields["reader_open"] == "1" ? "1" : "0";
    for (const char *key : {"frames_delivered", "captured", "published", "dropped"})
        (*state)[std::string("camera_") + key] = valid ? fields[key] : "0";
    // Delivered records are useful telemetry, not an image-quality/physical
    // sensor acceptance result. Old camera.pass cannot certify this new path.
    (*state)["camera_functional"] = "0";
    (*state)["camera_active"] = "0";
    (*state)["camera_acceptance"] = available ? "unverified" : "unavailable";

    append_cpu1_ai_state(state, initialized, owner);
}
}
