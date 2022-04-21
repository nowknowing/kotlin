/*
 * Copyright 2010-2020 JetBrains s.r.o. Use of this source code is governed by the Apache 2.0 license
 * that can be found in the LICENSE file.
 */

#include "MultiSourceQueue.hpp"

#include <atomic>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "AllocatorTestSupport.hpp"
#include "ScopedThread.hpp"
#include "TestSupport.hpp"
#include "Types.h"

using namespace kotlin;

namespace {

template <typename T, typename Mutex>
KStdVector<T> Collect(MultiSourceQueue<T, Mutex>& queue) {
    KStdVector<T> result;
    for (const auto& element : queue.LockForIter()) {
        result.push_back(element);
    }
    return result;
}

} // namespace

using IntQueue = MultiSourceQueue<int, SpinLock<MutexThreadStateHandling::kIgnore>>;

TEST(MultiSourceQueueTest, Insert) {
    IntQueue queue;
    IntQueue::Producer producer(queue);

    constexpr int kFirst = 1;
    constexpr int kSecond = 2;

    auto* node1 = producer.Insert(kFirst);
    auto* node2 = producer.Insert(kSecond);

    EXPECT_THAT(**node1, kFirst);
    EXPECT_THAT(**node2, kSecond);
}

TEST(MultiSourceQueueTest, EraseFromTheSameProducer) {
    IntQueue queue;
    IntQueue::Producer producer(queue);

    constexpr int kFirst = 1;
    constexpr int kSecond = 2;

    producer.Insert(kFirst);
    auto* node2 = producer.Insert(kSecond);
    producer.Erase(node2);
    producer.Publish();

    auto actual = Collect(queue);
    EXPECT_THAT(actual, testing::ElementsAre(kFirst));
}

TEST(MultiSourceQueueTest, EraseFromGlobal) {
    IntQueue queue;
    IntQueue::Producer producer(queue);

    constexpr int kFirst = 1;
    constexpr int kSecond = 2;

    producer.Insert(kFirst);
    auto* node2 = producer.Insert(kSecond);
    producer.Publish();
    producer.Erase(node2);
    producer.Publish();

    auto actual1 = Collect(queue);
    EXPECT_THAT(actual1, testing::ElementsAre(kFirst, kSecond));

    queue.ApplyDeletions();

    auto actual2 = Collect(queue);
    EXPECT_THAT(actual2, testing::ElementsAre(kFirst));
}

TEST(MultiSourceQueueTest, EraseFromOtherProducer) {
    IntQueue queue;
    IntQueue::Producer producer1(queue);
    IntQueue::Producer producer2(queue);

    constexpr int kFirst = 1;
    constexpr int kSecond = 2;

    producer1.Insert(kFirst);
    auto* node2 = producer1.Insert(kSecond);
    producer2.Erase(node2);
    producer1.Publish();

    auto actual1 = Collect(queue);
    EXPECT_THAT(actual1, testing::ElementsAre(kFirst, kSecond));

    queue.ApplyDeletions();

    auto actual2 = Collect(queue);
    EXPECT_THAT(actual2, testing::ElementsAre(kFirst, kSecond));

    producer2.Publish();

    auto actual3 = Collect(queue);
    EXPECT_THAT(actual3, testing::ElementsAre(kFirst, kSecond));

    queue.ApplyDeletions();

    auto actual4 = Collect(queue);
    EXPECT_THAT(actual4, testing::ElementsAre(kFirst));
}

TEST(MultiSourceQueueTest, Empty) {
    IntQueue queue;

    auto actual = Collect(queue);
    EXPECT_THAT(actual, testing::IsEmpty());
}

TEST(MultiSourceQueueTest, DoNotPublish) {
    IntQueue queue;
    IntQueue::Producer producer(queue);

    producer.Insert(1);
    producer.Insert(2);

    auto actual = Collect(queue);
    EXPECT_THAT(actual, testing::IsEmpty());
}

TEST(MultiSourceQueueTest, Publish) {
    IntQueue queue;
    IntQueue::Producer producer1(queue);
    IntQueue::Producer producer2(queue);

    producer1.Insert(1);
    producer1.Insert(2);
    producer2.Insert(10);
    producer2.Insert(20);

    producer1.Publish();
    producer2.Publish();

    auto actual = Collect(queue);
    EXPECT_THAT(actual, testing::ElementsAre(1, 2, 10, 20));
}

TEST(MultiSourceQueueTest, PublishSeveralTimes) {
    IntQueue queue;
    IntQueue::Producer producer(queue);

    // Add 2 elements and publish.
    producer.Insert(1);
    producer.Insert(2);
    producer.Publish();

    // Add another element and publish.
    producer.Insert(3);
    producer.Publish();

    // Publish without adding elements.
    producer.Publish();

    // Add yet another two elements and publish.
    producer.Insert(4);
    producer.Insert(5);
    producer.Publish();

    auto actual = Collect(queue);
    EXPECT_THAT(actual, testing::ElementsAre(1, 2, 3, 4, 5));
}

