// ─────────────────────────────────────────────────────────────────────────────
//  NETWATCH PRO 3.0 – Production-Ready Terminal Network Diagnostics & Monitor
//  Visual multi-target ping monitor, latency analytics, and integrated network
//  toolbox (traceroute, dig/nslookup, port scan, curl/http, interfaces, routes,
//  netstat/ss, packet statistics, and custom command runner).
//
//  Zero external dependencies. Pure modern C++17.
//  Cross-platform: macOS, Linux, and Windows.
// ─────────────────────────────────────────────────────────────────────────────

#ifndef _WIN32
  #include <arpa/inet.h>
  #include <ifaddrs.h>
  #include <netdb.h>
  #include <net/if.h>
  #if defined(__APPLE__)
    #include <net/if_dl.h>
  #elif defined(__linux__)
    #include <netpacket/packet.h>
  #endif
  #include <netinet/icmp6.h>
  #include <netinet/in.h>
  #include <netinet/ip_icmp.h>
  #include <poll.h>
  #include <signal.h>
  #include <sys/ioctl.h>
  #include <sys/select.h>
  #include <sys/socket.h>
  #include <sys/time.h>
  #include <sys/types.h>
  #include <termios.h>
  #include <unistd.h>
  #include <fcntl.h>
#else
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <iphlpapi.h>
  #include <conio.h>
  #include <io.h>
  #pragma comment(lib, "ws2_32.lib")
  #pragma comment(lib, "iphlpapi.lib")
#endif

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using Clock = std::chrono::steady_clock;
using std::string;
using std::vector;
using std::deque;

// ═════════════════════════════════════════════════════════════════════════════
//  ANSI Terminal Colors & Visual Assets
// ═════════════════════════════════════════════════════════════════════════════
#define RST   "\033[0m"
#define BOLD  "\033[1m"
#define DIM   "\033[2m"
#define ITAL  "\033[3m"
#define UNDR  "\033[4m"
#define BLINK "\033[5m"
#define INV   "\033[7m"

// 256-color palette
#define C_RED    "\033[38;5;203m"
#define C_DRED   "\033[38;5;160m"
#define C_GRN    "\033[38;5;78m"
#define C_DGRN   "\033[38;5;28m"
#define C_YEL    "\033[38;5;221m"
#define C_ORG    "\033[38;5;208m"
#define C_CYN    "\033[38;5;81m"
#define C_BLU    "\033[38;5;75m"
#define C_PUR    "\033[38;5;141m"
#define C_PNK    "\033[38;5;213m"
#define C_WHT    "\033[38;5;255m"
#define C_GRY    "\033[38;5;244m"
#define C_DGRY   "\033[38;5;238m"

// Backgrounds
#define BG_HDR   "\033[48;5;24m\033[38;5;255m"
#define BG_SEL   "\033[48;5;237m\033[38;5;255m"
#define BG_BADGE "\033[48;5;235m"

static const char *PAL[] = {
    "\033[38;5;81m",   // Cyan
    "\033[38;5;213m",  // Pink
    "\033[38;5;221m",  // Yellow
    "\033[38;5;78m",   // Green
    "\033[38;5;208m",  // Orange
    "\033[38;5;141m",  // Purple
    "\033[38;5;75m",   // Blue
    "\033[38;5;203m"   // Coral
};
static const size_t PAL_SIZE = sizeof(PAL) / sizeof(PAL[0]);

static const char *MARK[] = {"●", "◆", "■", "▲", "★", "✚", "✦", "⯁"};
static const char *BARS[] = {" ", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
static const char *SPIN[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};

// ═════════════════════════════════════════════════════════════════════════════
//  Platform Primitives
// ═════════════════════════════════════════════════════════════════════════════
#ifdef _WIN32
  using socket_t = SOCKET;
  #define IS_VALIDSOCK(s) ((s) != INVALID_SOCKET)
  #define CLOSE_SOCK(s) closesocket(s)
#else
  using socket_t = int;
  #define IS_VALIDSOCK(s) ((s) >= 0)
  #define CLOSE_SOCK(s) close(s)
#endif

static void platformInitSockets() {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
}

static void platformCleanupSockets() {
#ifdef _WIN32
    WSACleanup();
#endif
}

// ═════════════════════════════════════════════════════════════════════════════
//  Terminal Controller (Raw mode, Resize, Safe Buffering)
// ═════════════════════════════════════════════════════════════
#ifndef _WIN32
  static termios g_origTerm;
  static bool g_rawEnabled = false;
#else
  static DWORD g_origConsoleMode = 0;
  static bool g_rawEnabled = false;
#endif

static volatile sig_atomic_t g_quitRequested = 0;
static volatile sig_atomic_t g_resized = 0;

static void handleSignal(int sig) {
#ifndef _WIN32
    if (sig == SIGWINCH) {
        g_resized = 1;
        return;
    }
#else
    (void)sig;
#endif
    g_quitRequested = 1;
}

static void termRestore() {
    const char *resetSeq = "\033[?25h\033[?1049l\033[0m";
    (void)!write(STDOUT_FILENO, resetSeq, strlen(resetSeq));
#ifndef _WIN32
    if (g_rawEnabled) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_origTerm);
        g_rawEnabled = false;
    }
#else
    if (g_rawEnabled) {
        HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
        SetConsoleMode(hIn, g_origConsoleMode);
        g_rawEnabled = false;
    }
#endif
    platformCleanupSockets();
}

static void termEnableRaw() {
#ifndef _WIN32
    if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &g_origTerm) == 0) {
        termios raw = g_origTerm;
        raw.c_lflag &= ~(ICANON | ECHO | ISIG);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
        g_rawEnabled = true;
    }
#else
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    GetConsoleMode(hIn, &g_origConsoleMode);
    DWORD mode = g_origConsoleMode;
    mode &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);
    SetConsoleMode(hIn, mode);
    DWORD outMode = 0;
    GetConsoleMode(hOut, &outMode);
    SetConsoleMode(hOut, outMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    g_rawEnabled = true;
#endif
    std::atexit(termRestore);
#ifndef _WIN32
    signal(SIGINT, handleSignal);
    signal(SIGTERM, handleSignal);
    signal(SIGHUP, handleSignal);
    signal(SIGQUIT, handleSignal);
    signal(SIGWINCH, handleSignal);
    signal(SIGPIPE, SIG_IGN);
#endif
    const char *enterAltScreen = "\033[?1049h\033[?25l\033[2J";
    (void)!write(STDOUT_FILENO, enterAltScreen, strlen(enterAltScreen));
}

static void getTermSize(int &cols, int &rows) {
    cols = 80; rows = 24;
#ifndef _WIN32
    winsize w{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
        if (w.ws_col > 0) cols = w.ws_col;
        if (w.ws_row > 0) rows = w.ws_row;
    }
#else
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    }
#endif
    if (cols < 20) cols = 20;
    if (rows < 5) rows = 5;
}

// ═════════════════════════════════════════════════════════════════════════════
//  String & Formatting Utilities
// ═════════════════════════════════════════════════════════════
static string fmt(const char *f, ...) {
    char b[2048];
    va_list ap;
    va_start(ap, f);
    vsnprintf(b, sizeof(b), f, ap);
    va_end(ap);
    return b;
}

static string col(const char *c, const string &s) {
    return string(c) + s + RST;
}
static string col(const string &c, const string &s) {
    return c + s + RST;
}
static string col(const char *c, const char *s) {
    return string(c) + string(s) + RST;
}

static string repeat(const string &s, int n) {
    string o;
    if (n <= 0) return o;
    o.reserve(s.size() * n);
    for (int i = 0; i < n; i++) o += s;
    return o;
}

static string timeStr(time_t t, const char *format = "%H:%M:%S") {
    char b[64];
    struct tm tmv;
#ifndef _WIN32
    localtime_r(&t, &tmv);
#else
    localtime_s(&tmv, &t);
#endif
    strftime(b, sizeof(b), format, &tmv);
    return b;
}

static string fmtSpan(long s) {
    if (s < 0) s = 0;
    if (s < 60) return fmt("%lds", s);
    if (s < 3600) return fmt("%ldm%02lds", s / 60, s % 60);
    return fmt("%ldh%02ldm", s / 3600, (s % 3600) / 60);
}

static string fmtDur(long s) {
    if (s < 0) s = 0;
    return fmt("%02ld:%02ld:%02ld", s / 3600, (s % 3600) / 60, s % 60);
}

static const char *latColor(double ms) {
    if (ms < 0) return C_RED;
    if (ms < 30) return C_GRN;
    if (ms < 80) return C_CYN;
    if (ms < 150) return C_YEL;
    if (ms < 300) return C_ORG;
    return C_RED;
}

static string trim(const string &s) {
    size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t' || s[start] == '\r' || s[start] == '\n')) start++;
    size_t end = s.size();
    while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r' || s[end - 1] == '\n')) end--;
    return s.substr(start, end - start);
}

static string toLower(string s) {
    for (char &c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

static size_t visibleWidth(const string &s) {
    size_t width = 0;
    size_t i = 0;
    size_t len = s.size();
    while (i < len) {
        if (s[i] == '\033') {
            i++;
            if (i < len && s[i] == '[') {
                i++;
                while (i < len && (s[i] < '@' || s[i] > '~')) i++;
                if (i < len) i++;
            }
            continue;
        }
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) {
            if (c >= 32 && c != 127) width++;
            i++;
        } else if ((c & 0xE0) == 0xC0) {
            width++;
            i += (i + 1 < len) ? 2 : 1;
        } else if ((c & 0xF0) == 0xE0) {
            width++;
            i += (i + 2 < len) ? 3 : 1;
        } else if ((c & 0xF8) == 0xF0) {
            width += 2;
            i += (i + 3 < len) ? 4 : 1;
        } else {
            i++;
        }
    }
    return width;
}

static string truncateToWidth(const string &s, size_t maxCols) {
    size_t width = 0;
    size_t i = 0;
    size_t len = s.size();
    size_t cutByte = len;
    bool hasEscape = false;

    while (i < len) {
        if (s[i] == '\033') {
            hasEscape = true;
            i++;
            if (i < len && s[i] == '[') {
                i++;
                while (i < len && (s[i] < '@' || s[i] > '~')) i++;
                if (i < len) i++;
            }
            continue;
        }
        size_t charW = 1;
        size_t nextI = i + 1;
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) {
            charW = (c >= 32 && c != 127) ? 1 : 0;
            nextI = i + 1;
        } else if ((c & 0xE0) == 0xC0) {
            charW = 1;
            nextI = (i + 1 < len) ? i + 2 : len;
        } else if ((c & 0xF0) == 0xE0) {
            charW = 1;
            nextI = (i + 2 < len) ? i + 3 : len;
        } else if ((c & 0xF8) == 0xF0) {
            charW = 2;
            nextI = (i + 3 < len) ? i + 4 : len;
        }

        if (width + charW > maxCols) {
            cutByte = i;
            break;
        }
        width += charW;
        i = nextI;
    }

    if (cutByte >= len) return s;
    string res = s.substr(0, cutByte);
    if (hasEscape) res += "\033[0m";
    return res;
}

static string cleanHost(const string &raw) {
    string s = trim(raw);
    if (s.empty()) return s;
    size_t scheme = s.find("://");
    if (scheme != string::npos) s = s.substr(scheme + 3);
    size_t slash = s.find('/');
    if (slash != string::npos) s = s.substr(0, slash);
    if (!s.empty() && s[0] == '[') {
        size_t bClose = s.find(']');
        if (bClose != string::npos) s = s.substr(1, bClose - 1);
    } else {
        size_t fCol = s.find(':');
        size_t lCol = s.rfind(':');
        if (fCol != string::npos && fCol == lCol) s = s.substr(0, fCol);
    }
    return trim(s);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Process Execution Helper
// ═════════════════════════════════════════════════════════════
static vector<string> runCmdLines(const string &cmd, int maxLines = 1000) {
    vector<string> out;
#ifndef _WIN32
    FILE *pipe = popen(cmd.c_str(), "r");
#else
    FILE *pipe = _popen(cmd.c_str(), "r");
#endif
    if (!pipe) return out;
    char buffer[2048];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        string l = buffer;
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
        out.push_back(l);
        if ((int)out.size() >= maxLines) break;
    }
#ifndef _WIN32
    pclose(pipe);
#else
    _pclose(pipe);
#endif
    return out;
}

// ═════════════════════════════════════════════════════════════════════════════
//  ICMP Ping Engine
// ═════════════════════════════════════════════════════════════
struct Probe {
    bool ok = false;
    double ms = 0;
    double dnsMs = 0;
    int ttl = 0;
    string ip;
    string err;
};

static uint16_t icmpChecksum(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t sum = 0;
    while (len > 1) {
        sum += (uint32_t)((p[0] << 8) | p[1]);
        p += 2;
        len -= 2;
    }
    if (len) sum += (uint32_t)(p[0] << 8);
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return htons((uint16_t)~sum);
}

static bool doSystemPing(const string &targetHost, int fam, int timeoutSec, Probe &p) {
    auto t0 = Clock::now();
    string cmd;
#if defined(_WIN32)
    cmd = fmt("ping %s -n 1 -w %d %s", (fam == AF_INET6 ? "-6" : "-4"), timeoutSec * 1000, targetHost.c_str());
#elif defined(__APPLE__)
    if (fam == AF_INET6) {
        cmd = fmt("ping6 -c 1 -t %d %s 2>&1", timeoutSec, targetHost.c_str());
    } else {
        cmd = fmt("ping -c 1 -t %d %s 2>&1", timeoutSec, targetHost.c_str());
    }
#else
    if (fam == AF_INET6) {
        cmd = fmt("ping -6 -c 1 -W %d %s 2>&1 || ping6 -c 1 -W %d %s 2>&1", timeoutSec, targetHost.c_str(), timeoutSec, targetHost.c_str());
    } else {
        cmd = fmt("ping -c 1 -W %d %s 2>&1", timeoutSec, targetHost.c_str());
    }
#endif
    auto lines = runCmdLines(cmd, 15);
    auto t1 = Clock::now();
    double fallbackMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

    for (const auto &l : lines) {
        string lower = toLower(l);
        size_t tPos = lower.find("time=");
        if (tPos == string::npos) tPos = lower.find("time<");

        if (tPos != string::npos && (lower.find("bytes") != string::npos || lower.find("reply from") != string::npos)) {
            double ms = 0;
            if (lower[tPos + 4] == '<') {
                ms = 0.5;
            } else {
                sscanf(l.c_str() + tPos + 5, "%lf", &ms);
            }
            if (ms <= 0) ms = fallbackMs;
            p.ok = true;
            p.ms = ms;

            size_t ttlPos = lower.find("ttl=");
            if (ttlPos != string::npos) {
                int ttl = 0;
                sscanf(lower.c_str() + ttlPos + 4, "%d", &ttl);
                p.ttl = ttl;
            }
            p.err.clear();
            return true;
        }
    }
    p.err = "Request timed out";
    return false;
}

