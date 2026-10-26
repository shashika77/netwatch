# ─────────────────────────────────────────────────────────────
#  NetWatch Pro 3.0 Makefile (macOS / Linux / Windows MinGW)
# ─────────────────────────────────────────────────────────────

UNAME_S := $(shell uname -s 2>/dev/null)
UNAME_M := $(shell uname -m 2>/dev/null)

ifeq ($(OS),Windows_NT)
    TARGET = netwatch.exe
    TARGET_PING = netping.exe
    LDFLAGS += -lws2_32 -liphlpapi
else
    TARGET = netwatch
    TARGET_PING = netping
    ifeq ($(UNAME_S),Darwin)
        CXX ?= clang++
    else
        CXX ?= g++
    endif
endif

CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic
LDFLAGS ?= -pthread

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
USER_PREFIX ?= $(HOME)/.local
USER_BINDIR ?= $(USER_PREFIX)/bin

all: $(TARGET) $(TARGET_PING)

$(TARGET): netwatch.cpp
	@echo "==> Compiling $(TARGET)..."
	$(CXX) $(CXXFLAGS) netwatch.cpp -o $(TARGET) $(LDFLAGS)
	@echo "==> Built $(TARGET) successfully."

$(TARGET_PING): $(TARGET)
ifeq ($(OS),Windows_NT)
	@copy $(TARGET) $(TARGET_PING) >nul
else
	@ln -sf $(TARGET) $(TARGET_PING)
endif
	@echo "==> Created $(TARGET_PING) alias."

# Standard system-wide install (may require sudo on macOS/Linux)
install: $(TARGET) $(TARGET_PING)
	@if [ ! -w "$(DESTDIR)$(BINDIR)" ] && [ -d "$(DESTDIR)$(BINDIR)" ] && [ "$$(id -u 2>/dev/null)" != "0" ]; then \
		echo ""; \
		echo "───────────────────────────────────────────────────────────────────"; \
		echo " [!] Permission denied: Cannot write to $(DESTDIR)$(BINDIR)"; \
		echo ""; \
		echo " You can install NetWatch in one of the following ways:"; \
		echo "   1) Install system-wide with sudo:"; \
		echo "        sudo make install"; \
		echo ""; \
		echo "   2) Install for your current user without sudo (Recommended):"; \
		echo "        make install-user"; \
		echo ""; \
		echo "   3) Custom prefix (e.g. Homebrew on Apple Silicon):"; \
		echo "        make install PREFIX=/opt/homebrew"; \
		echo "───────────────────────────────────────────────────────────────────"; \
		exit 1; \
	fi
	@echo "==> Installing to $(DESTDIR)$(BINDIR)..."
	@mkdir -p $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)
ifeq ($(OS),Windows_NT)
	install -m 755 $(TARGET_PING) $(DESTDIR)$(BINDIR)/$(TARGET_PING)
else
	ln -sf $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET_PING)
endif
	@echo "==> NetWatch Pro 3.0 installed successfully to $(DESTDIR)$(BINDIR)/$(TARGET)"
	@echo "    Run 'netwatch' to launch dashboard, or 'netping <host>' for targeted ping."

# User-level install (No root/sudo required, installs to ~/.local/bin)
install-user: $(TARGET) $(TARGET_PING)
	@echo "==> Installing to $(USER_BINDIR)..."
	@mkdir -p $(USER_BINDIR)
	install -m 755 $(TARGET) $(USER_BINDIR)/$(TARGET)
ifeq ($(OS),Windows_NT)
	install -m 755 $(TARGET_PING) $(USER_BINDIR)/$(TARGET_PING)
else
	ln -sf $(TARGET) $(USER_BINDIR)/$(TARGET_PING)
endif
	@echo "==> NetWatch Pro 3.0 installed successfully to $(USER_BINDIR)/$(TARGET)"
	@case ":$$PATH:" in \
		*":$(USER_BINDIR):"*) \
			echo "==> $(USER_BINDIR) is already in your PATH. You're ready to go!"; \
			;; \
		*) \
			echo ""; \
			echo " [!] Notice: $(USER_BINDIR) is not yet in your PATH."; \
			echo "     To run 'netwatch' from anywhere, add this line to your ~/.zshrc or ~/.bash_profile:"; \
			echo "         export PATH=\"\$$HOME/.local/bin:\$$PATH\""; \
			echo ""; \
			;; \
	esac

uninstall:
	@echo "==> Removing from $(DESTDIR)$(BINDIR)..."
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET_PING)
	@echo "==> Uninstalled successfully."

uninstall-user:
	@echo "==> Removing from $(USER_BINDIR)..."
	rm -f $(USER_BINDIR)/$(TARGET)
	rm -f $(USER_BINDIR)/$(TARGET_PING)
	@echo "==> Uninstalled from $(USER_BINDIR) successfully."

clean:
	@echo "==> Cleaning build artifacts..."
	rm -rf $(TARGET) $(TARGET_PING) *.exe *.dSYM *.o

help:
	@echo "NetWatch Pro 3.0 Build & Installation Options:"
	@echo "  make              - Build netwatch binary and netping alias"
	@echo "  make install      - Install to $(PREFIX)/bin (may require sudo)"
	@echo "  make install-user - Install to ~/.local/bin (no sudo required)"
	@echo "  make uninstall    - Remove installed files from $(PREFIX)/bin"
	@echo "  make uninstall-user - Remove installed files from ~/.local/bin"
	@echo "  make clean        - Clean build artifacts"

.PHONY: all clean install install-user uninstall uninstall-user help
