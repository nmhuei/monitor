#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <thread>
#include <atomic>
#include <cstring>
#include <algorithm>

// Define rename macro for json_helper.hpp to avoid conflict with json_wrapper.hpp
#define json helper_json
#include "json_helper.hpp"
#undef json

#include "json_wrapper.hpp"
#include "thresholds.hpp"
#include "dashboard.hpp"
#include "metrics_store.hpp"
#include "server_stats.hpp"

#define ASSERT_TRUE(x) do { \
  if (!(x)) { \
    std::cerr << "FAIL: " << #x << " at line " << __LINE__ << std::endl; \
    std::exit(1); \
  } \
} while(0)

#define ASSERT_FALSE(x) do { \
  if (x) { \
    std::cerr << "FAIL: !" << #x << " at line " << __LINE__ << std::endl; \
    std::exit(1); \
  } \
} while(0)

#define ASSERT_EQ(x, y) do { \
  if ((x) != (y)) { \
    std::cerr << "FAIL: " << #x << " == " << #y << " at line " << __LINE__ << std::endl; \
    std::exit(1); \
  } \
} while(0)

#define ASSERT_NEAR(x, y, tol) do { \
  auto valx = (double)(x); \
  auto valy = (double)(y); \
  if (std::abs(valx - valy) > (double)(tol)) { \
    std::cerr << "FAIL: " << #x << " near " << #y << " (" << valx << " vs " << valy << ") at line " << __LINE__ << std::endl; \
    std::exit(1); \
  } \
} while(0)

// Helper to check if string contains substring
bool str_contains(const std::string &haystack, const std::string &needle) {
  return haystack.find(needle) != std::string::npos;
}

// 1. test_protocol
void test_protocol() {
  std::cout << "1. Running test_protocol..." << std::endl;
  ASSERT_EQ(std::string(monitor::statusStr(monitor::HostStatus::ONLINE)), "OK");
  ASSERT_EQ(std::string(monitor::statusStr(monitor::HostStatus::WARNING)), "WARN");
  ASSERT_EQ(std::string(monitor::statusStr(monitor::HostStatus::ALERT)), "ALERT");
  ASSERT_EQ(std::string(monitor::statusStr(monitor::HostStatus::STALE)), "STALE");
  ASSERT_EQ(std::string(monitor::statusStr(monitor::HostStatus::OFFLINE)), "OFFLINE");
  ASSERT_EQ(monitor::DEFAULT_PORT, 8784);
  ASSERT_EQ(monitor::DEFAULT_VPORT, 8785);
  ASSERT_EQ(monitor::MAX_HISTORY, 60);
  ASSERT_EQ(monitor::MAX_LOG_ENTRIES, 500);
  ASSERT_EQ(monitor::DEFAULT_STALE_SEC, 30);
  ASSERT_EQ(monitor::DEFAULT_OFFLINE_SEC, 90);
}

// 2. test_json_helper_escape
void test_json_helper_escape() {
  std::cout << "2. Running test_json_helper_escape..." << std::endl;
  ASSERT_EQ(monitor::helper_json::escapeStr("hello \"world\""), "hello \\\"world\\\"");
  ASSERT_EQ(monitor::helper_json::escapeStr("a\\b"), "a\\\\b");
  ASSERT_EQ(monitor::helper_json::escapeStr("a\nb"), "a\\nb");
  ASSERT_EQ(monitor::helper_json::escapeStr("a\tb"), "a\\tb");
  
  std::string ctrl;
  ctrl.push_back(0x01);
  ASSERT_EQ(monitor::helper_json::escapeStr(ctrl), "\\u0001");
  ASSERT_EQ(monitor::helper_json::escapeStr("hello"), "hello");
}

// 3. test_json_helper_encode
void test_json_helper_encode() {
  std::cout << "3. Running test_json_helper_encode..." << std::endl;
  std::vector<float> cores = {1.5f, 2.5f};
  
  // All fields
  std::string encoded = monitor::helper_json::encode("host1", 10.0f, 20.0f, 30.0f, 123456, cores, 1.0f, 2.0f, 0.5f, 10, "secret123");
  ASSERT_TRUE(str_contains(encoded, "\"auth\":\"secret123\""));
  ASSERT_TRUE(str_contains(encoded, "\"host\":\"host1\""));
  ASSERT_TRUE(str_contains(encoded, "\"cpu\":10"));
  ASSERT_TRUE(str_contains(encoded, "\"cores\":[1.5"));
  
  // Empty cores
  std::string encoded_no_cores = monitor::helper_json::encode("host1", 10.0f, 20.0f, 30.0f, 123456, {}, 1.0f, 2.0f, 0.5f, 10, "secret123");
  ASSERT_FALSE(str_contains(encoded_no_cores, "\"cores\""));
  
  // No auth token
  std::string encoded_no_auth = monitor::helper_json::encode("host1", 10.0f, 20.0f, 30.0f, 123456, cores, 1.0f, 2.0f, 0.5f, 10, "");
  ASSERT_FALSE(str_contains(encoded_no_auth, "\"auth\""));
}

