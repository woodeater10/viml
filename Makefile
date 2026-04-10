CC      := gcc
STD     := -std=c99
WARN    := -Wall -Wextra -Wpedantic -Wshadow \
           -Wstrict-prototypes -Wmissing-prototypes \
           -Wdouble-promotion -Wformat=2 -Wcast-align
OPT     := -O2 -g
CFLAGS  := $(STD) $(WARN) $(OPT)
TARGET  := viml

SRCS    := main.c terminal.c editor.c autocomplete.c highlight.c highlight.c
OBJS    := $(SRCS:.c=.o)
DEPS    := $(SRCS:.c=.d)

PREFIX  ?= /usr/local

# ── Default build ──────────────────────────────────────────────
.PHONY: all clean install uninstall debug

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

-include $(DEPS)

# ── Debug: AddressSanitizer + UBSanitizer ─────────────────────
debug: OPT = -O0 -g -fsanitize=address,undefined -fno-omit-frame-pointer
debug: $(TARGET)

# ── Install / uninstall ────────────────────────────────────────
install: $(TARGET)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(TARGET)

# ── Clean ──────────────────────────────────────────────────────
clean:
	rm -f $(OBJS) $(DEPS) $(TARGET)
