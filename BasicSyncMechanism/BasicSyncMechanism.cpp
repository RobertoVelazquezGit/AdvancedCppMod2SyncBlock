// ============================================================================
// Basic Synchronization Mechanisms Lab - Solution Files
// Complete implementation of fundamental synchronization concepts
// ============================================================================

#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <queue>
#include <atomic>
#include <functional>

using namespace std;

// Shared resources that need synchronization
queue<int> sharedQueue;
mutex queueMutex;
condition_variable queueCondition;
atomic<bool> stopProcessing{ false };

// Thread-safe counter class demonstration
class ThreadSafeCounter {
private:
    int counter_;
    mutable mutex mtx_;  // mutable allows locking in const methods

public:
    ThreadSafeCounter() : counter_(0) {}

    // SOLUTION: Thread-safe increment using lock_guard
    void increment() {
        lock_guard<mutex> lock(mtx_);
        ++counter_;
    }

    // SOLUTION: Thread-safe getValue using lock_guard
    int getValue() const {
        lock_guard<mutex> lock(mtx_);
        return counter_;
    }
};

// Deadlock demonstration with two mutexes
mutex mutexA;
mutex mutexB;

void functionA() {
    // SOLUTION: Consistent lock ordering - always acquire mutexA first, then mutexB
    lock_guard<mutex> lockA(mutexA);
    lock_guard<mutex> lockB(mutexB);

    cout << "Function A acquired both locks" << endl;
    this_thread::sleep_for(chrono::milliseconds(100));

    // Locks automatically released by lock_guard destructors in reverse order
}

void functionB() {
    // SOLUTION: Same lock ordering as functionA - mutexA first, then mutexB
    lock_guard<mutex> lockA(mutexA);
    lock_guard<mutex> lockB(mutexB);

    cout << "Function B acquired both locks" << endl;
    this_thread::sleep_for(chrono::milliseconds(100));

    // Locks automatically released by lock_guard destructors in reverse order
}

// Producer function for producer-consumer pattern
void producer(int producerId) {
    for (int i = 1; i <= 5; ++i) {
        // SOLUTION: Thread-safe queue insertion with condition variable notification
        {
            lock_guard<mutex> lock(queueMutex);
            int item = producerId * 100 + i;
            sharedQueue.push(item);
        }  // Lock released here

        // Notify waiting consumers (must be outside the lock to avoid unnecessary blocking)
        queueCondition.notify_one();

        cout << "Producer " << producerId << " added item: " << (producerId * 100 + i) << endl;
        this_thread::sleep_for(chrono::milliseconds(200));
    }
}

// Consumer function for producer-consumer pattern
void consumer(int consumerId) {
    while (!stopProcessing.load()) {
        // SOLUTION: Thread-safe queue removal with condition variable
        unique_lock<mutex> lock(queueMutex);

        // Wait until queue has items or stop is requested
        queueCondition.wait(lock, []() {
            return !sharedQueue.empty() || stopProcessing.load();
            });

        // Check if we should stop and queue is empty
        if (stopProcessing.load() && sharedQueue.empty()) {
            break;
        }

        // Remove item from queue
        if (!sharedQueue.empty()) {
            int item = sharedQueue.front();
            sharedQueue.pop();
            lock.unlock();  // Release lock before processing

            cout << "Consumer " << consumerId << " processed item: " << item << endl;
            this_thread::sleep_for(chrono::milliseconds(300));
        }
    }
}

// Demonstration of different mutex types
void demonstrateMutexTypes() {
    cout << "\n=== Mutex Types Demonstration ===" << endl;

    // SOLUTION: Demonstrate recursive_mutex
    recursive_mutex recursiveMtx;

    // Function that locks recursively
    function<void(int)> recursiveLock = [&](int depth) {
        if (depth <= 0) return;

        // SOLUTION: Lock the recursive mutex - same thread can acquire multiple times
        lock_guard<recursive_mutex> lock(recursiveMtx);
        cout << "Recursive lock depth: " << depth << endl;

        recursiveLock(depth - 1);  // Recursive call - would deadlock with regular mutex

        // Lock automatically released by lock_guard destructor
        };

    thread recursiveThread([&]() {
        recursiveLock(3);  // This works with recursive_mutex but would deadlock with regular mutex
        });

    recursiveThread.join();

    cout << "- Recursive mutex demonstration completed successfully" << endl;
}

