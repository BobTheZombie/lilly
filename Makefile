CC ?= cc
PREFIX ?= /usr
CFLAGS ?= -O2 -g
WARN = -std=c17 -Wall -Wextra -Wpedantic -Werror -Wformat=2 -Wshadow
.PHONY: all check clean install
all: build/lilly
build/lilly: src/lilly.c
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) $< $(LDFLAGS) -o $@
check: all
	python3 tests/test_supervisor.py
install: all
	install -Dm755 build/lilly $(DESTDIR)$(PREFIX)/bin/lilly
	install -Dm644 README.md $(DESTDIR)$(PREFIX)/share/doc/lilly/README.md
	install -Dm644 docs/COMPATIBILITY.md $(DESTDIR)$(PREFIX)/share/doc/lilly/COMPATIBILITY.md
	install -Dm644 docs/ROADMAP.md $(DESTDIR)$(PREFIX)/share/doc/lilly/ROADMAP.md
	install -Dm644 LICENSE $(DESTDIR)$(PREFIX)/share/licenses/lilly/LICENSE
	install -Dm644 examples/hello.service $(DESTDIR)$(PREFIX)/share/doc/lilly/examples/hello.service
clean:
	rm -rf build
