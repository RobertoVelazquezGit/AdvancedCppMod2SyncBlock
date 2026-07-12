#include <atomic>
#include <memory>
#include <stack>
#include <mutex>
#include <vector>
#include <thread>
#include <iostream>
#include <chrono>


template<typename T>
class LockFreeStack {
private:
    struct Node {
        T data;
        std::atomic<Node*> next;

        Node(T item) : data(std::move(item)), next(nullptr) {}
    };

    std::atomic<Node*> head_;
    std::atomic<size_t> size_{ 0 };

public:
    LockFreeStack() : head_(nullptr) {}

    ~LockFreeStack() {
        Node* node = head_.load();
        while (node) {
            Node* next_node = node->next;
            delete node;
            head_.store(next_node);
            node = next_node;

        }
    }

    void push(T item) {
        // Allocate a new node that will be inserted into the stack.
        Node* newNode = new Node(std::move(item));

        // Read the current head of the stack.
        // This is our expected value for the compare-and-exchange operation.
        Node* oldHead = head_.load();

        do {
            // Link the new node to what we currently believe is the stack's head.
            //
            // If another thread changes the head before we complete the insertion,
            // compare_exchange_weak() will update oldHead with the new value.
            // On the next iteration we must reconnect newNode->next to that
            // updated head before trying again.
            newNode->next = oldHead;

        } while (
            // Atomically replace head_ with newNode only if head_ is still equal
            // to oldHead. If another thread has modified the stack, the operation
            // fails, oldHead is updated with the current head, and we retry.
            !head_.compare_exchange_weak(oldHead, newNode)
            );

        // The new node has been successfully inserted.
        size_.fetch_add(1);
    }

    bool pop(T& result) {
        Node* head = head_.load();

        while (head && !head_.compare_exchange_weak(head, head->next.load())) {
            // Retry with updated head
        }

        if (!head) {
            return false;
        }

        result = std::move(head->data);
        size_.fetch_sub(1);

        // Safe deletion - in production, use hazard pointers or epochs
        delete head;
        return true;
    }

    bool empty() const {
        return head_.load() == nullptr;
    }

    size_t size() const {
        return size_.load();
    }
};

class PerformanceComparison {
public:
    static void benchmarkLockBased(int operations, int threads) {
        std::stack<int> stack;
        std::mutex stackMutex;

        auto start = std::chrono::high_resolution_clock::now();

        std::vector<std::thread> workerThreads;
        for (int i = 0; i < threads; ++i) {
            workerThreads.emplace_back([&, i]() {
                for (int j = 0; j < operations / threads; ++j) {
                    {
                        std::lock_guard<std::mutex> lock(stackMutex);
                        stack.push(i * 1000 + j);
                    }

                    {
                        std::lock_guard<std::mutex> lock(stackMutex);
                        if (!stack.empty()) {
                            stack.pop();
                        }
                    }
                }
                });
        }

        for (auto& t : workerThreads) {
            t.join();
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        std::cout << "Lock-based stack: " << duration.count() << " ms" << std::endl;
    }

    static void benchmarkLockFree(int operations, int threads) {
        LockFreeStack<int> stack;

        auto start = std::chrono::high_resolution_clock::now();

        std::vector<std::thread> workerThreads;
        for (int i = 0; i < threads; ++i) {
            workerThreads.emplace_back([&, i]() {
                for (int j = 0; j < operations / threads; ++j) {
                    stack.push(i * 1000 + j);

                    int dummy;
                    stack.pop(dummy);
                }
                });
        }

        for (auto& t : workerThreads) {
            t.join();
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        std::cout << "Lock-free stack: " << duration.count() << " ms" << std::endl;
    }
};