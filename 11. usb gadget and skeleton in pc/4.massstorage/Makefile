usb_f_mass_storage-y            := f_mass_storage.o storage_common.o
obj-m += usb_f_mass_storage.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