// 4. test_json_helper_decode
void test_json_helper_decode() {
  std::cout << "4. Running test_json_helper_decode..." << std::endl;
  std::string jsonStr = "{\"host\":\"host1\",\"cpu\":12.3,\"cores\":[1.0,2.0,3.0],\"detail\":\"a\\\\b\\\"c\"}";
  auto obj = monitor::helper_json::decode(jsonStr);
  
  ASSERT_EQ(obj["host"].str, "host1");
  ASSERT_TRUE(obj["host"].is_str);
  ASSERT_NEAR(obj["cpu"].num, 12.3, 0.01);
  ASSERT_TRUE(obj["cores"].is_arr);
  ASSERT_EQ(obj["cores"].arr.size(), 3UL);
  ASSERT_NEAR(obj["cores"].arr[0], 1.0, 0.01);
  ASSERT_NEAR(obj["cores"].arr[1], 2.0, 0.01);
  ASSERT_NEAR(obj["cores"].arr[2], 3.0, 0.01);
  ASSERT_EQ(obj["detail"].str, "a\\b\"c");
  
  // Decode empty
  auto empty_obj = monitor::helper_json::decode("{}");
  ASSERT_TRUE(empty_obj.empty());
  
  // Decode with whitespace
  auto obj_ws = monitor::helper_json::decode("  {  \"host\"  :  \"host1\"  }  ");
  ASSERT_EQ(obj_ws["host"].str, "host1");
  
  // Invalid JSON throws runtime_error
  bool threw = false;
  try {
    monitor::helper_json::decode("invalid_json");
  } catch (const std::runtime_error &e) {
    threw = true;
  }
  ASSERT_TRUE(threw);
}

// 5. test_json_helper_roundtrip
void test_json_helper_roundtrip() {
  std::cout << "5. Running test_json_helper_roundtrip..." << std::endl;
  std::vector<float> cores = {1.2f, 3.4f, 5.6f};
  std::string enc = monitor::helper_json::encode("host-rt", 50.0f, 60.0f, 70.0f, 999999, cores, 12.0f, 34.0f, 1.5f, 200, "token-rt");
  auto dec = monitor::helper_json::decode(enc);
  
  ASSERT_EQ(dec["auth"].str, "token-rt");
  ASSERT_EQ(dec["host"].str, "host-rt");
  ASSERT_NEAR(dec["cpu"].num, 50.0, 0.01);
  ASSERT_NEAR(dec["ram"].num, 60.0, 0.01);
  ASSERT_NEAR(dec["disk"].num, 70.0, 0.01);
  ASSERT_NEAR(dec["timestamp"].num, 999999.0, 0.01);
  ASSERT_TRUE(dec["cores"].is_arr);
  ASSERT_EQ(dec["cores"].arr.size(), 3UL);
  ASSERT_NEAR(dec["cores"].arr[0], 1.2, 0.01);
  ASSERT_NEAR(dec["cores"].arr[1], 3.4, 0.01);
  ASSERT_NEAR(dec["cores"].arr[2], 5.6, 0.01);
}

// 6. test_thresholds_defaults
void test_thresholds_defaults() {
  std::cout << "6. Running test_thresholds_defaults..." << std::endl;
  monitor::Thresholds t;
  ASSERT_NEAR(t.cpu, 80.0f, 0.01);
  ASSERT_NEAR(t.ram, 90.0f, 0.01);
  ASSERT_NEAR(t.disk, 85.0f, 0.01);
  
  ASSERT_NEAR(t.getCPU("unknown-host"), 80.0f, 0.01);
  ASSERT_NEAR(t.getRAM("unknown-host"), 90.0f, 0.01);
  ASSERT_NEAR(t.getDisk("unknown-host"), 85.0f, 0.01);
}

