aqmprobe
========

A kernel module that probes qdiscs and extracts drop statistics from it.
Uses kprobe. My intention was to support multiple qdiscs, but in the first
version I have focused on `pfifo` and `bfifo`.

For more information, refer the documentation on [kprobes](https://www.kernel.org/doc/Documentation/kprobes.txt),
[tcpprobe](http://www.linuxfoundation.org/collaborate/workgroups/networking/tcpprobe) and 
[netem](http://www.linuxfoundation.org/collaborate/workgroups/networking/netem).

**NB!** This module only works for x86 due to architecture specific behaviour.
