UNAME=$(shell uname)
PREFIX=/usr/local
CFLAGS=-fPIC -fno-stack-protector
LDFLAGS=-ldl
PRELOAD_LIB=libtimebox-preload.so
GROUP=root

ifeq ($(UNAME), Linux)
CFLAGS=-fPIC -fno-stack-protector -D_GNU_SOURCE -DHAVE_GLIBC -DHAVE_RT
LDFLAGS=-ldl -lrt -Wl,--version-script=timebox.symvers
endif
ifeq ($(UNAME), Darwin)
PRELOAD_LIB=libtimebox-preload.dylib
GROUP=wheel
endif

all: timebox $(PRELOAD_LIB)

timebox: timebox.c
	gcc -DPRELOAD_LIB="\"$(PRELOAD_LIB)\"" $(CFLAGS) -rdynamic timebox.c -o timebox

$(PRELOAD_LIB): timebox-preload.c timebox.symvers
	gcc -shared $(CFLAGS) timebox-preload.c -o $(PRELOAD_LIB) $(LDFLAGS)

install:
	install -o root -g $(GROUP) -s -m 0755 timebox $(PREFIX)/bin
	install -o root -g $(GROUP) -m 0755 $(PRELOAD_LIB) $(PREFIX)/lib

clean:	
	rm -f timebox
	rm -f *.o
	rm -f $(PRELOAD_LIB)