// 7. test_thresholds_overrides
void test_thresholds_overrides() {
  std::cout << "7. Running test_thresholds_overrides..." << std::endl;
  std::string path = "test_overrides.conf";
  std::ofstream out(path);
  out << "CPU=70\n";
  out << "RAM=75\n";
  out << "DISK=80\n";
  out << "web-1.cpu=85\n";
  out << "db-1.ram=95\n";
  out.close();
  
  monitor::Thresholds t = monitor::loadThresholds(path);
  std::filesystem::remove(path);
  
  ASSERT_NEAR(t.getCPU("other"), 70.0f, 0.01);
  ASSERT_NEAR(t.getRAM("other"), 75.0f, 0.01);
  ASSERT_NEAR(t.getDisk("other"), 80.0f, 0.01);
  
  ASSERT_NEAR(t.getCPU("web-1"), 85.0f, 0.01);
  ASSERT_NEAR(t.getCPU("WEB-1"), 85.0f, 0.01); // Case insensitivity
  ASSERT_NEAR(t.getRAM("db-1"), 95.0f, 0.01);
  ASSERT_NEAR(t.getRAM("DB-1"), 95.0f, 0.01);
}

// 8. test_thresholds_comments_and_whitespace
void test_thresholds_comments_and_whitespace() {
  std::cout << "8. Running test_thresholds_comments_and_whitespace..." << std::endl;
  std::string path = "test_comments.conf";
  std::ofstream out(path);
  out << "  # This is a comment\n";
  out << "CPU  =  65  # inline comment\n";
  out << "\n";
  out << "  web-1.cpu  =  92  \n";
  out.close();
  
  monitor::Thresholds t = monitor::loadThresholds(path);
  std::filesystem::remove(path);
  
  ASSERT_NEAR(t.cpu, 65.0f, 0.01);
  ASSERT_NEAR(t.getCPU("web-1"), 92.0f, 0.01);
}

// 9. test_thresholds_missing_file
void test_thresholds_missing_file() {
  std::cout << "9. Running test_thresholds_missing_file..." << std::endl;
  monitor::Thresholds t = monitor::loadThresholds("nonexistent_file_xyz.conf");
  ASSERT_NEAR(t.cpu, 80.0f, 0.01);
  ASSERT_NEAR(t.ram, 90.0f, 0.01);
  ASSERT_NEAR(t.disk, 85.0f, 0.01);
}

// 10. test_ui_statusGlyph
void test_ui_statusGlyph() {
  std::cout << "10. Running test_ui_statusGlyph..." << std::endl;
  ASSERT_EQ(std::string(monitor::ui::statusGlyph(monitor::HostStatus::ONLINE)), "●");
  ASSERT_EQ(std::string(monitor::ui::statusGlyph(monitor::HostStatus::WARNING)), "◐");
  ASSERT_EQ(std::string(monitor::ui::statusGlyph(monitor::HostStatus::ALERT)), "⬤");
  ASSERT_EQ(std::string(monitor::ui::statusGlyph(monitor::HostStatus::STALE)), "◌");
  ASSERT_EQ(std::string(monitor::ui::statusGlyph(monitor::HostStatus::OFFLINE)), "○");
}

// 11. test_ui_statusColorPair
void test_ui_statusColorPair() {
  std::cout << "11. Running test_ui_statusColorPair..." << std::endl;
  ASSERT_EQ(monitor::ui::statusColorPair(monitor::HostStatus::ONLINE), monitor::ui::CP_OK);
  ASSERT_EQ(monitor::ui::statusColorPair(monitor::HostStatus::WARNING), monitor::ui::CP_WARN);
  ASSERT_EQ(monitor::ui::statusColorPair(monitor::HostStatus::ALERT), monitor::ui::CP_ALERT);
  ASSERT_EQ(monitor::ui::statusColorPair(monitor::HostStatus::STALE), monitor::ui::CP_STALE);
  ASSERT_EQ(monitor::ui::statusColorPair(monitor::HostStatus::OFFLINE), monitor::ui::CP_OFFLINE);
}

// 12. test_ui_formatAge
void test_ui_formatAge() {
  std::cout << "12. Running test_ui_formatAge..." << std::endl;
  time_t now = time(nullptr);
  ASSERT_EQ(monitor::ui::formatAge(0, monitor::HostStatus::ONLINE), "never");
  ASSERT_EQ(monitor::ui::formatAge(now - 10, monitor::HostStatus::OFFLINE), "offline");
  ASSERT_EQ(monitor::ui::formatAge(now - 5, monitor::HostStatus::STALE), "stale 5s");
  ASSERT_EQ(monitor::ui::formatAge(now, monitor::HostStatus::ONLINE), "0s");
  ASSERT_EQ(monitor::ui::formatAge(now - 45, monitor::HostStatus::ONLINE), "45s");
  ASSERT_EQ(monitor::ui::formatAge(now - 120, monitor::HostStatus::ONLINE), "2m");
  ASSERT_EQ(monitor::ui::formatAge(now - 7200, monitor::HostStatus::ONLINE), "2h");
}

