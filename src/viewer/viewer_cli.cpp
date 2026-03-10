#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <deque>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <ncursesw/curses.h>

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
  if (fd < 0) {
    freeaddrinfo(res);
    return -1;
  }
  if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
    close(fd);
    freeaddrinfo(res);
    return -1;
  }
  freeaddrinfo(res);
  return fd;
}


static std::string sanitizeText(const std::string &in) {
  std::string out;
  out.reserve(in.size());
  for (unsigned char c : in) {
    if (c == '\n' || c == '\t' || (c >= 32 && c <= 126))
      out.push_back((char)c);
    else
      out.push_back(' ');
  }
  return out;
}

static std::string stripAnsi(const std::string &in) {
  std::string out;
  out.reserve(in.size());
  bool esc = false;
  for (size_t i = 0; i < in.size(); ++i) {
    unsigned char c = (unsigned char)in[i];
    if (!esc) {
      if (c == 0x1B) {
        esc = true;
      } else {
        out.push_back((char)c);
      }
    } else {
      // CSI sequence typically ESC [ ... letter
      if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
        esc = false;
    }
  }
  return out;
}

static std::vector<std::string> splitLines(const std::string &s) {
  std::vector<std::string> out;
  std::stringstream ss(s);
  std::string line;
  while (std::getline(ss, line))
    out.push_back(line);
  return out;
}

static std::string nowStr(time_t t) {
  char b[32];
  auto *tm_ = localtime(&t);
  strftime(b, sizeof(b), "%H:%M:%S", tm_);
  return b;
}

int main(int argc, char **argv) {
  std::string host = "127.0.0.1";
  uint16_t port = 8785;

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "-server" && i + 1 < argc) {
      std::string sv = argv[++i];
      auto p = sv.rfind(':');
      if (p == std::string::npos)
        host = sv;
      else {
        host = sv.substr(0, p);
        port = (uint16_t)std::stoi(sv.substr(p + 1));
      }
    }
  }

  int fd = connectTo(host, port);
  if (fd < 0) {
    std::cerr << "viewer_cli: cannot connect to " << host << ":" << port << "\n";
    return 1;
  }

  initscr();
  noecho();
  cbreak();
  keypad(stdscr, TRUE);
  nodelay(stdscr, TRUE);
  curs_set(0);

  std::string sockBuf;
  std::string lastFrame;
  std::deque<std::pair<time_t, std::string>> frameHist;
  std::vector<std::string> commandOutput;

  bool cmdMode = false;
  std::string cmd;

  auto addFrame = [&](const std::string &frame) {
    if (frame.empty())
      return;
    std::string clean = sanitizeText(stripAnsi(frame));
    lastFrame = clean;
    frameHist.push_back({time(nullptr), clean});
    while (frameHist.size() > 120)
      frameHist.pop_front();
  };

  while (true) {
    char b[4096];
    int n = recv(fd, b, sizeof(b), MSG_DONTWAIT);
    if (n > 0) {
      sockBuf.append(b, n);
      size_t pos;
      const std::string marker = "\033[2J\033[H";
      while ((pos = sockBuf.find(marker, marker.size())) != std::string::npos) {
        std::string frame = sockBuf.substr(0, pos);
        addFrame(frame);
        sockBuf.erase(0, pos);
      }
    } else if (n == 0) {
      commandOutput = {"Disconnected from server."};
    }

    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    erase();

    // Idle screen: show available commands. Results area appears after command runs.
    if (commandOutput.empty()) {
      int y = 1;
      mvaddstr(y++, 0, "viewer_cli ready. Available commands:");
      mvaddstr(y++, 0, "  /history <host> <n>   - show last n matched lines for host");
      mvaddstr(y++, 0, "  /warning <host> <n>   - show last n ALERT/WARN matches");
      mvaddstr(y++, 0, "  /clear                - clear output");
      mvaddstr(y++, 0, "  /help                 - show this command list");
      y++;
      mvaddstr(y++, 0, "Tips: press '/' to type command, Enter to run, Esc to cancel, q to quit.");
    } else {
      int oy = 1;
      mvaddstr(oy, 0, "Command output:");
      int idx = 1;
      for (auto &l : commandOutput) {
        if (oy + idx >= rows - 1)
          break;
        mvaddnstr(oy + idx, 0, l.c_str(), cols - 1);
        idx++;
      }
    }

    std::string prompt = cmdMode ? ("/" + cmd) : "Press / to enter command, /help for list, q to quit";
    mvhline(rows - 1, 0, ' ', cols);
    mvaddnstr(rows - 1, 0, prompt.c_str(), cols - 1);

    refresh();

    int ch = getch();
    if (ch == 'q' || ch == 'Q')
      break;

    if (!cmdMode) {
      if (ch == '/') {
        cmdMode = true;
        cmd.clear();
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(120));
      continue;
    }

    if (ch == 27) {
      cmdMode = false;
      cmd.clear();
      continue;
    }

    if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
      if (!cmd.empty())
        cmd.pop_back();
      continue;
    }

    if (ch == '\n' || ch == KEY_ENTER) {
      std::string c = trim(cmd);
      cmdMode = false;
      cmd.clear();
      commandOutput.clear();

      if (c.empty())
        continue;

      std::stringstream ss(c);
      std::string verb, hostArg;
      int nArg = 10;
      ss >> verb >> hostArg >> nArg;

      if (verb == "history") {
        if (hostArg.empty()) {
          commandOutput.push_back("Usage: /history <host> <n>");
          continue;
        }
        int cnt = 0;
        for (auto it = frameHist.rbegin(); it != frameHist.rend() && cnt < nArg; ++it) {
          auto lines2 = splitLines(it->second);
          for (auto &ln : lines2) {
            if (ln.find(hostArg) != std::string::npos) {
              commandOutput.push_back("[" + nowStr(it->first) + "] " + ln);
              cnt++;
              if (cnt >= nArg)
                break;
            }
          }
        }
        if (commandOutput.empty())
          commandOutput.push_back("No history match for host=" + hostArg);
      } else if (verb == "warning") {
        if (hostArg.empty()) {
          commandOutput.push_back("Usage: /warning <host> <n>");
          continue;
        }
        int cnt = 0;
        for (auto it = frameHist.rbegin(); it != frameHist.rend() && cnt < nArg; ++it) {
          auto lines2 = splitLines(it->second);
          for (auto &ln : lines2) {
            bool hit = (ln.find(hostArg) != std::string::npos) &&
                       (ln.find("ALERT") != std::string::npos || ln.find("WARN") != std::string::npos);
            if (hit) {
              commandOutput.push_back("[" + nowStr(it->first) + "] " + ln);
              cnt++;
              if (cnt >= nArg)
                break;
            }
          }
        }
        if (commandOutput.empty())
          commandOutput.push_back("No warning match for host=" + hostArg);
      } else if (verb == "clear") {
        commandOutput.clear();
      } else if (verb == "help") {
        commandOutput = {
            "Available commands:",
            "  /history <host> <n>  - last n lines containing host",
            "  /warning <host> <n>  - last n ALERT/WARN lines containing host",
            "  /clear               - clear command output",
        };
      } else {
        commandOutput.push_back("Unknown command. Use: /history <host> <n> | /warning <host> <n> | /clear");
      }

      continue;
    }

    if (ch >= 32 && ch < 127)
      cmd.push_back((char)ch);
  }

  endwin();
  close(fd);
  return 0;
}
