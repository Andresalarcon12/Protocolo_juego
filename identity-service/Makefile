CC     = gcc
CFLAGS = -Wall -Wextra -pthread
TARGET = identity

all: $(TARGET)

$(TARGET): identity.c
	$(CC) $(CFLAGS) -o $(TARGET) identity.c

clean:
	rm -f $(TARGET)