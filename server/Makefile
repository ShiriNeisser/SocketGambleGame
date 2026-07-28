CC      = gcc
CFLAGS  = -Wall -Wextra -pthread -g
TARGET  = server
SRCS    = server_main.c globals.c network.c client_handler.c game.c thread_pool.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRCS) server.h thread_pool.h
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS)

clean:
	rm -f $(TARGET)
