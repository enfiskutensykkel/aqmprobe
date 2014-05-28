TARGET 	:= aqmprobe
OBJECTS := main.o message_queue.o qdisc_probe.o file_operations.o
QDISC   := pfifo
MODARGS := qdisc_len=62 buf_len=1024 concurrent_evts=40 flush_freq=2000

ifneq ($(KERNELRELEASE),)
	ccflags-y += -DDEBUG
	obj-m := $(TARGET).o
	$(TARGET)-objs = $(OBJECTS)
else
	KDIR ?= /lib/modules/$(shell uname -r)/build
	PWD := $(shell pwd)

default: 
	$(MAKE) -C $(KDIR) M=$(PWD) modules

reload: unload load

unload:
	-rmmod $(TARGET).ko

load:
	insmod $(TARGET).ko filename=$(QDISC) qdisc=$(QDISC) $(MODARGS)

clean:
	-rm analyzer
	$(MAKE) -C $(KDIR) M=$(PWD) clean

analyzer:
	g++ -Wall -Wextra -pedantic -o $@ analyzer.cpp

todo:
	-@for file in $(OBJECTS:%.o=%.c) $(wildcard *.h); do \
		fgrep -H -e TODO -e FIXME $$file; \
	done; true

endif
