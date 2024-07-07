include config.mk

SRC = util.c sdwm.c 
OBJ = ${SRC:.c=.o}

all: sdwm

.c.o:
	${CC} -c ${CFLAGS} $<

${OBJ}: config.h config.mk

sdwm: ${OBJ}
	${CC} ${LDFLAGS} ${OBJ} -o $@

clean:
	rm -f ${OBJ} sdwm-${VERSION}.tar.gz sdwm

dist: clean
	mkdir -p sdwm-${VERSION}
	cp -R LICENSE Makefile README config.def.h config.mk \
		sdwm.1 util.h ${SRC} sdwm-${VERSION}
	tar -cf sdwm-${VERSION}.tar sdwm-${VERSION}
	gzip sdwm-${VERSION}.tar
	rm -rf sdwm-${VERSION}

install: all
	mkdir -p ${DESTDIR}${PREFIX}/bin
	cp -f sdwm ${DESTDIR}${PREFIX}/bin/
	chmod 755 ${DESTDIR}${PREFIX}/bin/sdwm
	mkdir -p ${DESTDIR}${MANPREFIX}/man1/
	sed "s/VERSION/${VERSION}/g" < sdwm.1 > ${DESTDIR}${MANPREFIX}/man1/sdwm.1
	chmod 644 ${DESTDIR}${MANPREFIX}/man1/sdwm.1

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/sdwm \
		${DESTDIR}${MANPREFIX}/man1/sdwm.1

.PHONY: all dist install clean uninstall
