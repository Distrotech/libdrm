# $FreeBSD$

KMOD	= r128
SRCS    = r128_cce.c r128_drv.c r128_state.c
SRCS   += device_if.h bus_if.h pci_if.h
CFLAGS += ${DEBUG_FLAGS} -I..
KERN    = /usr/src/sys
KMODDEPS = drm

@:
	ln -sf /sys @

machine:
	ln -sf /sys/i386/include machine

.include <bsd.kmod.mk>