static Probe doPing(const string &host, int timeoutSec, uint16_t seq) {
    Probe p;
    auto d0 = Clock::now();
    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    int gai = getaddrinfo(host.c_str(), nullptr, &hints, &res);
    p.dnsMs = std::chrono::duration<double, std::milli>(Clock::now() - d0).count();
    if (gai != 0 || !res) {
        p.err = "DNS resolution failed";
        return p;
    }

    addrinfo *pick = nullptr;
    for (addrinfo *a = res; a; a = a->ai_next) {
        if (a->ai_family == AF_INET) { pick = a; break; }
    }
    if (!pick) {
        for (addrinfo *a = res; a; a = a->ai_next) {
            if (a->ai_family == AF_INET6) { pick = a; break; }
        }
    }
    if (!pick) {
        freeaddrinfo(res);
        p.err = "No IP address found";
        return p;
    }

    sockaddr_storage dst{};
    socklen_t dlen = (socklen_t)pick->ai_addrlen;
    memcpy(&dst, pick->ai_addr, dlen);
    int fam = pick->ai_family;
    freeaddrinfo(res);

    char ipbuf[INET6_ADDRSTRLEN] = "";
    if (fam == AF_INET) {
        inet_ntop(AF_INET, &((sockaddr_in *)&dst)->sin_addr, ipbuf, sizeof(ipbuf));
    } else {
        inet_ntop(AF_INET6, &((sockaddr_in6 *)&dst)->sin6_addr, ipbuf, sizeof(ipbuf));
    }
    p.ip = ipbuf;

#ifndef _WIN32
    bool trySocket = true;
#if defined(__APPLE__)
    if (getuid() != 0) {
        trySocket = false;
    }
#endif

    if (trySocket) {
        int s = socket(fam, SOCK_DGRAM, fam == AF_INET ? IPPROTO_ICMP : IPPROTO_ICMPV6);
        if (s < 0) {
            s = socket(fam, SOCK_RAW, fam == AF_INET ? IPPROTO_ICMP : IPPROTO_ICMPV6);
        }

        if (s >= 0) {
            uint8_t pkt[64];
            memset(pkt, 0, sizeof(pkt));
            for (size_t i = 8; i < sizeof(pkt); i++) pkt[i] = (uint8_t)(i & 0xFF);
            uint16_t id = (uint16_t)getpid();
            pkt[0] = (fam == AF_INET) ? ICMP_ECHO : ICMP6_ECHO_REQUEST;
            pkt[1] = 0;
            pkt[4] = (uint8_t)(id >> 8);  pkt[5] = (uint8_t)id;
            pkt[6] = (uint8_t)(seq >> 8); pkt[7] = (uint8_t)seq;
            if (fam == AF_INET) {
                uint16_t cs = icmpChecksum(pkt, sizeof(pkt));
                memcpy(pkt + 2, &cs, 2);
            }

            auto t0 = Clock::now();
            auto deadline = t0 + std::chrono::seconds(timeoutSec);

            if (sendto(s, (const char *)pkt, sizeof(pkt), 0, (sockaddr *)&dst, dlen) >= 0) {
                uint8_t buf[2048];
                for (;;) {
                    long left = (long)std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
                    if (left <= 0) break;
                    pollfd pfd{s, POLLIN, 0};
                    int pr = poll(&pfd, 1, (int)left);
                    if (pr < 0) {
                        if (errno == EINTR) continue;
                        break;
                    }
                    if (pr == 0) break;

                    sockaddr_storage from{};
                    socklen_t flen = sizeof(from);
                    ssize_t n = recvfrom(s, (char *)buf, sizeof(buf), 0, (sockaddr *)&from, &flen);
                    auto t1 = Clock::now();
                    if (n <= 0) continue;

                    const uint8_t *icmp = buf;
                    size_t len = (size_t)n;
                    int ttl = 0;
                    if (fam == AF_INET && len >= 20 && (buf[0] >> 4) == 4) {
                        size_t ihl = (size_t)(buf[0] & 0x0F) * 4;
                        if (ihl <= len) {
                            ttl = buf[8];
                            icmp += ihl;
                            len -= ihl;
                        }
                    }
                    if (len < 8) continue;

                    uint8_t replyType = (fam == AF_INET) ? ICMP_ECHOREPLY : ICMP6_ECHO_REPLY;
                    uint16_t rseq = (uint16_t)((icmp[6] << 8) | icmp[7]);
                    if (icmp[0] == replyType && rseq == seq) {
                        p.ok = true;
                        p.ttl = ttl;
                        p.ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
                        break;
                    }
                    if (fam == AF_INET && icmp[0] == ICMP_UNREACH) {
                        p.err = "Destination unreachable";
                        break;
                    }
                }
            }
            CLOSE_SOCK(s);
        }
    }

    if (!p.ok) {
        doSystemPing(p.ip.empty() ? host : p.ip, fam, timeoutSec, p);
    }
#else
    doSystemPing(p.ip.empty() ? host : p.ip, fam, timeoutSec, p);
#endif
    return p;
}

// ═════════════════════════════════════════════════════════════════════════════
//  Data Models
// ═════════════════════════════════════════════════════════════
struct Site {
    string name;
    string host;
    string ip;
    uint16_t seq = 0;
    bool ok = false;
    double ms = 0, dnsMs = 0;
    int ttl = 0;
    int sent = 0, recv = 0;
    double sum = 0, mn = 0, mx = 0;
    double lastRtt = -1, jitter = 0;
    double p50 = 0, p95 = 0, p99 = 0, sd = 0, loss60 = 0;
    int consecLost = 0, streak = 0;
    time_t firstLost = 0;
    deque<double> hist;  // <0 = lost packet
    string err;

    void resetStats() {
        ok = false; ms = 0; dnsMs = 0; ttl = 0; sent = recv = 0;
        sum = mn = mx = 0; lastRtt = -1; jitter = 0;
        p50 = p95 = p99 = sd = loss60 = 0;
        consecLost = streak = 0; firstLost = 0;
        hist.clear(); err.clear();
    }
};

struct Outage {
    size_t site;
    time_t start;
    time_t end; // 0 = ongoing
    string reason;
};

// ═════════════════════════════════════════════════════════════════════════════
//  Global State
// ═════════════════════════════════════════════════════════════
static int INTERVAL = 3;       // Ping interval (seconds)
static int TIMEOUT = 2;        // Reply timeout (seconds)
static const size_t HIST_CAP = 1000;

static vector<Site> sites;
static vector<Outage> outages;
static deque<string> events;
static int ROUND = 0;
static int VIEW = 1;          // 1: Overview, 2: Compare, 3: Detail, 4: Outages, 5: Toolbox, 6: Help
static int SEL_SITE = 0;
static bool PAUSED = false;
static bool LOGSCALE = false;
static bool ALERTS = false;
static FILE *g_log = nullptr;
static string g_logPath;
static Clock::time_point START_TIME;

// Active Modal Input State
enum class ModalMode { NONE, ADD_HOST, SET_INTERVAL, TOOL_CUSTOM_HOST, TOOL_CUSTOM_CMD, TOOL_SEARCH };
static ModalMode g_modal = ModalMode::NONE;
static string g_inputBuf;
static string g_inputPrompt;

// ═════════════════════════════════════════════════════════════════════════════
//  Toolbox Engine & Diagnostic Results
// ═════════════════════════════════════════════════════════════
enum class ToolType {
    NONE,
    TRACEROUTE,
    DNS_LOOKUP,
    PORT_SCAN,
    HTTP_PROBE,
    INTERFACES,
    ROUTES,
    SOCKETS,
    TRAFFIC_STATS,
    CUSTOM_CMD
};

struct ToolResult {
    ToolType type = ToolType::NONE;
    string title;
    string targetHost;
    string targetIp;
    string command;
    vector<string> rawLines;
    vector<string> formattedRows;
    bool running = false;
    double durationMs = 0;
    string statusText;
    int scrollOffset = 0;
    bool rawView = false;
    string searchQuery;
};

static ToolResult g_toolResult;
static std::thread g_toolWorker;
static std::mutex g_toolMutex;
static int TOOL_ACTIVE_ID = 1;

// Forward Declarations
static string detectGateway();
static void addEvent(const string &s);
static void notifyOS(const string &msg);
static int hopsFrom(int ttl);

// ═════════════════════════════════════════════════════════════════════════════
//  Statistics Calculations
// ═════════════════════════════════════════════════════════════
static double calcPercentile(const vector<double> &sorted, double q) {
    if (sorted.empty()) return 0;
    size_t idx = (size_t)std::ceil(q * sorted.size());
    if (idx < 1) idx = 1;
    if (idx > sorted.size()) idx = sorted.size();
    return sorted[idx - 1];
}

static void recomputeSiteStats(Site &s) {
    vector<double> validRtt;
    validRtt.reserve(s.hist.size());
    for (double x : s.hist) {
        if (x >= 0) validRtt.push_back(x);
    }
    if (!validRtt.empty()) {
        double mean = 0;
        for (double x : validRtt) mean += x;
        mean /= validRtt.size();
        double var = 0;
        for (double x : validRtt) var += (x - mean) * (x - mean);
        s.sd = std::sqrt(var / validRtt.size());
        std::sort(validRtt.begin(), validRtt.end());
        s.p50 = calcPercentile(validRtt, 0.50);
        s.p95 = calcPercentile(validRtt, 0.95);
        s.p99 = calcPercentile(validRtt, 0.99);
    }
    size_t n = std::min<size_t>(60, s.hist.size()), lost = 0;
    for (size_t i = s.hist.size() - n; i < s.hist.size(); i++) {
        if (s.hist[i] < 0) lost++;
    }
    s.loss60 = n ? (lost * 100.0 / n) : 0;
}

static long downtimeOf(size_t i) {
    long t = 0;
    time_t now = time(nullptr);
    for (const auto &o : outages) {
        if (o.site == i) {
            t += (long)((o.end ? o.end : now) - o.start);
        }
    }
    return t;
}

static int outageCountOf(size_t i) {
    int c = 0;
    for (const auto &o : outages) if (o.site == i) c++;
    return c;
}

