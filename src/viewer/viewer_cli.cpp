/*
 * viewer_cli.cpp — Monitor Viewer Client
 * v2: Uses CMD query protocol to fetch real data from server.
 *     Supports: /hosts, /history <host> [n], /log [n], /help, /clear
 *     Legacy snapshot push mode removed.
 */
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <ctime>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef HAVE_NCURSESW
#  include <ncursesw/curses.h>
#else
#  include <curses.h>
#endif

static std::string trim(const std::string &s) {
  auto b = s.find_first_not_of(" \t\r\n");
  auto e = s.find_last_not_of(" \t\r\n");
  return (b == std::string::npos) ? "" : s.substr(b, e - b + 1);
}

static int connectTo(const std::string &host, uint16_t port) {
  addrinfo hints{}, *res = nullptr;
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0)
    return -1;
  int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (fd < 0) { freeaddrinfo(res); return -1; }
  if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
    close(fd); freeaddrinfo(res); return -1;
  }
  freeaddrinfo(res);
  return fd;
}

// Send a CMD query to server; returns raw JSON response line
static std::string queryServer(const std::string &serverHost, uint16_t serverPort,
                               const std::string &cmd) {
  int fd = connectTo(serverHost, serverPort);
  if (fd < 0) return "{\"error\":\"cannot connect\"}";

  // Send command line
  std::string req = "CMD " + cmd + "\n";
  (void)send(fd, req.c_str(), req.size(), 0);

  // Receive response (up to 1MB)
  std::string resp;
  char buf[4096];
  struct timeval tv{}; tv.tv_sec = 5;
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  int n;
  while ((n = recv(fd, buf, sizeof(buf), 0)) > 0)
    resp.append(buf, n);
  close(fd);
  // Strip trailing newline
  while (!resp.empty() && (resp.back()=='\n'||resp.back()=='\r'))
    resp.pop_back();
  return resp;
}

// Minimal JSON array parser: split top-level objects
static std::vector<std::string> splitJsonObjects(const std::string &json) {
  std::vector<std::string> out;
  if (json.size() < 2 || json.front() != '[') return out;
  int depth = 0;
  size_t start = std::string::npos;
  for (size_t i = 0; i < json.size(); i++) {
    char c = json[i];
    if (c == '{') { if (depth == 0) start = i; depth++; }
    else if (c == '}') {
      depth--;
      if (depth == 0 && start != std::string::npos) {
        out.push_back(json.substr(start, i - start + 1));
        start = std::string::npos;
      }
    }
  }
  return out;
}

// Extract a JSON string value for a key
static std::string jstr(const std::string &obj, const std::string &key) {
  std::string pat = "\"" + key + "\":\"";
  auto pos = obj.find(pat);
  if (pos == std::string::npos) return "";
  pos += pat.size();
  auto end = obj.find('"', pos);
  if (end == std::string::npos) return "";
  return obj.substr(pos, end - pos);
}

// Extract a JSON numeric value for a key (as string)
static std::string jnum(const std::string &obj, const std::string &key) {
  std::string pat = "\"" + key + "\":";
  auto pos = obj.find(pat);
  if (pos == std::string::npos) return "-";
  pos += pat.size();
  auto end = obj.find_first_of(",}", pos);
  if (end == std::string::npos) return "-";
  return obj.substr(pos, end - pos);
}

static std::string nowStr(time_t t) {
  char b[32];
  strftime(b, sizeof(b), "%H:%M:%S", localtime(&t));
  return b;
}

// Format timestamp seconds since epoch
static std::string fmtTs(const std::string &tsStr) {
  try {
    time_t t = (time_t)std::stoll(tsStr);
    return nowStr(t);
  } catch(...) { return tsStr; }
}

// ── ncurses color helpers ─────────────────────────────────────────────────────
static void initViewerColors() {
  start_color();
  use_default_colors();
  init_pair(1, COLOR_WHITE,   -1); // normal
  init_pair(2, COLOR_GREEN,   -1); // ok/online
  init_pair(3, COLOR_RED,     -1); // alert
  init_pair(4, COLOR_YELLOW,  -1); // warn
  init_pair(5, COLOR_CYAN,    -1); // header
  init_pair(6, COLOR_MAGENTA, -1); // stale
  init_pair(7, COLOR_WHITE,   COLOR_BLUE); // header bar
  if (COLORS >= 256) {
    init_pair(2,  46,  -1);
    init_pair(3, 196,  -1);
    init_pair(4, 202,  -1);
    init_pair(5, 153,  -1);
    init_pair(6, 141,  -1);
    init_pair(7,  15,  17);
  }
}

