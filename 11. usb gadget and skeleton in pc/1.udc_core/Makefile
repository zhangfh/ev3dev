CFLAGS_trace.o			:= -I$(src)
udc-core-y			:= core.o trace.o

#
# USB peripheral controller drivers
#
obj-m	+= udc-core.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
