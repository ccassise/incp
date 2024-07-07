.POSIX:
CC      = cc
CFLAGS  = -Wall -Wpedantic -Wextra
LDFLAGS =

TARGET  = incp
SOURCES = incp.c
OBJECTS = incp.o

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

.PHONY: release
release:
	$(MAKE) CFLAGS="$(CFLAGS) -O3 -DNDEBUG"

.PHONY: sanitize-gcc
sanitize-gcc:
	$(MAKE) CFLAGS="$(CFLAGS) -fsanitize=address" LDFLAGS="$(LDFLAGS) -static-libasan"

.PHONY: sanitize-clang
sanitize-clang:
	$(MAKE) CFLAGS="$(CFLAGS) -fsanitize=address" LDFLAGS="$(LDFLAGS) -static-libsan"

.PHONY: test
test:
	python3 tests/test_incp.py

.PHONY: clean
clean:
	rm -f $(TARGET) $(OBJECTS)