static void mergeProbe(size_t i, const Probe &p) {
    Site &s = sites[i];
    bool first = (s.sent == 0);
    time_t now = time(nullptr);
    s.sent++;
    s.err = p.err;
    s.ok = p.ok;
    s.dnsMs = p.dnsMs;
    if (!p.ip.empty()) s.ip = p.ip;
    if (p.ttl) s.ttl = p.ttl;

    if (g_log) {
        fprintf(g_log, "%s,%s,%s,%d,%.2f\n",
                timeStr(now, "%Y-%m-%d %H:%M:%S").c_str(),
                s.host.c_str(), s.ip.c_str(), p.ok ? 1 : 0, p.ok ? p.ms : -1.0);
        fflush(g_log);
    }

    if (p.ok) {
        s.recv++;
        s.ms = p.ms;
        s.sum += p.ms;
        if (s.recv == 1 || p.ms < s.mn) s.mn = p.ms;
        if (p.ms > s.mx) s.mx = p.ms;
        if (s.lastRtt >= 0) s.jitter += (std::fabs(p.ms - s.lastRtt) - s.jitter) / 16.0;
        s.lastRtt = p.ms;
        s.hist.push_back(p.ms);

        if (s.consecLost >= 2) {
            for (size_t k = outages.size(); k-- > 0;) {
                if (outages[k].site == i && outages[k].end == 0) {
                    outages[k].end = now;
                    break;
                }
            }
            string msg = s.name + " recovered after " + fmtSpan((long)(now - s.firstLost));
            addEvent(msg);
            notifyOS(msg);
        } else if (first) {
            addEvent(s.name + " online (" + (s.ip.empty() ? s.host : s.ip) + ")");
        }
        s.consecLost = 0;
        s.streak = s.streak > 0 ? s.streak + 1 : 1;
    } else {
        s.ms = -1;
        s.hist.push_back(-1);
        if (s.consecLost == 0) s.firstLost = now;
        s.consecLost++;
        s.streak = s.streak < 0 ? s.streak - 1 : -1;
        if (s.consecLost == 2) {
            outages.push_back({i, s.firstLost, 0, p.err});
            string msg = s.name + " is DOWN (" + p.err + ")";
            addEvent(msg);
            notifyOS(msg);
            if (ALERTS) {
                (void)!write(STDOUT_FILENO, "\a", 1);
            }
        } else if (first) {
            addEvent(s.name + " initial ping failed: " + p.err);
        }
    }
    while (s.hist.size() > HIST_CAP) s.hist.pop_front();
    recomputeSiteStats(s);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Helpers: Gateway, Notifications, Hops
// ═════════════════════════════════════════════════════════════
static string detectGateway() {
#ifndef _WIN32
    #if defined(__APPLE__)
        auto lines = runCmdLines("route -n get default 2>/dev/null", 15);
        for (const auto &l : lines) {
            size_t pos = l.find("gateway:");
            if (pos != string::npos) {
                return trim(l.substr(pos + 8));
            }
        }
    #else
        auto lines = runCmdLines("ip route show default 2>/dev/null", 5);
        for (const auto &l : lines) {
            size_t pos = l.find("via ");
            if (pos != string::npos) {
                string sub = l.substr(pos + 4);
                size_t sp = sub.find(' ');
                return sp != string::npos ? sub.substr(0, sp) : sub;
            }
        }
    #endif
#else
    auto lines = runCmdLines("route print 0.0.0.0", 30);
    for (const auto &l : lines) {
        if (l.find("0.0.0.0") != string::npos) {
            std::stringstream ss(l);
            string dest, mask, gw;
            ss >> dest >> mask >> gw;
            if (!gw.empty() && gw != "0.0.0.0") return gw;
        }
    }
#endif
    return "";
}

static void addEvent(const string &s) {
    events.push_front(timeStr(time(nullptr), "%H:%M:%S") + "  " + s);
    while (events.size() > 10) events.pop_back();
}

static void notifyOS(const string &msg) {
    if (!ALERTS) return;
#if defined(__APPLE__)
    string safe;
    for (char c : msg) {
        if (isalnum((unsigned char)c) || c == ' ' || c == '.' || c == '-' || c == ':') safe += c;
    }
    string cmd = "osascript -e 'display notification \"" + safe + "\" with title \"NetWatch Pro\"' >/dev/null 2>&1 &";
    (void)!system(cmd.c_str());
#endif
}

static int hopsFrom(int ttl) {
    if (ttl <= 0) return -1;
    int init = (ttl <= 64) ? 64 : (ttl <= 128) ? 128 : 255;
    return init - ttl;
}

struct SiteHealth {
    const char *badge;
    const char *color;
    const char *tag;
};

static SiteHealth healthOf(const Site &s) {
    if (s.sent == 0) return {"⋯ INIT", C_GRY, "Initializing"};
    if (s.consecLost >= 2) return {"✕ DOWN", C_RED, "Offline"};
    if (s.loss60 >= 10 || s.consecLost == 1) return {"▲ LOSS", C_ORG, "Packet Loss"};
    if (s.p95 > 200 || s.jitter > 30) return {"◆ HIGH-LAT", C_YEL, "Degraded"};
    if (s.p95 > 80) return {"● GOOD", C_CYN, "Fair"};
    return {"✔ EXCELLENT", C_GRN, "Healthy"};
}

// ═════════════════════════════════════════════════════════════════════════════
//  Network Toolbox Implementations
// ═════════════════════════════════════════════════════════════

// 1. Visual Traceroute (traceroute / tracert / mtr)
static void runTracerouteTool(const string &target) {
    std::lock_guard<std::mutex> lock(g_toolMutex);
    g_toolResult.type = ToolType::TRACEROUTE;
    g_toolResult.title = "Visual Hop-by-Hop Traceroute (MTR / Traceroute)";
    g_toolResult.targetHost = target;
    g_toolResult.running = true;
    g_toolResult.scrollOffset = 0;
    g_toolResult.rawLines.clear();
    g_toolResult.formattedRows.clear();
    g_toolResult.statusText = "Tracing route to " + target + "...";

#ifndef _WIN32
    g_toolResult.command = "traceroute -n -m 25 -q 2 -w 2 " + target + " 2>&1";
#else
    g_toolResult.command = "tracert -d -h 25 " + target;
#endif

    g_toolResult.formattedRows.push_back(col(C_CYN, BOLD + fmt("%-4s  %-24s  %-10s  %-10s  %-10s  %-8s  %s",
        "Hop", "Node IP / Host", "Min RTT", "Avg RTT", "Max RTT", "Loss", "Visual Latency Graph")));
    g_toolResult.formattedRows.push_back(col(C_DGRY, repeat("─", 88)));

    if (g_toolWorker.joinable()) g_toolWorker.join();
    g_toolWorker = std::thread([target]() {
        auto t0 = Clock::now();
#ifndef _WIN32
        string cmd = "traceroute -n -m 25 -q 2 -w 2 " + target + " 2>/dev/null || tracepath -n -m 25 " + target + " 2>&1";
#else
        string cmd = "tracert -d -h 25 " + target;
#endif
        auto lines = runCmdLines(cmd, 35);
        auto t1 = Clock::now();
        double dur = std::chrono::duration<double, std::milli>(t1 - t0).count();

        std::lock_guard<std::mutex> lk(g_toolMutex);
        g_toolResult.durationMs = dur;
        g_toolResult.rawLines = lines;

        int totalHops = 0, timeouts = 0;
        for (const auto &raw : lines) {
            string l = trim(raw);
            if (l.empty() || l[0] == 't' || l.find("Tracing") != string::npos || l.find("traceroute to") != string::npos) {
                continue;
            }
            std::stringstream ss(l);
            int hop = 0;
            if (!(ss >> hop)) continue;
            totalHops++;

            string ip = "*";
            vector<double> rtts;
            string token;
            while (ss >> token) {
                if (token == "*") {
                    // lost probe
                } else if (token == "ms") {
                    // ignore unit
                } else {
                    double val = 0;
                    if (sscanf(token.c_str(), "%lf", &val) == 1) {
                        rtts.push_back(val);
                    } else if (token.find('.') != string::npos || token.find(':') != string::npos) {
                        ip = token;
                    }
                }
            }

            string hopCol = (hop == 1) ? C_GRN : (ip == "*") ? C_RED : C_CYN;
            string minStr = "--", avgStr = "--", maxStr = "--", lossStr = "0%";
            string graph = col(C_GRY, "·");

            if (rtts.empty()) {
                timeouts++;
                lossStr = col(C_RED, "100%");
                graph = col(C_RED, "✕ [ TIMEOUT ]");
            } else {
                double mn = rtts[0], mx = rtts[0], sum = 0;
                for (double r : rtts) {
                    if (r < mn) mn = r;
                    if (r > mx) mx = r;
                    sum += r;
                }
                double avg = sum / rtts.size();
                minStr = fmt("%.1fms", mn);
                avgStr = fmt("%.1fms", avg);
                maxStr = fmt("%.1fms", mx);
                int fill = std::max(1, std::min(8, (int)(avg / 25.0 * 8)));
                graph = string(latColor(avg)) + BARS[fill - 1] + RST + " " + col(latColor(avg), fmt("%.1fms", avg));
            }

            string typeTag = (hop == 1) ? col(C_GRN, "[Gateway]") : (ip == target) ? col(C_PNK, "[Destination]") : "";
            string row = fmt("%-4d  %-24s  %-10s  %-10s  %-10s  %-8s  %s  %s",
                hop, ip.c_str(), minStr.c_str(), avgStr.c_str(), maxStr.c_str(), lossStr.c_str(), graph.c_str(), typeTag.c_str());
            g_toolResult.formattedRows.push_back(row);
        }

        g_toolResult.statusText = fmt("Route completed in %.0fms: %d hops inspected (%d timed out)",
                                      dur, totalHops, timeouts);
        g_toolResult.running = false;
    });
}

// 2. DNS Query Tool (dig / nslookup)
static void runDnsTool(const string &target) {
    std::lock_guard<std::mutex> lock(g_toolMutex);
    g_toolResult.type = ToolType::DNS_LOOKUP;
    g_toolResult.title = "DNS Records & Resolver Inspector (dig / nslookup)";
    g_toolResult.targetHost = target;
    g_toolResult.running = true;
    g_toolResult.scrollOffset = 0;
    g_toolResult.rawLines.clear();
    g_toolResult.formattedRows.clear();
    g_toolResult.statusText = "Querying DNS records for " + target + "...";
    g_toolResult.command = "dig +noall +answer +stats " + target + " A " + target + " AAAA " + target + " MX " + target + " TXT " + target + " NS 2>&1";

    g_toolResult.formattedRows.push_back(col(C_CYN, BOLD + fmt("%-8s  %-30s  %-8s  %s", "Record", "Domain / Name", "TTL", "Value / Destination")));
    g_toolResult.formattedRows.push_back(col(C_DGRY, repeat("─", 80)));

    if (g_toolWorker.joinable()) g_toolWorker.join();
    g_toolWorker = std::thread([target]() {
        auto t0 = Clock::now();
        string cmd = "dig +noall +answer +stats " + target + " A " + target + " AAAA " + target + " MX " + target + " TXT " + target + " NS 2>&1";
        auto lines = runCmdLines(cmd, 60);
        auto t1 = Clock::now();
        double dur = std::chrono::duration<double, std::milli>(t1 - t0).count();

        std::lock_guard<std::mutex> lk(g_toolMutex);
        g_toolResult.durationMs = dur;
        g_toolResult.rawLines = lines;

        string resolver = "System Default", queryTime = fmt("%.0f ms", dur);
        int recordCount = 0;

        for (const auto &raw : lines) {
            string l = trim(raw);
            if (l.empty()) continue;
            if (l.find(";; Query time:") != string::npos) {
                queryTime = trim(l.substr(14));
                continue;
            }
            if (l.find(";; SERVER:") != string::npos) {
                resolver = trim(l.substr(10));
                continue;
            }
            if (l[0] == ';') continue;

            std::stringstream ss(l);
            string name, ttl, inClass, type;
            if (ss >> name >> ttl >> inClass >> type) {
                string val;
                std::getline(ss, val);
                val = trim(val);
                recordCount++;

                string badgeColor = C_CYN;
                if (type == "A") badgeColor = C_GRN;
                else if (type == "AAAA") badgeColor = C_BLU;
                else if (type == "MX") badgeColor = C_YEL;
                else if (type == "TXT") badgeColor = C_PUR;
                else if (type == "NS") badgeColor = C_ORG;

                string badge = col(badgeColor, string(BOLD) + fmt("[%-4s]", type.c_str()));
                string row = fmt("%-16s  %-30s  %-8s  %s",
                    badge.c_str(), name.substr(0, 28).c_str(), ttl.c_str(), val.c_str());
                g_toolResult.formattedRows.push_back(row);
            }
        }

        // Fallback to nslookup if dig returned no records
        if (recordCount == 0) {
            auto nsLines = runCmdLines("nslookup " + target + " 2>&1", 40);
            for (const auto &nl : nsLines) {
                g_toolResult.rawLines.push_back(nl);
                string l = trim(nl);
                if (l.find("Address:") != string::npos && l.find("#") == string::npos) {
                    string ip = trim(l.substr(8));
                    string type = (ip.find(':') != string::npos) ? "AAAA" : "A";
                    string badge = col(type == "A" ? C_GRN : C_BLU, string(BOLD) + fmt("[%-4s]", type.c_str()));
                    g_toolResult.formattedRows.push_back(fmt("%-16s  %-30s  %-8s  %s", badge.c_str(), target.c_str(), "auto", ip.c_str()));
                    recordCount++;
                }
            }
        }

        // Fallback to native getaddrinfo if still no records
        if (recordCount == 0) {
            addrinfo hints{}, *res = nullptr;
            hints.ai_family = AF_UNSPEC;
            if (getaddrinfo(target.c_str(), nullptr, &hints, &res) == 0 && res) {
                for (addrinfo *a = res; a; a = a->ai_next) {
                    char ipbuf[INET6_ADDRSTRLEN] = "";
                    string type = (a->ai_family == AF_INET) ? "A" : "AAAA";
                    if (a->ai_family == AF_INET) {
                        inet_ntop(AF_INET, &((sockaddr_in *)a->ai_addr)->sin_addr, ipbuf, sizeof(ipbuf));
                    } else if (a->ai_family == AF_INET6) {
                        inet_ntop(AF_INET6, &((sockaddr_in6 *)a->ai_addr)->sin6_addr, ipbuf, sizeof(ipbuf));
                    }
                    if (ipbuf[0]) {
                        string badge = col(type == "A" ? C_GRN : C_BLU, string(BOLD) + fmt("[%-4s]", type.c_str()));
                        g_toolResult.formattedRows.push_back(fmt("%-16s  %-30s  %-8s  %s", badge.c_str(), target.c_str(), "native", ipbuf));
                        recordCount++;
                    }
                }
                freeaddrinfo(res);
            }
        }

        g_toolResult.formattedRows.insert(g_toolResult.formattedRows.begin() + 2,
            col(C_GRY, "Resolver Server: ") + col(C_WHT, resolver) +
            col(C_GRY, "   DNS Query Time: ") + col(C_GRN, queryTime) + "\n");

        g_toolResult.statusText = fmt("DNS resolution finished: %d records found via %s", recordCount, resolver.c_str());
        g_toolResult.running = false;
    });
}

// 3. Fast TCP Port Scanner & Service Prober (nc / nmap / TCP)
static void runPortScanTool(const string &target) {
    std::lock_guard<std::mutex> lock(g_toolMutex);
    g_toolResult.type = ToolType::PORT_SCAN;
    g_toolResult.title = "High-Speed Port Scanner & Service Prober (nc / nmap)";
    g_toolResult.targetHost = target;
    g_toolResult.running = true;
    g_toolResult.scrollOffset = 0;
    g_toolResult.rawLines.clear();
    g_toolResult.formattedRows.clear();
    g_toolResult.statusText = "Scanning common TCP ports on " + target + "...";
    g_toolResult.command = "Native Multi-threaded TCP Port Scan + Service Fingerprint";

    g_toolResult.formattedRows.push_back(col(C_CYN, BOLD + fmt("%-8s  %-8s  %-12s  %-16s  %s", "Port", "Proto", "Status", "Latency", "Service & Banner")));
    g_toolResult.formattedRows.push_back(col(C_DGRY, repeat("─", 75)));

    if (g_toolWorker.joinable()) g_toolWorker.join();
    g_toolWorker = std::thread([target]() {
        struct PortDef { int port; const char *svc; };
        static const PortDef commonPorts[] = {
            {21, "FTP"}, {22, "SSH"}, {23, "Telnet"}, {25, "SMTP"}, {53, "DNS"},
            {80, "HTTP Web"}, {110, "POP3"}, {143, "IMAP"}, {443, "HTTPS Web"},
            {445, "SMB/CIFS"}, {465, "SMTPS"}, {587, "Submission"}, {993, "IMAPS"},
            {995, "POP3S"}, {1433, "MSSQL"}, {1521, "Oracle"}, {3306, "MySQL/MariaDB"},
            {3389, "RDP"}, {5432, "PostgreSQL"}, {5900, "VNC"}, {6379, "Redis"},
            {8080, "HTTP-Proxy/Alt"}, {8443, "HTTPS-Alt"}, {9000, "PHP-FPM/Sonar"}, {27017, "MongoDB"}
        };
        const int numPorts = sizeof(commonPorts) / sizeof(commonPorts[0]);

        auto t0 = Clock::now();
        addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        int gai = getaddrinfo(target.c_str(), nullptr, &hints, &res);
        if (gai != 0 || !res) {
            std::lock_guard<std::mutex> lk(g_toolMutex);
            g_toolResult.statusText = "DNS resolution failed for port scan target: " + target;
            g_toolResult.running = false;
            return;
        }
        sockaddr_in saddr;
        memcpy(&saddr, res->ai_addr, sizeof(saddr));
        char ipstr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &saddr.sin_addr, ipstr, sizeof(ipstr));
        freeaddrinfo(res);

        struct ScanItem {
            int port;
            string svc;
            bool open = false;
            double ms = 0;
        };
        vector<ScanItem> results(numPorts);
        vector<std::thread> workers;

        for (int i = 0; i < numPorts; i++) {
            results[i].port = commonPorts[i].port;
            results[i].svc = commonPorts[i].svc;
            int port = commonPorts[i].port;
            workers.emplace_back([&results, i, port, saddr]() {
                socket_t s = socket(AF_INET, SOCK_STREAM, 0);
                if (!IS_VALIDSOCK(s)) return;
#ifndef _WIN32
                fcntl(s, F_SETFL, O_NONBLOCK);
#else
                u_long mode = 1;
                ioctlsocket(s, FIONBIO, &mode);
#endif
                sockaddr_in targetAddr = saddr;
                targetAddr.sin_port = htons(port);
                auto tStart = Clock::now();
                connect(s, (sockaddr *)&targetAddr, sizeof(targetAddr));

#ifndef _WIN32
                pollfd pfd{s, POLLOUT, 0};
                int pr = poll(&pfd, 1, 800);
#else
                WSAPOLLFD pfd;
                pfd.fd = s;
                pfd.events = POLLOUT;
                pfd.revents = 0;
                int pr = WSAPoll(&pfd, 1, 800);
#endif
                if (pr > 0) {
                    int err = 0;
                    socklen_t len = sizeof(err);
                    getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&err, &len);
                    if (err == 0) {
                        results[i].open = true;
                        results[i].ms = std::chrono::duration<double, std::milli>(Clock::now() - tStart).count();
                    }
                }
                CLOSE_SOCK(s);
            });
        }

        for (auto &w : workers) w.join();
        auto t1 = Clock::now();
        double dur = std::chrono::duration<double, std::milli>(t1 - t0).count();

        std::lock_guard<std::mutex> lk(g_toolMutex);
        g_toolResult.durationMs = dur;
        g_toolResult.targetIp = ipstr;

        int openCount = 0;
        for (const auto &item : results) {
            string statusBadge = item.open ? col(C_GRN, BOLD + string("[ OPEN ]")) : col(C_DGRY, "[CLOSED]");
            string latStr = item.open ? col(latColor(item.ms), fmt("%.1f ms", item.ms)) : col(C_DGRY, "--");
            string svcStr = item.open ? col(C_WHT, BOLD + item.svc) : col(C_GRY, item.svc);

            if (item.open) openCount++;

            string row = fmt("%-8d  %-8s  %-20s  %-24s  %s",
                item.port, "tcp", statusBadge.c_str(), latStr.c_str(), svcStr.c_str());
            g_toolResult.formattedRows.push_back(row);
            g_toolResult.rawLines.push_back(fmt("Port %d/tcp: %s (%s)", item.port, item.open ? "OPEN" : "CLOSED", item.svc.c_str()));
        }

        g_toolResult.statusText = fmt("Port scan finished in %.0fms: %d open, %d closed across %d probed ports",
                                      dur, openCount, numPorts - openCount, numPorts);
        g_toolResult.running = false;
    });
}

