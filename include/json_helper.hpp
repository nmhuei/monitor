#pragma once
/*
 * Minimal JSON encoder/decoder for monitor metrics.
 * Fixed: proper string escaping (", \, \n, \r, \t), escaped char decoding.
 */
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace monitor::json {

inline std::string escapeStr(const std::string &s) {
  std::string out; out.reserve(s.size() + 8);
  for (unsigned char c : s) {
    switch (c) {
    case '"':  out += "\\\""; break;
    case '\\': out += "\\\\"; break;
    case '\n': out += "\\n";  break;
    case '\r': out += "\\r";  break;
    case '\t': out += "\\t";  break;
    case '\b': out += "\\b";  break;
    case '\f': out += "\\f";  break;
    default:
      if (c < 0x20) { char b[8]; snprintf(b,sizeof(b),"\\u%04x",c); out+=b; }
      else out += (char)c;
    }
  }
  return out;
}

inline std::string encode(const std::string &host, float cpu, float ram,
                          float disk, time_t ts,
                          const std::vector<float> &cores = {},
                          float netRxKB = 0, float netTxKB = 0,
                          float loadAvg = 0, int procCount = 0,
                          const std::string &authToken = "") {
  std::ostringstream o;
  o << "{";
  if (!authToken.empty())
    o << "\"auth\":\"" << escapeStr(authToken) << "\",";
  o << "\"host\":\"" << escapeStr(host) << "\","
    << "\"cpu\":"  << cpu  << ","
    << "\"ram\":"  << ram  << ","
    << "\"disk\":" << disk << ","
    << "\"timestamp\":" << static_cast<long long>(ts) << ","
    << "\"net_rx\":" << netRxKB << ","
    << "\"net_tx\":" << netTxKB << ","
    << "\"load_avg\":" << loadAvg << ","
    << "\"proc_count\":" << procCount;
  if (!cores.empty()) {
    o << ",\"cores\":[";
    for (size_t i = 0; i < cores.size(); i++) { if (i>0) o << ","; o << cores[i]; }
    o << "]";
  }
  o << "}";
  return o.str();
}

struct Value {
  std::string str; double num = 0.0;
  bool is_str = false, is_arr = false;
  std::vector<double> arr;
};
using Object = std::unordered_map<std::string, Value>;

inline std::string trimWs(const std::string &s) {
  auto b = s.find_first_not_of(" \t\r\n");
  auto e = s.find_last_not_of(" \t\r\n");
  return (b == std::string::npos) ? "" : s.substr(b, e-b+1);
}

inline std::string parseJString(const std::string &s, size_t &pos) {
  if (pos >= s.size() || s[pos] != '"') throw std::runtime_error("Expected '\"'");
  pos++;
  std::string r;
  while (pos < s.size() && s[pos] != '"') {
    if (s[pos] == '\\' && pos+1 < s.size()) {
      pos++;
      switch(s[pos]) {
      case '"': r+='"'; break; case '\\': r+='\\'; break;
      case '/': r+='/'; break; case 'n': r+='\n'; break;
      case 'r': r+='\r'; break; case 't': r+='\t'; break;
      case 'b': r+='\b'; break; case 'f': r+='\f'; break;
      case 'u': if(pos+4<s.size()) pos+=4; break;
      default: r+=s[pos]; break;
      }
    } else { r+=s[pos]; }
    pos++;
  }
  if (pos < s.size()) pos++;
  return r;
}

inline Object decode(const std::string &json) {
  Object obj;
  std::string s = trimWs(json);
  if (s.empty() || s[0] != '{') throw std::runtime_error("Not a JSON object");
  size_t pos = 1;
  auto sk = [&]() { while(pos<s.size()&&(s[pos]==' '||s[pos]=='\t'||s[pos]=='\n'||s[pos]=='\r')) pos++; };
  while (pos < s.size()) {
    sk();
    if (pos >= s.size() || s[pos] == '}') break;
    if (s[pos] == ',') { pos++; continue; }
    std::string key = parseJString(s, pos);
    sk();
    if (pos >= s.size() || s[pos] != ':') throw std::runtime_error("Expected ':'");
    pos++; sk();
    Value v;
    if (pos < s.size() && s[pos] == '"') {
      v.is_str = true; v.str = parseJString(s, pos);
    } else if (pos < s.size() && s[pos] == '[') {
      v.is_arr = true; pos++;
      while (pos < s.size() && s[pos] != ']') {
        sk(); if (s[pos]==','){pos++;continue;} if(s[pos]==']') break;
        size_t ne=pos;
        while(ne<s.size()&&s[ne]!=','&&s[ne]!=']') ne++;
        std::string ns=trimWs(s.substr(pos,ne-pos));
        if(!ns.empty()) try { v.arr.push_back(std::stod(ns)); } catch(...) {}
        pos=ne;
      }
      if (pos < s.size()) pos++;
    } else {
      size_t ne=pos;
      while(ne<s.size()&&s[ne]!=','&&s[ne]!='}'&&s[ne]!=']') ne++;
      try { v.num = std::stod(trimWs(s.substr(pos,ne-pos))); } catch(...) { v.num=0; }
      pos=ne;
    }
    obj[key] = v;
  }
  return obj;
}

} // namespace monitor::json
