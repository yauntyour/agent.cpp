#pragma once
#include <string>
#include <string_view>
#include <memory>
#include <functional>
#include <typeindex>
#include <unordered_map>
#include <vector>
#include <optional>
#include <concepts>
#include <coroutine>
#include <atomic>
#include <mutex>
#include <shared_mutex>

namespace agent {

enum class ModuleState {
    Uninitialized,
    Initializing,
    Active,
    Suspended,
    ShuttingDown,
    Terminated
};

template<typename T>
concept ModuleComponent = requires(T m) {
    { m.name() } -> std::convertible_to<std::string_view>;
    { m.initialize() } -> std::same_as<void>;
    { m.shutdown() } -> std::same_as<void>;
    { m.state() } -> std::same_as<ModuleState>;
};

class IModule {
public:
    virtual ~IModule() = default;
    virtual std::string_view name() const = 0;
    virtual void initialize() = 0;
    virtual void shutdown() = 0;
    virtual ModuleState state() const = 0;

    bool is_active() const { return state() == ModuleState::Active; }
    bool is_initialized() const { return state() != ModuleState::Uninitialized; }
};

template<typename T>
class Module : public IModule {
public:
    Module() = default;
    ~Module() override { if (m_state != ModuleState::Terminated) shutdown(); }

    std::string_view name() const override { return static_name(); }

    void initialize() override {
        if (m_state != ModuleState::Uninitialized) return;
        m_state = ModuleState::Initializing;
        static_cast<T*>(this)->on_initialize();
        m_state = ModuleState::Active;
    }

    void shutdown() override {
        if (m_state == ModuleState::Terminated || m_state == ModuleState::ShuttingDown) return;
        m_state = ModuleState::ShuttingDown;
        static_cast<T*>(this)->on_shutdown();
        m_state = ModuleState::Terminated;
    }

    ModuleState state() const override { return m_state.load(); }

    static constexpr std::string_view static_name() { return T::static_name(); }

protected:
    void on_initialize() {}
    void on_shutdown() {}

private:
    std::atomic<ModuleState> m_state{ModuleState::Uninitialized};
};

class ModuleRegistry {
public:
    static ModuleRegistry& instance();

    template<ModuleComponent M>
    ModuleRegistry& register_module(std::unique_ptr<M> module) {
        std::unique_lock lock(m_mutex);
        m_modules[std::type_index(typeid(M))] = std::move(module);
        return *this;
    }

    template<typename T>
    T* get() {
        std::shared_lock lock(m_mutex);
        auto it = m_modules.find(std::type_index(typeid(T)));
        if (it == m_modules.end()) return nullptr;
        return static_cast<T*>(it->second.get());
    }

    template<typename T>
    T& require() {
        auto* ptr = get<T>();
        if (!ptr) throw std::runtime_error("Required module not registered: " + std::string(typeid(T).name()));
        return *ptr;
    }

    IModule* get_by_name(std::string_view name);

    void initialize_all();
    void shutdown_all();

    std::vector<IModule*> all_modules();

    template<typename T>
    bool has() {
        std::shared_lock lock(m_mutex);
        return m_modules.contains(std::type_index(typeid(T)));
    }

private:
    ModuleRegistry() = default;
    std::shared_mutex m_mutex;
    std::unordered_map<std::type_index, std::unique_ptr<IModule>> m_modules;
};

// ── RAII lifecycle guard ──────────────────────────────────────────
class ModuleLifecycle {
public:
    template<typename T>
    explicit ModuleLifecycle(T* module) {
        if (module && module->state() == ModuleState::Uninitialized) {
            module->initialize();
            m_module = module;
        }
    }

    ~ModuleLifecycle() {
        if (m_module && m_module->state() == ModuleState::Active) {
            m_module->shutdown();
        }
    }

    ModuleLifecycle(const ModuleLifecycle&) = delete;
    ModuleLifecycle& operator=(const ModuleLifecycle&) = delete;
    ModuleLifecycle(ModuleLifecycle&&) = delete;
    ModuleLifecycle& operator=(ModuleLifecycle&&) = delete;

private:
    IModule* m_module = nullptr;
};

// ── Lazy module access ────────────────────────────────────────────
template<typename T>
class LazyModule {
public:
    LazyModule() = default;

    T* operator->() {
        if (!m_ptr) {
            auto& registry = ModuleRegistry::instance();
            m_ptr = registry.get<T>();
            if (m_ptr && m_ptr->state() == ModuleState::Uninitialized) {
                m_ptr->initialize();
            }
        }
        return m_ptr;
    }

    operator bool() const {
        return ModuleRegistry::instance().has<T>();
    }

private:
    T* m_ptr = nullptr;
};

} // namespace agent
