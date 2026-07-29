CC      = gcc
CFLAGS  = -Wall -Wextra -pthread -g
TARGET  = client
SRCS    = client_main.c globals.c udp.c tcp.c messaging.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS)
	chmod 755 $(TARGET)

clean:
	rm -f $(TARGET)
