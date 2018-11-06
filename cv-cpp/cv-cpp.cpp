#include <iostream>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <queue>
#include <cassert>
#include <cstdint>

#define QUEUE_SUCCESS 0
#define QUEUE_FAIL    1

uint32_t get_wait = 0;
uint32_t put_wait = 0;
uint32_t get_count = 0;
uint32_t put_count = 0;
bool kill_all = false;

// bounded queue type
template<typename T> class bounded_queue_t {
private:
	// critical section for condition variable functions
	std::mutex m_cvmtx;
	// condition variable for inserting data into the queue
	std::condition_variable m_cvput;
	// condition varialbe for getting data from the queue
	std::condition_variable m_cvget;
	// use a dequeu for the queue
	std::queue<T> m_queue;
	// max elements in queue
	size_t m_max_nodes;
public:
	bounded_queue_t(size_t max_nodes) :
		m_max_nodes(max_nodes) {
		// mutex and crit sections are constructed to be ready
	}

	size_t size() {
		return m_queue.size();
	}

	/**
		put an element into the queue, suspend  calling thread until room is available
		data is COPIED into the queue from the client pointer
		@param q pointer to instantiated queue
		@param data pointer to data item to enqueue
		@param size size of data item to enqueue
		@return QUEUE_SUCCESS if there is room in the queue
				QUEUE_FAIL if a system error occurred
	*/
	int32_t put(const T &data)

	{		
		// lock object for condition variable
		std::unique_lock<std::mutex> lck(m_cvmtx);

		//  invariant : ount never overflows or underflows
		assert(m_queue.size() <= m_max_nodes);

		// wait for room to be available in the queue
		// wait for an element to be available in the queue
		// if queue is full, wait on the condition variable
		//    mutex is released and condition variable waits for notification
		// on notification mutex is relocked and predicate is reevaluated
		while (m_queue.size() == m_max_nodes) {
			m_cvput.wait(lck);
			put_wait++;
		}
		put_count++;

		// invariant : there is room in the queue
		assert(m_queue.size() < m_max_nodes);

		// add to queue
		m_queue.push(data);
		
		// unlock the critical section before notifying
		lck.unlock();

		// signal the GET condition variable to wake up any getters
		m_cvget.notify_one();

		return QUEUE_SUCCESS;
	}

	/**
		get an element from the queue, suspend calling thread until an element is available
		data is copied from the queue into the client pointer

		@param q pointer to instantiated queue
		@param data pointer to variable to receive data
		@param size pinter to variable to receive size of data item that is dequeued
		@return QUEUE_SUCCESS if there is an element in the queue
				QUEUE_FAIL if a system error occurred
	*/
	int32_t get(T &data)
	{
		// lock object for condition variable
		std::unique_lock<std::mutex> lck(m_cvmtx);

		//  invariant : m_count never overflows or underflows
		assert(m_queue.size() <= m_max_nodes);

		// wait for an element to be available in the queue
		// if queue is empty, wait on the condition variable
		//    mutex is released and condition variable waits for notification
		// on notification mutex is relocked and predicate is reevaluated
		while (m_queue.size() == 0) {
			m_cvget.wait(lck);
			++get_wait;
		}
		++get_count;

		// invariant : there is data in the queue
		assert(m_queue.size() > 0);

		// get from the head
		data = m_queue.front();
		m_queue.pop();

		// unlock the critical section before notifying
		lck.unlock();

		// signal the PUT condition variable to wake up any putters
		m_cvput.notify_one();

		return QUEUE_SUCCESS;
	}
};



void update(size_t c)
{
	static uint32_t count = 0;
	printf("%u:%zu PUT::%u:%u  GET:%u:%u COUNT:%u\n",put_count-get_count,c, put_count,put_wait,get_count, get_wait, ++count);
}

void getter(bounded_queue_t<uint64_t> *q)
{
	uint64_t v;
	uint64_t u;
	int32_t  status;

	u = 0;
	v = 0;
	while(!kill_all) {
		// sleep a random amount
		std::this_thread::sleep_for(std::chrono::milliseconds(rand() % 100));

		// get an item
		status = q->get(v);

		// quit on failure
		if (status != QUEUE_SUCCESS) {
			printf("%s:%d QUEUE GET FAIL : %zu\n", __FILE__, __LINE__, u);
			exit(1);
		}
		// check the data (v must be equal or greater than u)
		if (v < u) {
			printf("%s:%d QUEUE GET MISMATCH : %zu %zu\n", __FILE__, __LINE__, u, v);
			exit(1);
		}
		// update u
		u = v;

	}
}

void putter(bounded_queue_t<uint64_t> *q)
{
	uint64_t v;
	int32_t  status;

	v = 0;
	while(!kill_all) {
		// sleep a random amount
		std::this_thread::sleep_for(std::chrono::milliseconds(rand() % 200));

		// get an item
		status = q->put(v);

		// quit on failure
		if (status != QUEUE_SUCCESS) {
			printf("%s:%d QUEUE GET FAIL : %zu\n", __FILE__, __LINE__, v);
			exit(1);
		}

		// update v
		v += 1;
	}
}

int main(int argc, char *argv[])
{

	bounded_queue_t<uint64_t> q(8);


	// start getter thread, pass in queue pointer
	std::thread t1(putter, &q);

	// start putter thread, pass in queue pointer
	std::thread t2(getter, &q);

	// quit on any key entered
	for(int i=0;i<15;i++) {
		std::this_thread::sleep_for(std::chrono::seconds(1));

		update(q.size());
	}
	kill_all = true;
	t1.join();
	t2.join();
	return 0;
}
