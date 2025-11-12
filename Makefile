CC = gcc
CFLAGS = -std=c11 -O2 -Wall -Wextra -g -pthread
SRCS = main.c colmena.c planificador.c monitor.c utils.c
OBJS = $(SRCS:.c=.o)
TARGET = colmena_sim

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: all
	./$(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)
