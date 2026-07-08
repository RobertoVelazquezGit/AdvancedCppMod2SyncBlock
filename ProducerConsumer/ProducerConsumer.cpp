#include <queue>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread> 

enum class Priority { LOW = 1, NORMAL = 2, HIGH = 3, CRITICAL = 4 };

struct Task {
    int id;
    Priority priority;
    std::string payload;
    std::chrono::steady_clock::time_point timestamp;

    Task(int id, Priority p, const std::string& data)
        : id(id), priority(p), payload(data),
        timestamp(std::chrono::steady_clock::now()) {
    }
};

struct TaskComparator {
    bool operator()(const Task& a, const Task& b) const {
        if (a.priority != b.priority) {
            return static_cast<int>(a.priority) < static_cast<int>(b.priority);
        }
        return a.timestamp > b.timestamp; // Earlier timestamp has higher priority
    }
};

class PriorityTaskQueue {
private:
    mutable std::mutex mutex_;
    std::priority_queue<Task, std::vector<Task>, TaskComparator> queue_;
    std::condition_variable condition_;
    std::atomic<bool> shutdown_{ false };
    std::atomic<int> waitingConsumers_{ 0 };

public:
    void push(const Task& task) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(task);
        condition_.notify_one();
    }

    bool pop(Task& task, std::chrono::milliseconds timeout = std::chrono::milliseconds(1000)) {
        std::unique_lock<std::mutex> lock(mutex_);
        waitingConsumers_.fetch_add(1);

        bool result = condition_.wait_for(lock, timeout, [this] {
            return !queue_.empty() || shutdown_.load();
            });

        waitingConsumers_.fetch_sub(1);

        if (!result || (shutdown_.load() && queue_.empty())) {
            return false;
        }

        task = queue_.top();
        queue_.pop();
        return true;
    }

    void shutdown() {
        shutdown_.store(true);
        condition_.notify_all();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    int getWaitingConsumers() const {
        return waitingConsumers_.load();
    }
};

class TaskProcessor {
private:
    int processorId_;
    std::atomic<int> processedTasks_{ 0 };

public:
    TaskProcessor(int id) : processorId_(id) {}

    void processTask(const Task& task) {
        auto processingTime = std::chrono::milliseconds(
            50 + static_cast<int>(task.priority) * 25);

        std::cout << "Processor " << processorId_
            << " processing task " << task.id
            << " (Priority: " << static_cast<int>(task.priority)
            << ")" << std::endl;

        std::this_thread::sleep_for(processingTime);
        processedTasks_.fetch_add(1);
    }

    int getProcessedCount() const {
        return processedTasks_.load();
    }
};