static int statusColor(const std::string &st) {
  if (st == "ALERT")   return 3;
  if (st == "WARN")    return 4;
  if (st == "STALE")   return 6;
  if (st == "OK")      return 2;
  return 1; // OFFLINE or unknown = white/dim
}

int main(int argc, char **argv) {
  std::string host = "127.0.0.1";
  uint16_t port = 8785;

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "-server" && i + 1 < argc) {
      std::string sv = argv[++i];
      auto p = sv.rfind(':');
      if (p == std::string::npos) host = sv;
      else {
        host = sv.substr(0, p);
        try { port = (uint16_t)std::stoi(sv.substr(p + 1)); }
        catch(...) { std::cerr << "Bad port value\n"; return 1; }
      }
    }
  }

  initscr(); noecho(); cbreak();
  keypad(stdscr, TRUE); nodelay(stdscr, TRUE); curs_set(0);
  initViewerColors();

  std::vector<std::string> outputLines;
  std::string lastCmd;
  bool cmdMode = false;
  std::string cmd;

  // Initial welcome screen
  outputLines = {
    "viewer_cli — Monitor Query Client",
    "",
    "Commands:",
    "  /hosts              - List all hosts with current status",
    "  /history <host> [n] - Metric history for a host (default n=30)",
    "  /log [n]            - Recent event log (default n=50)",
    "  /help               - Show this help",
    "  /clear              - Clear output",
    "",
    "Press '/' to enter a command, Esc to cancel, q to quit.",
  };

  while (true) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    erase();

    // Header bar
    attron(COLOR_PAIR(7) | A_BOLD);
    char hdrBuf[256];
    snprintf(hdrBuf, sizeof(hdrBuf), " ◈ MONITOR VIEWER  server=%s:%d ", host.c_str(), port);
    for (int i = 0; i < cols; i++) mvaddch(0, i, ' ');
    mvaddstr(0, 1, hdrBuf);
    int tsLen = 8; mvaddstr(0, cols - tsLen - 1, nowStr(time(nullptr)).c_str());
    attroff(COLOR_PAIR(7) | A_BOLD);

    // Output area
    int maxLines = rows - 3;
    int startLine = 0;
    if ((int)outputLines.size() > maxLines)
      startLine = (int)outputLines.size() - maxLines;
    for (int i = 0; i < maxLines && startLine + i < (int)outputLines.size(); i++) {
      const std::string &ln = outputLines[startLine + i];
      mvaddnstr(1 + i, 0, ln.c_str(), cols - 1);
    }

    // Status bar
    attron(COLOR_PAIR(5));
    mvhline(rows - 2, 0, ' ', cols);
    if (!lastCmd.empty()) {
      char sb[256]; snprintf(sb, sizeof(sb), " Last: /%s ", lastCmd.c_str());
      mvaddstr(rows - 2, 0, sb);
    }
    attroff(COLOR_PAIR(5));

    // Command bar
    attron(A_BOLD);
    mvhline(rows - 1, 0, ' ', cols);
    if (cmdMode) {
      attron(COLOR_PAIR(5) | A_BOLD);
      mvaddstr(rows - 1, 0, " > /");
      addstr(cmd.c_str()); addstr("_");
      attroff(COLOR_PAIR(5) | A_BOLD);
    } else {
      attron(COLOR_PAIR(1));
      mvaddstr(rows - 1, 0, " [/] command  [q] quit");
      attroff(COLOR_PAIR(1));
    }
    attroff(A_BOLD);

    refresh();
    std::this_thread::sleep_for(std::chrono::milliseconds(80));

    int ch = getch();
    if (ch == 'q' || ch == 'Q') break;

    if (!cmdMode) {
      if (ch == '/') { cmdMode = true; cmd.clear(); }
      continue;
    }

    if (ch == 27) { cmdMode = false; cmd.clear(); continue; }
    if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
      if (!cmd.empty()) cmd.pop_back();
      continue;
    }
    if (ch != '\n' && ch != KEY_ENTER) {
      if (ch >= 32 && ch < 127) cmd.push_back((char)ch);
      continue;
    }

    // Execute command
    std::string c = trim(cmd);
    cmdMode = false; cmd.clear();
    if (c.empty()) continue;

    std::istringstream ss(c);
    std::string verb, arg1; int nArg = 30;
    ss >> verb >> arg1 >> nArg;
    lastCmd = c;

    if (verb == "clear") { outputLines.clear(); continue; }
    if (verb == "help") {
      outputLines = {
        "Commands:",
        "  /hosts              - All hosts with status, CPU/RAM/DISK/LOAD",
        "  /history <host> [n] - Last n metric samples for a host",
        "  /log [n]            - Last n log events",
        "  /clear              - Clear output",
      };
      continue;
    }

    // Fetch from server
    outputLines.clear();
    outputLines.push_back("Querying server " + host + ":" + std::to_string(port) + " ...");
    refresh();

    std::string resp;
    if (verb == "hosts") {
      resp = queryServer(host, port, "hosts");
    } else if (verb == "history") {
      if (arg1.empty()) { outputLines = {"Usage: /history <host> [n]"}; continue; }
      resp = queryServer(host, port, "history " + arg1 + " " + std::to_string(nArg));
    } else if (verb == "log") {
      resp = queryServer(host, port, "log " + std::to_string(nArg));
    } else {
      outputLines = {"Unknown command: /" + verb, "Try /help"};
      continue;
    }

    outputLines.clear();
    if (resp.empty() || resp == "[]") { outputLines = {"(no data)"}; continue; }
    if (resp.front() == '{') { outputLines = {resp}; continue; } // error obj

    auto objs = splitJsonObjects(resp);
    if (objs.empty()) { outputLines = {"(empty response)"}; continue; }

    if (verb == "hosts") {
      char hdr[128];
      snprintf(hdr, sizeof(hdr), "%-16s %-8s %6s %6s %6s %6s  %-12s",
               "HOST","STATUS","CPU%","RAM%","DISK%","LOAD","LAST SEEN");
      outputLines.push_back(hdr);
      outputLines.push_back(std::string(cols-1, '-'));
      for (auto &obj : objs) {
        std::string st = jstr(obj, "status");
        int co = statusColor(st);
        std::string ts = fmtTs(jnum(obj, "lastSeen"));
        char row[256];
        snprintf(row, sizeof(row), "%-16s %-8s %5s%% %5s%% %5s%% %6s  %s",
                 jstr(obj,"host").substr(0,16).c_str(),
                 st.substr(0,8).c_str(),
                 jnum(obj,"cpu").c_str(), jnum(obj,"ram").c_str(),
                 jnum(obj,"disk").c_str(), jnum(obj,"load").c_str(),
                 ts.c_str());
        // Prefix with color code indicator (simplistic text marker)
        std::string marker = (co==3?"[!] ":co==4?"[~] ":co==6?"[?] ":"[.] ");
        outputLines.push_back(marker + row);
      }
    } else if (verb == "history") {
      char hdr[128];
      snprintf(hdr, sizeof(hdr), "Host: %s  (%d samples)", arg1.c_str(), (int)objs.size());
      outputLines.push_back(hdr);
      snprintf(hdr, sizeof(hdr), "%-10s %6s %6s %6s %7s  %8s %8s",
               "TIME","CPU%","RAM%","DISK%","LOAD","RX KB/s","TX KB/s");
      outputLines.push_back(hdr);
      outputLines.push_back(std::string(cols-1, '-'));
      // Most recent last
      for (int i = (int)objs.size()-1; i >= 0; i--) {
        auto &obj = objs[i];
        char row[256];
        snprintf(row, sizeof(row), "%-10s %5s%% %5s%% %5s%% %7s  %8s %8s",
                 fmtTs(jnum(obj,"ts")).c_str(),
                 jnum(obj,"cpu").c_str(), jnum(obj,"ram").c_str(),
                 jnum(obj,"disk").c_str(), jnum(obj,"load").c_str(),
                 jnum(obj,"rx").c_str(), jnum(obj,"tx").c_str());
        outputLines.push_back(row);
        if ((int)outputLines.size() > 500) break;
      }
    } else if (verb == "log") {
      char hdr[128];
      snprintf(hdr, sizeof(hdr), "Event Log (%d entries)", (int)objs.size());
      outputLines.push_back(hdr);
      outputLines.push_back(std::string(cols-1, '-'));
      for (int i = (int)objs.size()-1; i >= 0; i--) {
        auto &obj = objs[i];
        std::string type = jstr(obj,"type");
        char row[256];
        snprintf(row, sizeof(row), "%-10s %-12s %-12s %-10s  %s",
                 fmtTs(jnum(obj,"ts")).c_str(),
                 jstr(obj,"host").substr(0,12).c_str(),
                 jstr(obj,"ip").substr(0,12).c_str(),
                 type.c_str(),
                 jstr(obj,"detail").c_str());
        outputLines.push_back(row);
        if ((int)outputLines.size() > 500) break;
      }
    }
  }

  endwin();
  return 0;
}
