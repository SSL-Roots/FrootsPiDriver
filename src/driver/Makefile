# SPDX-License-Identifier: GPL-2.0
# Makefile for the frootspi.
#
# Ref:https://github.com/raspberrypi/linux/blob/rpi-5.4.y/Documentation/kbuild/modules.rst

KDIR ?= /lib/modules/`uname -r`/build
 
default:
	$(MAKE) -C $(KDIR) M=$$PWD

clean:
	$(MAKE) -C $(KDIR) M=$$PWD clean