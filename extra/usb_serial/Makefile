obj-m := raiden.o

.PHONY: all

all: modules

.DEFAULT:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build \
		M=$(shell pwd) \
		$(MAKECMDGOALS)
