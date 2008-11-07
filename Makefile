ifneq ($(KERNELRELEASE),)

obj-m	:= v4l_virtual.o

else

KVER  := $(shell uname -r)
KLINK := $(shell test -e /lib/modules/${KVER}/source/ && echo source || echo "build")
KSRC  := /lib/modules/$(KVER)/$(KLINK)
PWD   := $(shell pwd)
DEST  := /lib/modules/$(KVER)/kernel/drivers/misc

# Fix some problem with suse < 9.2 and suse >= 9.2
is_suse := $(shell test -e /etc/SuSE-release && echo 1 || echo 0)
ifeq ($(is_suse),1)
  suse_version := $(shell grep VERSION /etc/SuSE-release | cut -f 3 -d " "| tr -d .)
  is_suse_92_or_greater := $(shell test $(suse_version) -ge 92 && echo 1)
  ifeq ($(is_suse_92_or_greater),1)
	KSRC := /lib/modules/$(KVER)/build
  endif
endif
             
all default:
	$(MAKE) -C $(KSRC) SUBDIRS=$(PWD) modules
	gcc -g -lX11 -o source_selector source_selector.c

run:
	./screencast

install:
	install -d $(DEST)
	install -m 644 -c v4l_virtual.ko $(DEST)
	-/sbin/depmod -a

uninstall:
	rm -f $(DEST)/v4l_virtual.ko
	-/sbin/depmod -a

clean:
	rm -f .*.cmd *.o *.mod.c *.ko .v* *~ core
	rm -rf .tmp_versions/
endif
