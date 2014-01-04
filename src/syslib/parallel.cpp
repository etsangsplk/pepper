/*
 * pepper - SCM statistics report generator
 * Copyright (C) 2010-2012 Jonas Gehring
 *
 * Released under the GNU General Public License, version 3.
 * Please see the COPYING file in the source distribution for license
 * terms and conditions, or see http://www.gnu.org/licenses/.
 *
 * file: syslib/parallel.cpp
 * POSIX thread classes and utilities
 */


#include <cassert>

#include <unistd.h>

#include <sys/time.h>

#include "main.h"

#if defined(POS_BSD)
 #include <sys/sysctl.h>
#endif

#include "logger.h"

#include "parallel.h"


namespace sys 
{

namespace parallel
{

// Returns the ideal number of threads, based on the system's CPU resources
// This function is mainly from Qt, version 4.8, but the more exotic systems
// will fall back to '1'
int idealThreadCount()
{
	int cores = 1;
#if defined(POS_BSD)
	size_t len = sizeof(cores);
	int mib[2];
	mib[0] = CTL_HW;
	mib[1] = HW_NCPU;
	if (sysctl(mib, 2, &cores, &len, NULL, 0) != 0) {
		perror("sysctl");
		cores = -1;
	}
#elif defined(POS_LINUX)
	cores = (int)sysconf(_SC_NPROCESSORS_ONLN);
#elif defined(POS_WIN)
	SYSTEM_INFO sysinfo;
	GetSystemInfo(&sysinfo);
	cores = sysinfo.dwNumberOfProcessors;
#else
 #warning "Unknown operating system; sys::parallel::idealThreadCount() will always return 1"
#endif
	return cores;
}


// Constructor
Mutex::Mutex()
{
	int ret = pthread_mutex_init(&m_pmx, NULL);
	switch (ret) {
		case 0: break;
		case EBUSY: throw PEX("Mutex is locked by a thread");
		default: throw PEX_ERR(ret);
	}
}

// Destructor
Mutex::~Mutex()
{
	int ret = pthread_mutex_destroy(&m_pmx);
	switch (ret) {
		case 0: break;
		case EBUSY: throw PEX("Mutex is locked by a thread");
		default: throw PEX_ERR(ret);
	}
}

// Locks the mutex
void Mutex::lock()
{
	int ret = pthread_mutex_lock(&m_pmx);
	switch (ret) {
		case 0: break;
		default: throw PEX_ERR(ret);
	}
}

// Unlocks the mutex
void Mutex::unlock()
{
	int ret = pthread_mutex_unlock(&m_pmx);
	switch (ret) {
		case 0: break;
		default: throw PEX_ERR(ret);
	}
}


// Constructor
MutexLocker::MutexLocker(Mutex *mutex)
	: m_mutex(mutex)
{
	m_mutex->lock();
}

// Destructor
MutexLocker::~MutexLocker()
{
	m_mutex->unlock();
}

// Re-locks the mutex
void MutexLocker::relock()
{
	m_mutex->lock();
}

// Unlocks the mutex
void MutexLocker::unlock()
{
	m_mutex->unlock();
}


// Constructor
Thread::Thread()
	: m_running(0)
{

}

// Destructor
Thread::~Thread()
{
	m_mutex.lock();
	assert(m_running == 0);
	if (m_running != 0) {
		m_mutex.unlock();
		pthread_cancel(m_pth);
		pthread_join(m_pth, 0);
	}
	m_mutex.unlock();
}

// Starts the thread
void Thread::start()
{
	m_mutex.lock();
	assert(m_running == 0);
	if (m_running != 0) {
		m_mutex.unlock();
		return;
	}
	m_running = 1;
	m_mutex.unlock();
	pthread_create(&m_pth, NULL, &Thread::main, this);
}

// Blocks the current thread until this thread has finished
void Thread::wait()
{
	if (running()) {
		pthread_join(m_pth, 0);
	}
}

// Returns whether the thread is currently running
bool Thread::running()
{
	int t;
	m_mutex.lock();
	t = m_running;
	m_mutex.unlock();
	return (t != 0);
}

// Aborts the thread
void Thread::abort()
{
	if (running()) {
		pthread_cancel(m_pth);
	}
}

// Sleeping
void Thread::msleep(int msecs)
{
	// This is from Qt-4.6, qthread_unix.cpp
	struct timeval tv;
	gettimeofday(&tv, 0);

	timespec ti;
	ti.tv_nsec = (tv.tv_usec + (msecs % 1000) * 1000) * 1000;
	ti.tv_sec = tv.tv_sec + (msecs / 1000) + (ti.tv_nsec / 1000000000);
	ti.tv_nsec %= 1000000000;

	pthread_mutex_t mtx;
	pthread_cond_t cnd;

	pthread_mutex_init(&mtx, 0);
	pthread_cond_init(&cnd, 0);

	pthread_mutex_lock(&mtx);
	(void) pthread_cond_timedwait(&cnd, &mtx, &ti);
	pthread_mutex_unlock(&mtx);

	pthread_cond_destroy(&cnd);
	pthread_mutex_destroy(&mtx);
}

// Start function
void *Thread::main(void *obj)
{
	reinterpret_cast<Thread *>(obj)->setupAndRun();
	return NULL;
}

// Cleanup function
void Thread::cleanup(void *obj)
{
	reinterpret_cast<Thread *>(obj)->m_mutex.lock();
	reinterpret_cast<Thread *>(obj)->m_running = 0;
	reinterpret_cast<Thread *>(obj)->m_mutex.unlock();
	pthread_detach(reinterpret_cast<Thread *>(obj)->m_pth);
}

// Do initial setup and call run()
void Thread::setupAndRun()
{
	m_mutex.lock();
	m_running = 2;
	pthread_cleanup_push(Thread::cleanup, this);
	m_mutex.unlock();
	run();
	pthread_cleanup_pop(1);
}


// Constructor
WaitCondition::WaitCondition()
{
	int ret = pthread_cond_init(&m_pcond, NULL);
	switch (ret) {
		case 0: break;
		default: throw PEX_ERR(ret);
	}
}

// Destructor
WaitCondition::~WaitCondition()
{
	int ret = pthread_cond_destroy(&m_pcond);
	switch (ret) {
		case 0: break;
		default: throw PEX_ERR(ret);
	}
}

// Blocks the current thread and waits for a signal
void WaitCondition::wait(Mutex *mutex)
{
	int ret = pthread_cond_wait(&m_pcond, &mutex->m_pmx);
	switch (ret) {
		case 0: break;
		default: throw PEX_ERR(ret);
	}
}

// Wakes a single waiting thread
void WaitCondition::wake()
{
	int ret = pthread_cond_signal(&m_pcond);
	switch (ret) {
		case 0: break;
		default: throw PEX_ERR(ret);
	}
}

// Wakes all waiting threads
void WaitCondition::wakeAll()
{
	int ret = pthread_cond_broadcast(&m_pcond);
	switch (ret) {
		case 0: break;
		default: throw PEX_ERR(ret);
	}
}


// Constructor
Semaphore::Semaphore(int n)
	: m_avail(n)
{

}

int Semaphore::available()
{
	int t;
	m_mutex.lock();
	t = m_avail;
	m_mutex.unlock();
	return t;
}

void Semaphore::acquire(int n)
{
	m_mutex.lock();
	while (n > m_avail) {
		m_cond.wait(&m_mutex);
	}
	m_avail -= n;
	m_mutex.unlock();
}

int Semaphore::maxAcquire(int n)
{
	int ac;
	m_mutex.lock();
	while (1 > m_avail) {
		m_cond.wait(&m_mutex);
	}
	if (m_avail > n) {
		ac = n;
		m_avail -= n;
	} else {
		ac = m_avail;
		m_avail = 0;
	}
	m_mutex.unlock();
	return ac;
}

void Semaphore::release(int n)
{
	m_mutex.lock();
	m_avail += n;
	m_cond.wakeAll();
	m_mutex.unlock();
}

} // namespace parallel

} // namespace sys