// 4. HTTP / HTTPS Inspector (curl / wget)
static void runHttpTool(const string &target) {
    std::lock_guard<std::mutex> lock(g_toolMutex);
    g_toolResult.type = ToolType::HTTP_PROBE;
    g_toolResult.title = "HTTP/HTTPS Latency & Header Inspector (curl / wget)";
    g_toolResult.targetHost = target;
    g_toolResult.running = true;
    g_toolResult.scrollOffset = 0;
    g_toolResult.rawLines.clear();
    g_toolResult.formattedRows.clear();
    g_toolResult.statusText = "Sending HTTP/HTTPS inspection probe to " + target + "...";

    string url = target;
    if (url.find("://") == string::npos) {
        url = "https://" + url;
    }
    g_toolResult.command = "curl -I -s -L --max-time 8 -w \"\\n__METRICS__\\n%{time_namelookup}\\n%{time_connect}\\n%{time_appconnect}\\n%{time_starttransfer}\\n%{time_total}\\n%{http_code}\\n\" " + url;

    if (g_toolWorker.joinable()) g_toolWorker.join();
    g_toolWorker = std::thread([url]() {
        auto t0 = Clock::now();
        string cmd = "curl -I -s -L --max-time 8 -w \"\\n__METRICS__\\n%{time_namelookup}\\n%{time_connect}\\n%{time_appconnect}\\n%{time_starttransfer}\\n%{time_total}\\n%{http_code}\\n\" " + url + " 2>&1";
        auto lines = runCmdLines(cmd, 100);
        auto t1 = Clock::now();
        double dur = std::chrono::duration<double, std::milli>(t1 - t0).count();

        std::lock_guard<std::mutex> lk(g_toolMutex);
        g_toolResult.durationMs = dur;
        g_toolResult.rawLines = lines;

        double dnsSec = 0, connSec = 0, tlsSec = 0, ttfbSec = 0, totalSec = 0;
        int httpCode = 0;
        vector<string> headers;
        bool inMetrics = false;
        int metricIdx = 0;

        for (const auto &l : lines) {
            if (l.find("__METRICS__") != string::npos) {
                inMetrics = true;
                continue;
            }
            if (inMetrics) {
                if (metricIdx == 0) sscanf(l.c_str(), "%lf", &dnsSec);
                else if (metricIdx == 1) sscanf(l.c_str(), "%lf", &connSec);
                else if (metricIdx == 2) sscanf(l.c_str(), "%lf", &tlsSec);
                else if (metricIdx == 3) sscanf(l.c_str(), "%lf", &ttfbSec);
                else if (metricIdx == 4) sscanf(l.c_str(), "%lf", &totalSec);
                else if (metricIdx == 5) sscanf(l.c_str(), "%d", &httpCode);
                metricIdx++;
            } else {
                if (!trim(l).empty()) headers.push_back(l);
            }
        }

        string codeCol = (httpCode >= 200 && httpCode < 300) ? C_GRN : (httpCode >= 300 && httpCode < 400) ? C_YEL : C_RED;
        string statusBadge = col(codeCol, string(BOLD) + fmt("[%d %s]", httpCode, httpCode == 200 ? "OK" : httpCode == 301 ? "MOVED" : httpCode == 404 ? "NOT FOUND" : "HTTP"));

        g_toolResult.formattedRows.push_back(col(C_CYN, string(BOLD) + "URL: ") + col(C_WHT, url) + "   " + col(C_CYN, string(BOLD) + "Status: ") + statusBadge);
        g_toolResult.formattedRows.push_back(col(C_DGRY, repeat("─", 80)));

        // Visual timing breakdown bar
        double dnsMs = dnsSec * 1000.0;
        double connMs = (connSec - dnsSec) * 1000.0;
        double tlsMs = (tlsSec > 0 && connSec > 0) ? (tlsSec - connSec) * 1000.0 : 0;
        double ttfbMs = (ttfbSec - (tlsSec > 0 ? tlsSec : connSec)) * 1000.0;
        double totMs = totalSec * 1000.0;

        string timingBar = col(C_CYN, "DNS: ") + col(C_GRN, fmt("%.1fms", dnsMs)) + "  │  " +
                           col(C_CYN, "Connect: ") + col(C_GRN, fmt("%.1fms", std::max(0.0, connMs))) + "  │  " +
                           col(C_CYN, "TLS Handshake: ") + col(C_GRN, fmt("%.1fms", std::max(0.0, tlsMs))) + "  │  " +
                           col(C_CYN, "TTFB: ") + col(C_YEL, fmt("%.1fms", std::max(0.0, ttfbMs))) + "  │  " +
                           col(C_CYN, "Total: ") + col(C_WHT, string(BOLD) + fmt("%.1fms", totMs));
        g_toolResult.formattedRows.push_back(timingBar);
        g_toolResult.formattedRows.push_back(col(C_DGRY, repeat("─", 80)));

        g_toolResult.formattedRows.push_back(col(C_CYN, string(BOLD) + "HTTP Response Headers:"));
        for (const auto &h : headers) {
            size_t colon = h.find(':');
            if (colon != string::npos) {
                string key = h.substr(0, colon);
                string val = h.substr(colon + 1);
                g_toolResult.formattedRows.push_back("  " + col(C_PUR, key) + ":" + col(C_WHT, val));
            } else {
                g_toolResult.formattedRows.push_back("  " + col(C_GRY, h));
            }
        }

        g_toolResult.statusText = fmt("HTTP probe completed: %d returned in %.1fms", httpCode, totMs);
        g_toolResult.running = false;
    });
}

