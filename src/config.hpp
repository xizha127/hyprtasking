#pragma once

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/config/values/ConfigValues.hpp>
#include <hyprutils/math/Vector2D.hpp>

#include "globals.hpp"

using namespace Config::Values;

namespace HTConfig {

template<typename T>
inline T value(std::string config) {
    static std::unordered_map<std::string, CConfigValue<T>> cache;

    if (!cache.count(config)) {
        const CConfigValue<T> val("plugin:hyprtasking:" + config);
        cache[config] = val;
    }

    return *cache[config];
}

template<typename T>
inline T value_for_monitor(const std::string& monitor_name, std::string config) {
    static std::unordered_map<std::string, CConfigValue<T>> cache;

    if (!monitor_name.empty()) {
        const std::string scoped = "plugin:hyprtasking:labels:monitors:" + monitor_name + ":" + config;
        if (!cache.count(scoped)) {
            const CConfigValue<T> val(scoped);
            cache[scoped] = val;
        }

        if (cache[scoped].good())
            return *cache[scoped];
    }

    return value<T>(config);
}

} // namespace HTConfig
