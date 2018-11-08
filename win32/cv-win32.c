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
	// size of data elements
	size_t m_data_size;
} bounded_queue_t;

uint32_t get_count = 0;
uint32_t put_count = 0;
uint32_t put_wait = 0;
uint32_t get_wait = 0;

void update()
{
	static uint32_t count = 0;
	printf("%u PUT::%u:%u  GET:%u:%u COUNT:%u\n", put_count - get_count, put_count, put_wait, get_count, get_wait, ++count);
}

int32_t queue_init(bounded_queue_t *q, size_t max_nodes, size_t data_size)
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

	// size of data elements
	q->m_data_size = data_size;

	// return success
	return 0;
}

/**
	put an element into the queue, suspend  calling thread until room is available
	data is copied into the queue from the client pointer
	@param q pointer to instantiated queue
	@param data pointer to data item to enqueue	
*/
void queue_put(bounded_queue_t *q, void *data)
{
	queue_node_t *node;
	BOOL status;

	// lock the critical section
	EnterCriticalSection(&q->m_csec);

	//  invariant : m_count never overflows or underflows
	assert(q->m_count <= q->m_max_nodes);

	// allocate a node
	node = malloc(sizeof(queue_node_t));
	assert(node != NULL);

	// allocate room in data for the node
	node->m_data = malloc(q->m_data_size);
	assert(node->m_data != NULL);

	// copy the data into the node
	memcpy(node->m_data, data, q->m_data_size);

	// node has no successor
	node->m_next = NULL;

	// wait for room to be available in the queue
	while (q->m_count == q->m_max_nodes) {
		// no room in queue, wait on PUT condition variable
		// this releases the critical section
		// on return, the critical section is locked
		// this version doesn't use a timeout
		status = SleepConditionVariableCS(&q->m_cvput, &q->m_csec, INFINITE);
		assert(status != 0);

		++put_wait;
	}
	++put_count;

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
}

/**
	get an element from the queue, suspend calling thread until an element is available
	data is copied from the queue into the client pointer

	@param q pointer to instantiated queue
	@param data pointer to variable to receive data
*/
void queue_get(bounded_queue_t *q, void *data)
{
	queue_node_t *node;
	BOOL status;

	// lock the critical section
	EnterCriticalSection(&q->m_csec);

	//  invariant : m_count never overflows or underflows
	assert(q->m_count <= q->m_max_nodes);

	// wait for an element to be available in the queue
	while (q->m_count == 0) {
		// no data in queue, wait on GET condition variable
		// this releases the critical section
		// on return, the critical section is locked
		// this version doesn't use a timeout
		status = SleepConditionVariableCS(&q->m_cvget, &q->m_csec, INFINITE);
		assert(status != 0);

		++get_wait;
	}
	++get_count;

	// invariant : there is data in the queue
	assert(q->m_count > 0);

	// get from the head
	node = q->m_head;

	// copy data to client pointer
	memcpy(data, node->m_data, q->m_data_size);

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

	// free the data
	free(node->m_data);

	// free the node
	free(node);

	// unlock the critical section
	LeaveCriticalSection(&q->m_csec);

	// signal the PUT condition variable to wake up any putters
	WakeConditionVariable(&q->m_cvput);
}

void getter(void *arg)
{
	bounded_queue_t *q;
	uint64_t v;
	uint64_t u;

	// get bounded_queue_t pointer
	q = (bounded_queue_t *)arg;

	u = 0;
	v = 0;
	for (;;) {
		// sleep a random amount
		Sleep(rand() % 10);

		// get an item
		queue_get(q, &v);

		// check the data
		assert(v == u);

		// update u
		++u;
	}
}

void putter(void *arg)
{
	bounded_queue_t *q;
	uint64_t v;

	// get bounded_queue_t pointer
	q = (bounded_queue_t *)arg;

	v = 0;
	for (;;) {
		// sleep a random amount
		Sleep(rand() % 10);

		// get an item
		queue_put(q, &v);

		// update v
		++v;
	}
}

int main(int argc,char *argv)
{

	bounded_queue_t q;

	// initialize a queue
	queue_init(&q, 8, sizeof(uint64_t));

	// start getter thread, pass in queue pointer
	_beginthread(getter, 0, &q);

	// start putter thread, pass in queue pointer
	_beginthread(putter, 0, &q);
	
	// quit on any key entered
	for(int i=0;i<15;++i) {
		Sleep(1000);
		update();
	}

	return 0;
}
