CC=gcc
LDFLAGS=-L./vendor/raylib-5.5_linux_amd64/lib \
		-l:libraylib.a \
		-ldl \
		-lm \
		-lpthread
CFLAGS=-I./vendor/raylib-5.5_linux_amd64/include \
	   -I./vendor/subprocess
SOURCES=$(shell find . -type f -name *.c)
OBJECTS=$(patsubst %.c,%.o,$(SOURCES))

all: xtatus

xtatus: $(OBJECTS)
	$(CC) -o $@ $^ $(LDFLAGS)

$(OBJECTS): $(SOURCES)
	$(CC) -c -o $@ $(CFLAGS) $<

clean: $(OBJECTS) xtatus
	rm $^

.PHONY: all clean
