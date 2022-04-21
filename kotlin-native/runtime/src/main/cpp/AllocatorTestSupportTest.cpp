/*
 * Copyright 2010-2022 JetBrains s.r.o. Use of this source code is governed by the Apache 2.0 license
 * that can be found in the LICENSE file.
 */

#include "AllocatorTestSupport.hpp"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using namespace kotlin;

namespace {

struct EmptyClass {};

struct Class {
    int32_t x;
};
static_assert(sizeof(Class) > sizeof(EmptyClass));

} // namespace

TEST(AllocatorTestSupportTest, MockAllocate) {
    testing::StrictMock<test_support::MockAllocatorBase> allocator;
    auto a = test_support::MakeAllocator<Class>(allocator);

    auto* expectedPtr = reinterpret_cast<Class*>(13);
    EXPECT_CALL(allocator, allocate(2 * sizeof(Class))).WillOnce(testing::Return(expectedPtr));
    auto* ptr = std::allocator_traits<decltype(a)>::allocate(a, 2);
    EXPECT_THAT(ptr, expectedPtr);
}

TEST(AllocatorTestSupportTest, MockDeallocate) {
    testing::StrictMock<test_support::MockAllocatorBase> allocator;
    auto a = test_support::MakeAllocator<Class>(allocator);

    auto* ptr = reinterpret_cast<Class*>(13);
    EXPECT_CALL(allocator, deallocate(ptr, 2 * sizeof(Class)));
    std::allocator_traits<decltype(a)>::deallocate(a, ptr, 2);
}

TEST(AllocatorTestSupportTest, MockAdjustType) {
    testing::StrictMock<test_support::MockAllocatorBase> allocator;
    auto initial = test_support::MakeAllocator<EmptyClass>(allocator);

    using Allocator = std::allocator_traits<decltype(initial)>::template rebind_alloc<Class>;
    using Traits = std::allocator_traits<decltype(initial)>::template rebind_traits<Class>;
    Allocator a = Allocator(initial);

    auto* expectedPtr = reinterpret_cast<Class*>(13);
    EXPECT_CALL(allocator, allocate(2 * sizeof(Class))).WillOnce(testing::Return(expectedPtr));
    auto* ptr = Traits::allocate(a, 2);
    EXPECT_THAT(ptr, expectedPtr);

    EXPECT_CALL(allocator, deallocate(ptr, 2 * sizeof(Class)));
    Traits::deallocate(a, ptr, 2);
}

TEST(AllocatorTestSupportTest, Counting) {
    test_support::CountingAllocatorBase allocator;
    auto a = test_support::MakeAllocator<Class>(allocator);

    auto* ptr1 = std::allocator_traits<decltype(a)>::allocate(a, 1);
    auto* ptr2 = std::allocator_traits<decltype(a)>::allocate(a, 2);

    using Allocator = std::allocator_traits<decltype(a)>::template rebind_alloc<EmptyClass>;
    using Traits = std::allocator_traits<decltype(a)>::template rebind_traits<EmptyClass>;
    Allocator b = Allocator(a);

    auto* ptr3 = Traits::allocate(b, 2);

    EXPECT_THAT(allocator.find(ptr1), std::make_optional(sizeof(Class)));
    EXPECT_THAT(allocator.find(ptr2), std::make_optional(2 * sizeof(Class)));
    EXPECT_THAT(allocator.find(ptr3), std::make_optional(2 * sizeof(EmptyClass)));

    std::allocator_traits<decltype(a)>::deallocate(a, ptr1, 1);
    std::allocator_traits<decltype(a)>::deallocate(a, ptr2, 2);
    Traits::deallocate(b, ptr3, 2);

    EXPECT_THAT(allocator.find(ptr1), std::nullopt);
    EXPECT_THAT(allocator.find(ptr2), std::nullopt);
    EXPECT_THAT(allocator.find(ptr3), std::nullopt);
}
