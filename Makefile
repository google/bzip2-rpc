# ------------------------------------------------------------------
# This file is part of bzip2/libbzip2, a program and library for
# lossless, block-sorting data compression.
#
# bzip2/libbzip2 version 1.0.6 of 6 September 2010
# Copyright (C) 1996-2010 Julian Seward <jseward@bzip.org>
#
# Please read the WARNING, DISCLAIMER and PATENTS sections in the
# README file.
#
# This program is released under the terms of the license contained
# in the file LICENSE.
# ------------------------------------------------------------------

SHELL=/bin/sh

# To assist in cross-compiling
CC=gcc
AR=ar
RANLIB=ranlib
LDFLAGS=-L/usr/local/lib

BIGFILES=-D_FILE_OFFSET_BITS=64
CFLAGS=-Wall -Winline -O2 -g $(BIGFILES) -Ithird_party/libnv -I/usr/include/dbus-1.0 -I/usr/lib/x86_64-linux-gnu/dbus-1.0/include
CXXFLAGS=$(CFLAGS) -I/usr/local/include -std=c++11
GRPC_CPP_PLUGIN = grpc_cpp_plugin
GRPC_CPP_PLUGIN_PATH ?= `which $(GRPC_CPP_PLUGIN)`

# Where you want it installed when you do 'make install'
PREFIX=/usr/local


OBJS= blocksort.o  \
      huffman.o    \
      crctable.o   \
      randtable.o  \
      compress.o   \
      decompress.o \
      stream.o     \
      bzlib.o

NVOBJS= dnvlist.o  \
        nvlist.o   \
        nvpair.o   \
        msgio.o

GRPC_SRC = bzlib.grpc.pb.cc bzlib.pb.cc
GRPC_OBJS = bzlib.grpc.pb.o bzlib.pb.o

PROGS = bzip2 bzip2recover bzip2-libnv bzip2-dbus bzip2-grpc bzip2-capnp
DRIVERS = bz2-driver-libnv bz2-driver-dbus bz2-driver-grpc bz2-driver-capnp
LIBS = libbz2.a libnv.a libbz2-libnv.a libbz2-dbus.a libbz2-grpc.a libbz2-capnp.a

all: $(LIBS) $(PROGS) $(DRIVERS)

bzip2: libbz2.a bzip2.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ bzip2.o -L. -lbz2

bzip2-libnv: libbz2-libnv.a libnv.a bzip2.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ bzip2.o -L. -lbz2-libnv -lnv

bzip2-dbus: libbz2-dbus.a bzip2.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ bzip2.o -L. -lbz2-dbus -ldbus-1

bzip2-grpc: libbz2-grpc.a bzip2.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ bzip2.o -L. -lbz2-grpc -lgrpc++_unsecure -lgrpc -lprotobuf -lpthread -ldl

bzip2-capnp: libbz2-capnp.a bzip2.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ bzip2.o -L. -lbz2-capnp -lcapnp-rpc -lcapnp -lkj-async -lkj

bzip2recover: bzip2recover.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ bzip2recover.o

bz2-driver-libnv: libbz2.a libnv.a bz2-driver-libnv.o rpc-util.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ bz2-driver-libnv.o rpc-util.o -L. -lbz2 -lnv

bz2-driver-dbus: libbz2.a bz2-driver-dbus.o rpc-util.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ bz2-driver-dbus.o rpc-util.o -L. -lbz2 -ldbus-1

