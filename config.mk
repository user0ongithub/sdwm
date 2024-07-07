VERSION = 0.0.1

# paths
PREFIX = /usr/local
MANPREFIX = ${PREFIX}/share/man

X11INC = -I /usr/X11R6/include
X11LIB = -L /usr/X11R6/lib

# Xinerama, comment if you don't want it
XINERAMALIBS  = -l Xinerama
XINERAMAFLAGS = -D XINERAMA

# includes and libs
INCS = ${X11INC}
LIBS = ${X11LIB} -l X11 ${XINERAMALIBS}

# flags
CPPFLAGS = -D VERSION=\"${VERSION}\" ${XINERAMAFLAGS}
#CFLAGS   = -g -std=c99 -pedantic -Wall -O0 ${INCS} ${CPPFLAGS}
CFLAGS   = -std=c17 -pedantic -Wall -Wno-deprecated-declarations -Os ${INCS} ${CPPFLAGS}
LDFLAGS  = ${LIBS}

# Solaris
#CFLAGS = -fast ${INCS} -DVERSION=\"${VERSION}\"
#LDFLAGS = ${LIBS}

# compiler and linker
CC = cc
