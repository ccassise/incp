.POSIX:
CC      = cc
CFLAGS  = -Wall -Wpedantic -Wextra
LDFLAGS =

TARGET  = incp
SOURCES = incp.c
OBJECTS = incp.o

sanitize: CFLAGS += -fsanitize=address
sanitize: LDFLAGS += -static-libasan

release: CFLAGS += -O3 -DNDEBUG

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

.PHONY: sanitize
sanitize: $(TARGET)

.PHONY: release
release: $(TARGET)

.PHONY: test
test:
	python3 tests/test_incp.py

.PHONY: clean
clean:
	rm -f $(TARGET) $(OBJECTS)
