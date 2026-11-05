#pragma once
#include <string>
struct TestStorage {
  bool readFileToString(const char*, const std::string&, unsigned, std::string& out) {
    out = "{}";
    return true;
  }
};
inline TestStorage Storage;
