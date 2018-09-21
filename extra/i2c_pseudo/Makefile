obj-m := i2c-pseudo.o

.PHONY: all

all: modules

.DEFAULT:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build \
		M=$(shell pwd) \
		$(MAKECMDGOALS)
