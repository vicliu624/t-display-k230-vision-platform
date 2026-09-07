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

    (*state)["kpu_owner"] = "cpu1";
    (*state)["kpu_transport"] = endpoint ? "1" : "0";
    (*state)["kpu_driver"] = initialized ? "1" : "0";
    (*state)["kpu_runtime"] = initialized ? "cpu1-rtsmart-no-model" : "";
    (*state)["kpu_acceptance"] = initialized ? "unverified" : "unavailable";
    (*state)["kpu_acceptance_state"] = initialized ? "cpu1-model-unconfigured" : owner;
    for (const char *key : {"kpu_available", "kpu_functional", "kpu_active", "kpu_gnne_device",
            "kpu_ai2d_device", "kpu_kernel_ready", "kpu_reference_runtime_available",
            "kpu_acceptance_service_active", "kpu_acceptance_passed", "kpu_acceptance_skipped"})
        (*state)[key] = "0";
}
}
