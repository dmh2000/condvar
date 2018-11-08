---
title: Bounded Queue With Condition Variable
date: '2018-11-08T19:28:05.385Z'
---

# What is a Condition Variable

Recently I was interviewing and was asked 'what is a condition variable'. I knew the answer but because
of nervousness I rambled a bit until I was able to spit out 'a construct that allows threads to wait on
an arbitrary condition and wake up when the condition is satisfied'. 

In the history of multithreading, the concept of a Monitor was invented. [Monitors were invented by Per Brinch Hansen and C. A. R. Hoare, and were first implemented in Brinch Hansen's Concurrent Pascal language] [https://en.wikipedia.org/wiki/Monitor_(synchronization)]. You might want to read that article for the full scoop. There is a basic explanation here so we can get to the code.

You need a mutex and a condition variable. The mutex provides mutual exclusion and the condition variable provides the means of sleeping until a condition is met.

When would you need one? Whenever you have some condition that can be expressed as a logical predicate, and you want threads to wait for the condition to be satisfied, and a corresponding thread that will satisfy the condition and signal the condition variable that it is ready.

Example : Wait for a boolean to be true

```
b = false
m = mutex
c = condition variable

Thread 1
--------

MONITOR
    // wait for b to be true
    lock(m)

    while(!b) {
        wait(m,c)
    }
    b = false

    unlock(m)

Thread 2
--------
do work...
    lock(m)
    b = true
    unlock(m)
    signal(c)
```

This raises questions:
* Can't I just poll on *b* and continue until it is true? 

    Yes but that means Thread 1 will be busy. Thats not what we want. We want it to sleep
    until *b* is true.
* What does *wait(m,c)* do?    

    This is the core of how a condition variable works, see below
* Why is the mutex required?

    Depending on the data structure with which the boolean predicate
    is implemented, you need a mutex to provide mutual exclusion when
    reading and setting the data to avoid synchronization errors. In this case you 
    might argue that reads and writes to *b* are atomic, but for the condition variable
    to work you still need the mutex. 
* When is *b* set to false?

    That depends on how the code is supposed to work. it might be a one
    time event or maybe it should be reset to false at some point. Maybe 
    in Thread 1 or later in Thread 2. That's a design issue. In this case it is set to
    false inside the Monitor 

## Exactly what happens in the 'wait' call above?

There are two operations on a condition variable: wait and signal (unrelated to Unix/Linux signals).
The wait function does two things: it releases the mutex and then suspends the calling thread until
the condition variable is signalled. When the signal is sent, the condition variable relocks the mutex 
(possibly waiting until it is available), and returns. Now the surrounding code can retry the predicate,
almost always in a loop. If the predicate is not satisfied, the code calls *wait* again to sleep.
if the predicate is satisfied, the loop is exited and the code is protected by the locked mutex so 
it can manipulate whatever data structures are needed.

The Wikipedia article shows pseudocode examples of various applications. One is the [bounded producer/consumer
problem](https://en.wikipedia.org/wiki/Monitor_\(synchronization\)#Solving_the_bounded_producer/consumer_problem).
The following links point to real implementations of a bounded queue in C and C++. The semantics of this *bounded_queue* 
implementation have the queue initialized with a maximum number of elements, a *put* method that adds an element to the end of the queue, and a get method that takes the first element off the queue. In the *put* method, if the queue is full,
the thread blocks until room is available in the queue. In the *get* method, if the queue is empty, the calling
thread blocks until an element becomes available. 

* [C Implemention for WIN32](https://github.com/dmh2000/condvar/tree/master/win32])
* [C Implemention for POSIX](https://github.com/dmh2000/condvar/tree/master/posix)
* [C++ 2011 Implemention](https://github.com/dmh2000/condvar/tree/master/cpp)

Notes:
* The C++ version requires C++2011 or later
* The C++ version was tested with VS2017 and g++ version 4.x
* The Win32 version was built with VS2017 in 64 bit mode
* The Win32 version should work with Windows Vista or Server 2008 or later
* the POSIX version was tested on Linux with gcc/g++ version 4.x
