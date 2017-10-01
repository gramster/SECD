CC=bcc
CFLAGS=-ls -v
XSUF=.exe
OSUF=.obj

secd: secd$(XSUF)

secd$(XSUF): secd$(OSUF)
	$(CC) $(CFLAGS) secd$(OSUF)

secd$(OSUF): secd.c secd.h


