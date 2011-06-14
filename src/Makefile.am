DRIVERS = fglrx

SUBDIRS = shaders

INCLUDES = \
	$(XVBA_VIDEO_CFLAGS)

LDADD = \
	$(XVBA_VIDEO_LT_LDFLAGS) -module \
	-no-undefined -module -Wl,--no-undefined

if USE_GLX
source_glx_h  = xvba_video_glx.h utils_glx.h
source_glx_c  = xvba_video_glx.c utils_glx.c
endif

source_x11_h  = xvba_video_x11.h utils_x11.h
source_x11_c  = xvba_video_x11.c utils_x11.c

source_h = \
	color_matrix.h		\
	debug.h			\
	fglrxinfo.h		\
	object_heap.h		\
	sysdeps.h		\
	utils.h			\
	uarray.h		\
	uasyncqueue.h		\
	ulist.h			\
	uqueue.h		\
	vaapi_compat.h		\
	xvba_buffer.h		\
	xvba_decode.h		\
	xvba_driver.h		\
	xvba_driver_template.h	\
	xvba_dump.h		\
	xvba_gate.h		\
	xvba_image.h		\
	xvba_subpic.h		\
	xvba_video.h		\
	$(source_glx_h)		\
	$(source_x11_h)		\
	$(NULL)

source_c = \
	color_matrix.c		\
	debug.c			\
	fglrxinfo.c		\
	object_heap.c		\
	utils.c			\
	uarray.c		\
	uasyncqueue.c		\
	ulist.c			\
	uqueue.c		\
	xvba_buffer.c		\
	xvba_decode.c		\
	xvba_driver.c		\
	xvba_dump.c		\
	xvba_gate.c		\
	xvba_image.c		\
	xvba_subpic.c		\
	xvba_video.c		\
	$(source_glx_c)		\
	$(source_x11_c)		\
	$(NULL)

xvba_drv_video_la_LTLIBRARIES	= xvba_drv_video.la
xvba_drv_video_ladir		= @LIBVA_DRIVERS_PATH@
xvba_drv_video_la_SOURCES	= $(source_c)
xvba_drv_video_la_LIBADD	= $(XVBA_VIDEO_LIBS) -lX11 -lXext
xvba_drv_video_la_LDFLAGS	= $(LDADD)

noinst_HEADERS = $(source_h)

install-data-hook:
	cd $(DESTDIR)$(LIBVA_DRIVERS_PATH) ;			\
	for drv in $(DRIVERS); do				\
	    ln -s xvba_drv_video.so $${drv}_drv_video.so;	\
	done

# Extra clean files so that maintainer-clean removes *everything*
MAINTAINERCLEANFILES = Makefile.in config.h.in
