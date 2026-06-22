#pragma once

#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>

#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/config/values/ConfigValues.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include "globals.hpp"

using namespace Config::Values;

namespace HTConfig {

using SMonitorConfigValue =
    std::variant<Config::BOOL, Config::INTEGER, Config::FLOAT, std::string>;

struct SMonitorConfigOverride {
    std::unordered_map<std::string, SMonitorConfigValue> values;
};

inline std::unordered_map<std::string, SMonitorConfigOverride> monitor_overrides;

template<typename T>
inline T value(std::string config) {
    static std::unordered_map<std::string, CConfigValue<T>> cache;

    if (!cache.count(config)) {
        const CConfigValue<T> val("plugin:hyprtasking:" + config);
        cache[config] = val;
    }

    return *cache[config];
}

inline void clear_monitor_overrides() {
    monitor_overrides.clear();
}

inline void set_monitor_override(std::string selector, SMonitorConfigOverride override) {
    monitor_overrides[std::move(selector)] = std::move(override);
}

inline const SMonitorConfigOverride* monitor_override(
    const std::string& name,
    const std::string& description = ""
) {
    const auto by_name = monitor_overrides.find(name);
    if (by_name != monitor_overrides.end())
        return &by_name->second;

    if (description.empty())
        return nullptr;

    const auto by_desc = monitor_overrides.find("desc:" + description);
    if (by_desc != monitor_overrides.end())
        return &by_desc->second;

    return nullptr;
}

template<typename T>
inline std::optional<T> monitor_override_value(
    const SMonitorConfigOverride* override,
    const std::string& key
) {
    if (override == nullptr)
        return std::nullopt;

    const auto it = override->values.find(key);
    if (it == override->values.end())
        return std::nullopt;

    const auto& value = it->second;

    if constexpr (std::is_same_v<T, Config::BOOL>) {
        if (const auto* raw = std::get_if<Config::BOOL>(&value))
            return *raw;
        if (const auto* raw = std::get_if<Config::INTEGER>(&value))
            return *raw != 0;
        if (const auto* raw = std::get_if<Config::FLOAT>(&value))
            return *raw != 0.f;
        return std::nullopt;
    } else if constexpr (std::is_same_v<T, Config::INTEGER>) {
        if (const auto* raw = std::get_if<Config::BOOL>(&value))
            return *raw ? 1 : 0;
        if (const auto* raw = std::get_if<Config::INTEGER>(&value))
            return *raw;
        if (const auto* raw = std::get_if<Config::FLOAT>(&value))
            return static_cast<Config::INTEGER>(*raw);
        return std::nullopt;
    } else if constexpr (std::is_same_v<T, Config::FLOAT>) {
        if (const auto* raw = std::get_if<Config::BOOL>(&value))
            return *raw ? 1.f : 0.f;
        if (const auto* raw = std::get_if<Config::INTEGER>(&value))
            return static_cast<Config::FLOAT>(*raw);
        if (const auto* raw = std::get_if<Config::FLOAT>(&value))
            return *raw;
        return std::nullopt;
    } else if constexpr (std::is_same_v<T, Config::STRING> || std::is_same_v<T, std::string>) {
        if (const auto* raw = std::get_if<std::string>(&value))
            return *raw;
        return std::nullopt;
    } else {
        return std::nullopt;
    }
}

template<typename T>
inline T value_for_monitor(const PHLMONITOR monitor, const std::string& config) {
    if (monitor != nullptr) {
        const auto* override = monitor_override(monitor->m_name, monitor->m_description);
        if (const auto scoped = monitor_override_value<T>(override, config); scoped.has_value())
            return *scoped;
    }

    return value<T>(config);
}

} // namespace HTConfig