// 13. test_ui_getBlockGlyph
void test_ui_getBlockGlyph() {
  std::cout << "13. Running test_ui_getBlockGlyph..." << std::endl;
  ASSERT_EQ(std::string(monitor::ui::getBlockGlyph(0.0f)), " ");
  ASSERT_EQ(std::string(monitor::ui::getBlockGlyph(12.5f)), "▁");
  ASSERT_EQ(std::string(monitor::ui::getBlockGlyph(25.0f)), "▂");
  ASSERT_EQ(std::string(monitor::ui::getBlockGlyph(37.5f)), "▃");
  ASSERT_EQ(std::string(monitor::ui::getBlockGlyph(50.0f)), "▄");
  ASSERT_EQ(std::string(monitor::ui::getBlockGlyph(62.5f)), "▅");
  ASSERT_EQ(std::string(monitor::ui::getBlockGlyph(75.0f)), "▆");
  ASSERT_EQ(std::string(monitor::ui::getBlockGlyph(87.5f)), "▇");
  ASSERT_EQ(std::string(monitor::ui::getBlockGlyph(100.0f)), "█");
  ASSERT_EQ(std::string(monitor::ui::getBlockGlyph(-5.0f)), " ");
  ASSERT_EQ(std::string(monitor::ui::getBlockGlyph(105.0f)), "█");
}

// 14. test_ui_drawBar
void test_ui_drawBar() {
  std::cout << "14. Running test_ui_drawBar..." << std::endl;
  ASSERT_EQ(monitor::ui::drawBar(0.0f, 5), "░░░░░");
  ASSERT_EQ(monitor::ui::drawBar(100.0f, 5), "█████");
  ASSERT_EQ(monitor::ui::drawBar(50.0f, 4), "██░░");
  ASSERT_EQ(monitor::ui::drawBar(0.0f, 0), "");
}

// 15. test_ui_formatNet
void test_ui_formatNet() {
  std::cout << "15. Running test_ui_formatNet..." << std::endl;
  ASSERT_EQ(monitor::ui::formatNet(0.0f), "0B");
  ASSERT_EQ(monitor::ui::formatNet(0.05f), "0B");
  ASSERT_EQ(monitor::ui::formatNet(0.1f), "0K");
  ASSERT_EQ(monitor::ui::formatNet(500.0f), "500K");
  ASSERT_EQ(monitor::ui::formatNet(1024.0f), "1.0M");
  ASSERT_EQ(monitor::ui::formatNet(2560.0f), "2.5M");
}

// 16. test_ui_formatUptime
void test_ui_formatUptime() {
  std::cout << "16. Running test_ui_formatUptime..." << std::endl;
  ASSERT_EQ(monitor::ui::formatUptime(0), "0s");
  ASSERT_EQ(monitor::ui::formatUptime(59), "59s");
  ASSERT_EQ(monitor::ui::formatUptime(60), "1m 0s");
  ASSERT_EQ(monitor::ui::formatUptime(90), "1m 30s");
  ASSERT_EQ(monitor::ui::formatUptime(3600), "1h 0m");
  ASSERT_EQ(monitor::ui::formatUptime(3665), "1h 1m");
  ASSERT_EQ(monitor::ui::formatUptime(7261), "2h 1m");
}

// 17. test_ui_makeBrailleChar
void test_ui_makeBrailleChar() {
  std::cout << "17. Running test_ui_makeBrailleChar..." << std::endl;
  std::string br_empty = monitor::ui::makeBrailleChar(0, 0);
  ASSERT_EQ(br_empty.size(), 3UL);
  ASSERT_EQ((unsigned char)br_empty[0], 0xE2);
  ASSERT_EQ((unsigned char)br_empty[1], 0xA0);
  ASSERT_EQ((unsigned char)br_empty[2], 0x80);
  
  std::string br_full = monitor::ui::makeBrailleChar(4, 4);
  ASSERT_EQ(br_full.size(), 3UL);
  
  std::string br_clamp = monitor::ui::makeBrailleChar(-1, 5);
  std::string br_clamp_ref = monitor::ui::makeBrailleChar(0, 4);
  ASSERT_EQ(br_clamp, br_clamp_ref);
}

