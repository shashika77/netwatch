# ─────────────────────────────────────────────────────────────
#  NetWatch Pro 3.0 Makefile (macOS / Linux / Windows MinGW)
# ─────────────────────────────────────────────────────────────

CXX ?= clang++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic
LDFLAGS ?= -pthread

ifeq ($(OS),Windows_NT)
    TARGET = netwatch.exe
    TARGET_PING = netping.exe
    LDFLAGS += -lws2_32 -liphlpapi
else
    TARGET = netwatch
    TARGET_PING = netping
endif

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

all: $(TARGET) $(TARGET_PING)

$(TARGET): netwatch.cpp
	$(CXX) $(CXXFLAGS) netwatch.cpp -o $(TARGET) $(LDFLAGS)

$(TARGET_PING): netping.cpp
	$(CXX) $(CXXFLAGS) netping.cpp -o $(TARGET_PING) $(LDFLAGS)

install: $(TARGET) $(TARGET_PING)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/netwatch
	install -m 755 $(TARGET_PING) $(DESTDIR)$(BINDIR)/netping

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/netwatch
	rm -f $(DESTDIR)$(BINDIR)/netping

clean:
	rm -rf netwatch netping *.exe *.dSYM *.o

.PHONY: all clean install uninstall
