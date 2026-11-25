#pragma once

#include "Rice/Core.h"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <typeindex>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Rice {

// Scene-local slots stay compact independently of the IDs serialized in scene files.
// Each concrete component type owns a sparse set; moving a dense entry never moves
// the component itself, so references held by scripts and lifecycle callbacks survive.
template<class Base>
class ComponentStorage {
    static constexpr size_t Missing = (std::numeric_limits<size_t>::max)();

public:
    class Pool {
        friend class ComponentStorage;

        struct Entry {
            size_t slot;
            uint64_t insertion;
            Scope<Base> component;
        };

        ComponentStorage& storage;
        std::type_index type;
        std::vector<size_t> sparse;
        std::vector<Entry> dense;
        size_t iterationDepth = 0;
        size_t holes = 0;

        Pool(ComponentStorage& owner, std::type_index componentType)
            : storage(owner), type(componentType) {}

        Base* Find(size_t slot) const {
            if (slot >= sparse.size() || sparse[slot] == Missing) return nullptr;
            return dense[sparse[slot]].component.get();
        }

        Base* Representative() const {
            if (holes == dense.size()) return nullptr;
            for (const auto& entry : dense)
                if (entry.component) return entry.component.get();
            return nullptr;
        }

        void EraseDense(size_t index) noexcept {
            if (index != dense.size() - 1) {
                dense[index] = std::move(dense.back());
                const auto& moved = dense[index];
                if (moved.component) sparse[moved.slot] = index;
            }
            dense.pop_back();
        }

        void EndIteration() noexcept {
            if (--iterationDepth || !holes) return;
            for (size_t i = 0; i < dense.size();) {
                if (dense[i].component) ++i;
                else EraseDense(i);
            }
            holes = 0;
        }

        template<class T, class F>
        void Visit(uint64_t limit, F& visit) {
            // A pool is homogeneous. Check one live instance to recognize base
            // classes and interfaces, and skip unrelated pools in constant time
            // when there are no holes from a nested traversal.
            if (!dynamic_cast<T*>(Representative())) return;

            struct Iteration {
                Pool& pool;
                ~Iteration() { pool.EndIteration(); }
            };
            ++iterationDepth;
            Iteration guard{*this};
            const auto count = dense.size();
            for (size_t i = 0; i < count; ++i) {
                if (dense[i].component && dense[i].insertion <= limit)
                    visit(*dynamic_cast<T*>(dense[i].component.get()));
            }
        }

    public:
        Pool(const Pool&) = delete;
        Pool& operator=(const Pool&) = delete;

        void Insert(size_t slot, Scope<Base> value) {
            if (!value || std::type_index(typeid(*value)) != type)
                throw std::invalid_argument("Component type does not match its pool");
            if (slot >= storage.nextFree.size()) throw std::out_of_range("Invalid component slot");
            if (slot >= sparse.size()) sparse.resize(slot + 1, Missing);
            if (sparse[slot] != Missing) throw std::logic_error("Component slot already occupied");
            const bool firstLive = holes == dense.size();
            dense.push_back({slot, ++storage.lastInsertion, std::move(value)});
            sparse[slot] = dense.size() - 1;
            if (firstLive) storage.derivedMatches.clear();
        }

        Scope<Base> Take(size_t slot) noexcept {
            if (slot >= sparse.size() || sparse[slot] == Missing) return {};
            const size_t index = std::exchange(sparse[slot], Missing);
            auto value = std::move(dense[index].component);
            if (iterationDepth) ++holes;
            else EraseDense(index);
            return value;
        }
    };

private:
    std::vector<size_t> nextFree;
    size_t firstFree = Missing;
    uint64_t lastInsertion = 0;
    std::vector<Scope<Pool>> pools;
    std::unordered_map<std::type_index, Pool*> byType;
    mutable std::unordered_map<std::type_index, bool> derivedMatches;

public:
    ComponentStorage() = default;
    ComponentStorage(const ComponentStorage&) = delete;
    ComponentStorage& operator=(const ComponentStorage&) = delete;

    size_t AcquireSlot() {
        if (firstFree != Missing) {
            const auto slot = firstFree;
            firstFree = nextFree[slot];
            return slot;
        }
        nextFree.push_back(Missing);
        return nextFree.size() - 1;
    }

    void ReleaseSlot(size_t slot) noexcept {
        nextFree[slot] = firstFree;
        firstFree = slot;
    }

    template<class T>
    Pool& Assure() {
        const std::type_index type = typeid(T);
        auto found = byType.find(type);
        if (found != byType.end()) return *found->second;
        Scope<Pool> pool(new Pool(*this, type));
        auto& result = *pool;
        pools.push_back(std::move(pool));
        try {
            byType.emplace(type, &result);
        } catch (...) {
            pools.pop_back();
            throw;
        }
        return result;
    }

    template<class T>
    T* GetExact(size_t slot) const {
        auto found = byType.find(typeid(T));
        return found == byType.end() ? nullptr : dynamic_cast<T*>(found->second->Find(slot));
    }

    // An exact miss only needs the object's ordered polymorphic search when
    // some other concrete type could satisfy this query. A cached positive
    // remains conservative when its last instance disappears.
    template<class T>
    bool MayHaveDerived() const {
        if constexpr (std::is_final_v<T>) return false;
        if (pools.empty()) return false;
        const std::type_index requested = typeid(T);
        auto known = derivedMatches.find(requested);
        if (known != derivedMatches.end()) return known->second;
        bool match = false;
        for (const auto& pool : pools) {
            if (pool->type != requested && dynamic_cast<T*>(pool->Representative())) {
                match = true;
                break;
            }
        }
        derivedMatches.emplace(requested, match);
        return match;
    }

    template<class T, class F>
    void Each(F&& visit) {
        // The same cutoff applies to later pools. A callback may attach another
        // component (even at a just-recycled slot) or create a new pool.
        const auto limit = lastInsertion;
        if (!MayHaveDerived<T>()) {
            auto exact = byType.find(typeid(T));
            if (exact != byType.end()) exact->second->template Visit<T>(limit, visit);
            return;
        }
        const auto count = pools.size();
        for (size_t i = 0; i < count; ++i) pools[i]->template Visit<T>(limit, visit);
    }
};

}
