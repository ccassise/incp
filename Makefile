.POSIX:
CC      = cc
CFLAGS  = -Wall -Wpedantic -Wextra
LDFLAGS =

TARGET  = incp
SOURCES = incp.c
OBJECTS = incp.o

sanitize: CFLAGS += -fsanitize=address
sanitize: LDFLAGS += -static-libasan

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

.PHONY: sanitize
sanitize: $(TARGET)

.PHONY: test
test:
	python3 tests/test_incp.py

.PHONY: clean
clean:
	rm -f $(TARGET) $(OBJECTS)
