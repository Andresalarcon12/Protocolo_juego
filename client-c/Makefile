CC     = gcc
CFLAGS = -Wall -Wextra -pthread
TARGET = cliente
SRCS   = client.c

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS)

clean:
	rm -f $(TARGET)