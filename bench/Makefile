CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -g
TARGET  = auto_client

.PHONY: all clean

all: $(TARGET)

$(TARGET): auto_client.c
	$(CC) $(CFLAGS) -o $(TARGET) auto_client.c

clean:
	rm -f $(TARGET)
