#include "bluetooth.hpp"

#include "paths.hpp"

#include <dbus/dbus.h>

#include <chrono>
#include <cstring>
#include <string>
#include <thread>

namespace vpl::hardware {
namespace {

constexpr char kBluezService[] = "org.bluez";
constexpr char kAdapterInterface[] = "org.bluez.Adapter1";
constexpr char kPropertiesInterface[] = "org.freedesktop.DBus.Properties";
constexpr char kObjectManagerInterface[] = "org.freedesktop.DBus.ObjectManager";
constexpr char kPoweredProperty[] = "Powered";
constexpr int kDbusTimeoutMs = 350;

struct AdapterState {
    bool found = false;
    bool powered = false;
    std::string path;
};

void put(State *state, const std::string &key, bool value)
{
    (*state)[key] = value ? "1" : "0";
}

bool acceptance_passed(const std::string &name)
{
    return paths::exists("/run/vicliu-pocket-linux-hardware/acceptance/" + name + ".pass");
}

DBusConnection *open_system_bus()
{
    DBusError error;
    dbus_error_init(&error);
    DBusConnection *const connection = dbus_bus_get_private(DBUS_BUS_SYSTEM, &error);
    if (dbus_error_is_set(&error))
        dbus_error_free(&error);
    if (connection != nullptr)
        dbus_connection_set_exit_on_disconnect(connection, false);
    return connection;
}

void close_system_bus(DBusConnection *connection)
{
    if (connection == nullptr)
        return;
    dbus_connection_close(connection);
    dbus_connection_unref(connection);
}

bool read_adapter_properties(DBusMessageIter *properties, AdapterState *adapter)
{
    if (properties == nullptr || adapter == nullptr ||
        dbus_message_iter_get_arg_type(properties) != DBUS_TYPE_ARRAY) {
        return false;
    }
    DBusMessageIter entries;
    dbus_message_iter_recurse(properties, &entries);
    while (dbus_message_iter_get_arg_type(&entries) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter entry;
        dbus_message_iter_recurse(&entries, &entry);
        if (dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_STRING) {
            dbus_message_iter_next(&entries);
            continue;
        }
        const char *name = nullptr;
        dbus_message_iter_get_basic(&entry, &name);
        if (!dbus_message_iter_next(&entry) ||
            dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_VARIANT ||
            name == nullptr || std::strcmp(name, kPoweredProperty) != 0) {
            dbus_message_iter_next(&entries);
            continue;
        }
        DBusMessageIter value;
        dbus_message_iter_recurse(&entry, &value);
        if (dbus_message_iter_get_arg_type(&value) == DBUS_TYPE_BOOLEAN) {
            dbus_bool_t powered = false;
            dbus_message_iter_get_basic(&value, &powered);
            adapter->powered = powered != 0;
            return true;
        }
        dbus_message_iter_next(&entries);
    }
    return false;
}

bool query_adapter(AdapterState *adapter)
{
    if (adapter == nullptr)
        return false;
    *adapter = {};
    DBusConnection *const connection = open_system_bus();
    if (connection == nullptr)
        return false;

    DBusMessage *const request = dbus_message_new_method_call(
        kBluezService, "/", kObjectManagerInterface, "GetManagedObjects");
    if (request == nullptr) {
        close_system_bus(connection);
        return false;
    }
    DBusError error;
    dbus_error_init(&error);
    DBusMessage *const reply = dbus_connection_send_with_reply_and_block(
        connection, request, kDbusTimeoutMs, &error);
    dbus_message_unref(request);
    if (dbus_error_is_set(&error))
        dbus_error_free(&error);
    if (reply == nullptr) {
        close_system_bus(connection);
        return false;
    }

    bool found = false;
    DBusMessageIter root;
    if (dbus_message_iter_init(reply, &root) &&
        dbus_message_iter_get_arg_type(&root) == DBUS_TYPE_ARRAY) {
        DBusMessageIter objects;
        dbus_message_iter_recurse(&root, &objects);
        while (!found && dbus_message_iter_get_arg_type(&objects) == DBUS_TYPE_DICT_ENTRY) {
            DBusMessageIter object;
            dbus_message_iter_recurse(&objects, &object);
            if (dbus_message_iter_get_arg_type(&object) != DBUS_TYPE_OBJECT_PATH) {
                dbus_message_iter_next(&objects);
                continue;
            }
            const char *path = nullptr;
            dbus_message_iter_get_basic(&object, &path);
            if (!dbus_message_iter_next(&object) ||
                dbus_message_iter_get_arg_type(&object) != DBUS_TYPE_ARRAY || path == nullptr) {
                dbus_message_iter_next(&objects);
                continue;
            }
            DBusMessageIter interfaces;
            dbus_message_iter_recurse(&object, &interfaces);
            while (!found && dbus_message_iter_get_arg_type(&interfaces) == DBUS_TYPE_DICT_ENTRY) {
                DBusMessageIter interface_entry;
                dbus_message_iter_recurse(&interfaces, &interface_entry);
                if (dbus_message_iter_get_arg_type(&interface_entry) != DBUS_TYPE_STRING) {
                    dbus_message_iter_next(&interfaces);
                    continue;
                }
                const char *interface_name = nullptr;
                dbus_message_iter_get_basic(&interface_entry, &interface_name);
                if (!dbus_message_iter_next(&interface_entry) ||
                    dbus_message_iter_get_arg_type(&interface_entry) != DBUS_TYPE_ARRAY ||
                    interface_name == nullptr || std::strcmp(interface_name, kAdapterInterface) != 0) {
                    dbus_message_iter_next(&interfaces);
                    continue;
                }
                AdapterState candidate;
                candidate.path = path;
                if (read_adapter_properties(&interface_entry, &candidate)) {
                    candidate.found = true;
                    *adapter = candidate;
                    found = true;
                    break;
                }
                dbus_message_iter_next(&interfaces);
            }
            dbus_message_iter_next(&objects);
        }
    }
    dbus_message_unref(reply);
    close_system_bus(connection);
    return found;
}

bool set_adapter_power(const AdapterState &adapter, bool enabled)
{
    if (!adapter.found || adapter.path.empty())
        return false;
    DBusConnection *const connection = open_system_bus();
    if (connection == nullptr)
        return false;
    DBusMessage *const request = dbus_message_new_method_call(
        kBluezService, adapter.path.c_str(), kPropertiesInterface, "Set");
    if (request == nullptr) {
        close_system_bus(connection);
        return false;
    }
    DBusMessageIter arguments;
    dbus_message_iter_init_append(request, &arguments);
    const char *interface = kAdapterInterface;
    const char *property = kPoweredProperty;
    dbus_bool_t value = enabled ? true : false;
    DBusMessageIter variant;
    const bool appended = dbus_message_iter_append_basic(&arguments, DBUS_TYPE_STRING, &interface) &&
        dbus_message_iter_append_basic(&arguments, DBUS_TYPE_STRING, &property) &&
        dbus_message_iter_open_container(&arguments, DBUS_TYPE_VARIANT, DBUS_TYPE_BOOLEAN_AS_STRING,
                                         &variant) &&
        dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &value) &&
        dbus_message_iter_close_container(&arguments, &variant);
    if (!appended) {
        dbus_message_unref(request);
        close_system_bus(connection);
        return false;
    }
    DBusError error;
    dbus_error_init(&error);
    DBusMessage *const reply = dbus_connection_send_with_reply_and_block(
        connection, request, kDbusTimeoutMs, &error);
    dbus_message_unref(request);
    const bool ok = reply != nullptr && !dbus_error_is_set(&error) &&
        dbus_message_get_type(reply) != DBUS_MESSAGE_TYPE_ERROR;
    if (reply != nullptr)
        dbus_message_unref(reply);
    if (dbus_error_is_set(&error))
        dbus_error_free(&error);
    close_system_bus(connection);
    return ok;
}

}  // namespace

