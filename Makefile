# Adjust to your install; on Gentoo the headers are usually /usr/include
# and the stub library lives in /usr/lib64.
CFLAGS := -O2 -Wall
CFLAGS += -DUSE_TCL_STUBS $$(pkg-config --cflags tcl)
LDFLAGS := $$(pkg-config --libs tcl)

PLUGINS := bish.c exec.c getopt.c

libbishc.so: ${PLUGINS}
	${CC} -shared -fPIC ${CFLAGS} -o $@ ${PLUGINS} ${LDFLAGS}