// 18. test_ui_padRight_padLeft
void test_ui_padRight_padLeft() {
  std::cout << "18. Running test_ui_padRight_padLeft..." << std::endl;
  ASSERT_EQ(monitor::ui::padRight("abc", 5), "abc  ");
  ASSERT_EQ(monitor::ui::padRight("abcdef", 3), "abc");
  ASSERT_EQ(monitor::ui::padRight("", 3), "   ");
  ASSERT_EQ(monitor::ui::padLeft("42", 5), "   42");
  ASSERT_EQ(monitor::ui::padLeft("abcdef", 3), "abc");
}

// 19. test_metrics_store_pipe_encoding
void test_metrics_store_pipe_encoding() {
  std::cout << "19. Running test_metrics_store_pipe_encoding..." << std::endl;
  std::string orig = "a|b\\c";
  std::string enc = monitor::pipeEncode(orig);
  ASSERT_EQ(enc, "a\\Pb\\\\c");
  std::string dec = monitor::pipeDecode(enc);
  ASSERT_EQ(dec, orig);
  
  ASSERT_EQ(monitor::pipeEncode(""), "");
  ASSERT_EQ(monitor::pipeDecode(""), "");
  
  std::string clean = "clean_string";
  ASSERT_EQ(monitor::pipeDecode(monitor::pipeEncode(clean)), clean);
}

// 20. test_metrics_store_jsonStr
void test_metrics_store_jsonStr() {
  std::cout << "20. Running test_metrics_store_jsonStr..." << std::endl;
  ASSERT_EQ(monitor::jsonStr("hello"), "\"hello\"");
  ASSERT_EQ(monitor::jsonStr("hello \"world\" \n\t\\"), "\"hello \\\"world\\\" \\n\\t\\\\\"");
  ASSERT_EQ(monitor::jsonStr(""), "\"\"");
}

// 21. test_host_state_push
void test_host_state_push() {
  std::cout << "21. Running test_host_state_push..." << std::endl;
  monitor::HostState state;
  state.name = "host-push";
  
  monitor::MetricPayload p;
  p.host = "host-push";
  p.cpu = 10.0f;
  p.ram = 20.0f;
  p.disk = 30.0f;
  p.timestamp = time(nullptr);
  p.cores = {5.0f, 15.0f};
  p.netRxKB = 1.0f;
  p.netTxKB = 2.0f;
  p.loadAvg = 0.5f;
  p.procCount = 100;
  
  state.push(p);
  ASSERT_EQ(state.cpu, 10.0f);
  ASSERT_EQ(state.ram, 20.0f);
  ASSERT_EQ(state.disk, 30.0f);
  ASSERT_EQ(state.coreCount, 2);
  ASSERT_EQ(state.cores.size(), 2UL);
  ASSERT_EQ(state.history.size(), 1UL);
  
  for (int i = 0; i < monitor::MAX_HISTORY + 5; i++) {
    p.cpu = (float)i;
    state.push(p);
  }
  ASSERT_EQ(state.history.size(), (size_t)monitor::MAX_HISTORY);
  ASSERT_EQ(state.history.front().cpu, (float)(monitor::MAX_HISTORY + 5 - monitor::MAX_HISTORY));
}

// 22. test_metrics_store_upsert
void test_metrics_store_upsert() {
  std::cout << "22. Running test_metrics_store_upsert..." << std::endl;
  monitor::MetricsStore store;
  monitor::Thresholds thresh;
  
  monitor::MetricPayload p;
  p.host = "host-upsert";
  p.ip = "127.0.0.1";
  p.timestamp = time(nullptr);
  
  p.cpu = 50.0f;
  p.ram = 50.0f;
  p.disk = 50.0f;
  auto res = store.upsert(p, thresh);
  ASSERT_EQ(res.first, monitor::HostStatus::OFFLINE);
  ASSERT_EQ(res.second, monitor::HostStatus::ONLINE);
  
  p.cpu = 70.0f;
  res = store.upsert(p, thresh);
  ASSERT_EQ(res.first, monitor::HostStatus::ONLINE);
  ASSERT_EQ(res.second, monitor::HostStatus::WARNING);
  
  p.cpu = 85.0f;
  res = store.upsert(p, thresh);
  ASSERT_EQ(res.first, monitor::HostStatus::WARNING);
  ASSERT_EQ(res.second, monitor::HostStatus::ALERT);
  
  auto logs = store.logSnapshot();
  ASSERT_FALSE(logs.empty());
  bool found_alert = false;
  for (const auto &log : logs) {
    if (log.type == monitor::LogEventType::ALERT && log.host == "host-upsert") {
      found_alert = true;
    }
  }
  ASSERT_TRUE(found_alert);
}

