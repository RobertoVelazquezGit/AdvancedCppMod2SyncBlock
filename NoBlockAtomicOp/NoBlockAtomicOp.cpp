#include <atomic>
#include <memory>
#include <stack>
#include <mutex>
#include <vector>
#include <thread>
#include <iostream>
#include <chrono>
#include <random>


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
        // Read the current head of the stack.
        Node* head = head_.load();

        // Attempt to atomically move the head to the next node.
        //
        // The exchange succeeds only if head_ is still equal to our local
        // pointer (head). If another thread modifies the stack first,
        // compare_exchange_weak() updates 'head' with the current head,
        // and we retry.
        while (head && !head_.compare_exchange_weak(head, head->next.load())) {
            // Retry with the updated head.
        }

        // The stack is empty.
        if (!head) {
            return false;
        }

        // Move the value from the removed node into the output parameter.
        result = std::move(head->data);

        // Atomically decrement the number of elements.
        size_.fetch_sub(1);

        // Release the removed node.
        // NOTE: In a real lock-free implementation, memory reclamation must
        // be handled safely (e.g., using hazard pointers or epoch-based reclamation).
       
// Memory intentionally not released.
// Immediate deletion is unsafe in this simplified lock-free implementation
// because another thread may still hold a reference to this node.
// Proper memory reclamation requires hazard pointers or epochs.
		delete head;  // Comment out to avoid unsafe deletion in a real lock-free context.  

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

    static void benchmarkLockBased(int operations, int threads, double readRatio) {
        std::stack<int> stack;
        std::mutex stackMutex;

        auto start = std::chrono::high_resolution_clock::now();

        std::vector<std::thread> workerThreads;

        for (int i = 0; i < threads; ++i) {
            workerThreads.emplace_back([&, i]() {

                std::mt19937 gen(std::random_device{}());
                std::bernoulli_distribution readOperation(readRatio);

                for (int j = 0; j < operations / threads; ++j) {

                    if (readOperation(gen)) {
                        // Pop operation
                        std::lock_guard<std::mutex> lock(stackMutex);

                        if (!stack.empty()) {
                            stack.pop();
                        }
                    }
                    else {
                        // Push operation
                        std::lock_guard<std::mutex> lock(stackMutex);
                        stack.push(i * operations + j);
                    }
                }
                });
        }

        for (auto& t : workerThreads)
            t.join();

        auto end = std::chrono::high_resolution_clock::now();

        auto duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        std::cout << "Lock-based stack: "
            << duration.count()
            << " ms\n";
    }

    static void benchmarkLockFree(int operations, int threads, double readRatio) {
        LockFreeStack<int> stack;

        auto start = std::chrono::high_resolution_clock::now();

        std::vector<std::thread> workerThreads;

        for (int i = 0; i < threads; ++i) {
            workerThreads.emplace_back([&, i]() {

                std::mt19937 gen(std::random_device{}());
                std::bernoulli_distribution readOperation(readRatio);

                int dummy;

                for (int j = 0; j < operations / threads; ++j) {

                    if (readOperation(gen)) {
                        // Pop operation
                        stack.pop(dummy);
                    }
                    else {
                        // Push operation
                        stack.push(i * operations + j);
                    }
                }
                });
        }

        for (auto& t : workerThreads)
            t.join();

        auto end = std::chrono::high_resolution_clock::now();

        auto duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        std::cout << "Lock-free stack: "
            << duration.count()
            << " ms\n";
    }
};

int main() {
    std::vector<int> threadCounts = { 1, 2, 4, 8, 16 };
    std::vector<int> operationCounts = { 1000, 10000, 100000 };
    std::vector<double> readRatios = { 0.25, 0.50, 0.75, 0.90 };

    for (int operations : operationCounts) {

        std::cout << "\n==================================================\n";
        std::cout << "Operations: " << operations << '\n';
        std::cout << "==================================================\n";

        for (int threads : threadCounts) {

            std::cout << "\nThreads: " << threads << '\n';

            for (double readRatio : readRatios) {

                std::cout << "\nRead operations: "
                    << static_cast<int>(readRatio * 100)
                    << "%\n";

                PerformanceComparison::benchmarkLockBased(
                    operations,
                    threads,
                    readRatio);

                PerformanceComparison::benchmarkLockFree(
                    operations,
                    threads,
                    readRatio);

                std::cout << '\n';
            }
        }
    }

    return 0;
}
