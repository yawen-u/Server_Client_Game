OUTPUT = client
CFLAGS = -g -Wall -Wvla -I inc -D_REENTRANT
LFLAGS = -L lib -lSDL2 -lSDL2_image -lSDL2_ttf -lm

%.o: %.c %.h
	gcc $(CFLAGS) -c -o $@ $<

%.o: %.c
	gcc $(CFLAGS) -c -o $@ $<

all: $(OUTPUT)

runclient: $(OUTPUT)
	LD_LIBRARY_PATH=lib ./client hostname 1027

client: client.o
	gcc $(CFLAGS) -o $@ $^ $(LFLAGS) -pthread

server: server.o
	gcc $(CFLAGS) -o $@ $^ -pthread

clean:
	rm -f $(OUTPUT) *.o
