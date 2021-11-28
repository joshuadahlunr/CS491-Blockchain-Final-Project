/**
 * @file monitor.hpp
 * @author Joshua Dahl (jdahl@unr.edu)
 * @brief File which provides a thread safe wrapper around arbitrary data
 * @version 0.1
 * @date 2021-11-29
 * 
 * @copyright Copyright (c) 2021
 * 
 */
#ifndef MONITOR_HPP
#define MONITOR_HPP

#include <mutex>
#include <shared_mutex>

/**
 * @brief Class that provides some thread safe wrappers around other types
 * @note Modified from https://stackoverflow.com/questions/12647217/making-a-c-class-a-monitor-in-the-concurrent-sense/48408987#48408987
 *
 * @tparam T - The type this monitor guards
 * @tparam Mutex - The mutex the locks the data
 * @tparam LockingMutex - The mutex that locks, un/locking
 */
template<class T, typename Mutex = std::shared_mutex, typename LockingMutex = std::recursive_mutex>
class monitor {
	/**
	 * @brief Base class that provides pointer type access to locked data
	 */
	struct monitor_helper_base {
		// The monitor that created this wrapper
		monitor *const creator;

		monitor_helper_base(const monitor* creator) : creator((monitor*) creator) {}
		// Arrow operator
		T* operator->() { return &creator->data; }
		const T* operator->() const { return &creator->data; }
		// Dereference operator
		T& operator*() { return creator->data; }
		const T& operator*() const { return creator->data; }
		// Subscript operator
		template<typename _T> auto& operator[](_T&& index) { return creator->data[index]; }
		template<typename _T> const auto& operator[](_T&& index) const { return creator->data[index]; }
	};

private:
	// The wrapped data
	T data;
	// Data mutex
	mutable Mutex mutex;
	// Un/locking mutex
	mutable LockingMutex lockingMutex;

public:
	// Construction is forwarded to the wrapped type
	template<typename ...Args>
	monitor(Args&&... args) : data(std::forward<Args>(args)...) { }

	/**
	 * @brief A pointer type wrapper with a unique (write) lock
	 */
	struct monitor_helper_unique : public monitor_helper_base {
		monitor_helper_unique(const monitor* creator) : monitor_helper_base(creator), lock(creator->mutex) { }
		template<typename Duration> monitor_helper_unique(const monitor* creator, Duration d) : monitor_helper_base(creator), lock(creator->mutex, d) { }

		// The lock this "pointer" holds
		std::unique_lock<Mutex> lock;
	};

	/**
	 * @brief A pointer type wrapper with a shared (read) lock
	 */
	struct monitor_helper_shared : public monitor_helper_base {
		monitor_helper_shared(const monitor* creator) : monitor_helper_base(creator), lock(creator->mutex) { }
		template<typename Duration> monitor_helper_shared(const monitor* creator,  Duration d) : monitor_helper_base(creator), lock(creator->mutex, d) { }

		// The lock this "pointer" holds
		std::shared_lock<Mutex> lock;
	};

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

	// Call a function or access a member of the base type directly
	monitor_helper_unique operator->() { return write_lock(); }				// If we might change the data return a write lock
	const monitor_helper_shared operator->() const { return read_lock(); }	// If we can't change the data return a read lock
};

#endif /* end of include guard: MONITOR_HPP */
