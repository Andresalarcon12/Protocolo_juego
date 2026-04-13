CC      = gcc
CFLAGS  = -Wall -Wextra -pthread
LIBS    = -lm
TARGET  = servidor
SRCS = main.c server.c logger.c game.c http_server.c auth_client.c
OBJS    = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)