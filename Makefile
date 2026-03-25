CC=gcc
CFLAGS=-Wall -Wextra -pthread

TARGET=build/example
SRC=examples/example.c threadpool.c

$(TARGET): $(SRC)
	mkdir -p build
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -r ./build