TEST(MultiSourceQueueTest, PublishInDestructor) {
    IntQueue queue;

    {
        IntQueue::Producer producer(queue);
        producer.Insert(1);
        producer.Insert(2);
    }

    auto actual = Collect(queue);
    EXPECT_THAT(actual, testing::ElementsAre(1, 2));
}

TEST(MultiSourceQueueTest, ConcurrentPublish) {
    IntQueue queue;
    constexpr int kThreadCount = kDefaultThreadCount;
    std::atomic<bool> canStart(false);
    std::atomic<int> readyCount(0);
    KStdVector<ScopedThread> threads;
    KStdVector<int> expected;

    for (int i = 0; i < kThreadCount; ++i) {
        expected.push_back(i);
        threads.emplace_back([i, &queue, &canStart, &readyCount]() {
            IntQueue::Producer producer(queue);
            producer.Insert(i);
            ++readyCount;
            while (!canStart) {
            }
            producer.Publish();
        });
    }

    while (readyCount < kThreadCount) {
    }
    canStart = true;
    threads.clear();

    auto actual = Collect(queue);
    EXPECT_THAT(actual, testing::UnorderedElementsAreArray(expected));
}

TEST(MultiSourceQueueTest, IterWhileConcurrentPublish) {
    IntQueue queue;
    constexpr int kStartCount = 50;
    constexpr int kThreadCount = kDefaultThreadCount;

    KStdVector<int> expectedBefore;
    KStdVector<int> expectedAfter;
    IntQueue::Producer producer(queue);
    for (int i = 0; i < kStartCount; ++i) {
        expectedBefore.push_back(i);
        expectedAfter.push_back(i);
        producer.Insert(i);
    }
    producer.Publish();

    std::atomic<bool> canStart(false);
    std::atomic<int> readyCount(0);
    std::atomic<int> startedCount(0);
    KStdVector<ScopedThread> threads;
    for (int i = 0; i < kThreadCount; ++i) {
        int j = i + kStartCount;
        expectedAfter.push_back(j);
        threads.emplace_back([j, &queue, &canStart, &startedCount, &readyCount]() {
            IntQueue::Producer producer(queue);
            producer.Insert(j);
            ++readyCount;
            while (!canStart) {
            }
            ++startedCount;
            producer.Publish();
        });
    }

    KStdVector<int> actualBefore;
    {
        auto iter = queue.LockForIter();
        while (readyCount < kThreadCount) {
        }
        canStart = true;
        while (startedCount < kThreadCount) {
        }

        for (int element : iter) {
            actualBefore.push_back(element);
        }
    }

    threads.clear();

    EXPECT_THAT(actualBefore, testing::ElementsAreArray(expectedBefore));

    auto actualAfter = Collect(queue);
    EXPECT_THAT(actualAfter, testing::UnorderedElementsAreArray(expectedAfter));
}

TEST(MultiSourceQueueTest, ConcurrentPublishAndApplyDeletions) {
    IntQueue queue;
    constexpr int kThreadCount = kDefaultThreadCount;

    std::atomic<bool> canStart(false);
    std::atomic<int> readyCount(0);
    std::atomic<int> startedCount(0);
    KStdVector<ScopedThread> threads;
    for (int i = 0; i < kThreadCount; ++i) {
        threads.emplace_back([&queue, i, &canStart, &readyCount, &startedCount]() {
            IntQueue::Producer producer(queue);
            auto* node = producer.Insert(i);
            producer.Publish();
            producer.Erase(node);
            ++readyCount;
            while (!canStart) {
            }
            ++startedCount;
            producer.Publish();
        });
    }

    while (readyCount < kThreadCount) {
    }
    canStart = true;
    while (startedCount < kThreadCount) {
    }

    queue.ApplyDeletions();

    threads.clear();

    // We do not know which elements were deleted at this point. Expecting not to crash by this point.

    // This must make the queue empty.
    queue.ApplyDeletions();

    auto actual = Collect(queue);
    EXPECT_THAT(actual, testing::IsEmpty());
}

TEST(MultiSourceQueueTest, CustomAllocator) {
    test_support::CountingAllocatorBase allocator;
    auto a = test_support::MakeAllocator<int>(allocator);

    using Queue = MultiSourceQueue<int, SpinLock<MutexThreadStateHandling::kIgnore>, decltype(a)>;
    Queue queue(a);
    Queue::Producer producer1(queue);
    Queue::Producer producer2(queue);

    auto* node11 = producer1.Insert(1);
    auto* node12 = producer1.Insert(2);
    auto* node21 = producer2.Insert(10);
    auto* node22 = producer2.Insert(20);
    auto* node23 = producer2.Insert(30);

    EXPECT_THAT(allocator.size(), 5);

    producer2.Erase(node22);

    EXPECT_THAT(allocator.size(), 4);

    producer1.Publish();
    producer2.Publish();

    EXPECT_THAT(allocator.size(), 4);

    producer1.Erase(node11);
    producer1.Erase(node23);
    producer2.Erase(node12);
    producer2.Erase(node21);

    EXPECT_THAT(allocator.size(), 8);

    producer1.Publish();
    producer2.Publish();

    EXPECT_THAT(allocator.size(), 8);

    queue.ApplyDeletions();

    EXPECT_THAT(allocator.size(), 0);
}