// 23. test_metrics_store_set_online_offline
void test_metrics_store_set_online_offline() {
  std::cout << "23. Running test_metrics_store_set_online_offline..." << std::endl;
  monitor::MetricsStore store;
  
  store.setOnline("host-oo", "10.0.0.1", 42);
  auto snapshot = store.snapshot();
  bool found = false;
  for (const auto &h : snapshot) {
    if (h.name == "host-oo") {
      ASSERT_EQ(h.ip, "10.0.0.1");
      ASSERT_EQ(h.fd, 42);
      ASSERT_EQ(h.status, monitor::HostStatus::ONLINE);
      found = true;
    }
  }
  ASSERT_TRUE(found);
  
  auto logs = store.logSnapshot();
  bool found_connect = false;
  for (const auto &log : logs) {
    if (log.type == monitor::LogEventType::CONNECT && log.host == "host-oo") {
      found_connect = true;
    }
  }
  ASSERT_TRUE(found_connect);
  
  store.setOffline("host-oo");
  snapshot = store.snapshot();
  found = false;
  for (const auto &h : snapshot) {
    if (h.name == "host-oo") {
      ASSERT_EQ(h.status, monitor::HostStatus::OFFLINE);
      ASSERT_EQ(h.fd, -1);
      found = true;
    }
  }
  ASSERT_TRUE(found);
  
  store.setOffline("nonexistent-oo");
}

// 24. test_metrics_store_mark_stale_offline
void test_metrics_store_mark_stale_offline() {
  std::cout << "24. Running test_metrics_store_mark_stale_offline..." << std::endl;
  monitor::MetricsStore store;
  monitor::Thresholds thresh;
  time_t now = time(nullptr);
  
  store.setOnline("host-fresh", "10.0.0.1", 1);
  monitor::MetricPayload p;
  p.host = "host-fresh";
  p.ip = "10.0.0.1";
  p.timestamp = now;
  store.upsert(p, thresh);
  
  store.setOnline("host-stale", "10.0.0.2", 2);
  p.host = "host-stale";
  p.ip = "10.0.0.2";
  p.timestamp = now - 5;
  store.upsert(p, thresh);
  
  store.setOnline("host-offline", "10.0.0.3", 3);
  p.host = "host-offline";
  p.ip = "10.0.0.3";
  p.timestamp = now - 15;
  store.upsert(p, thresh);
  
  auto counts = store.markStaleOffline(3, 10);
  ASSERT_EQ(counts.first, 1);
  ASSERT_EQ(counts.second, 1);
  
  auto snap = store.snapshot();
  for (const auto &h : snap) {
    if (h.name == "host-fresh") ASSERT_EQ(h.status, monitor::HostStatus::ONLINE);
    if (h.name == "host-stale") ASSERT_EQ(h.status, monitor::HostStatus::STALE);
    if (h.name == "host-offline") ASSERT_EQ(h.status, monitor::HostStatus::OFFLINE);
  }
}

// 25. test_metrics_store_snapshot
void test_metrics_store_snapshot() {
  std::cout << "25. Running test_metrics_store_snapshot..." << std::endl;
  monitor::MetricsStore store;
  store.setOnline("host1", "10.0.0.1", 1);
  store.setOnline("host2", "10.0.0.2", 2);
  store.setOnline("host3", "10.0.0.3", 3);
  
  auto snap = store.snapshot();
  ASSERT_EQ(snap.size(), 3UL);
  
  snap[0].name = "modified";
  auto snap2 = store.snapshot();
  for (const auto &h : snap2) {
    ASSERT_FALSE(h.name == "modified");
  }
}

// 26. test_metrics_store_log_snapshot
void test_metrics_store_log_snapshot() {
  std::cout << "26. Running test_metrics_store_log_snapshot..." << std::endl;
  monitor::MetricsStore store;
  store.setOnline("host-log", "10.0.0.1", 1);
  
  auto logs = store.logSnapshot();
  ASSERT_FALSE(logs.empty());
  ASSERT_EQ(logs.front().type, monitor::LogEventType::CONNECT);
  
  for (int i = 0; i < monitor::MAX_LOG_ENTRIES + 10; i++) {
    store.setOnline("host-log", "10.0.0.1", 1);
  }
  logs = store.logSnapshot();
  ASSERT_EQ(logs.size(), (size_t)monitor::MAX_LOG_ENTRIES);
}