bool bluetooth_hci_present()
{
    for (const std::string &entry : paths::children("/sys/class/bluetooth")) {
        if (entry.rfind("hci", 0) == 0)
            return true;
    }
    return false;
}

void append_bluetooth_state(State *state)
{
    if (state == nullptr)
        return;
    const bool hci_present = bluetooth_hci_present();
    AdapterState adapter;
    const bool runtime_ready = hci_present && query_adapter(&adapter);
    const bool available = hci_present && runtime_ready;
    const bool functional = available && acceptance_passed("bluetooth");
    put(state, "bluetooth_transport", hci_present);
    put(state, "bluetooth_driver", hci_present);
    put(state, "bluetooth_runtime", runtime_ready);
    put(state, "bluetooth_functional", functional);
    put(state, "bluetooth_available", available);
    put(state, "bluetooth_active", adapter.powered);
    (*state)["bluetooth_acceptance"] = functional ? "passed" : (available ? "unverified" : "unavailable");
    put(state, "bluetooth_requested", adapter.powered);
    put(state, "bluetooth_enabled", adapter.powered);
    put(state, "bluetooth_control_available", available);
}

bool ensure_bluetooth_service()
{
    if (!bluetooth_hci_present())
        return false;
    AdapterState adapter;
    if (query_adapter(&adapter))
        return true;
    if (paths::run_quietly({"/bin/systemctl", "start", "bluetooth.service"}) != 0)
        return false;
    for (int attempt = 0; attempt < 5; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (query_adapter(&adapter))
            return true;
    }
    return false;
}

bool set_bluetooth_power(bool enabled)
{
    if (!ensure_bluetooth_service())
        return false;
    AdapterState adapter;
    if (!query_adapter(&adapter) || !set_adapter_power(adapter, enabled))
        return false;
    AdapterState readback;
    return query_adapter(&readback) && readback.powered == enabled;
}

}  // namespace vpl::hardware
