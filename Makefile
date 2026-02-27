CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -lpthread

TARGET = example client
OBJS = network_lib.o example.o client.o log.o cJSON.o

all: $(TARGET)

example: network_lib.o example.o log.o cJSON.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) -L. -llwrb

client: client.o
	$(CC) $(CFLAGS) -o $@ $^

network_lib.o: network_lib.c network_lib.h
	$(CC) $(CFLAGS) -c -o $@ $<

example.o: example.c network_lib.h
	$(CC) $(CFLAGS) -c -o $@ $<

client.o: client.c
	$(CC) $(CFLAGS) -c -o $@ $<

log.o: log.c log.h
	$(CC) $(CFLAGS) -c -o $@ $<

cJSON.o: cJSON.c cJSON.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: all clean
