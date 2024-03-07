#pragma once
#include <mutex>
#include <condition_variable>

class EventLock
{
private:
    std::mutex isSetLock;
    bool isSet_ = false;
    std::mutex setTriggerLock;
    std::condition_variable setTrigger;
public:
    bool isSet() {
        std::lock_guard<std::mutex>  lockGuard(isSetLock);    // Calls lock.
        return isSet_;
    };
    void set() {
        std::lock_guard<std::mutex> lockGuard(isSetLock);
        isSet_ = true;
        setTrigger.notify_all();
    };
    void clear() {
        std::lock_guard<std::mutex> lockGuard(isSetLock);
        isSet_ = false;
    };
    void wait() {
        std::unique_lock <std::mutex> setTriggerUniqueLock(isSetLock);
        setTrigger.wait(setTriggerUniqueLock, [&]() {return isSet_; });
    };
};