// 27. test_metrics_store_hosts_json
void test_metrics_store_hosts_json() {
  std::cout << "27. Running test_metrics_store_hosts_json..." << std::endl;
  monitor::MetricsStore store;
  store.setOnline("web-1", "10.0.0.1", 1);
  store.setOnline("db-1", "10.0.0.2", 2);
  
  std::string json = store.hostsJson();
  ASSERT_TRUE(str_contains(json, "\"host\":\"web-1\""));
  ASSERT_TRUE(str_contains(json, "\"host\":\"db-1\""));
}

// 28. test_metrics_store_history_json
void test_metrics_store_history_json() {
  std::cout << "28. Running test_metrics_store_history_json..." << std::endl;
  monitor::MetricsStore store;
  monitor::Thresholds thresh;
  store.setOnline("host-hist", "10.0.0.1", 1);
  
  monitor::MetricPayload p;
  p.host = "host-hist";
  for (int i = 0; i < 5; i++) {
    p.cpu = (float)(i * 10);
    p.timestamp = time(nullptr) - (5 - i);
    store.upsert(p, thresh);
  }
  
  std::string hist3 = store.historyJson("host-hist", 3);
  {
    auto j = nlohmann::json::parse(hist3);
    ASSERT_TRUE(j.is_array());
    ASSERT_EQ(j.size(), 3UL);
  }
  
  ASSERT_EQ(store.historyJson("nonexistent", 5), "[]");
  
  std::string hist100 = store.historyJson("host-hist", 100);
  {
    auto j = nlohmann::json::parse(hist100);
    ASSERT_TRUE(j.is_array());
    ASSERT_EQ(j.size(), 5UL);
  }
}

// 29. test_metrics_store_log_json
void test_metrics_store_log_json() {
  std::cout << "29. Running test_metrics_store_log_json..." << std::endl;
  monitor::MetricsStore store;
  store.setOnline("host1", "10.0.0.1", 1);
  store.setOnline("host2", "10.0.0.2", 2);
  
  std::string log2 = store.logJson(2);
  ASSERT_TRUE(str_contains(log2, "host1"));
  ASSERT_TRUE(str_contains(log2, "host2"));
  
  ASSERT_EQ(store.logJson(0), "[]");
}

// 30. test_metrics_store_save_load
void test_metrics_store_save_load() {
  std::cout << "30. Running test_metrics_store_save_load..." << std::endl;
  monitor::MetricsStore store;
  monitor::Thresholds thresh;
  store.setOnline("host-sl", "10.0.0.1", 1);
  monitor::MetricPayload p;
  p.host = "host-sl";
  p.cpu = 42.0f;
  p.timestamp = time(nullptr);
  store.upsert(p, thresh);
  
  std::string db_file = "test_state.db";
  std::filesystem::remove(db_file);
  
  bool saved = store.saveToFile(db_file);
  ASSERT_TRUE(saved);
  ASSERT_TRUE(std::filesystem::exists(db_file));
  
  monitor::MetricsStore store2;
  bool loaded = store2.loadFromFile(db_file);
  ASSERT_TRUE(loaded);
  
  auto snap = store2.snapshot();
  bool found = false;
  for (const auto &h : snap) {
    if (h.name == "host-sl") {
      ASSERT_NEAR(h.cpu, 42.0f, 0.01);
      found = true;
    }
  }
  ASSERT_TRUE(found);
  
  std::filesystem::remove(db_file);
}

// 31. test_metrics_store_legacy_migration
void test_metrics_store_legacy_migration() {
  std::cout << "31. Running test_metrics_store_legacy_migration..." << std::endl;
  std::string db_file = "test_legacy_state.db";
  std::filesystem::remove(db_file);
  
  std::ofstream out(db_file);
  out << "HOST|web-1|192.168.1.1|50.0|60.0|70.0|1234567890|ONLINE\n";
  out << "LOG|1234567890|web-1|192.168.1.1|0|50.0|60.0|70.0|connected\n";
  out.close();
  
  monitor::MetricsStore store;
  bool loaded = store.loadFromFile(db_file);
  ASSERT_TRUE(loaded);
  
  auto snap = store.snapshot();
  bool found_host = false;
  for (const auto &h : snap) {
    if (h.name == "web-1") {
      ASSERT_EQ(h.ip, "192.168.1.1");
      ASSERT_NEAR(h.cpu, 50.0f, 0.01);
      ASSERT_NEAR(h.ram, 60.0f, 0.01);
      ASSERT_NEAR(h.disk, 70.0f, 0.01);
      ASSERT_EQ(h.lastSeen, 1234567890LL);
      found_host = true;
    }
  }
  ASSERT_TRUE(found_host);
  
  auto logs = store.logSnapshot();
  bool found_log = false;
  for (const auto &log : logs) {
    if (log.host == "web-1" && log.type == monitor::LogEventType::CONNECT) {
      ASSERT_EQ(log.ip, "192.168.1.1");
      ASSERT_NEAR(log.cpu, 50.0f, 0.01);
      ASSERT_EQ(log.detail, "connected");
      found_log = true;
    }
  }
  ASSERT_TRUE(found_log);
  
  std::filesystem::remove(db_file);
}

