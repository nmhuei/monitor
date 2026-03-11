#pragma once
/*
 * logger.hpp — Thread-safe file logger with log levels and size-based rotation.
 *
 * Usage:
 *   Logger::init("data/monitor.log", LogLevel::INFO, 10*1024*1024);
 *   LOG_INFO("Server started on port " + std::to_string(port));
 *   LOG_WARN("Auth failed for " + ip);
 *   LOG_ERROR("Failed to bind socket");
 */
#include <ctime>
#include <fstream>
#include <mutex>
#include <string>
#include <filesystem>

namespace monitor {

enum class LogLevel { DEBUG = 0, INFO = 1, WARN = 2, ERROR = 3, NONE = 4 };

class Logger {
public:
  static Logger &instance() {
    static Logger inst;
    return inst;
  }

  void init(const std::string &path, LogLevel minLevel, size_t maxBytes = 10*1024*1024) {
    std::lock_guard<std::mutex> lk(mtx_);
    path_     = path;
    minLevel_ = minLevel;
    maxBytes_ = maxBytes;
    try {
      std::filesystem::path p(path);
      if (p.has_parent_path())
        std::filesystem::create_directories(p.parent_path());
    } catch (...) {}
    out_.open(path, std::ios::app);
  }

  void log(LogLevel lv, const std::string &msg) {
    if (lv < minLevel_ || path_.empty()) return;
    std::lock_guard<std::mutex> lk(mtx_);
    if (!out_.is_open()) return;

    // Rotate if too large
    checkRotate();

    // Timestamp
    char ts[32];
    time_t now = time(nullptr);
    std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", std::localtime(&now));

    const char *lname = lv==LogLevel::DEBUG?"DEBUG":
                        lv==LogLevel::INFO ?"INFO ":
                        lv==LogLevel::WARN ?"WARN ":"ERROR";
    out_ << '[' << ts << "] [" << lname << "] " << msg << '\n';
    out_.flush();
  }

private:
  Logger() = default;

  void checkRotate() {
    // Check size periodically (every 256 writes to avoid stat overhead)
    if ((++writeCount_ & 0xFF) != 0) return;
    try {
      if (std::filesystem::file_size(path_) >= maxBytes_) {
        out_.close();
        std::string rotated = path_ + ".1";
        // Remove old .1 if exists
        std::filesystem::remove(rotated);
        std::filesystem::rename(path_, rotated);
        out_.open(path_, std::ios::app);
      }
    } catch (...) {}
  }

  std::mutex    mtx_;
  std::ofstream out_;
  std::string   path_;
  LogLevel      minLevel_ = LogLevel::INFO;
  size_t        maxBytes_ = 10 * 1024 * 1024;
  uint64_t      writeCount_ = 0;
};

// Convenience macros
#define LOG_DEBUG(msg) ::monitor::Logger::instance().log(::monitor::LogLevel::DEBUG, (msg))
#define LOG_INFO(msg)  ::monitor::Logger::instance().log(::monitor::LogLevel::INFO,  (msg))
#define LOG_WARN(msg)  ::monitor::Logger::instance().log(::monitor::LogLevel::WARN,  (msg))
#define LOG_ERROR(msg) ::monitor::Logger::instance().log(::monitor::LogLevel::ERROR, (msg))

} // namespace monitor
