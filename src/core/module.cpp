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
            module->shutdown();
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
