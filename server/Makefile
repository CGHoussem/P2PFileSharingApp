LIBS = `pkg-config --cflags gtk+-3.0`
LIBS2 = `pkg-config --libs gtk+-3.0`

all:
	gcc -rdynamic $(LIBS) fileserver.c -o server.o $(LIBS2) -g
	
clean:
	rm *.o
	