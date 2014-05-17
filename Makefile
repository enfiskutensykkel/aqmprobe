TARGET 	:= aqmprobe
OBJECTS := main.o message_queue.o qdisc_probe.o file_operations.o

ifneq ($(KERNELRELEASE),)
	CFLAGS_EXTRA += -DDEBUG
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
	insmod $(TARGET).ko qdisc=pfifo maximum_concurrent_events=20 buffer_size=15

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

todo:
	-@for file in $(OBJECTS:%.o=%.c) $(wildcard *.h); do \
		fgrep -H -e TODO -e FIXME $$file; \
	done; true

endif
