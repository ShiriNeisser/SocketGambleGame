CC      = gcc
CFLAGS  = -Wall -Wextra -pthread -g
TARGET  = server
SRCS    = server_main.c globals.c network.c client_handler.c game.c

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS)

clean:
	rm -f $(TARGET)