// 32. test_metrics_store_concurrent_access
void test_metrics_store_concurrent_access() {
  std::cout << "32. Running test_metrics_store_concurrent_access..." << std::endl;
  monitor::MetricsStore store;
  monitor::Thresholds thresh;
  
  std::vector<std::thread> threads;
  for (int i = 0; i < 4; i++) {
    threads.emplace_back([&store, &thresh, i]() {
      for (int j = 0; j < 100; j++) {
        monitor::MetricPayload p;
        p.host = "host-thread-" + std::to_string(i);
        p.ip = "127.0.0.1";
        p.cpu = 10.0f + j;
        p.timestamp = time(nullptr);
        store.upsert(p, thresh);
        store.touchLastSeen(p.host);
        store.snapshot();
      }
    });
  }
  
  for (auto &t : threads) {
    t.join();
  }
  
  auto snap = store.snapshot();
  ASSERT_EQ(snap.size(), 4UL);
}

// 33. test_server_stats
void test_server_stats() {
  std::cout << "33. Running test_server_stats..." << std::endl;
  monitor::ServerStats stats;
  stats.reset();
  ASSERT_EQ(stats.agentsOnline.load(), 0);
  ASSERT_EQ(stats.msgsTotal.load(), 0UL);
  
  stats.agentsOnline = 2;
  stats.agentsStale = 1;
  stats.msgsTotal = 10;
  stats.msgsDropped = 1;
  stats.alertsSent = 2;
  stats.viewerConnects = 3;
  
  std::string json = stats.toJson();
  ASSERT_TRUE(str_contains(json, "\"agents_online\":2"));
  ASSERT_TRUE(str_contains(json, "\"agents_stale\":1"));
  ASSERT_TRUE(str_contains(json, "\"msgs_total\":10"));
  ASSERT_TRUE(str_contains(json, "\"msgs_dropped\":1"));
  
  std::string prom = stats.toPrometheus();
  ASSERT_TRUE(str_contains(prom, "monitor_agents_online 2"));
  ASSERT_TRUE(str_contains(prom, "monitor_agents_stale 1"));
  ASSERT_TRUE(str_contains(prom, "monitor_msgs_total 10"));
}

// 34. test_server_stats_atomic
void test_server_stats_atomic() {
  std::cout << "34. Running test_server_stats_atomic..." << std::endl;
  monitor::ServerStats stats;
  stats.reset();
  
  std::vector<std::thread> threads;
  for (int i = 0; i < 4; i++) {
    threads.emplace_back([&stats]() {
      for (int j = 0; j < 1000; j++) {
        stats.msgsTotal++;
      }
    });
  }
  
  for (auto &t : threads) {
    t.join();
  }
  
  ASSERT_EQ(stats.msgsTotal.load(), 4000UL);
}

int main() {
  std::cout << "=== Running C++ Unit Tests ===" << std::endl;
  test_protocol();
  test_json_helper_escape();
  test_json_helper_encode();
  test_json_helper_decode();
  test_json_helper_roundtrip();
  test_thresholds_defaults();
  test_thresholds_overrides();
  test_thresholds_comments_and_whitespace();
  test_thresholds_missing_file();
  test_ui_statusGlyph();
  test_ui_statusColorPair();
  test_ui_formatAge();
  test_ui_getBlockGlyph();
  test_ui_drawBar();
  test_ui_formatNet();
  test_ui_formatUptime();
  test_ui_makeBrailleChar();
  test_ui_padRight_padLeft();
  test_metrics_store_pipe_encoding();
  test_metrics_store_jsonStr();
  test_host_state_push();
  test_metrics_store_upsert();
  test_metrics_store_set_online_offline();
  test_metrics_store_mark_stale_offline();
  test_metrics_store_snapshot();
  test_metrics_store_log_snapshot();
  test_metrics_store_hosts_json();
  test_metrics_store_history_json();
  test_metrics_store_log_json();
  test_metrics_store_save_load();
  test_metrics_store_legacy_migration();
  test_metrics_store_concurrent_access();
  test_server_stats();
  test_server_stats_atomic();
  std::cout << "=== ALL 34 UNIT TESTS PASSED SUCCESSFULLY! ===" << std::endl;
  return 0;
}
