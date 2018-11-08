#include <Windows.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <conio.h>

#define QUEUE_SUCCESS 0
#define QUEUE_FAIL    1

// forward references
typedef struct queue_node_t queue_node_t;

// linked list nodes
typedef struct queue_node_t {
	// pointer to data 
	void *m_data;
	// size of data
	size_t m_size;
	// next node
	queue_node_t *m_next;
} queue_node_t;


// bounded queue type
typedef struct bounded_queue_t {
	// critical section for condition variable functions
	CRITICAL_SECTION m_csec;
	// condition variable for inserting data into the queue
	CONDITION_VARIABLE m_cvput;
	// condition varialbe for getting data from the queue
	CONDITION_VARIABLE m_cvget;
	// head of queue
	queue_node_t *m_head;
	// tail of queue
	queue_node_t *m_tail;
	// current queue size
	size_t m_count;
	// max elements in queue
	size_t m_max_nodes;
} bounded_queue_t;

uint32_t get_count = 0;
uint32_t put_count = 0;

void update(void)
{
	printf("\rPUT : %8u  GET : %8u", put_count, get_count);
}

int32_t queue_init(bounded_queue_t *q, size_t max_nodes)
{
	// initialize a critical section dedicated to the condition variable
	InitializeCriticalSection(&q->m_csec);

	// initialize the condition variables
	InitializeConditionVariable(&q->m_cvput);
	InitializeConditionVariable(&q->m_cvget);

	// queue is empty
	q->m_head = NULL;
	q->m_tail = NULL;

	// count of nodes
	q->m_count = 0;

	// max nodes allowed
	q->m_max_nodes = max_nodes;

	// return success
	return 0;
}

