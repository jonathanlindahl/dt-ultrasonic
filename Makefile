obj-m += dt_ultrasonic.o

PWD := $(CURDIR)

all: module dt
	#make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
	echo built ultrasonic and overlay

module:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

dt: usncoverlay.dts
	dtc -@ -I dts -O dtb -o usncoverlay.dtbo usncoverlay.dts

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
