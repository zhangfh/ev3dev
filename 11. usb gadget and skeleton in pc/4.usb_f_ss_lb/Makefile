usb_f_ss_lb-y                   := f_loopback.o f_sourcesink.o
obj-m       += usb_f_ss_lb.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
