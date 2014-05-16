# Module arguments
TARGET 	:= aqmprobe

ifneq ($(KERNELRELEASE),)
	obj-m := $(TARGET).o
	$(TARGET)-objs = main.o message_queue.o qdisc_probe.o file_operations.o
else
	KERNELDIR ?= /lib/modules/$(shell uname -r)/build
	PWD := $(shell pwd)
default: 
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules
endif

reload: unload load

unload:
	-rmmod $(TARGET).ko

load:
	insmod $(TARGET).ko qdisc=pfifo maximum_concurrent_events=20 buffer_size=15

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean
