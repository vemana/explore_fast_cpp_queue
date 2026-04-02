# What is this queue?
This repo does a quick and dirty benchmark of a few optimizations to a SPSC queue impl. Focused around memory ordering facilities of C++ and datastructure alignment to take full advantage of the hardware.

# Attribution
While many of these ideas are standard and first widely popularized via Martin Thompson's Mechanical Sympathy (e.g. LMAX Disruptor & later Aeron in Java land), this current code is motivated directly by a [talk](https://youtu.be/qNs0_kKpcIA?si=COnvOPppKrXGaqwP) given by Christopher Fretz. The [slides](https://github.com/boostcon/cppnow_presentations_2025/blob/main/Presentations/Beyond_Sequential_Consistency.pptx) contributed the skeleton and I added the caching optimization based on the talk.
 

# Defaults
Unless specified otherwise, the parameters are
* queue capacity = 1<<20
* alignas = 8. Governs the alignment of the datastructure memebers of the `ring_buffer` instance
* no caching of producer/consumer sequences
* Tested over exchanging 1<<35 int64s.

# Approaches
Different optimizations of the queue
* Naive = use sequential consistency reads/writes for producer and consumer cursors.
* Optimized = judicious use of memory order acquire/release instead of sequentially consistent
* Cached = Consumer uses its local cached copy of the producer's cursor as long as items are available for it to consume. It only reads the producer's cursor when it has no items left to process. This has a batching effect. Everytime the consumer reads the producer's cursor, it causes cache coherency traffic because producer is constantly updating and invaliding the consumer's local cpu's cached copy. Once the consumer caches it locally and reads from it most of the time, it amortizes the cache traffic cost over a batch of accesses.

# Test environment

Compiler
```text
gcc version 12.3.0 (Ubuntu 12.3.0-1ubuntu1~22.04.3)

-O2
```

Hardware
```text
Architecture:                x86_64
  CPU op-mode(s):            32-bit, 64-bit
  Address sizes:             48 bits physical, 48 bits virtual
  Byte Order:                Little Endian
CPU(s):                      32
  On-line CPU(s) list:       0-31
Vendor ID:                   AuthenticAMD
  Model name:                AMD Ryzen 9 7950X 16-Core Processor
    CPU family:              25
    Model:                   97
    Thread(s) per core:      2
    Core(s) per socket:      16
    Socket(s):               1
    Stepping:                2
    CPU max MHz:             5881.0000
    CPU min MHz:             400.0000
    BogoMIPS:                8982.57
Caches (sum of all):         
  L1d:                       512 KiB (16 instances)
  L1i:                       512 KiB (16 instances)
  L2:                        16 MiB (16 instances)
  L3:                        64 MiB (2 instances)
NUMA:                        
  NUMA node(s):              1
  NUMA node0 CPU(s):         0-31
```

# Results

## Naive, alignas = 8
45M exchanges/s


## Naive, alignas = 64
25M exchanges/s

Suprisingly, cacheline isolation makes performance worse. Likely because hosting them in a single cacheline updates both producer & consumer records for the other in one unit of cache coherency traffic.

## Optimized, alignas = 8
550M exchanges/s


## Optimized, alignas = 64
340M exchanges/s

Surprising again. Cacheline isolation makes performance worse just like with the naive case. This behavior is not replicated with Cached version (see below) though.

## Cached, alignas=8
495M exchanges/s


## Cached, alignas=64
2200M exchanges/s

Here, alignas=64 improves throughput by 5x compared to alignas=8.


## Cached, alignas=64, capacity = 1<<19
2150M exchanges/s


## Cached, alignas=64, capacity = 1<<21
2090M exchanges/s.


## Cached, alignas=64, capacity = 1<<25
1900M exchanges/s


## Cached, alignas=64, capacity = 1<<15
295M exchanges/s
Queue capacity matters. 10x loss in throughput moving queue capacity from 1<<20 to 1<<15


## Cached, alignas=64, capacity = 1<<10
20M exchanges/s
Queue capacity matters. 100x loss in throughput moving queue capacity from 1<<20 to 1<<10
