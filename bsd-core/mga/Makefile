# $FreeBSD$

KMOD= 	mga
NOMAN=	YES
SRCS=		mga_drv.c mga_state.c mga_warp.c mga_dma.c
SRCS+=	device_if.h bus_if.h pci_if.h opt_drm_linux.h
CFLAGS+=	${DEBUG_FLAGS} -I. -I.. -DSMP -DAPIC_IO

@:
	ln -sf /sys @

machine:
	ln -sf /sys/i386/include machine

.if ${MACHINE_ARCH} == "i386"
# This line enables linux ioctl handling by default
# comment out if you don't want it
TDFX_OPTS=     "\#define DRM_LINUX" 1
.endif

opt_drm_linux.h:
	touch opt_drm_linux.h
	echo $(TDFX_OPTS) >> opt_drm_linux.h

.include <bsd.kmod.mk>
