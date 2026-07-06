#include <mutex>
#include <map>
#include <thread>
#include <chrono>
#include <algorithm>
#include <vector>
#include <iostream>
#include <functional>


class Resource {
private:
    std::mutex mutex_;
    std::string name_;
    int value_;

public:
    Resource(const std::string& name, int initialValue = 0)
        : name_(name), value_(initialValue) {
    }

    void lock() { mutex_.lock(); }
	void unlock() { mutex_.unlock(); }  // from the static analysis, it seems that unlock() is not used in the code, but it's still a good practice to have it for completeness.    
	bool try_lock() { return mutex_.try_lock(); }  // try_lock() is also not used in the current code, but it can be useful for more complex scenarios where you want to attempt to acquire a lock without blocking.    

    void modify(int delta) {
        value_ += delta;
        std::cout << "Resource " << name_ << " modified to: " << value_ << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    int getValue() const { return value_; }
    const std::string& getName() const { return name_; }
};

class DeadlockPreventionManager {
private:
    std::map<std::string, Resource*> resources_;

public:
    void addResource(Resource* resource) {
        resources_[resource->getName()] = resource;
    }

    // Ordered lock acquisition to prevent deadlocks
    void executeTransaction(const std::vector<std::string>& resourceNames,
        std::function<void(std::vector<Resource*>&)> transaction) {

        // Sort resource names to ensure consistent ordering
        std::vector<std::string> sortedNames = resourceNames;
        std::sort(sortedNames.begin(), sortedNames.end());

        // Acquire resources in sorted order
        std::vector<Resource*> acquiredResources;
        for (const auto& name : sortedNames) {
            auto it = resources_.find(name);
            if (it != resources_.end()) {
                it->second->lock();
                acquiredResources.push_back(it->second);
            }
        }

        try {
            // Execute the transaction
            transaction(acquiredResources);
        }
        catch (...) {
            // Release locks in reverse order on exception
            for (auto it = acquiredResources.rbegin();
                it != acquiredResources.rend(); ++it) {
                (*it)->unlock();
            }
            throw;
        }

        // Release locks in reverse order
        for (auto it = acquiredResources.rbegin();
            it != acquiredResources.rend(); ++it) {
            (*it)->unlock();
        }
    }
};

int main() {
    Resource account1("account1", 1000);
    Resource account2("account2", 2000);
    Resource ledger("ledger", 0);
    Resource auditLog("audit_log", 0);

    DeadlockPreventionManager manager;
    manager.addResource(&account1);
    manager.addResource(&account2);
    manager.addResource(&ledger);
    manager.addResource(&auditLog);

    std::vector<std::thread> transactions;

    // Transaction A: Transfer from account1 to account2
    transactions.emplace_back([&]() {
        manager.executeTransaction(
            { "account1", "account2", "ledger" },
            [](std::vector<Resource*>& resources) {
                resources[0]->modify(-100);  // account1
                resources[1]->modify(100);   // account2
                resources[2]->modify(1);     // ledger
            });
        });

    // Transaction B: Transfer from account2 to account1
    transactions.emplace_back([&]() {
        manager.executeTransaction(
            { "account2", "account1", "audit_log" },
            [](std::vector<Resource*>& resources) {
                resources[1]->modify(-50);   // account1 (sorted order)
                resources[0]->modify(50);    // account2 (sorted order)
                resources[2]->modify(1);     // audit_log
            });
        });

    for (auto& t : transactions) {
        t.join();
    }

    std::cout << "All transactions completed successfully!" << std::endl;
    return 0;
}