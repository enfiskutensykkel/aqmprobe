TARGET 	:= aqmprobe
OBJECTS := main.o message_queue.o qdisc_probe.o file_operations.o
QDISC   := pfifo
MODARGS := buffer_size=15 maximum_concurrent_events=20 flush_frequency=20

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
	insmod $(TARGET).ko qdisc=$(QDISC) $(MODARGS)

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

todo:
	-@for file in $(OBJECTS:%.o=%.c) $(wildcard *.h); do \
		fgrep -H -e TODO -e FIXME $$file; \
	done; true

endif
