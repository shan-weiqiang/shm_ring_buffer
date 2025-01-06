# shm_ring_buffer for variable-length payloads

This is a derived version of shared memory ring buffer developed by Bo Yang. While the original version can only support fixed-lengthed POD data, this one can be used for *variable-length* bytes streams. User can use this ring buffer to store and pop variable lengthed bytes streams. See the `test_shmringbuffer_payload.cc` tests for how to use it. Test results:

```
...

child: insert 10990, index 672; count: 55; length: 15
parent: insert 10994, index 672; count: 55; length: 15
pop_ front: china shangahi
parent: insert 10995, index 688; count: 55; length: 12
child: insert 10992, index 688; count: 55; length: 15
pop_ front: japan tokyo
pop_ front: china shangahi
parent: insert 10996, index 707; count: 54; length: 15
pop_ front: japan tokyo
child: insert 10994, index 726; count: 54; length: 15
pop_ front: china shangahi
pop_ front: china shangahi
parent: insert 10998, index 745; count: 53; length: 15
child: insert 10995, index 761; count: 54; length: 12
pop_ front: china shangahi
pop_ front: japan tokyo
child: insert 10996, index 780; count: 53; length: 15
>>>>>>>>>>> parent pushed: 5214
pop_ front: china shangahi
pop_ front: china shangahi
child: insert 10998, index 799; count: 52; length: 15
pop_ front: japan tokyo
pop_ front: china shangahi
>>>>>>>>>>> child pushed: 5167
pop_ front: china shangahi
pop_ front: china shangahi
pop_ front: japan tokyo
pop_ front: china shangahi
pop_ front: japan tokyo
pop_ front: china shangahi
pop_ front: china shangahi
pop_ front: japan tokyo
pop_ front: china shangahi
pop_ front: japan tokyo
pop_ front: china shangahi
pop_ front: china shangahi
pop_ front: china shangahi
pop_ front: japan tokyo
pop_ front: china shangahi
pop_ front: china shangahi
pop_ front: china shangahi
pop_ front: japan tokyo
pop_ front: china shangahi
pop_ front: china shangahi
pop_ front: china shangahi
pop_ front: japan tokyo
pop_ front: china shangahi
pop_ front: japan tokyo
pop_ front: china shangahi
pop_ front: china shangahi
pop_ front: china shangahi
pop_ front: japan tokyo
pop_ front: china shangahi
pop_ front: china shangahi
pop_ front: japan tokyo
pop_ front: china shangahi
pop_ front: china shangahi
pop_ front: china shangahi
pop_ front: japan tokyo
pop_ front: china shangahi
pop_ front: china shangahi
pop_ front: japan tokyo
pop_ front: china shangahi
pop_ front: china shangahi
pop_ front: china shangahi
pop_ front: japan tokyo
pop_ front: china shangahi
pop_ front: japan tokyo
pop_ front: china shangahi
pop_ front: china shangahi
pop_ front: china shangahi
pop_ front: japan tokyo
pop_ front: china shangahi
pop_ front: china shangahi
pop_front: 10381
Ring Buffer count:
0
```

# shm_ring_buffer
Shared-memory based ring buffer

This is a C++ template of shared-memory based ring buffer, which can be read/written by multiple processes. Due to the POSIX shared memory limitation, only [POD type](http://en.cppreference.com/w/cpp/concept/PODType) is supported. A builtin multi-reader/multi-writer lock is implemented in class `ShmRingBuffer` using pthread.

For detailed explanation, please refer to my post: [Shared-memory Based Ring Buffer](http://www.bo-yang.net/2016/07/27/shared-memory-ring-buffer).

To try this library out, download the source code to a directory, and run `make`. To clean the binaries, please use `make clean`.

In the test program `test_shmringbuffer.cc`, two processes are forked. The parent process inserts "2xxx" to the ring buffer, while the child process inserts "1xxx". A sample output of this program is like:

```
parent: insert 2192, index 14
child: insert 1181, index 15
parent: insert 2193, index 17
child: insert 1182, index 17
child: insert 1183, index 18
parent: insert 2194, index 19
parent: insert 2195, index 0
child: insert 1184, index 1
parent: insert 2196, index 2
child: insert 1185, index 3
parent: insert 2197, index 4
child: insert 1186, index 5
parent: insert 2198, index 6
child: insert 1187, index 7
child: insert 1188, index 8
parent: insert 2199, index 9
child: insert 1189, index 10
child: insert 1190, index 11
child: insert 1191, index 12
child: insert 1192, index 13
child: insert 1193, index 14
Ring Buffer:
[0] 15: 2193
[0] 15: 1182
[0] 17: 1183
[0] 18: 2194
[0] 19: 2195
[0] 0: 1184
[0] 1: 2196
[0] 2: 1185
[0] 3: 2197
[0] 4: 1186
[0] 5: 2198
[0] 6: 1187
[0] 7: 1188
[0] 8: 2199
[0] 9: 1189
[0] 10: 1190
[0] 11: 1191
[0] 12: 1192
[0] 13: 1193

boyang:shm_ring_buffer$ child: insert 1194, index 15
child: insert 1195, index 16
child: insert 1196, index 17
child: insert 1197, index 18
child: insert 1198, index 19
child: insert 1199, index 0
```

In the above test, there's a race condition when parent pushing 2193 and child inserting 1182. When formulating the log, the index of the ring buffer was still 15 - since a read/write lock is used, both parent and child can get the index 15. We can see that both parent and child successfully inserted their logs into the ring buffer.

When the parent process dumping the buffer, the child process hadn't finished insertion yet. That's why we can see 200 logs inserted from parent process, but only 193 logs inserted by the child process.
