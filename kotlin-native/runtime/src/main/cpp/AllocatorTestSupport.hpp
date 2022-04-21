/*
 * Copyright 2010-2022 JetBrains s.r.o. Use of this source code is governed by the Apache 2.0 license
 * that can be found in the LICENSE file.
 */

#pragma once

#include <mutex>
#include <optional>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "Utils.hpp"
#include "std_support/CStdlib.hpp"
#include "std_support/Map.hpp"

namespace kotlin::test_support {

class MockAllocatorBase : private Pinned {
public:
    MOCK_METHOD(void*, allocate, (std::size_t), (noexcept));
    MOCK_METHOD(void, deallocate, (void*, std::size_t), (noexcept));
};

class CountingAllocatorBase : private Pinned {
public:
    void* allocate(std::size_t size) noexcept {
        std::unique_lock guard(registryMutex_);
        void* ptr = std_support::malloc(size);
        registry_.insert({ptr, size});
        return ptr;
    }

    void deallocate(void* ptr, std::size_t size) noexcept {
        std::unique_lock guard(registryMutex_);
        registry_.erase(ptr);
        std_support::free(ptr);
    }

    std::size_t size() const noexcept { return registry_.size(); }

    std::optional<std::size_t> find(void* ptr) const noexcept {
        std::unique_lock guard(registryMutex_);
        auto it = registry_.find(ptr);
        if (it == registry_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

private:
    std_support::map<void*, std::size_t> registry_;
    mutable std::mutex registryMutex_;
};

template <typename T, typename Base>
struct Allocator {
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using propagate_on_container_move_assignment = std::true_type;
    using is_always_equal = std::false_type;

    explicit Allocator(Base& base) : base_(&base) {}

    Allocator(const Allocator&) noexcept = default;

    template <typename U>
    Allocator(const Allocator<U, Base>& other) noexcept : base_(other.base_) {}

    template <typename U>
    Allocator& operator=(const Allocator<U, Base>& other) noexcept {
        base_ = other.base_;
    }

    T* allocate(std::size_t n) noexcept { return static_cast<T*>(base_->allocate(sizeof(T) * n)); }

    void deallocate(T* p, std::size_t n) noexcept { base_->deallocate(p, sizeof(T) * n); }

    Base* base_;
};

template <typename T, typename Base>
auto MakeAllocator(Base& base) {
    return Allocator<T, Base>(base);
}

template <typename T, typename U, typename Base>
bool operator==(const Allocator<T, Base>& lhs, const Allocator<U, Base>& rhs) noexcept {
    return &lhs.base_ == &rhs.base_;
}

template <typename T, typename U, typename Base>
bool operator!=(const Allocator<T, Base>& lhs, const Allocator<U, Base>& rhs) noexcept {
    return !(lhs == rhs);
}

} // namespace kotlin::test_support
