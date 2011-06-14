shaders_c =		\
	Bicubic.cg	\
	NV12.cg		\
	YV12.cg		\
	ProcAmp.cg	\
	$(NULL)

shaders_c += Evergreen_mix_1.cg
shaders_c += Evergreen_mix_2.cg
shaders_c += Evergreen_mix_3.cg
shaders_c += Evergreen_mix_4.cg

shaders_o = $(shaders_c:%.cg=%.pso)
shaders_o += Bicubic_FLOAT.pso

shaders_h = $(shaders_o:%.pso=%.h)

BUILT_SOURCES =
CLEANFILES =
EXTRA_DIST = pso2h.py Evergreen.cg $(shaders_c) $(shaders_o) $(shaders_h)

# Only add those targets if python is available
if HAVE_PYTHON
BUILT_SOURCES += $(shaders_h)
CLEANFILES += $(shaders_h)

%.h: %.pso pso2h.py
	$(PYTHON) pso2h.py -n $*_fp -o $@ $<
endif

# Only add those targets if the Cg compiler is available
if HAVE_CGC
BUILT_SOURCES += $(shaders_o)

Evergreen_%.pso: Evergreen_%.cg Evergreen.cg
	$(CGC) -entry Evergreen_main -profile arbfp1 -o $@ $<

%.pso: %.cg
	$(CGC) -entry $*_main -profile arbfp1 -o $@ $<

%_FLOAT.pso: %.cg
	$(CGC) -entry $*_main -profile arbfp1 -o $@ -DUSE_TEXTURE_FLOAT $<
endif

# Extra clean files so that maintainer-clean removes *everything*
MAINTAINERCLEANFILES = Makefile.in
