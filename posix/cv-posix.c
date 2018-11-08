#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

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
	pthread_mutex_t m_cvmtx;
	// condition variable for inserting data into the queue
	pthread_cond_t m_cvput;
	// condition varialbe for getting data from the queue
	pthread_cond_t m_cvget;
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
uint32_t get_wait = 0;
uint32_t put_wait = 0;

void update()
{
	static uint32_t count = 0;
	printf("%u PUT::%u:%u  GET:%u:%u COUNT:%u\n", put_count - get_count, put_count, put_wait, get_count, get_wait, ++count);
}

int32_t queue_init(bounded_queue_t *q, size_t max_nodes,size_t data_size)
{
    int status;

	// initialize a critical section dedicated to the condition variable
	status = pthread_mutex_init(&q->m_cvmtx,NULL);
    assert(status == 0);

	// initialize the condition variables
	status = pthread_cond_init(&q->m_cvput, NULL);
    assert(status == 0);
    status = pthread_cond_init(&q->m_cvget, NULL);
    assert(status == 0);

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
	@param size size of data item to enqueue
	@return QUEUE_SUCCESS if there is room in the queue
			QUEUE_FAIL if a system error occurred
*/
void queue_put(bounded_queue_t *q, void *data, size_t size)
{
	queue_node_t *node;
	int status;

	// lock the critical section
	status = pthread_mutex_lock(&q->m_cvmtx);
    assert(status == 0);

	//  invariant : m_count never overflows or underflows
	assert(q->m_count <= q->m_max_nodes);

	// allocate a node
	node = malloc(sizeof(queue_node_t));
	assert(node != NULL);


	// allocate room in data for the node
	node->m_data = malloc(size);
	assert(node->m_data != NULL);

	// copy the data into the node
	memcpy(node->m_data, data, size);


	// node has no successor
	node->m_next = NULL;

	// wait for room to be available in the queue
	while (q->m_count == q->m_max_nodes) {
		// no room in queue, wait on PUT condition variable
		// this releases the critical section
		// on return, the critical section is locked
		// this version doesn't use a timeout
        status = pthread_cond_wait(&q->m_cvput,&q->m_cvmtx);
        assert(status == 0);

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

	// unlock the critical section
    status = pthread_mutex_unlock(&q->m_cvmtx);
    assert(status == 0);

	// signal the GET condition variable to wake up any getters
	status = pthread_cond_signal(&q->m_cvget);
	assert(status == 0);

	return QUEUE_SUCCESS;
}

/**
	get an element from the queue, suspend calling thread until an element is available
	data is copied from the queue into the client pointer

	@param q pointer to instantiated queue
	@param data pointer to variable to receive data
	@param size pinter to variable to receive size of data item that is dequeued
*/
void  queue_get(bounded_queue_t *q, void *data, size_t *size)
{
	queue_node_t *node;
	int status;

	// lock the critical section
    status = pthread_mutex_lock(&q->m_cvmtx);
    assert(status == 0);

	//  invariant : m_count never overflows or underflows
	assert(q->m_count <= q->m_max_nodes);

	// wait for an element to be available in the queue
	while (q->m_count == 0) {
		// no data in queue, wait on GET condition variable
		// this releases the critical section
		// on return, the critical section is locked
		// this version doesn't use a timeout
		status = pthread_cond_wait(&q->m_cvget,&q->m_cvmtx);

		// if status indicates an error, release critical section and return failure
		if (status != 0) {
			status = pthread_mutex_unlock(&q->m_cvmtx);
    		assert(status == 0);
			return QUEUE_FAIL;
		}
		++get_wait;
	}
	+=get_count;

	// invariant : there is data in the queue
	assert(q->m_count > 0);

	// get from the head
	node = q->m_head;

	// copy data to client pointer
	memcpy(data, node->m_data, node->m_size);

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
	status = pthread_mutex_unlock(&q->m_cvmtx);
	assert(status == 0);

	// signal the PUT condition variable to wake up any putters
	pthread_cond_signal(&q->m_cvput);
}

void *getter(void *arg)
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
		usleep((rand() % 10) * 1000);

		// get an item
		queue_get(q, &v, &size);

		// check the data
		assert(u == v);

		// update u
		++u;
	}

	return NULL;
}

void *putter(void *arg)
{
	bounded_queue_t *q;
	uint64_t v;

	// get bounded_queue_t pointer
	q = (bounded_queue_t *)arg;

	v = 0;
	for (;;) {
		// sleep a random amount
		usleep((rand() % 10) * 1000);

		// get an item
		queue_put(q, &v, sizeof(v));
		
		// update v
		v += 1;
	}
	return NULL;
}

int main(int argc,char *argv)
{

	bounded_queue_t q;
    pthread_t t1;
    pthread_t t2;

	// initialize a queue
	queue_init(&q, 8);

	// start getter thread, pass in queue pointer
	pthread_create(&t1, NULL, getter, &q);
	// start putter thread, pass in queue pointer
	pthread_create(&t2, NULL, putter, &q);
	
	// quit on any key entered
	for(int i=0;i<15;++i) {
		sleep(1);
		update();
	}

	return 0;
}
