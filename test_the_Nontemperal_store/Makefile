CC ?= gcc
CFLAGS ?= -O3 -march=native -std=c11 -Wall -Wextra
LDFLAGS ?=

TARGET := nt_store_bench
SRC := nt_store_bench.c

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