/**
	put an element into the queue, suspend  calling thread until room is available
	data is copied into the queue from the client pointer
	@param q pointer to instantiated queue
	@param data pointer to data item to enqueue
	@param size size of data item to enqueue
	@return QUEUE_SUCCESS if there is room in the queue
			QUEUE_FAIL if a system error occurred
*/
int32_t queue_put(bounded_queue_t *q, void *data, size_t size)
{
	queue_node_t *node;
	BOOL status;

	// lock the critical section
	EnterCriticalSection(&q->m_csec);

	//  invariant : m_count never overflows or underflows
	assert(q->m_count <= q->m_max_nodes);

	// allocate a node
	node = malloc(sizeof(queue_node_t));

	// fail if heap is empty
	if (node == NULL) {
		LeaveCriticalSection(&q->m_csec);
		return QUEUE_FAIL;
	}

	// allocate room in data for the node
	node->m_data = malloc(size);

	// fail if heap is empty
	if (node->m_data == NULL) {
		LeaveCriticalSection(&q->m_csec);
		return QUEUE_FAIL;
	}

	// copy the data into the node
	memcpy(node->m_data, data, size);

	// record the size
	node->m_size = size;

	// node has no successor
	node->m_next = NULL;

	// wait for room to be available in the queue
	while (q->m_count == q->m_max_nodes) {
		// test data
		put_count++;

		// no room in queue, wait on PUT condition variable
		// this releases the critical section
		// on return, the critical section is locked
		// this version doesn't use a timeout
		status = SleepConditionVariableCS(&q->m_cvput, &q->m_csec, INFINITE);
		assert(status != 0);
	}

	// invariant : there is room in the queue
	assert(q->m_count < q->m_max_nodes);

	// add to queue
	if (q->m_count == 0) {
		// empty, add to head and tail
		q->m_head = q->m_tail = node;

		// update head and tail
		q->m_head->m_next = NULL;
		q->m_tail->m_next = NULL;

		q->m_count = 1;
	}
	else {
		// invariant : head is not null
		assert(q->m_head != NULL);

		// invariant : tail is not null
		assert(q->m_tail != NULL);

		// invariant : tail next is null
		assert(q->m_tail->m_next == NULL);

		// add to tail
		q->m_tail->m_next = node;

		// advance the tail
		q->m_tail = node;

		// increment count
		q->m_count += 1;
	}

	// signal the GET condition variable to wake up any getters
	WakeConditionVariable(&q->m_cvget);

	// unlock the critical section
	LeaveCriticalSection(&q->m_csec);

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
int32_t queue_get(bounded_queue_t *q, void *data, size_t *size)
{
	queue_node_t *node;
	BOOL status;

	// lock the critical section
	EnterCriticalSection(&q->m_csec);

	//  invariant : m_count never overflows or underflows
	assert(q->m_count <= q->m_max_nodes);

	// wait for an element to be available in the queue
	while (q->m_count == 0) {
		get_count++;
		// no data in queue, wait on GET condition variable
		// this releases the critical section
		// on return, the critical section is locked
		// this version doesn't use a timeout
		status = SleepConditionVariableCS(&q->m_cvget, &q->m_csec, INFINITE);
		assert(status != 0);
	}

	// invariant : there is data in the queue
	assert(q->m_count > 0);

	// get from the head
	node = q->m_head;

	// copy data to client pointer
	memcpy(data, node->m_data, node->m_size);

	// return size of node
	*size = node->m_size;

	// decrement the count
	q->m_count -= 1;

	if (q->m_count == 0) {
		// invariant : head->next is null
		assert(q->m_head->m_next == NULL);

		// invariant : head == tail
		assert(q->m_head == q->m_tail);

		// no more data, fix head and tail
		q->m_head = q->m_tail = NULL;
	}
	else {
		// invariant : head->next is not null
		assert(q->m_head->m_next != NULL);

		// elements remaining in the queue, advance the head
		q->m_head = q->m_head->m_next;
	}

	// signal the PUT condition variable to wake up any putters
	WakeConditionVariable(&q->m_cvput);

	// unlock the critical section
	LeaveCriticalSection(&q->m_csec);

	return QUEUE_SUCCESS;
}

void getter(void *arg)
{
	bounded_queue_t *q;
	uint64_t v;
	uint64_t u;
	size_t   size;
	int32_t  status;

	// get bounded_queue_t pointer
	q = (bounded_queue_t *)arg;

	u = 0;
	v = 0;
	for (;;) {
		// sleep a random amount
		Sleep(rand() % 10);

		// get an item
		status = queue_get(q, &v, &size);

		// quit on failure
		if (status != QUEUE_SUCCESS) {
			printf("%s:%d QUEUE GET FAIL : %llu\n", __FILE__, __LINE__,u);
			exit(1);
		}
		// check the data
		if (v < u) {
			printf("%s:%d QUEUE GET MISMATCH : %llu %llu\n", __FILE__, __LINE__,u,  v);
			exit(1);
		}
		// check the size
		if (size != sizeof(uint64_t)) {
			printf("%s:%d QUEUE GET SIZE : %llu %llu\n", __FILE__, __LINE__, u, size);
			exit(1);
		}
		// update u
		u = v;

	}
}

void putter(void *arg)
{
	bounded_queue_t *q;
	uint64_t v;
	int32_t  status;

	// get bounded_queue_t pointer
	q = (bounded_queue_t *)arg;

	v = 0;
	for (;;) {
		// sleep a random amount
		Sleep(rand() % 10);

		// get an item
		status = queue_put(q, &v, sizeof(v));

		// quit on failure
		if (status != QUEUE_SUCCESS) {
			printf("%s:%d QUEUE GET FAIL : %llu\n", __FILE__, __LINE__, v);
			exit(1);
		}

		// update v
		v += 1;
	}
}

int main(int argc,char *argv)
{

	bounded_queue_t q;

	// initialize a queue
	queue_init(&q, 8);

	// start getter thread, pass in queue pointer
	_beginthread(getter, 0, &q);

	// start putter thread, pass in queue pointer
	_beginthread(putter, 0, &q);
	
	// quit on any key entered
	while (!_kbhit()) {
		Sleep(1000);
		update();
	}

	return 0;
}
