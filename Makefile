# Module arguments
NAME 	 := aqmprobe

ifneq ($(KERNELRELEASE),)
	obj-m := $(NAME).o
else
KERNELDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

default: 
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules
endif

reload: unload load

unload:
	-rmmod $(NAME).ko

load:
	insmod $(NAME).ko qdisc=pfifo max_active=20 buffer_size=15

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean
