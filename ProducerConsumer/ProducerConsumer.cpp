//#include <queue>
//#include <condition_variable>
//#include <atomic>
//#include <chrono>
//#include <iostream>
//#include <thread> 

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <queue>
#include <random>
#include <string>
#include <thread>
#include <vector>

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
        // Earlier timestamp means higher priority (e.g. 10:00:00 before 10:00:05).
        return a.timestamp > b.timestamp; // Earlier timestamp has higher priority
    }
};

//class PriorityTaskQueue {
//private:
//    mutable std::mutex mutex_;
//    std::priority_queue<Task, std::vector<Task>, TaskComparator> queue_;
//    std::condition_variable condition_;
//    std::atomic<bool> shutdown_{ false };
//    std::atomic<int> waitingConsumers_{ 0 };
//
//public:
//    void push(const Task& task) {
//        std::lock_guard<std::mutex> lock(mutex_);
//        queue_.push(task);
//        condition_.notify_one();
//    }
//
//    bool pop(Task& task, std::chrono::milliseconds timeout = std::chrono::milliseconds(1000)) {
//        std::unique_lock<std::mutex> lock(mutex_);
//        waitingConsumers_.fetch_add(1);
//
//        bool result = condition_.wait_for(lock, timeout, [this] {
//            return !queue_.empty() || shutdown_.load();
//            });
//
//        waitingConsumers_.fetch_sub(1);
//
//        if (!result || (shutdown_.load() && queue_.empty())) {
//            return false;
//        }
//
//        task = queue_.top();
//        queue_.pop();
//        return true;
//    }
//
//    void shutdown() {
//        shutdown_.store(true);
//        condition_.notify_all();
//    }
//
//    size_t size() const {
//        std::lock_guard<std::mutex> lock(mutex_);
//        return queue_.size();
//    }
//
//    int getWaitingConsumers() const {
//        return waitingConsumers_.load();
//    }
//};

class PriorityTaskQueue {
private:
    mutable std::mutex mutex_;
    std::priority_queue<Task, std::vector<Task>, TaskComparator> queue_;
    std::condition_variable condition_;

    std::atomic<bool> shutdown_{ false };
    std::atomic<int> waitingConsumers_{ 0 };

public:

    void push(const Task& task)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push(task);
        }

        condition_.notify_all();
    }

    //-----------------------------------------------------------------
    // Consumer that accepts every task
    //-----------------------------------------------------------------
    bool pop(Task& task)
    {
        std::unique_lock<std::mutex> lock(mutex_);

        waitingConsumers_++;

        condition_.wait(lock, [this]
            {
                return shutdown_ || !queue_.empty();
            });

        waitingConsumers_--;

        if (shutdown_ && queue_.empty())
            return false;

        task = queue_.top();
        queue_.pop();

        return true;
    }

    //-----------------------------------------------------------------
    // Consumer that only accepts HIGH or CRITICAL tasks
    //-----------------------------------------------------------------
    bool popHighPriority(Task& task)
    {
        std::unique_lock<std::mutex> lock(mutex_);

        waitingConsumers_++;

        condition_.wait(lock, [this]
            {
                if (shutdown_)
                    return true;

                if (queue_.empty())
                    return false;

                return queue_.top().priority >= Priority::HIGH;
            });

        waitingConsumers_--;

        if (shutdown_)
            return false;

        task = queue_.top();
        queue_.pop();

        return true;
    }

    void shutdown()
    {
        shutdown_ = true;
        condition_.notify_all();
    }

    bool empty() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    size_t size() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    int waitingConsumers() const
    {
        return waitingConsumers_;
    }
};

//class TaskProcessor {
//private:
//    int processorId_;
//    std::atomic<int> processedTasks_{ 0 };
//
//public:
//    TaskProcessor(int id) : processorId_(id) {}
//
//    void processTask(const Task& task) {
//        auto processingTime = std::chrono::milliseconds(
//            50 + static_cast<int>(task.priority) * 25);
//
//        std::cout << "Processor " << processorId_
//            << " processing task " << task.id
//            << " (Priority: " << static_cast<int>(task.priority)
//            << ")" << std::endl;
//
//        std::this_thread::sleep_for(processingTime);
//        processedTasks_.fetch_add(1);
//    }
//
//    int getProcessedCount() const {
//        return processedTasks_.load();
//    }
//};

class TaskProcessor {
private:
    int processorId_;
    std::atomic<int> processedTasks_{ 0 };

public:
    TaskProcessor(int id)
        : processorId_(id)
    {
    }

    void processTask(const Task& task)
    {
        auto processingTime =
            std::chrono::milliseconds(
                50 + static_cast<int>(task.priority) * 25);

        std::cout
            << "Processor "
            << processorId_
            << " processing task "
            << task.id
            << " (Priority "
            << static_cast<int>(task.priority)
            << ")\n";

        std::this_thread::sleep_for(processingTime);

        processedTasks_++;
    }

    int getProcessedCount() const
    {
        return processedTasks_.load();
    }

    int getId() const
    {
        return processorId_;
    }
};

void producer(
    PriorityTaskQueue& queue,
    int producerId,
    int numTasks)
{
    std::random_device rd;
    std::mt19937 gen(rd());

    std::uniform_int_distribution<> priorityDist(1, 4);
    std::uniform_int_distribution<> sleepDist(50, 150);

    for (int i = 0; i < numTasks; i++)
    {
        Priority p =
            static_cast<Priority>(priorityDist(gen));

        Task task(
            producerId * 1000 + i,
            p,
            "Payload");

        std::cout
            << "[Producer "
            << producerId
            << "] generated task "
            << task.id
            << " priority "
            << static_cast<int>(p)
            << '\n';

        queue.push(task);

        std::this_thread::sleep_for(
            std::chrono::milliseconds(
                sleepDist(gen)));
    }
}

void consumer(
    PriorityTaskQueue& queue,
    TaskProcessor& processor)
{
    Task task(0, Priority::LOW, "");

    while (queue.pop(task))
    {
        processor.processTask(task);
    }

    std::cout
        << "Normal consumer finished\n";
}

void highPriorityConsumer(
    PriorityTaskQueue& queue,
    TaskProcessor& processor)
{
    Task task(0, Priority::LOW, "");

    while (queue.popHighPriority(task))
    {
        processor.processTask(task);
    }

    std::cout
        << "High priority consumer finished\n";
}

void monitor(
    PriorityTaskQueue& queue,
    std::atomic<bool>& stop)
{
    while (!stop)
    {
        std::cout
            << "\n------ Monitor ------\n"
            << "Queue size: "
            << queue.size()
            << '\n'
            << "Waiting consumers: "
            << queue.waitingConsumers()
            << "\n---------------------\n";

        std::this_thread::sleep_for(
            std::chrono::seconds(1));
    }

    std::cout
        << "Monitor stopped\n";
}