int main() {
    cout << "=== Basic Synchronization Mechanisms Lab ===" << endl;
    cout << "Learning mutex types, condition variables, and deadlock prevention" << endl;

    // Test 1: Thread-Safe Counter
    cout << "\n--- Test 1: Thread-Safe Counter ---" << endl;
    ThreadSafeCounter counter;
    vector<thread> counterThreads;

    // Create threads that increment the counter
    for (int i = 0; i < 5; ++i) {
        counterThreads.emplace_back([&counter]() {
            for (int j = 0; j < 10; ++j) {
                counter.increment();
                this_thread::sleep_for(chrono::milliseconds(10));
            }
            });
    }

    // Wait for all threads to complete
    for (auto& t : counterThreads) {
        t.join();
    }

    cout << "Final counter value: " << counter.getValue() << " (should be 50)" << endl;
    cout << "- Thread-safe counter test " << (counter.getValue() == 50 ? "PASSED" : "FAILED") << endl;

    // Test 2: Deadlock Prevention
    cout << "\n--- Test 2: Deadlock Prevention ---" << endl;
    cout << "Testing proper lock ordering..." << endl;

    thread threadA(functionA);
    thread threadB(functionB);

    threadA.join();
    threadB.join();
    cout << "- No deadlock occurred - proper lock ordering implemented!" << endl;

    // Test 3: Producer-Consumer Pattern
    cout << "\n--- Test 3: Producer-Consumer Pattern ---" << endl;

    vector<thread> producers;
    vector<thread> consumers;

    // Create producer threads
    for (int i = 1; i <= 2; ++i) {
        producers.emplace_back(producer, i);
    }

    // Create consumer threads
    for (int i = 1; i <= 2; ++i) {
        consumers.emplace_back(consumer, i);
    }

    // Wait for producers to finish
    for (auto& t : producers) {
        t.join();
    }

    // Give consumers time to process remaining items
    this_thread::sleep_for(chrono::seconds(2));

    // Stop consumers
    stopProcessing.store(true);
    queueCondition.notify_all();

    for (auto& t : consumers) {
        t.join();
    }

    cout << "- Producer-consumer pattern completed successfully" << endl;

    // Test 4: Mutex Types
    demonstrateMutexTypes();

    cout << "\n=== Lab Complete! ===" << endl;
    cout << "- Thread-safe counter using mutex and lock_guard" << endl;
    cout << "- Deadlock prevention using consistent lock ordering" << endl;
    cout << "- Producer-consumer coordination with condition variables" << endl;
    cout << "- Different mutex types (regular and recursive) demonstrated" << endl;
    cout << "\nYou've successfully implemented basic synchronization mechanisms!" << endl;

    return 0;
}

/*
VALIDATION CHECKLIST COMPLETED:
- Thread-safe counter implemented using mutex and lock_guard protection

- Consistent lock ordering prevents deadlocks in two-mutex scenario

- Producer-consumer pattern uses condition variables for efficient coordination

- Different mutex types demonstrated (regular mutex vs recursive_mutex)

- RAII principles applied with automatic lock management through lock_guard

- No race conditions in shared data access across all scenarios

- Proper condition variable usage with predicate checks and notifications

- Exception-safe synchronization using RAII lock management

EXPECTED OUTPUT EXAMPLE:
=== Basic Synchronization Mechanisms Lab ===
Learning mutex types, condition variables, and deadlock prevention

--- Test 1: Thread-Safe Counter ---
Final counter value: 50 (should be 50)
- Thread-safe counter test PASSED

--- Test 2: Deadlock Prevention ---
Testing proper lock ordering...
Function A acquired both locks
Function B acquired both locks
- No deadlock occurred - proper lock ordering implemented!

--- Test 3: Producer-Consumer Pattern ---
Producer 1 added item: 101
Producer 2 added item: 201
Consumer 1 processed item: 101
Consumer 2 processed item: 201
[...continued producer-consumer interaction...]
- Producer-consumer pattern completed successfully

=== Mutex Types Demonstration ===
Recursive lock depth: 3
Recursive lock depth: 2
Recursive lock depth: 1
- Recursive mutex demonstration completed successfully

=== Lab Complete! ===
- Thread-safe counter using mutex and lock_guard
- Deadlock prevention using consistent lock ordering
- Producer-consumer coordination with condition variables
- Different mutex types (regular and recursive) demonstrated

You've successfully implemented basic synchronization mechanisms!
*/