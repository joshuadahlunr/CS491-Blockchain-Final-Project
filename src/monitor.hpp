#ifndef MONITOR_HPP
#define MONITOR_HPP

#include <mutex>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <atomic>
#include <iostream>

// From https://stackoverflow.com/questions/12647217/making-a-c-class-a-monitor-in-the-concurrent-sense/48408987#48408987
// Modified from there
template<class T, typename Mutex = std::shared_timed_mutex, typename LockingMutex = std::recursive_mutex, size_t SLEEP_TIME_MS = 5>
class monitor {
	struct monitor_helper_base {
		monitor* creator;

		monitor_helper_base(const monitor* mon) : creator((monitor*) mon) {}
		T* operator->() { return &creator->data;}
		const T* operator->() const { return &creator->data;}
		T& operator*() { return creator->data;}
		const T& operator*() const { return creator->data;}
		template<typename _T> auto& operator[](_T&& index) { return creator->data[index]; }
		template<typename _T> const auto& operator[](_T&& index) const { return creator->data[index]; }
	};

private:
		T data;
		mutable Mutex mutex;
		mutable LockingMutex lockingMutex;

public:
	template<typename ...Args>
	monitor(Args&&... args) : data(std::forward<Args>(args)...) { }

	// Unique lock pointer
	struct monitor_helper_unique : public monitor_helper_base {
		monitor_helper_unique(const monitor* mon) : monitor_helper_base(mon), uniqueLock(mon->mutex) { }
		template<typename Duration> monitor_helper_unique(const monitor* mon, Duration d) : monitor_helper_base(mon), uniqueLock(mon->mutex, d) { }
		std::unique_lock<Mutex> uniqueLock;
	};

	// Shared lock pointer
	struct monitor_helper_shared : public monitor_helper_base {
		monitor_helper_shared(const monitor* mon) : monitor_helper_base(mon), sharedLock(mon->mutex) { }
		template<typename Duration> monitor_helper_shared(const monitor* mon,  Duration d) : monitor_helper_base(mon), sharedLock(mon->mutex, d) { }
		std::shared_lock<Mutex> sharedLock;
	};

	// Call a function or access a member of the base type directly
	monitor_helper_unique operator->() { return write_lock(); } // If we might change the data return a write lock
	const monitor_helper_shared operator->() const { return read_lock(); } // If we can't change the data return a read lock
	// Return a write locked pointer to the underlying data
	monitor_helper_unique write_lock() {
		std::scoped_lock lock(lockingMutex);
		return monitor_helper_unique(this);
	}
	const monitor_helper_unique write_lock() const {
		std::scoped_lock lock(lockingMutex);
		return monitor_helper_unique(this);
	}
	// Return a read locked pointer to the underlying data
	monitor_helper_shared read_lock() {
		std::scoped_lock lock(lockingMutex);
		return monitor_helper_shared(this);
	}
	const monitor_helper_shared read_lock() const {
		std::scoped_lock lock(lockingMutex);
		return monitor_helper_shared(this);
	}
	// Return an unsafe (no lock) reference to the underlying data
	T& unsafe() { return data; }
	const T& unsafe() const { return data; }
};

#endif /* end of include guard: MONITOR_HPP */