bz2-driver-grpc: libbz2.a bz2-driver-grpc.o rpc-util.o $(GRPC_OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ bz2-driver-grpc.o rpc-util.o $(GRPC_OBJS) -L. -lbz2 -lgrpc++_unsecure -lgrpc -lprotobuf -lpthread -ldl

bz2-driver-capnp: libbz2.a bz2-driver-capnp.o bzlib.capnp.o rpc-util.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ bz2-driver-capnp.o bzlib.capnp.o rpc-util.o -L. -lbz2 -lcapnp-rpc -lcapnp -lkj-async -lkj

libbz2-libnv.a: bz2-stub-libnv.o rpc-util.o
	rm -f $@
	$(AR) cq $@ $^

libbz2-dbus.a: bz2-stub-dbus.o rpc-util.o
	rm -f $@
	$(AR) cq $@ $^

libbz2-grpc.a: bz2-stub-grpc.o rpc-util.o $(GRPC_OBJS)
	rm -f $@
	$(AR) cq $@ $^

libbz2-capnp.a: bz2-stub-capnp.o rpc-util.o bzlib.capnp.o
	rm -f $@
	$(AR) cq $@ $^

libbz2.a: $(OBJS)
	rm -f $@
	$(AR) cq $@ $(OBJS)
	@if ( test -f $(RANLIB) -o -f /usr/bin/ranlib -o \
		-f /bin/ranlib -o -f /usr/ccs/bin/ranlib ) ; then \
		$(RANLIB) $@ ; \
	fi

libnv.a: $(NVOBJS)
	rm -f libnv.a
	$(AR) cq libnv.a $(NVOBJS)

check: test
test: test-direct test-libnv test-dbus test-grpc test-capnp
test-direct: bzip2
	./test-run.sh ./bzip2
test-libnv: bzip2-libnv bz2-driver-libnv
	./test-run.sh ./bzip2-libnv
test-dbus: bzip2-dbus bz2-driver-dbus
	./test-run.sh ./bzip2-dbus
test-grpc: bzip2-grpc bz2-driver-grpc
	./test-run.sh ./bzip2-grpc
test-capnp: bzip2-capnp bz2-driver-capnp
	./test-run.sh ./bzip2-capnp

install: bzip2 bzip2recover
	if ( test ! -d $(PREFIX)/bin ) ; then mkdir -p $(PREFIX)/bin ; fi
	if ( test ! -d $(PREFIX)/lib ) ; then mkdir -p $(PREFIX)/lib ; fi
	if ( test ! -d $(PREFIX)/man ) ; then mkdir -p $(PREFIX)/man ; fi
	if ( test ! -d $(PREFIX)/man/man1 ) ; then mkdir -p $(PREFIX)/man/man1 ; fi
	if ( test ! -d $(PREFIX)/include ) ; then mkdir -p $(PREFIX)/include ; fi
	cp -f bzip2 $(PREFIX)/bin/bzip2
	cp -f bzip2 $(PREFIX)/bin/bunzip2
	cp -f bzip2 $(PREFIX)/bin/bzcat
	cp -f bzip2recover $(PREFIX)/bin/bzip2recover
	chmod a+x $(PREFIX)/bin/bzip2
	chmod a+x $(PREFIX)/bin/bunzip2
	chmod a+x $(PREFIX)/bin/bzcat
	chmod a+x $(PREFIX)/bin/bzip2recover
	cp -f bzip2.1 $(PREFIX)/man/man1
	chmod a+r $(PREFIX)/man/man1/bzip2.1
	cp -f bzlib.h $(PREFIX)/include
	chmod a+r $(PREFIX)/include/bzlib.h
	cp -f libbz2.a $(PREFIX)/lib
	chmod a+r $(PREFIX)/lib/libbz2.a
	cp -f bzgrep $(PREFIX)/bin/bzgrep
	ln -s -f $(PREFIX)/bin/bzgrep $(PREFIX)/bin/bzegrep
	ln -s -f $(PREFIX)/bin/bzgrep $(PREFIX)/bin/bzfgrep
	chmod a+x $(PREFIX)/bin/bzgrep
	cp -f bzmore $(PREFIX)/bin/bzmore
	ln -s -f $(PREFIX)/bin/bzmore $(PREFIX)/bin/bzless
	chmod a+x $(PREFIX)/bin/bzmore
	cp -f bzdiff $(PREFIX)/bin/bzdiff
	ln -s -f $(PREFIX)/bin/bzdiff $(PREFIX)/bin/bzcmp
	chmod a+x $(PREFIX)/bin/bzdiff
	cp -f bzgrep.1 bzmore.1 bzdiff.1 $(PREFIX)/man/man1
	chmod a+r $(PREFIX)/man/man1/bzgrep.1
	chmod a+r $(PREFIX)/man/man1/bzmore.1
	chmod a+r $(PREFIX)/man/man1/bzdiff.1
	echo ".so man1/bzgrep.1" > $(PREFIX)/man/man1/bzegrep.1
	echo ".so man1/bzgrep.1" > $(PREFIX)/man/man1/bzfgrep.1
	echo ".so man1/bzmore.1" > $(PREFIX)/man/man1/bzless.1
	echo ".so man1/bzdiff.1" > $(PREFIX)/man/man1/bzcmp.1

clean:
	rm -f *.o libbz2.a libnv.a bzip2 bzip2recover \
	sample1.rb2 sample2.rb2 sample3.rb2 \
	sample1.tst sample2.tst sample3.tst \
	libbz2-libnv.a bz2-driver-libnv bzip2-libnv \
	libbz2-dbus.a bz2-driver-dbus bzip2-dbus

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@
%.o: third_party/libnv/%.c
	$(CC) $(CFLAGS) -c $< -o $@
%.o: %.cc
	$(CXX) $(CXXFLAGS) -c $< -o $@
%.o: %.c++
	$(CXX) $(CXXFLAGS) -c $< -o $@
bzlib.pb.cc: bzlib.proto
	protoc --cpp_out=. $<
bzlib.grpc.pb.cc: bzlib.proto
	protoc --grpc_out=. --plugin=protoc-gen-grpc=$(GRPC_CPP_PLUGIN_PATH) $<
bz2-stub-grpc.o : bzlib.grpc.pb.cc bzlib.pb.cc
bzlib.capnp.c++: bzlib.capnp
	capnp compile -oc++ $<
%.capnp:  # disable implicit rule

bz2-stub-capnp.o : bzlib.capnp.c++

distclean: clean
	rm -f manual.ps manual.html manual.pdf

DISTNAME=bzip2-1.0.6
dist: check manual
	rm -f $(DISTNAME)
	ln -s -f . $(DISTNAME)
	tar cvf $(DISTNAME).tar \
	   $(DISTNAME)/blocksort.c \
	   $(DISTNAME)/huffman.c \
	   $(DISTNAME)/crctable.c \
	   $(DISTNAME)/randtable.c \
	   $(DISTNAME)/compress.c \
	   $(DISTNAME)/decompress.c \
	   $(DISTNAME)/stream.c \
	   $(DISTNAME)/bzlib.c \
	   $(DISTNAME)/bzip2.c \
	   $(DISTNAME)/bzip2recover.c \
	   $(DISTNAME)/bzlib.h \
	   $(DISTNAME)/bzlib_private.h \
	   $(DISTNAME)/Makefile \
	   $(DISTNAME)/LICENSE \
	   $(DISTNAME)/bzip2.1 \
	   $(DISTNAME)/bzip2.1.preformatted \
	   $(DISTNAME)/bzip2.txt \
	   $(DISTNAME)/words0 \
	   $(DISTNAME)/words1 \
	   $(DISTNAME)/words2 \
	   $(DISTNAME)/words3 \
	   $(DISTNAME)/sample1.ref \
	   $(DISTNAME)/sample2.ref \
	   $(DISTNAME)/sample3.ref \
	   $(DISTNAME)/sample1.bz2 \
	   $(DISTNAME)/sample2.bz2 \
	   $(DISTNAME)/sample3.bz2 \
	   $(DISTNAME)/dlltest.c \
	   $(DISTNAME)/manual.html \
	   $(DISTNAME)/manual.pdf \
	   $(DISTNAME)/manual.ps \
	   $(DISTNAME)/README \
	   $(DISTNAME)/README.COMPILATION.PROBLEMS \
	   $(DISTNAME)/README.XML.STUFF \
	   $(DISTNAME)/CHANGES \
	   $(DISTNAME)/libbz2.def \
	   $(DISTNAME)/libbz2.dsp \
	   $(DISTNAME)/dlltest.dsp \
	   $(DISTNAME)/makefile.msc \
	   $(DISTNAME)/unzcrash.c \
	   $(DISTNAME)/spewG.c \
	   $(DISTNAME)/mk251.c \
	   $(DISTNAME)/bzdiff \
	   $(DISTNAME)/bzdiff.1 \
	   $(DISTNAME)/bzmore \
	   $(DISTNAME)/bzmore.1 \
	   $(DISTNAME)/bzgrep \
	   $(DISTNAME)/bzgrep.1 \
	   $(DISTNAME)/Makefile-libbz2_so \
	   $(DISTNAME)/bz-common.xsl \
	   $(DISTNAME)/bz-fo.xsl \
	   $(DISTNAME)/bz-html.xsl \
	   $(DISTNAME)/bzip.css \
	   $(DISTNAME)/entities.xml \
	   $(DISTNAME)/manual.xml \
	   $(DISTNAME)/format.pl \
	   $(DISTNAME)/xmlproc.sh
	gzip -v $(DISTNAME).tar

# For rebuilding the manual from sources on my SuSE 9.1 box

MANUAL_SRCS= 	bz-common.xsl bz-fo.xsl bz-html.xsl bzip.css \
		entities.xml manual.xml

manual: manual.html manual.ps manual.pdf

manual.ps: $(MANUAL_SRCS)
	./xmlproc.sh -ps manual.xml

manual.pdf: $(MANUAL_SRCS)
	./xmlproc.sh -pdf manual.xml

manual.html: $(MANUAL_SRCS)
	./xmlproc.sh -html manual.xml
