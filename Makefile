PROJECT := ach

VERSION := 0.20090718

LIBFILES := libach.so

BINFILES := achcat

DOXPATH := $(HOME)/prism/public_html/dox

LC_ALL := ascii
LANG := ascii

default: $(LIBFILES) $(BINFILES) test_sub test_pub ach_stream.o

include /usr/share/make-common/common.1.mk

CFLAGS := -g -Wall -Wextra -Wpointer-arith --std=gnu99 -fPIC

$(call LINKLIB, ach, ach.o)

$(call LINKBIN, test_pub, test_pub.c ach.o, pthread rt)
$(call LINKBIN, test_sub, test_sub.c ach.o, pthread rt)
$(call LINKBIN, achcat, achcat.o ach.o, pthread rt)

clean:
	rm -fv  *.o  test_pub ach.lisp test_sub $(BINFILES) $(LIBFILES)

doc: ach.h
	doxygen

