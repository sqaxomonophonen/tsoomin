PKGS=x11 xext gl
CFLAGS+=-std=c11
#CFLAGS+=-O0 -g
CFLAGS+=-O2
CFLAGS+=-Wall
CFLAGS+=$(shell pkg-config --cflags $(PKGS))
LDLIBS+=$(shell pkg-config --libs $(PKGS))

all: tsoomin
tsoomin: tsoomin.c

install:
	install tsoomin /usr/local/bin

clean:
	rm -f tsoomin