// 5. Local Network Interfaces (ifconfig / ip / ipconfig)
static void runInterfacesTool() {
    std::lock_guard<std::mutex> lock(g_toolMutex);
    g_toolResult.type = ToolType::INTERFACES;
    g_toolResult.title = "Local Network Interfaces & Hardware Addresses (ifconfig / ip / ipconfig)";
    g_toolResult.targetHost = "localhost";
    g_toolResult.running = true;
    g_toolResult.scrollOffset = 0;
    g_toolResult.rawLines.clear();
    g_toolResult.formattedRows.clear();
    g_toolResult.statusText = "Enumerating host network adapters...";
    g_toolResult.command = "Native getifaddrs / ifconfig adapter inspection";

    if (g_toolWorker.joinable()) g_toolWorker.join();
    g_toolWorker = std::thread([]() {
        auto t0 = Clock::now();
        vector<string> rows;
#ifndef _WIN32
        ifaddrs *ifaddr = nullptr;
        if (getifaddrs(&ifaddr) != -1) {
            std::map<string, string> macs;
            for (ifaddrs *ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
                if (!ifa->ifa_addr) continue;
#if defined(__APPLE__)
                if (ifa->ifa_addr->sa_family == AF_LINK) {
                    sockaddr_dl *sdl = (sockaddr_dl *)ifa->ifa_addr;
                    if (sdl->sdl_alen == 6) {
                        unsigned char *m = (unsigned char *)LLADDR(sdl);
                        macs[ifa->ifa_name] = fmt("%02x:%02x:%02x:%02x:%02x:%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
                    }
                }
#elif defined(__linux__)
                if (ifa->ifa_addr->sa_family == AF_PACKET) {
                    sockaddr_ll *sll = (sockaddr_ll *)ifa->ifa_addr;
                    if (sll->sll_halen == 6) {
                        unsigned char *m = sll->sll_addr;
                        macs[ifa->ifa_name] = fmt("%02x:%02x:%02x:%02x:%02x:%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
                    }
                }
#endif
            }

            rows.push_back(col(C_CYN, string(BOLD) + fmt("%-10s  %-8s  %-16s  %-16s  %-18s  %s", "Interface", "Status", "IPv4 Address", "Netmask", "MAC Hardware", "Broadcast / IPv6")));
            rows.push_back(col(C_DGRY, repeat("─", 90)));

            for (ifaddrs *ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
                if (!ifa->ifa_addr) continue;
                string ifname = ifa->ifa_name;
                bool isUp = (ifa->ifa_flags & IFF_UP) != 0;
                bool isLoop = (ifa->ifa_flags & IFF_LOOPBACK) != 0;

                char ip[INET6_ADDRSTRLEN] = "--";
                char mask[INET6_ADDRSTRLEN] = "--";
                char extra[INET6_ADDRSTRLEN] = "--";

                if (ifa->ifa_addr->sa_family == AF_INET) {
                    sockaddr_in *p = (sockaddr_in *)ifa->ifa_addr;
                    inet_ntop(AF_INET, &p->sin_addr, ip, sizeof(ip));
                    if (ifa->ifa_netmask) {
                        sockaddr_in *m = (sockaddr_in *)ifa->ifa_netmask;
                        inet_ntop(AF_INET, &m->sin_addr, mask, sizeof(mask));
                    }
                    if (ifa->ifa_broadaddr && !isLoop) {
                        sockaddr_in *b = (sockaddr_in *)ifa->ifa_broadaddr;
                        inet_ntop(AF_INET, &b->sin_addr, extra, sizeof(extra));
                    }
                    string mac = macs.count(ifname) ? macs[ifname] : "--";
                    string upBadge = isUp ? col(C_GRN, "[ UP ]") : col(C_RED, "[DOWN]");
                    rows.push_back(fmt("%-10s  %-18s  %-16s  %-16s  %-18s  %s",
                        ifname.c_str(), upBadge.c_str(), ip, mask, mac.c_str(), extra));
                }
            }
            freeifaddrs(ifaddr);
        }
#else
        auto lines = runCmdLines("ipconfig /all", 80);
        for (const auto &l : lines) rows.push_back(l);
#endif
        auto t1 = Clock::now();
        double dur = std::chrono::duration<double, std::milli>(t1 - t0).count();

        std::lock_guard<std::mutex> lk(g_toolMutex);
        g_toolResult.durationMs = dur;
        g_toolResult.formattedRows = rows;
        g_toolResult.statusText = fmt("Network interfaces refreshed in %.0fms", dur);
        g_toolResult.running = false;
    });
}

// 6. Routing Table & Gateway (route / ip route / netstat -r)
static void runRouteTool() {
    std::lock_guard<std::mutex> lock(g_toolMutex);
    g_toolResult.type = ToolType::ROUTES;
    g_toolResult.title = "System Routing Table & Default Gateway (route / ip route)";
    g_toolResult.targetHost = "routing-table";
    g_toolResult.running = true;
    g_toolResult.scrollOffset = 0;
    g_toolResult.rawLines.clear();
    g_toolResult.formattedRows.clear();
    g_toolResult.statusText = "Inspecting kernel routing tables...";

#ifndef _WIN32
    #if defined(__APPLE__)
        g_toolResult.command = "netstat -rn -f inet";
    #else
        g_toolResult.command = "ip route show || route -n";
    #endif
#else
    g_toolResult.command = "route print -4";
#endif

    if (g_toolWorker.joinable()) g_toolWorker.join();
    g_toolWorker = std::thread([]() {
        auto t0 = Clock::now();
#ifndef _WIN32
    #if defined(__APPLE__)
        auto lines = runCmdLines("netstat -rn -f inet", 45);
    #else
        auto lines = runCmdLines("ip route show 2>/dev/null || route -n", 45);
    #endif
#else
        auto lines = runCmdLines("route print -4", 45);
#endif
        auto t1 = Clock::now();
        double dur = std::chrono::duration<double, std::milli>(t1 - t0).count();

        std::lock_guard<std::mutex> lk(g_toolMutex);
        g_toolResult.durationMs = dur;
        g_toolResult.rawLines = lines;

        string gw = detectGateway();
        g_toolResult.formattedRows.push_back(col(C_CYN, string(BOLD) + "Default Gateway: ") + col(C_GRN, string(BOLD) + (gw.empty() ? "None detected" : gw)));
        g_toolResult.formattedRows.push_back(col(C_DGRY, repeat("─", 80)));
        for (const auto &l : lines) {
            if (l.find("default") != string::npos || l.find("via") != string::npos) {
                g_toolResult.formattedRows.push_back(col(C_YEL, string(BOLD) + l));
            } else {
                g_toolResult.formattedRows.push_back(l);
            }
        }
        g_toolResult.statusText = fmt("Routing table retrieved in %.0fms", dur);
        g_toolResult.running = false;
    });
}

// 7. Active Sockets & Ports (ss / netstat)
static void runSocketsTool() {
    std::lock_guard<std::mutex> lock(g_toolMutex);
    g_toolResult.type = ToolType::SOCKETS;
    g_toolResult.title = "Active Sockets & Listening Ports (ss / netstat)";
    g_toolResult.targetHost = "localhost";
    g_toolResult.running = true;
    g_toolResult.scrollOffset = 0;
    g_toolResult.rawLines.clear();
    g_toolResult.formattedRows.clear();
    g_toolResult.statusText = "Inspecting open sockets and established connections...";

#ifndef _WIN32
    #if defined(__APPLE__)
        g_toolResult.command = "netstat -an -f inet -p tcp";
    #else
        g_toolResult.command = "ss -tuln 2>/dev/null || netstat -tuln";
    #endif
#else
    g_toolResult.command = "netstat -ano";
#endif

    if (g_toolWorker.joinable()) g_toolWorker.join();
    g_toolWorker = std::thread([]() {
        auto t0 = Clock::now();
#ifndef _WIN32
    #if defined(__APPLE__)
        auto lines = runCmdLines("netstat -an -f inet -p tcp", 80);
    #else
        auto lines = runCmdLines("ss -tuln 2>/dev/null || netstat -tuln", 80);
    #endif
#else
        auto lines = runCmdLines("netstat -ano", 80);
#endif
        auto t1 = Clock::now();
        double dur = std::chrono::duration<double, std::milli>(t1 - t0).count();

        std::lock_guard<std::mutex> lk(g_toolMutex);
        g_toolResult.durationMs = dur;
        g_toolResult.rawLines = lines;

        for (const auto &l : lines) {
            if (l.find("LISTEN") != string::npos) {
                g_toolResult.formattedRows.push_back(col(C_GRN, l));
            } else if (l.find("ESTABLISHED") != string::npos) {
                g_toolResult.formattedRows.push_back(col(C_CYN, l));
            } else {
                g_toolResult.formattedRows.push_back(l);
            }
        }
        g_toolResult.statusText = fmt("Active sockets captured in %.0fms (%zu entries)", dur, lines.size());
        g_toolResult.running = false;
    });
}

// 8. Network Traffic & Packet Statistics (tcpdump / stats)
static void runTrafficTool() {
    std::lock_guard<std::mutex> lock(g_toolMutex);
    g_toolResult.type = ToolType::TRAFFIC_STATS;
    g_toolResult.title = "Network Traffic & Interface Packet Counters (tcpdump / stats)";
    g_toolResult.targetHost = "interfaces";
    g_toolResult.running = true;
    g_toolResult.scrollOffset = 0;
    g_toolResult.rawLines.clear();
    g_toolResult.formattedRows.clear();
    g_toolResult.statusText = "Sampling interface packet and byte transfer counters...";

#ifndef _WIN32
    #if defined(__APPLE__)
        g_toolResult.command = "netstat -ib";
    #else
        g_toolResult.command = "cat /proc/net/dev 2>/dev/null || netstat -i";
    #endif
#else
    g_toolResult.command = "netstat -e";
#endif

    if (g_toolWorker.joinable()) g_toolWorker.join();
    g_toolWorker = std::thread([]() {
        auto t0 = Clock::now();
#ifndef _WIN32
    #if defined(__APPLE__)
        auto lines = runCmdLines("netstat -ib", 50);
    #else
        auto lines = runCmdLines("cat /proc/net/dev 2>/dev/null || netstat -i", 50);
    #endif
#else
        auto lines = runCmdLines("netstat -e", 50);
#endif
        auto t1 = Clock::now();
        double dur = std::chrono::duration<double, std::milli>(t1 - t0).count();

        std::lock_guard<std::mutex> lk(g_toolMutex);
        g_toolResult.durationMs = dur;
        g_toolResult.rawLines = lines;

        for (const auto &l : lines) {
            if (l.find("Name") != string::npos || l.find("Interface") != string::npos) {
                g_toolResult.formattedRows.push_back(col(C_CYN, BOLD + l));
            } else {
                g_toolResult.formattedRows.push_back(l);
            }
        }
        g_toolResult.statusText = fmt("Packet counters read in %.0fms", dur);
        g_toolResult.running = false;
    });
}

// 9. Custom Command Runner
static void runCustomCommandTool(const string &customCmd) {
    std::lock_guard<std::mutex> lock(g_toolMutex);
    g_toolResult.type = ToolType::CUSTOM_CMD;
    g_toolResult.title = "Native Terminal Command Runner";
    g_toolResult.targetHost = "custom";
    g_toolResult.running = true;
    g_toolResult.scrollOffset = 0;
    g_toolResult.rawLines.clear();
    g_toolResult.formattedRows.clear();
    g_toolResult.statusText = "Executing: " + customCmd;
    g_toolResult.command = customCmd;

    if (g_toolWorker.joinable()) g_toolWorker.join();
    g_toolWorker = std::thread([customCmd]() {
        auto t0 = Clock::now();
        auto lines = runCmdLines(customCmd + " 2>&1", 300);
        auto t1 = Clock::now();
        double dur = std::chrono::duration<double, std::milli>(t1 - t0).count();

        std::lock_guard<std::mutex> lk(g_toolMutex);
        g_toolResult.durationMs = dur;
        g_toolResult.rawLines = lines;
        g_toolResult.formattedRows = lines;
        g_toolResult.statusText = fmt("Command finished in %.0fms with %zu lines of output", dur, lines.size());
        g_toolResult.running = false;
    });
}

// Execute Toolbox dispatch
static void launchTool(int toolId, const string &target) {
    TOOL_ACTIVE_ID = toolId;
    switch (toolId) {
        case 1: runTracerouteTool(target); break;
        case 2: runDnsTool(target); break;
        case 3: runPortScanTool(target); break;
        case 4: runHttpTool(target); break;
        case 5: runInterfacesTool(); break;
        case 6: runRouteTool(); break;
        case 7: runSocketsTool(); break;
        case 8: runTrafficTool(); break;
        case 9:
            g_modal = ModalMode::TOOL_CUSTOM_CMD;
            g_inputPrompt = "Enter custom command to run (e.g. nmap -F " + target + "): ";
            g_inputBuf = "nmap -F " + target;
            break;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  Visual Chart Renderers (Bar, Sparkline, Braille)
// ═════════════════════════════════════════════════════════════
static vector<string> g_frameLines;
static void addLine(const string &s) { g_frameLines.push_back(s); }

static double niceMax(double mx) {
    static const double steps[] = {
        5, 10, 20, 30, 50, 75, 100, 150, 200, 300, 500, 750, 1000, 1500, 2000, 3000, 5000, 10000
    };
    double t = mx * 1.15;
    for (double s : steps) if (s >= t) return s;
    return t > 0 ? t : 50;
}

static double posOf(double v, double ymax, bool lg) {
    if (ymax <= 0) return 0;
    double f = lg ? (std::log10(1 + std::max(0.0, v)) / std::log10(1 + ymax)) : (v / ymax);
    return std::max(0.0, std::min(1.0, f));
}

static double valueAt(double frac, double ymax, bool lg) {
    return lg ? (std::pow(10.0, frac * std::log10(1 + ymax)) - 1) : (ymax * frac);
}

// Render multi-level block bar chart
static void renderBigChart(const deque<double> &h, int plotW, int rows, const string &indent) {
    if (plotW < 5) plotW = 5;
    size_t start = h.size() > (size_t)plotW ? h.size() - plotW : 0;
    double mx = 0;
    for (size_t i = start; i < h.size(); i++) mx = std::max(mx, h[i]);
    double ymax = niceMax(mx);

    for (int r = rows - 1; r >= 0; r--) {
        string lab = "     ";
        if (r == rows - 1) lab = fmt("%5.0f", ymax);
        else if (r == 0) lab = fmt("%5.0f", 0.0);
        else if (rows >= 4 && r == rows / 2) lab = fmt("%5.0f", ymax * r / rows);

        string line = indent + col(C_DGRY, lab + " │");
        for (size_t i = start; i < h.size(); i++) {
            double v = h[i];
            if (v < 0) {
                line += (r == 0) ? col(C_RED, "✕") : " ";
                continue;
            }
            int total = (int)std::lround(v / ymax * rows * 8);
            int fill = std::min(8, total - r * 8);
            if (r == 0 && fill < 1) fill = 1;
            if (fill < 1) {
                line += " ";
            } else {
                line += string(latColor(v)) + BARS[fill - 1] + RST;
            }
        }
        addLine(line);
    }
}

// Sparkline string generator
static string makeSparkline(const deque<double> &h, int width) {
    if (h.empty() || width <= 0) return "";
    size_t start = h.size() > (size_t)width ? h.size() - width : 0;
    double mx = 0;
    for (size_t i = start; i < h.size(); i++) if (h[i] > mx) mx = h[i];
    if (mx <= 0) mx = 1;

    string out;
    for (size_t i = start; i < h.size(); i++) {
        double v = h[i];
        if (v < 0) {
            out += col(C_RED, "✕");
        } else {
            int idx = std::min(7, (int)(v / mx * 7));
            out += string(latColor(v)) + BARS[idx] + RST;
        }
    }
    return out;
}

// ═════════════════════════════════════════════════════════════════════════════
//  View 1: Overview Dashboard (Card-based Layout)
// ═════════════════════════════════════════════════════════════
static void viewOverview(int cols, int rows) {
    size_t n = sites.size();
    if (n == 0) {
        addLine(col(C_GRY, "  No target sites configured. Press 'a' to add a host, or run with arguments."));
        return;
    }

    int fixedRows = 4 /*hdr*/ + 2 /*footer*/ + 6 /*events*/;
    int cardRows = (rows - fixedRows) / (int)n;
    cardRows = std::max(4, std::min(10, cardRows));
    int chartHeight = std::max(1, cardRows - 4);

    for (size_t i = 0; i < n; i++) {
        Site &s = sites[i];
        bool isSel = ((int)i == SEL_SITE);
        string borderColor = isSel ? C_CYN : C_DGRY;
        SiteHealth hlth = healthOf(s);
        int hops = hopsFrom(s.ttl);
        string hopStr = (hops < 0) ? "" : (hops == 0 ? " ~Direct" : fmt(" ~%d hops", hops));

        // Top Border with Title, Mini Sparkline & Health Badge
        string spark = makeSparkline(s.hist, 12);
        string badgeStr = col(hlth.color, string(BOLD) + string(hlth.badge));
        string titleStr = (isSel ? col(C_WHT, string(BOLD) + " ▶ " + s.name + " ") : col(C_GRY, "   " + s.name + " ")) +
                          col(C_DGRY, "(" + s.host + (s.ip.empty() ? "" : " · " + s.ip) + ")");
        string rightStr = (spark.empty() ? "" : spark + "  ") + badgeStr + col(C_GRY, hopStr) + " ";
        int padTop = std::max(1, cols - 6 - (int)visibleWidth(titleStr) - (int)visibleWidth(rightStr));

        addLine(borderColor + "╭─ " + RST + titleStr + string(padTop, ' ') + rightStr + borderColor + "─╮" + RST);

        // Subheader: Current Latency, Min, Avg, Max, Jitter, Loss
        double avg = s.recv ? (s.sum / s.recv) : 0;
        double loss = s.sent ? ((s.sent - s.recv) * 100.0 / s.sent) : 0;
        string nowVal = s.ok ? col(latColor(s.ms), string(BOLD) + fmt("%.1fms", s.ms)) : col(C_RED, string(BOLD) + "TIMEOUT");

        string statLine;
        if (cols >= 95) {
            statLine = borderColor + "│" + RST + " " +
                col(C_GRY, "RTT: ") + nowVal + "   " +
                col(C_GRY, "Avg: ") + col(latColor(avg), fmt("%.1fms", avg)) + "   " +
                col(C_GRY, "Min: ") + fmt("%.1fms", s.mn) + "   " +
                col(C_GRY, "Max: ") + fmt("%.1fms", s.mx) + "   " +
                col(C_GRY, "P95: ") + fmt("%.1fms", s.p95) + "   " +
                col(C_GRY, "Jitter: ") + fmt("%.1fms", s.jitter) + "   " +
                col(C_GRY, "Loss: ") + (loss > 0 ? col(C_RED, fmt("%.1f%%", loss)) : col(C_GRN, "0.0%")) +
                (s.loss60 > 0 ? col(C_ORG, fmt(" (60s: %.0f%%)", s.loss60)) : "");
        } else {
            statLine = borderColor + "│" + RST + " " +
                col(C_GRY, "RTT: ") + nowVal + "  " +
                col(C_GRY, "Avg: ") + col(latColor(avg), fmt("%.1fms", avg)) + "  " +
                col(C_GRY, "Min/Max: ") + fmt("%.0f/%.0fms", s.mn, s.mx) + "  " +
                col(C_GRY, "Loss: ") + (loss > 0 ? col(C_RED, fmt("%.1f%%", loss)) : col(C_GRN, "0%")) + "  " +
                col(C_GRY, "Jitter: ") + fmt("%.1fms", s.jitter);
        }

        addLine(statLine);

        // Bar Chart
        renderBigChart(s.hist, std::max(10, cols - 12), chartHeight, borderColor + "│ " + RST);

        // Bottom Border
        addLine(borderColor + "╰" + repeat("─", std::max(2, cols - 2)) + "╯" + RST);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  View 2: Multi-Host Comparison Chart
// ═════════════════════════════════════════════════════════════
static void viewCompare(int cols, int rows) {
    size_t n = sites.size();
    if (ROUND == 0) {
        addLine(col(C_GRY, "  Waiting for initial probe cycles..."));
        return;
    }
    int plotW = std::max(10, cols - 10);
    int chartRows = rows - 3 - 2 - (int)n * 2 - 4;
    chartRows = std::max(4, std::min(20, chartRows));

    // Legend
    string leg = "  ";
    for (size_t i = 0; i < n; i++) {
        const char *c = PAL[i % PAL_SIZE];
        string now = sites[i].ok ? fmt("%.1fms", sites[i].ms) : "timeout";
        leg += string(c) + MARK[i % 8] + RST + " " + sites[i].name + " " + col(C_GRY, now) + "    ";
    }
    addLine(leg);
    addLine(col(C_GRY, fmt("  Scale: %s  (Press 'l' to toggle log/linear)", LOGSCALE ? "Logarithmic" : "Linear")));

    double mx = 0;
    for (const auto &s : sites) {
        size_t st = s.hist.size() > (size_t)plotW ? s.hist.size() - plotW : 0;
        for (size_t k = st; k < s.hist.size(); k++) mx = std::max(mx, s.hist[k]);
    }
    double ymax = niceMax(mx);

    struct Cell { const char *ch = " "; const char *color = RST; };
    vector<vector<Cell>> grid(chartRows, vector<Cell>(plotW));

    for (size_t i = 0; i < n; i++) {
        const auto &h = sites[i].hist;
        size_t st = h.size() > (size_t)plotW ? h.size() - plotW : 0;
        for (size_t k = st; k < h.size(); k++) {
            if (h[k] < 0) continue;
            int x = (int)(k - st);
            int y = (int)std::lround(posOf(h[k], ymax, LOGSCALE) * (chartRows - 1));
            y = std::max(0, std::min(chartRows - 1, y));
            grid[y][x] = {MARK[i % 8], PAL[i % PAL_SIZE]};
        }
    }

    for (int r = chartRows - 1; r >= 0; r--) {
        string lab = "     ";
        if (r == chartRows - 1) lab = fmt("%5.0f", ymax);
        else if (r == 0) lab = fmt("%5.0f", 0.0);
        else if (r == (chartRows - 1) / 2) lab = fmt("%5.0f", valueAt((double)r / (chartRows - 1), ymax, LOGSCALE));

        string line = col(C_DGRY, lab + " │");
        for (int x = 0; x < plotW; x++) {
            const Cell &c = grid[r][x];
            line += (c.ch[0] == ' ') ? string(" ") : string(c.color) + c.ch + RST;
        }
        addLine(line);
    }
    int axisDash = std::max(2, cols - 28);
    addLine(col(C_DGRY, "       " + repeat("─", axisDash) + "  ms · past ← → now"));

    // Loss Strip
    addLine("");
    addLine(BOLD + string("  Packet Loss Tape") + RST + col(C_GRY, "  (█ = dropped ping)"));
    for (size_t i = 0; i < n; i++) {
        const auto &h = sites[i].hist;
        size_t st = h.size() > (size_t)plotW ? h.size() - plotW : 0;
        string line = string(PAL[i % PAL_SIZE]) + "  " + MARK[i % 8] + RST + fmt(" %-16s ", sites[i].name.substr(0, 15).c_str());
        for (size_t k = st; k < h.size(); k++) {
            line += (h[k] < 0) ? col(C_RED, "█") : col(C_DGRY, "·");
        }
        addLine(line);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  View 3: Deep-Dive Single Host Inspector
// ═════════════════════════════════════════════════════════════
static void viewDetail(int cols, int rows) {
    if (sites.empty()) {
        addLine(col(C_GRY, "  No target sites configured. Press 'a' to add a host."));
        return;
    }
    Site &s = sites[SEL_SITE % sites.size()];
    SiteHealth hlth = healthOf(s);
    int hops = hopsFrom(s.ttl);

    // Host Header Banner
    string hostBannerTitle = string("╭─ ") + BOLD + s.name + RST + "  " + col(hlth.color, hlth.badge);
    string hostBannerHint = (cols >= 90) ? "  (Tab/j/k: target · Enter: Toolbox) " : " ";
    int bannerPad = std::max(2, cols - 3 - (int)visibleWidth(hostBannerTitle) - (int)visibleWidth(hostBannerHint));
    addLine(col(C_CYN, hostBannerTitle + hostBannerHint + repeat("─", bannerPad) + "╮"));

    string sub = fmt("  Host: %s   IP: %s   DNS: %.1fms   TTL: %d (%s)",
                     s.host.c_str(), s.ip.c_str(), s.dnsMs, s.ttl,
                     hops < 0 ? "unknown hops" : (hops == 0 ? "direct LAN" : fmt("~%d hops", hops).c_str()));
    addLine(col(C_GRY, sub));
    addLine("");

    // Metrics 4x4 Grid
    double avg = s.recv ? (s.sum / s.recv) : 0;
    double loss = s.sent ? ((s.sent - s.recv) * 100.0 / s.sent) : 0;
    struct KV { string k, v; };
    vector<KV> grid = {
        {"Current Latency", s.ok ? fmt("%.1f ms", s.ms) : "Timeout"},
        {"Minimum RTT", fmt("%.1f ms", s.mn)},
        {"Average RTT", fmt("%.1f ms", avg)},
        {"Maximum RTT", fmt("%.1f ms", s.mx)},
        {"50th % (Median)", fmt("%.1f ms", s.p50)},
        {"95th %ile", fmt("%.1f ms", s.p95)},
        {"99th %ile", fmt("%.1f ms", s.p99)},
        {"Std Deviation", fmt("%.1f ms", s.sd)},
        {"Jitter", fmt("%.1f ms", s.jitter)},
        {"Packet Loss", fmt("%.1f %%", loss)},
        {"Recent (60s) Loss", fmt("%.0f %%", s.loss60)},
        {"Sent / Recv", fmt("%d / %d", s.sent, s.recv)},
        {"Streak", s.streak >= 0 ? fmt("%d ok", s.streak) : fmt("%d lost", -s.streak)},
        {"Total Outages", fmt("%d", outageCountOf((size_t)(SEL_SITE % sites.size())))},
        {"Total Downtime", fmtSpan(downtimeOf((size_t)(SEL_SITE % sites.size())))},
        {"Availability", s.sent ? fmt("%.2f %%", s.recv * 100.0 / s.sent) : "--"}
    };

    int perRow = std::max(1, std::min(4, (cols - 4) / 24));
    for (size_t i = 0; i < grid.size(); i += perRow) {
        string line = "  ";
        for (int j = 0; j < perRow && i + j < grid.size(); j++) {
            string k = grid[i + j].k, v = grid[i + j].v;
            int pad = std::max(1, 24 - (int)(k.size() + 1 + v.size()));
            line += col(C_GRY, k) + " " + col(C_WHT, BOLD + v) + string(pad, ' ');
        }
        addLine(line);
    }
    addLine("");

    // High-Resolution Latency Chart
    int histRows = 7;
    int chartHeight = rows - (int)g_frameLines.size() - histRows - 6;
    chartHeight = std::max(3, std::min(12, chartHeight));
    addLine(BOLD + string("  Recent Latency Trend") + RST + col(C_GRY, " (ms per sample)"));
    renderBigChart(s.hist, std::max(10, cols - 12), chartHeight, "  ");

    // Distribution Histogram (Compact 2-Column Layout)
    static const double edges[] = {5, 10, 20, 35, 50, 75, 100, 150, 250, 500, 1e18};
    static const char *names[] = {"<5ms", "5-10ms", "10-20ms", "20-35ms", "35-50ms", "50-75ms",
                                  "75-100", "100-150", "150-250", "250-500", "500ms+"};
    int counts[11] = {0};
    int total = 0;
    for (double v : s.hist) {
        if (v < 0) continue;
        for (int b = 0; b < 11; b++) {
            if (v < edges[b]) { counts[b]++; break; }
        }
        total++;
    }
    int cmax = 1;
    for (int b = 0; b < 11; b++) cmax = std::max(cmax, counts[b]);
    addLine("");
    addLine(BOLD + string("  Latency Distribution") + RST + col(C_GRY, total > 0 ? fmt(" (reply distribution across %d samples)", total) : " (reply distribution buckets)"));
    int colW = std::max(15, (cols - 8) / 2);
    int barW = std::max(6, colW - 20);

    for (int r = 0; r < 6; r++) {
        int b1 = r;
        int b2 = r + 6;
        string leftStr, rightStr;

        int w1 = counts[b1] * barW / cmax;
        if (counts[b1] > 0 && w1 < 1) w1 = 1;
        double rep1 = (b1 == 10) ? 600 : (edges[b1] - 0.1);
        leftStr = col(C_GRY, fmt("%-7s ", names[b1])) + string(latColor(rep1)) + repeat("█", w1) + RST + " " +
                  col(C_GRY, fmt("%d", counts[b1]));

        int padLeft = std::max(1, colW - (int)visibleWidth(leftStr));
        leftStr += string(padLeft, ' ');

        if (b2 < 11) {
            int w2 = counts[b2] * barW / cmax;
            if (counts[b2] > 0 && w2 < 1) w2 = 1;
            double rep2 = (b2 == 10) ? 600 : (edges[b2] - 0.1);
            rightStr = col(C_GRY, fmt("%-7s ", names[b2])) + string(latColor(rep2)) + repeat("█", w2) + RST + " " +
                       col(C_GRY, fmt("%d", counts[b2]));
        }
        addLine("  " + leftStr + rightStr);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  View 4: Outages & Incident Log
// ═════════════════════════════════════════════════════════════
static void viewOutages(int cols, int rows) {
    (void)cols;
    addLine(BOLD + string("  Target Availability & Outage Summary") + RST);
    addLine(col(C_CYN, fmt("  %-20s %10s %12s %10s %12s", "Host / Target", "Uptime %", "Downtime", "Incidents", "Current")));
    addLine(col(C_DGRY, "  " + repeat("─", 68)));

    for (size_t i = 0; i < sites.size(); i++) {
        Site &s = sites[i];
        double up = s.sent ? (s.recv * 100.0 / s.sent) : 0;
        string name = s.name.substr(0, 18);
        SiteHealth hlth = healthOf(s);
        string line = fmt("  %-20s %9.2f%% %12s %10d   ",
                          name.c_str(), up, fmtSpan(downtimeOf(i)).c_str(), outageCountOf(i));
        line += col(hlth.color, BOLD + string(hlth.badge));
        addLine(line);
    }

    addLine("");
    addLine(BOLD + string("  Incident Log") + RST + col(C_GRY, " (2+ consecutive dropped pings; newest first)"));
    if (outages.empty()) {
        addLine(col(C_GRN, "  ✔ No network outages recorded during this session."));
        return;
    }

    addLine(col(C_CYN, fmt("  %-18s %-20s %-12s %-14s %s", "Host", "Started", "Duration", "Status", "Reason")));
    addLine(col(C_DGRY, "  " + repeat("─", 78)));

    int shown = 0, maxShow = std::max(3, rows - (int)g_frameLines.size() - 3);
    time_t now = time(nullptr);
    for (size_t k = outages.size(); k-- > 0 && shown < maxShow; shown++) {
        const Outage &o = outages[k];
        long dur = (long)((o.end ? o.end : now) - o.start);
        string host = sites[o.site].name.substr(0, 17);
        string statusStr = o.end ? col(C_GRN, "RECOVERED") : col(C_RED, string(BOLD) + "ONGOING");
        addLine(fmt("  %-18s %-20s %-12s ",
                    host.c_str(), timeStr(o.start, "%Y-%m-%d %H:%M:%S").c_str(), fmtSpan(dur).c_str()) +
                statusStr + "  " + col(C_GRY, o.reason));
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  View 5: Network Toolbox (Interactive Diagnostics Hub)
// ═════════════════════════════════════════════════════════════
static void viewToolbox(int cols, int rows) {
    string target = sites.empty() ? "8.8.8.8" : sites[SEL_SITE % sites.size()].host;
    if (!g_toolResult.targetHost.empty()) target = g_toolResult.targetHost;

    // Toolbox Subnav Menu
    addLine(col(C_CYN, "╭─ [ NETWORK TOOLBOX ] ") + repeat("─", std::max(2, cols - 26)) + "╮");

    if (cols >= 110) {
        string toolsLine = "  " +
            col(TOOL_ACTIVE_ID == 1 ? C_CYN : C_GRY, "[1 Traceroute]") + " " +
            col(TOOL_ACTIVE_ID == 2 ? C_CYN : C_GRY, "[2 DNS (dig)]") + " " +
            col(TOOL_ACTIVE_ID == 3 ? C_CYN : C_GRY, "[3 Port Scan]") + " " +
            col(TOOL_ACTIVE_ID == 4 ? C_CYN : C_GRY, "[4 HTTP/curl]") + " " +
            col(TOOL_ACTIVE_ID == 5 ? C_CYN : C_GRY, "[5 Interfaces]") + " " +
            col(TOOL_ACTIVE_ID == 6 ? C_CYN : C_GRY, "[6 Routes]") + " " +
            col(TOOL_ACTIVE_ID == 7 ? C_CYN : C_GRY, "[7 Sockets]") + " " +
            col(TOOL_ACTIVE_ID == 8 ? C_CYN : C_GRY, "[8 Traffic]") + " " +
            col(TOOL_ACTIVE_ID == 9 ? C_CYN : C_GRY, "[9 Custom]");
        addLine(toolsLine);
    } else {
        string l1 = "  " +
            col(TOOL_ACTIVE_ID == 1 ? C_CYN : C_GRY, "[1 Traceroute]") + " " +
            col(TOOL_ACTIVE_ID == 2 ? C_CYN : C_GRY, "[2 DNS (dig)]") + " " +
            col(TOOL_ACTIVE_ID == 3 ? C_CYN : C_GRY, "[3 Port Scan]") + " " +
            col(TOOL_ACTIVE_ID == 4 ? C_CYN : C_GRY, "[4 HTTP/curl]") + " " +
            col(TOOL_ACTIVE_ID == 5 ? C_CYN : C_GRY, "[5 Interfaces]");
        string l2 = "  " +
            col(TOOL_ACTIVE_ID == 6 ? C_CYN : C_GRY, "[6 Routes]") + " " +
            col(TOOL_ACTIVE_ID == 7 ? C_CYN : C_GRY, "[7 Sockets]") + " " +
            col(TOOL_ACTIVE_ID == 8 ? C_CYN : C_GRY, "[8 Traffic]") + " " +
            col(TOOL_ACTIVE_ID == 9 ? C_CYN : C_GRY, "[9 Custom]");
        addLine(l1);
        addLine(l2);
    }

    // Target Selector & Status
    string targetDisplay = col(C_WHT, BOLD + target) + col(C_GRY, "  (Press 'c' for custom target, Tab to cycle hosts)");
    string runningIndicator = g_toolResult.running
        ? col(C_YEL, BOLD + string(SPIN[(ROUND * 2) % 10]) + " " + g_toolResult.statusText)
        : col(C_GRN, "✔ " + g_toolResult.statusText);

    addLine("  " + col(C_CYN, "Target: ") + targetDisplay);
    addLine("  " + runningIndicator);
    addLine(col(C_DGRY, "├" + repeat("─", cols - 2) + "┤"));

    // Action bar inside Toolbox
    string viewModeStr = g_toolResult.rawView ? col(C_YEL, "[RAW VIEW]") : col(C_CYN, "[PARSED VISUAL]");
    addLine("  " + col(C_WHT, BOLD + g_toolResult.title) + "  " + viewModeStr +
            col(C_GRY, "  ·  v: Toggle View  r: Re-run  s: Save  /: Search  ↑/↓: Scroll"));
    addLine("");

    // Output Display Area
    std::lock_guard<std::mutex> lock(g_toolMutex);
    const vector<string> &content = g_toolResult.rawView ? g_toolResult.rawLines : g_toolResult.formattedRows;
    int availableRows = rows - (int)g_frameLines.size() - 3;
    if (availableRows < 3) availableRows = 3;

    if (content.empty()) {
        if (g_toolResult.running) {
            addLine(col(C_CYN, "  Running diagnostic tool in background, streaming output..."));
        } else {
            addLine(col(C_GRY, "  Select a tool above (1-9) or press 'r' to execute for " + target));
        }
    } else {
        int maxOffset = std::max(0, (int)content.size() - availableRows);
        int offset = std::min(maxOffset, std::max(0, g_toolResult.scrollOffset));

        for (int i = 0; i < availableRows && (offset + i) < (int)content.size(); i++) {
            const string &line = content[offset + i];
            if (!g_toolResult.searchQuery.empty() && toLower(line).find(toLower(g_toolResult.searchQuery)) != string::npos) {
                addLine(" " + col(BG_SEL, line));
            } else {
                addLine(" " + line);
            }
        }
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  View 6: Help & Keybindings Cheat Sheet
// ═════════════════════════════════════════════════════════════
static void viewHelp(int cols, int rows) {
    (void)rows;
    addLine(col(C_CYN, "╭─ [ NETWATCH PRO – KEYBOARD COMMANDS & USER GUIDE ] ") + repeat("─", std::max(2, cols - 56)) + "╮");
    addLine("");

    struct HelpKey { const char *key; const char *desc; };
    vector<HelpKey> keys = {
        {"1-6", "Switch views: 1 Overview, 2 Compare, 3 Detail, 4 Outages, 5 Toolbox, 6 Help"},
        {"Tab / ← / →", "Cycle through monitored target hosts"},
        {"a / A", "Add a new target host or IP address dynamically without restarting"},
        {"d / D", "Remove currently selected host from live monitoring"},
        {"+ / -", "Increase / decrease ping interval (1s to 60s)"},
        {"r / R", "Trigger immediate ping cycle or re-run current toolbox tool"},
        {"p / P", "Pause / resume automatic ping probes"},
        {"l / L", "Toggle logarithmic vs linear Y-axis scale (Compare view)"},
        {"t / T", "Jump directly into Network Toolbox"},
        {"v / V", "Toggle between Rich Parsed View and Raw Terminal Output in Toolbox"},
        {"c / C", "Specify a custom target domain/IP for Toolbox diagnostics"},
        {"/ ", "Search and filter lines inside diagnostic tool output"},
        {"s / S", "Save/export current view or tool report to a file"},
        {"z / Z", "Reset all latency, outage, and packet statistics"},
        {"q / Esc", "Quit NetWatch Pro and return to standard terminal"}
    };

    for (const auto &k : keys) {
        addLine(fmt("  %-16s %s", col(C_CYN, BOLD + string(k.key)).c_str(), col(C_WHT, k.desc).c_str()));
    }

    addLine("");
    addLine(BOLD + string("  Integrated Network Tools in Toolbox:") + RST);
    addLine(col(C_GRY, "  • Traceroute: Visual hop-by-hop path tracing with latency sparklines & loss"));
    addLine(col(C_GRY, "  • DNS (dig): Fast query for A, AAAA, MX, TXT, NS records + resolver timing"));
    addLine(col(C_GRY, "  • Port Scan: High-speed multi-threaded TCP scanner for common services"));
    addLine(col(C_GRY, "  • HTTP Inspector: Web response latency breakdown (DNS, Connect, TLS, TTFB)"));
    addLine(col(C_GRY, "  • Network Interfaces: Enumerate IPv4, IPv6, MAC hardware addresses, MTU"));
    addLine(col(C_GRY, "  • Routing & Gateway: Discover default gateway and active system routes"));
    addLine(col(C_GRY, "  • Sockets & Ports: Inspect open listening ports and active TCP connections"));
}

// ═════════════════════════════════════════════════════════════════════════════
//  Frame Rendering & Double-Buffering
// ═════════════════════════════════════════════════════════════
static void renderFrame(bool checking, Clock::time_point lastCheck) {
    int cols, rows;
    getTermSize(cols, rows);
    g_frameLines.clear();

    if (cols < 48 || rows < 10) {
        addLine(col(C_CYN, string(BOLD) + " ◉ NETWATCH PRO 3.0 "));
        addLine("");
        addLine(col(C_YEL, fmt("  Terminal size (%dx%d) is too small.", cols, rows)));
        addLine(col(C_GRY, "  Please expand your terminal window (min 48x10)."));
        while ((int)g_frameLines.size() < rows - 1) addLine("");
        g_frameLines.push_back(col(DIM, " q to quit"));
        string frameBuffer = "\033[H";
        for (size_t i = 0; i < g_frameLines.size(); i++) {
            frameBuffer += truncateToWidth(g_frameLines[i], (size_t)cols);
            frameBuffer += "\033[K";
            if (i + 1 < g_frameLines.size()) frameBuffer += "\n";
        }
        frameBuffer += "\033[J";
        (void)!write(STDOUT_FILENO, frameBuffer.data(), frameBuffer.size());
        return;
    }

    long nowMs = (long)std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - START_TIME).count();
    const char *spin = SPIN[(nowMs / 100) % 10];

    // Top Header Bar
    string title = " ◉ NETWATCH PRO 3.0 ";
    string right = timeStr(time(nullptr), "%a %d %b %H:%M:%S") + " ";
    int padHdr = std::max(1, cols - (int)visibleWidth(title) - (int)visibleWidth(right));
    addLine(string(BG_HDR) + BOLD + title + string(padHdr, ' ') + right + RST);

    // Tab Bar
    const char *tabs[] = {
        "1 Overview", "2 Compare", "3 Detail", "4 Outages", "5 Toolbox", "6 Help"
    };
    string tb = " ";
    for (int i = 0; i < 6; i++) {
        if (VIEW == i + 1) {
            tb += string(INV) + BOLD + " " + tabs[i] + " " + RST + " ";
        } else {
            tb += col(C_GRY, string(" ") + tabs[i] + " ") + " ";
        }
    }
    addLine(tb);

    // Context Info Subheader
    string info = fmt(" Running %s  ·  Every %ds  ·  Pings: %d  ·  Hosts: %zu",
                      fmtDur(nowMs / 1000).c_str(), INTERVAL, ROUND, sites.size());
    if (PAUSED) info += "  ·  " + col(C_YEL, BOLD + string("PAUSED"));
    if (ALERTS) info += "  ·  " + col(C_CYN, "Alerts ON");
    if (g_log) info += "  ·  Log: " + col(C_GRY, g_logPath);
    addLine(col(C_GRY, info));

    // Active View Content
    switch (VIEW) {
        case 1: viewOverview(cols, rows); break;
        case 2: viewCompare(cols, rows); break;
        case 3: viewDetail(cols, rows); break;
        case 4: viewOutages(cols, rows); break;
        case 5: viewToolbox(cols, rows); break;
        case 6: viewHelp(cols, rows); break;
    }

    // Modal Dialog Overlay (if active)
    if (g_modal != ModalMode::NONE) {
        addLine("");
        string modalBorder = col(C_YEL, "╭── " + g_inputPrompt + repeat("─", std::max(2, cols - (int)g_inputPrompt.size() - 10)) + "╮");
        string modalBody = col(C_YEL, "│ ") + BOLD + g_inputBuf + col(C_GRN, "█") +
                           string(std::max(1, cols - 6 - (int)g_inputBuf.size()), ' ') + col(C_YEL, "│");
        string modalFoot = col(C_YEL, "╰── (Press Enter to confirm, Esc or Ctrl+C to cancel) " + repeat("─", std::max(2, cols - 58)) + "╯");
        addLine(modalBorder);
        addLine(modalBody);
        addLine(modalFoot);
    } else if (VIEW == 1) {
        // Ping progress and recent events at bottom of overview
        int barWidth = std::max(10, cols - 36);
        if (checking) {
            addLine("  " + col(C_CYN, string(spin) + " Probing hosts in parallel..."));
        } else if (PAUSED) {
            addLine("  " + col(C_YEL, "Paused – press 'p' to resume, 'r' to ping once"));
        } else {
            long sinceMs = (long)std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - lastCheck).count();
            long totalMs = INTERVAL * 1000L;
            int filled = (int)std::min((long)barWidth, (sinceMs * barWidth) / totalMs);
            long leftSec = std::max(0L, (totalMs - sinceMs) / 1000L);
            addLine("  " + col(C_CYN, "Next probe: ") + col(C_CYN, repeat("█", filled) + repeat("░", barWidth - filled)) +
                    col(C_GRY, fmt(" in %lds", leftSec)));
        }

        addLine(string("  ") + BOLD + "Recent Network Events" + RST);
        if (events.empty()) {
            addLine(col(C_DGRY, "   None yet"));
        } else {
            int shown = 0;
            for (const auto &e : events) {
                if (shown++ >= 3) break;
                addLine("   " + col(C_GRY, e));
            }
        }
    }

    // Pad to bottom and render footer
    while ((int)g_frameLines.size() < rows - 1) addLine("");
    if ((int)g_frameLines.size() > rows - 1) g_frameLines.resize(rows - 1);

    string footer = " 1-6 Views · Tab/j/k Host · Enter Detail · a Add · t Toolbox · r Ping · p Pause · q Quit";
    g_frameLines.push_back(col(DIM, footer));

    // Atomic screen update with line clamping to eliminate all wrapping & jitter
    string frameBuffer = "\033[H";
    for (size_t i = 0; i < g_frameLines.size(); i++) {
        frameBuffer += truncateToWidth(g_frameLines[i], (size_t)cols);
        frameBuffer += "\033[K";
        if (i + 1 < g_frameLines.size()) frameBuffer += "\n";
    }
    frameBuffer += "\033[J";
    (void)!write(STDOUT_FILENO, frameBuffer.data(), frameBuffer.size());
}

// ═════════════════════════════════════════════════════════════════════════════
//  CLI Help & Usage
// ═════════════════════════════════════════════════════════════
static void showVersion() {
#if defined(__APPLE__)
    const char *osName = "macOS";
#elif defined(__linux__)
    const char *osName = "Linux";
#elif defined(_WIN32)
    const char *osName = "Windows";
#else
    const char *osName = "Unix";
#endif

#if defined(__arm64__) || defined(__aarch64__)
    const char *arch = "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
    const char *arch = "x86_64";
#else
    const char *arch = "generic";
#endif

    printf("NetWatch Pro 3.0.0 (%s %s)\n", osName, arch);
    printf("Modern C++17 Zero-Dependency Terminal Network Diagnostics & Monitor\n");
}

static void showUsage(const char *prog) {
    printf(
        "NETWATCH PRO 3.0 – Terminal Network Monitor & Diagnostics Hub\n\n"
        "Usage: %s [options] [host ...]\n\n"
        "Options:\n"
        "  -i <sec>        Interval between pings in seconds (default: 3)\n"
        "  -t <sec>        Reply timeout in seconds (default: 2)\n"
        "  -l <file>       Export ping metrics to CSV log file\n"
        "  -a              Enable audio bell and OS notifications on outage\n"
        "  -g              Automatically include default router/gateway\n"
        "  -v, --version   Show version and system architecture\n"
        "  -h, --help      Show this help message and exit\n\n"
        "Interactive Keybindings:\n"
        "  1-6             Switch Views (Overview, Compare, Detail, Outages, Toolbox, Help)\n"
        "  Tab / j / k     Navigate / switch selected host (or ← / → / ↑ / ↓)\n"
        "  Enter           Inspect selected host (Detail view) or re-run active tool\n"
        "  Esc / b         Back to Overview dashboard\n"
        "  a / d           Add new host (supports URL/IP) / Delete selected host\n"
        "  t               Open Network Toolbox (traceroute, dig, portscan, curl, etc.)\n"
        "  r               Force immediate ping or re-run toolbox tool\n"
        "  p               Pause / Resume pinging\n"
        "  + / -           Adjust ping interval\n"
        "  l               Toggle logarithmic / linear chart scale\n"
        "  v               Toggle rich visual vs raw output in Toolbox\n"
        "  s               Export report or snapshot to file\n"
        "  /               Search output in Toolbox\n"
        "  q / Ctrl+C      Quit NetWatch and show session summary\n",
        prog
    );
}

static void printExitSummary() {
    long sec = (long)std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - START_TIME).count();
    printf("\n" BOLD "NETWATCH PRO Summary" RST " (Active session: %s)\n", fmtDur(sec).c_str());
    printf("%s", col(C_CYN, fmt("%-20s %6s %6s %7s %8s %8s %8s %8s %10s\n",
        "Host", "Sent", "Recv", "Loss", "Min", "Avg", "Max", "P95", "Downtime")).c_str());
    printf("%s\n", repeat("─", 84).c_str());

    for (size_t i = 0; i < sites.size(); i++) {
        Site &s = sites[i];
        double loss = s.sent ? ((s.sent - s.recv) * 100.0 / s.sent) : 0;
        double avg = s.recv ? (s.sum / s.recv) : 0;
        printf("%-20s %6d %6d %6.1f%% %7.1fms %7.1fms %7.1fms %7.1fms %10s\n",
               s.name.substr(0, 19).c_str(), s.sent, s.recv, loss,
               s.mn, avg, s.mx, s.p95, fmtSpan(downtimeOf(i)).c_str());
    }
    if (g_log) {
        printf("\nCSV log written to: %s\n", g_logPath.c_str());
    }
    fflush(stdout);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Main Application Loop
// ═════════════════════════════════════════════════════════════
int main(int argc, char **argv) {
    platformInitSockets();
    START_TIME = Clock::now();

    string progName = (argc > 0 && argv[0]) ? argv[0] : "netwatch";
    bool isPingInvoked = (progName.find("netping") != string::npos);

    vector<string> cliHosts;
    bool withGateway = false;

    for (int a = 1; a < argc; a++) {
        string k = argv[a];
        if (k == "-v" || k == "--version" || k == "-V") {
            showVersion();
            return 0;
        } else if ((k == "-i" || k == "-t") && a + 1 < argc) {
            int v = atoi(argv[++a]);
            if (v < 1) { showUsage(argv[0]); return 1; }
            (k == "-i" ? INTERVAL : TIMEOUT) = v;
        } else if (k == "-l" && a + 1 < argc) {
            g_logPath = argv[++a];
        } else if (k == "-a") {
            ALERTS = true;
        } else if (k == "-g") {
            withGateway = true;
        } else if (k == "-h" || k == "--help") {
            showUsage(argv[0]);
            return 0;
        } else if (!k.empty() && k[0] == '-') {
            showUsage(argv[0]);
            return 1;
        } else {
            string cleaned = cleanHost(k);
            if (!cleaned.empty()) cliHosts.push_back(cleaned);
        }
    }

    struct InitTarget { string name, host; };
    vector<InitTarget> targets;

    if (!cliHosts.empty()) {
        for (const auto &h : cliHosts) {
            targets.push_back({h, h});
        }
    } else {
        // Smart production defaults: Google DNS & Cloudflare DNS
        targets.push_back({"Google DNS", "8.8.8.8"});
        targets.push_back({"Cloudflare DNS", "1.1.1.1"});
        withGateway = true; // Auto-detect router gateway by default
    }

    if (withGateway) {
        string gw = detectGateway();
        if (!gw.empty()) {
            targets.insert(targets.begin(), {"Default Gateway (Router)", gw});
        }
    }

    for (const auto &t : targets) {
        Site s;
        s.name = t.name;
        s.host = t.host;
        sites.push_back(s);
    }

    if (isPingInvoked && !sites.empty()) {
        VIEW = 3; // Launch directly in Single-Host Detail View when called as netping
    }

    if (!g_logPath.empty()) {
        g_log = fopen(g_logPath.c_str(), "a");
        if (g_log && ftell(g_log) == 0) {
            fprintf(g_log, "timestamp,host,ip,status,rtt_ms\n");
        }
    }

    termEnableRaw();

    vector<Probe> results(sites.size());
    vector<std::thread> workers;
    std::atomic<int> pendingProbes{0};
    bool checking = false;
    bool forceProbe = true;
    auto lastCheck = Clock::now();

    while (!g_quitRequested) {
        // Handle window resize event immediately
        if (g_resized) {
            g_resized = 0;
        }

        // Launch parallel ping probes if scheduled
        if (!checking && (forceProbe || (!PAUSED && !sites.empty() &&
            (std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - lastCheck).count() >= INTERVAL * 1000L)))) {
            forceProbe = false;
            checking = true;
            results.resize(sites.size());
            pendingProbes = (int)sites.size();

            for (size_t i = 0; i < sites.size(); i++) {
                string host = sites[i].host;
                uint16_t seq = ++sites[i].seq;
                workers.emplace_back([&results, &pendingProbes, i, host, seq]() {
                    results[i] = doPing(host, TIMEOUT, seq);
                    pendingProbes--;
                });
            }
        }

        // Collect completed probes
        if (checking && pendingProbes == 0) {
            for (auto &w : workers) {
                if (w.joinable()) w.join();
            }
            workers.clear();
            for (size_t i = 0; i < sites.size() && i < results.size(); i++) {
                mergeProbe(i, results[i]);
            }
            ROUND++;
            checking = false;
            lastCheck = Clock::now();
        }

        renderFrame(checking, lastCheck);

        // Non-blocking Keyboard Input
#ifndef _WIN32
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        timeval tv{0, 60000}; // 60ms refresh for smooth responsiveness
        if (select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv) > 0) {
            char kb[128];
            ssize_t n = read(STDIN_FILENO, kb, sizeof(kb));
            if (n <= 0) {
                if (n == 0 && !isatty(STDIN_FILENO)) {
                    g_quitRequested = 1;
                }
                continue;
            }
            for (ssize_t i = 0; i < n; i++) {
                char ch = kb[i];
                size_t cnt = sites.size();

                // Ctrl+C (ASCII 3): instantly cancel modal or quit application
                if (ch == 3) {
                    if (g_modal != ModalMode::NONE) {
                        g_modal = ModalMode::NONE;
                        g_inputBuf.clear();
                    } else {
                        g_quitRequested = 1;
                    }
                    continue;
                }

                // Handle active text input modal
                if (g_modal != ModalMode::NONE) {
                    if (ch == 27) { // Escape
                        g_modal = ModalMode::NONE;
                        g_inputBuf.clear();
                    } else if (ch == '\n' || ch == '\r') {
                        if (g_modal == ModalMode::ADD_HOST && !g_inputBuf.empty()) {
                            string clean = cleanHost(g_inputBuf);
                            if (!clean.empty()) {
                                if (checking) {
                                    for (auto &w : workers) { if (w.joinable()) w.join(); }
                                    workers.clear();
                                    checking = false;
                                }
                                Site newSite;
                                newSite.name = clean;
                                newSite.host = clean;
                                sites.push_back(newSite);
                                addEvent("Added target host: " + clean);
                                forceProbe = true;
                            }
                        } else if (g_modal == ModalMode::SET_INTERVAL) {
                            int val = atoi(g_inputBuf.c_str());
                            if (val >= 1 && val <= 60) INTERVAL = val;
                        } else if (g_modal == ModalMode::TOOL_CUSTOM_CMD && !g_inputBuf.empty()) {
                            runCustomCommandTool(g_inputBuf);
                        } else if (g_modal == ModalMode::TOOL_CUSTOM_HOST && !g_inputBuf.empty()) {
                            string clean = cleanHost(g_inputBuf);
                            if (!clean.empty()) {
                                g_toolResult.targetHost = clean;
                                launchTool(TOOL_ACTIVE_ID, clean);
                            }
                        } else if (g_modal == ModalMode::TOOL_SEARCH) {
                            g_toolResult.searchQuery = g_inputBuf;
                        }
                        g_modal = ModalMode::NONE;
                        g_inputBuf.clear();
                    } else if (ch == 127 || ch == 8) { // Backspace
                        if (!g_inputBuf.empty()) g_inputBuf.pop_back();
                    } else if (ch == 21) { // Ctrl+U: clear input line
                        g_inputBuf.clear();
                    } else if (isprint((unsigned char)ch)) {
                        g_inputBuf += ch;
                    }
                    continue;
                }

                // Handle ANSI escape sequences (Arrow keys, Home, End, PageUp/Down)
                if (ch == 27 && i + 1 < n && (kb[i + 1] == '[' || kb[i + 1] == 'O')) {
                    char prefix = kb[i + 1];
                    if (prefix == 'O' && i + 2 < n) {
                        // Application cursor keys (SS3 mode on macOS Terminal / iTerm2)
                        char c2 = kb[i + 2];
                        i += 2;
                        if (c2 == 'C') { // Right
                            if (cnt) SEL_SITE = (SEL_SITE + 1) % (int)cnt;
                        } else if (c2 == 'D') { // Left
                            if (cnt) SEL_SITE = (SEL_SITE + (int)cnt - 1) % (int)cnt;
                        } else if (c2 == 'B') { // Down
                            if (VIEW == 5) g_toolResult.scrollOffset += 3;
                            else if (cnt) SEL_SITE = (SEL_SITE + 1) % (int)cnt;
                        } else if (c2 == 'A') { // Up
                            if (VIEW == 5) g_toolResult.scrollOffset = std::max(0, g_toolResult.scrollOffset - 3);
                            else if (cnt) SEL_SITE = (SEL_SITE + (int)cnt - 1) % (int)cnt;
                        } else if (c2 == 'H') { // Home
                            if (VIEW == 5) g_toolResult.scrollOffset = 0;
                        } else if (c2 == 'F') { // End
                            if (VIEW == 5) g_toolResult.scrollOffset = 99999;
                        }
                        continue;
                    } else if (prefix == '[' && i + 2 < n) {
                        char c2 = kb[i + 2];
                        if (c2 == 'C') { // Right
                            i += 2;
                            if (cnt) SEL_SITE = (SEL_SITE + 1) % (int)cnt;
                            continue;
                        } else if (c2 == 'D') { // Left
                            i += 2;
                            if (cnt) SEL_SITE = (SEL_SITE + (int)cnt - 1) % (int)cnt;
                            continue;
                        } else if (c2 == 'B') { // Down
                            i += 2;
                            if (VIEW == 5) g_toolResult.scrollOffset += 3;
                            else if (cnt) SEL_SITE = (SEL_SITE + 1) % (int)cnt;
                            continue;
                        } else if (c2 == 'A') { // Up
                            i += 2;
                            if (VIEW == 5) g_toolResult.scrollOffset = std::max(0, g_toolResult.scrollOffset - 3);
                            else if (cnt) SEL_SITE = (SEL_SITE + (int)cnt - 1) % (int)cnt;
                            continue;
                        } else if (c2 == 'H') { // Home
                            i += 2;
                            if (VIEW == 5) g_toolResult.scrollOffset = 0;
                            continue;
                        } else if (c2 == 'F') { // End
                            i += 2;
                            if (VIEW == 5) g_toolResult.scrollOffset = 99999;
                            continue;
                        } else if ((c2 == '5' || c2 == '6' || c2 == '1' || c2 == '4') && i + 3 < n && kb[i + 3] == '~') {
                            i += 3; // Consume full 4-byte sequence e.g. \x1b[5~
                            if (c2 == '5') { // Page Up
                                if (VIEW == 5) g_toolResult.scrollOffset = std::max(0, g_toolResult.scrollOffset - 12);
                            } else if (c2 == '6') { // Page Down
                                if (VIEW == 5) g_toolResult.scrollOffset += 12;
                            } else if (c2 == '1') { // Home
                                if (VIEW == 5) g_toolResult.scrollOffset = 0;
                            } else if (c2 == '4') { // End
                                if (VIEW == 5) g_toolResult.scrollOffset = 99999;
                            }
                            continue;
                        }
                    }
                }

                // Normal Keybindings
                if (ch == 'q' || ch == 'Q') {
                    g_quitRequested = 1;
                } else if (VIEW == 5 && ch >= '1' && ch <= '9') {
                    string tgt = sites.empty() ? "8.8.8.8" : sites[SEL_SITE % sites.size()].host;
                    if (!g_toolResult.targetHost.empty()) tgt = g_toolResult.targetHost;
                    launchTool(ch - '0', tgt);
                } else if (ch >= '1' && ch <= '6') {
                    VIEW = ch - '0';
                    if (VIEW == 5 && g_toolResult.type == ToolType::NONE && !sites.empty()) {
                        launchTool(1, sites[SEL_SITE % sites.size()].host);
                    }
                } else if (ch == '\n' || ch == '\r') {
                    // Enter / Return: intuitive drill-down navigation
                    if (VIEW == 1 && !sites.empty()) {
                        VIEW = 3; // Jump directly to Deep Dive Detail view
                    } else if (VIEW == 3 && !sites.empty()) {
                        VIEW = 5; // Jump from Detail to Network Toolbox
                        if (g_toolResult.type == ToolType::NONE) {
                            launchTool(1, sites[SEL_SITE % sites.size()].host);
                        }
                    } else if (VIEW == 5) {
                        string tgt = sites.empty() ? "8.8.8.8" : sites[SEL_SITE % sites.size()].host;
                        if (!g_toolResult.targetHost.empty()) tgt = g_toolResult.targetHost;
                        launchTool(TOOL_ACTIVE_ID, tgt);
                    }
                } else if (ch == 27 || ch == 'b' || ch == 'B') {
                    // Esc / Back: return to Overview
                    if (VIEW != 1) VIEW = 1;
                } else if (ch == '?' || ch == 'h' || ch == 'H') {
                    VIEW = (VIEW == 6) ? 1 : 6;
                } else if (ch == 'j' || ch == 'J') { // Vim Down
                    if (VIEW == 5) {
                        g_toolResult.scrollOffset += 3;
                    } else if (cnt) {
                        SEL_SITE = (SEL_SITE + 1) % (int)cnt;
                    }
                } else if (ch == 'k' || ch == 'K') { // Vim Up
                    if (VIEW == 5) {
                        g_toolResult.scrollOffset = std::max(0, g_toolResult.scrollOffset - 3);
                    } else if (cnt) {
                        SEL_SITE = (SEL_SITE + (int)cnt - 1) % (int)cnt;
                    }
                } else if (ch == '\t' && cnt) {
                    SEL_SITE = (SEL_SITE + 1) % (int)cnt;
                    if (VIEW == 5) {
                        launchTool(TOOL_ACTIVE_ID, sites[SEL_SITE % sites.size()].host);
                    }
                } else if (ch == 't' || ch == 'T') {
                    VIEW = (VIEW == 5) ? 1 : 5;
                    if (VIEW == 5 && g_toolResult.type == ToolType::NONE && !sites.empty()) {
                        launchTool(1, sites[SEL_SITE % sites.size()].host);
                    }
                } else if (ch == 'r' || ch == 'R') {
                    if (VIEW == 5) {
                        if (TOOL_ACTIVE_ID == 9 && !g_toolResult.command.empty()) {
                            runCustomCommandTool(g_toolResult.command);
                        } else {
                            string tgt = sites.empty() ? "8.8.8.8" : sites[SEL_SITE % sites.size()].host;
                            if (!g_toolResult.targetHost.empty()) tgt = g_toolResult.targetHost;
                            launchTool(TOOL_ACTIVE_ID, tgt);
                        }
                    } else if (!checking) {
                        forceProbe = true;
                    }
                } else if (ch == 'p' || ch == 'P') {
                    PAUSED = !PAUSED;
                } else if (ch == 'a' || ch == 'A') {
                    g_modal = ModalMode::ADD_HOST;
                    g_inputPrompt = "Enter Host or IP to monitor (e.g. google.com, 8.8.8.8): ";
                    g_inputBuf.clear();
                } else if (ch == 'd' || ch == 'D') {
                    if (sites.size() > 1) {
                        if (checking) {
                            for (auto &w : workers) { if (w.joinable()) w.join(); }
                            workers.clear();
                            checking = false;
                        }
                        addEvent("Removed site: " + sites[SEL_SITE % sites.size()].name);
                        sites.erase(sites.begin() + (SEL_SITE % sites.size()));
                        if (SEL_SITE >= (int)sites.size()) SEL_SITE = (int)sites.size() - 1;
                    }
                } else if (ch == '+' || ch == '=') {
                    INTERVAL = std::min(60, INTERVAL + 1);
                } else if (ch == '-' || ch == '_') {
                    INTERVAL = std::max(1, INTERVAL - 1);
                } else if (ch == 'l' || ch == 'L') {
                    LOGSCALE = !LOGSCALE;
                } else if (ch == 'v' || ch == 'V') {
                    if (VIEW == 5) g_toolResult.rawView = !g_toolResult.rawView;
                } else if (ch == 'c' || ch == 'C') {
                    if (VIEW == 5) {
                        g_modal = ModalMode::TOOL_CUSTOM_HOST;
                        g_inputPrompt = "Enter custom host for Toolbox diagnostics: ";
                        g_inputBuf.clear();
                    }
                } else if (ch == '/') {
                    if (VIEW == 5) {
                        g_modal = ModalMode::TOOL_SEARCH;
                        g_inputPrompt = "Search tool output: ";
                        g_inputBuf.clear();
                    }
                } else if (ch == 's' || ch == 'S') {
                    string fname = "netwatch_report_" + std::to_string(time(nullptr)) + ".txt";
                    std::ofstream out(fname);
                    if (VIEW == 5) {
                        const auto &lines = g_toolResult.rawLines.empty() ? g_toolResult.formattedRows : g_toolResult.rawLines;
                        for (const auto &l : lines) out << l << "\n";
                    } else {
                        for (const auto &l : g_frameLines) out << l << "\n";
                    }
                    addEvent("Exported report to " + fname);
                } else if (ch == 'z' || ch == 'Z') {
                    if (checking) {
                        for (auto &w : workers) { if (w.joinable()) w.join(); }
                        workers.clear();
                        checking = false;
                    }
                    for (auto &s : sites) s.resetStats();
                    outages.clear();
                    events.clear();
                    ROUND = 0;
                    addEvent("Statistics reset");
                    forceProbe = true;
                }
            }
        }
#else
        Sleep(60);
        if (_kbhit()) {
            char ch = (char)_getch();
            if (ch == 'q' || ch == 'Q' || ch == 3) g_quitRequested = 1;
            else if (VIEW == 5 && ch >= '1' && ch <= '9') {
                string tgt = sites.empty() ? "8.8.8.8" : sites[SEL_SITE % sites.size()].host;
                if (!g_toolResult.targetHost.empty()) tgt = g_toolResult.targetHost;
                launchTool(ch - '0', tgt);
            }
            else if (ch >= '1' && ch <= '6') VIEW = ch - '0';
            else if (ch == 27 || ch == 'b' || ch == 'B') VIEW = 1;
            else if (ch == '\r' || ch == '\n') {
                if (VIEW == 1) VIEW = 3;
                else if (VIEW == 3) VIEW = 5;
            }
            else if ((ch == '\t' || ch == 'j' || ch == 'J') && !sites.empty()) {
                SEL_SITE = (SEL_SITE + 1) % (int)sites.size();
                if (VIEW == 5) launchTool(TOOL_ACTIVE_ID, sites[SEL_SITE % sites.size()].host);
            }
            else if ((ch == 'k' || ch == 'K') && !sites.empty()) {
                SEL_SITE = (SEL_SITE + (int)sites.size() - 1) % (int)sites.size();
                if (VIEW == 5) launchTool(TOOL_ACTIVE_ID, sites[SEL_SITE % sites.size()].host);
            }
            else if (ch == 't' || ch == 'T') {
                VIEW = (VIEW == 5) ? 1 : 5;
                if (VIEW == 5 && g_toolResult.type == ToolType::NONE && !sites.empty()) {
                    launchTool(1, sites[SEL_SITE % sites.size()].host);
                }
            }
            else if (ch == 'p') PAUSED = !PAUSED;
            else if (ch == 'r') {
                if (VIEW == 5) {
                    if (TOOL_ACTIVE_ID == 9 && !g_toolResult.command.empty()) {
                        runCustomCommandTool(g_toolResult.command);
                    } else {
                        string tgt = sites.empty() ? "8.8.8.8" : sites[SEL_SITE % sites.size()].host;
                        if (!g_toolResult.targetHost.empty()) tgt = g_toolResult.targetHost;
                        launchTool(TOOL_ACTIVE_ID, tgt);
                    }
                } else if (!checking) forceProbe = true;
            }
            else if (ch == 'v' && VIEW == 5) g_toolResult.rawView = !g_toolResult.rawView;
            else if (ch == 'l') LOGSCALE = !LOGSCALE;
            else if (ch == '+' || ch == '=') INTERVAL = std::min(60, INTERVAL + 1);
            else if (ch == '-' || ch == '_') INTERVAL = std::max(1, INTERVAL - 1);
        }
#endif
    }

    for (auto &w : workers) {
        if (w.joinable()) w.detach();
    }
    if (g_toolWorker.joinable()) g_toolWorker.detach();
    termRestore();
    printExitSummary();
    std::_Exit(0);
    return 0;
}
