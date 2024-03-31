# SimpleThreadScheduler

This project was done as part of the McGill Operating Systems course (ECSE427). It consists of a C program where a simple one-to-many (user-level) threading library with a first-come, first-served (FCFS) thread scheduler. The library will have two types of executors: one running computation tasks and another for IO tasks. Each executor is a kernel-level thread, while the tasks that run are user-level threads. 
