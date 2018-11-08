#include <iostream>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <queue>
#include <cassert>
#include <cstdint>

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
	// test help
	// remove before flight
	uint32_t m_get_wait = 0;
	uint32_t m_put_wait = 0;
	uint32_t m_get_count = 0;
	uint32_t m_put_count = 0;	
	void print()
	{
		static uint32_t count = 0;
		printf("%u PUT::%u:%u  GET:%u:%u COUNT:%u\n",m_put_count-m_get_count, m_put_count,m_put_wait,m_get_count, m_get_wait, ++count);
	}

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
	*/
	void put(const T &data)

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
			m_put_wait++;
		}
		m_put_count++;

		// invariant : there is room in the queue
		assert(m_queue.size() < m_max_nodes);

		// add to queue
		m_queue.push(data);
		
		// unlock the critical section before notifying
		lck.unlock();

		// signal the GET condition variable to wake up any getters
		m_cvget.notify_one();
	}

	/**
		get an element from the queue, suspend calling thread until an element is available
		data is copied from the queue into the client pointer

		@param q pointer to instantiated queue
		@param data pointer to variable to receive data
		@param size pinter to variable to receive size of data item that is dequeued
	*/
	void get(T &data)
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
			++m_get_wait;
		}
		++m_get_count;

		// invariant : there is data in the queue
		assert(m_queue.size() > 0);

		// get from the head
		data = m_queue.front();
		m_queue.pop();

		// unlock the critical section before notifying
		lck.unlock();

		// signal the PUT condition variable to wake up any putters
		m_cvput.notify_one();
	}
};

bool kill_all = false;



void getter(bounded_queue_t<uint64_t> *q)
{
	uint64_t v;
	uint64_t u;

	u = 0;
	v = 0;
	while(!kill_all) {
		// sleep a random amount
		std::this_thread::sleep_for(std::chrono::milliseconds(rand() % 100));

		// get an item
		q->get(v);

		// check the data
		assert(v == u);

		// update u
		++u;
	}
}

void putter(bounded_queue_t<uint64_t> *q)
{
	uint64_t v;

	v = 0;
	while(!kill_all) {
		// sleep a random amount
		std::this_thread::sleep_for(std::chrono::milliseconds(rand() % 200));

		// get an item
		q->put(v);

		// update v
		++v;
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

		q.print();
	}
	kill_all = true;
	t1.join();
	t2.join();
	return 0;
}
