EXE		= offzip
CFLAGS	+= -s -O2
CLIBS	+= -lz -lm
PREFIX	= /usr/local
BINDIR	= $(PREFIX)/bin
SRC		= $(EXE).c

all:
	$(CC) $(SRC) zopfli/*.c $(CFLAGS) -o $(EXE) $(CLIBS)

install:
	install -m 755 -d $(BINDIR)
	install -m 755 $(EXE) $(BINDIR)/$(EXE)

.PHONY:
	install
