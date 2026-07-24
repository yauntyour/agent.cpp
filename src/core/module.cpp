#include "core/module.hpp"

namespace agent {

ModuleRegistry& ModuleRegistry::instance() {
    static ModuleRegistry registry;
    return registry;
}

IModule* ModuleRegistry::get_by_name(std::string_view name) {
    std::shared_lock lock(m_mutex);
    for (auto& [type, module] : m_modules) {
        if (module->name() == name) return module.get();
    }
    return nullptr;
}

void ModuleRegistry::initialize_all() {
    std::shared_lock lock(m_mutex);
    for (auto& [type, module] : m_modules) {
        if (module->state() == ModuleState::Uninitialized) {
            module->initialize();
        }
    }
}

void ModuleRegistry::shutdown_all() {
    std::shared_lock lock(m_mutex);
    for (auto& [type, module] : m_modules) {
        if (module->state() == ModuleState::Active) {
            try {
                module->shutdown();
            } catch (const std::exception& e) {
                if (!g_is_shutting_down.load()) {
                    try { LOG_ERROR("Module", std::string("Shutdown error in ") + module->name().data() + ": " + e.what()); } catch (...) {}
                }
            } catch (...) {
                if (!g_is_shutting_down.load()) {
                    try { LOG_ERROR("Module", std::string("Unknown shutdown error in ") + module->name().data()); } catch (...) {}
                }
            }
        }
    }
}

std::vector<IModule*> ModuleRegistry::all_modules() {
    std::shared_lock lock(m_mutex);
    std::vector<IModule*> result;
    result.reserve(m_modules.size());
    for (auto& [type, module] : m_modules) {
        result.push_back(module.get());
    }
    return result;
}

} // namespace agent
