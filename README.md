aqmprobe
========

A kernel module that probes qdiscs and extracts drop statistics from it.
Uses kprobe. My intention was to support multiple qdiscs, but in the first
version I have focused on `pfifo` and `bfifo`.

For more information, refer the documentation on [kprobes](https://www.kernel.org/doc/Documentation/kprobes.txt),
[tcpprobe](http://www.linuxfoundation.org/collaborate/workgroups/networking/tcpprobe) and 
[netem](http://www.linuxfoundation.org/collaborate/workgroups/networking/netem).

**NB!** This module only works for x86 due to architecture specific behaviour.


Usage
-----
 1. Compile and load module: `make && make load`
 2. Dump results to file: `cat /proc/net/pfifo > result.bin`
 3. Stop dumping and unload module: `make unload`
 4. Compile analyser: `make analyzer`
 5. Analyse results: `cat result.bin | ./analyzer | ./clustering.py`
