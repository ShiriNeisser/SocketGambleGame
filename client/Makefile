CC      = gcc
CFLAGS  = -Wall -Wextra -pthread -g
TARGET  = client
SRCS    = client_main.c globals.c udp.c tcp.c messaging.c

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS)

clean:
	rm -f $(TARGET)
