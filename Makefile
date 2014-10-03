
all: massresolver

massresolver: massresolver.c
	$(CC) -O2 -o massresolver massresolver.c -lunbound -lldns

clean:
	rm -f massresolver

distclean: clean
