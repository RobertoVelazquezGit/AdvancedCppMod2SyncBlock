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
    // For std::priority_queue: if compare(a, b) is true,
    // 'b' has higher priority than 'a'.
    bool operator()(const Task& a, const Task& b) const {
        if (a.priority != b.priority) {
            return static_cast<int>(a.priority) < static_cast<int>(b.priority);
        }
        // Earlier timestamp means higher priority (e.g. 10:00:00 before 10:00:05).
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

    void push(const Task& task)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            // Store a copy of the task in the priority queue.
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
        // Gets copied to the object referenced by task
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
                return shutdown_ ||
                    (!queue_.empty() &&
                        queue_.top().priority >= Priority::HIGH);
            });

        waitingConsumers_--;

        if (shutdown_ && queue_.empty())
            return false;

        if (!queue_.empty() &&
            queue_.top().priority >= Priority::HIGH)
        {
            task = queue_.top();
            queue_.pop();
            return true;
        }

        return false;
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

void runScenario(
    int producersCount,
    int normalConsumersCount,
    int highConsumersCount,
    int tasksPerProducer)
{
    std::cout << "\n==============================\n";
    std::cout << "Starting scenario\n";
    std::cout << "==============================\n";

    PriorityTaskQueue queue;

    std::atomic<bool> stopMonitor{ false };

    //--------------------------------------------------------
    // Create processors
    //--------------------------------------------------------

    std::vector<std::unique_ptr<TaskProcessor>> processors;

    for (int i = 0;
        i < normalConsumersCount + highConsumersCount;
        i++)
    {
        processors.push_back(
            std::make_unique<TaskProcessor>(i + 1));
    }

    //--------------------------------------------------------
    // Launch monitor
    //--------------------------------------------------------

    std::thread monitorThread(
        monitor,
        std::ref(queue),
        std::ref(stopMonitor));

    //--------------------------------------------------------
    // Launch consumers
    //--------------------------------------------------------

    std::vector<std::thread> consumers;

    int processorIndex = 0;

    for (int i = 0; i < normalConsumersCount; i++)
    {
        consumers.emplace_back(
            consumer,
            std::ref(queue),
            std::ref(*processors[processorIndex++]));
    }

    for (int i = 0; i < highConsumersCount; i++)
    {
        consumers.emplace_back(
            highPriorityConsumer,
            std::ref(queue),
            std::ref(*processors[processorIndex++]));
    }

    //--------------------------------------------------------
    // Launch producers
    //--------------------------------------------------------

    std::vector<std::thread> producers;

    for (int i = 0; i < producersCount; i++)
    {
        producers.emplace_back(
            producer,
            std::ref(queue),
            i + 1,
            tasksPerProducer);
    }

    //--------------------------------------------------------
    // Wait producers
    //--------------------------------------------------------

    for (auto& t : producers)
        t.join();

    std::cout
        << "\nAll producers finished\n";

    //--------------------------------------------------------
    // Shutdown queue
    //--------------------------------------------------------

    queue.shutdown();

    //--------------------------------------------------------
    // Wait consumers
    //--------------------------------------------------------

    for (auto& t : consumers)
        t.join();

    //--------------------------------------------------------
    // Stop monitor
    //--------------------------------------------------------

    stopMonitor = true;

    monitorThread.join();

    //--------------------------------------------------------
    // Summary
    //--------------------------------------------------------

    std::cout
        << "\n========== SUMMARY ==========\n";

    for (const auto& p : processors)
    {
        std::cout
            << "Processor "
            << p->getId()
            << " processed "
            << p->getProcessedCount()
            << " tasks\n";
    }

    std::cout
        << "=============================\n";
}

int main()
{
    //------------------------------------------------------
    // Scenario 1
    //------------------------------------------------------

    runScenario(
        2,      // producers
        2,      // normal consumers
        1,      // high consumers
        10);    // tasks per producer

    //------------------------------------------------------
    // Scenario 2
    //------------------------------------------------------

    runScenario(
        5,
        3,
        2,
        25);

    //------------------------------------------------------
    // Scenario 3
    //------------------------------------------------------

    runScenario(
        1,
        4,
        1,
        50);

    return 0;
}