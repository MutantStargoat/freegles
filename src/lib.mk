PREFIX = /usr/local

rootdir = ../..
src = $(wildcard *.c)
obj = $(src:.c=.o)
dep = $(obj:.o=.d)

CFLAGS = -pedantic -Wall -g -fPIC -I$(rootdir)/include -I$(rootdir)/include/EGL

lib_a = lib$(name).a

ifeq ($(shell uname -s), Darwin)
	lib_so = lib$(name).dylib
	shared = -dynamiclib
else
	devlink = lib$(name).so
	soname = $(devlink).$(major_ver)
	lib_so = $(soname).$(minor_ver)
	shared = -shared -Wl,-soname=$(soname)
endif

.PHONY: all
all: $(lib_so) $(lib_a)

$(lib_so): $(obj)
	$(CC) -o $@ $(shared) $(obj) $(LDFLAGS)

$(lib_a): $(obj)
	$(AR) rcs $@ $(obj)

-include $(dep)

.c.d:
	@$(CPP) $(CFLAGS) $< -MM -MT $(@:.d=.o) >$@

.PHONY: clean
clean:
	rm -f $(obj) $(lib_so) $(lib_a) $(dep)

.PHONY: install
install: $(lib_so) $(lib_a)
	mkdir -p $(DESTDIR)$(PREFIX)/include $(DESTDIR)$(PREFIX)/lib
	cp -r $(rootdir)/include/EGL $(DESTDIR)$(PREFIX)/include/
	cp -r $(rootdir)/include/GLES2 $(DESTDIR)$(PREFIX)/include/
	cp $(lib_a) $(DESTDIR)$(PREFIX)/lib/$(lib_a)
	cp $(lib_so) $(DESTDIR)$(PREFIX)/lib/$(lib_so)
	[ -n "$(soname)" ] && \
		cd $(DESTDIR)$(PREFIX) && \
		rm -f $(soname) $(devlink) && \
		ln -s $(lib_so) $(soname) && \
		ln -s $(soname) $(devlink) || \
		true

.PHONY: uninstall
uninstall:
	rm -f $(DESTDIR)$(PREFIX)/include/EGL/*
	rmdir $(DESTDIR)$(PREFIX)/include/EGL
	rm -f $(DESTDIR)$(PREFIX)/include/GLES2/*
	rmdir $(DESTDIR)$(PREFIX)/include/GLES2
	rm -f $(DESTDIR)$(PREFIX)/lib/$(lib_a)
	rm -f $(DESTDIR)$(PREFIX)/lib/$(lib_so)
	[ -n "$(soname)" ] && \
		rm -f $(DESTDIR)$(PREFIX)/lib/$(soname) && \
		rm -f $(DESTDIR)$(PREFIX)/lib/$(devlink) || \
		true
