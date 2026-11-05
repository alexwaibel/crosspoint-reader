#pragma once

#include <Print.h>

#include <optional>
#include <string>

#include "TestState.h"

class Epub {
  std::string path = "/books/test.epub";

 public:
  struct SyncMetadata {
    std::string isbn, asin, series;
    std::optional<float> seriesIndex;
  };
  Epub() { ++testState.liveEpubs; }
  Epub(const std::string& filepath, const std::string&) : path(filepath) {
    ++testState.liveEpubs;
    ++testState.reloads;
  }
  ~Epub() { --testState.liveEpubs; }
  const std::string& getPath() const { return path; }
  std::string getTitle() const { return "Test book"; }
  std::string getAuthor() const { return "Test author"; }
  int getSpineItemsCount() const { return testState.spineCount; }
  void setupCacheDir() {}
  bool load(bool, bool) {
    testState.loadedPath = path;
    return testState.loadSuccess && path != testState.movedFrom;
  }
  struct SpineItem {
    std::string href;
  };
  SpineItem getSpineItem(int spine) const {
    return spine >= 0 && spine < getSpineItemsCount() ? SpineItem{"chapter.xhtml"} : SpineItem{};
  }
  bool readItemContentsToStream(const std::string&, Print& out, size_t, bool = false) const {
    if (path == testState.movedFrom) return false;
    const std::string content = "<html><body><p>Last page</p></body></html>";
    return out.write(reinterpret_cast<const uint8_t*>(content.data()), content.size()) == content.size();
  }
  bool loadSyncMetadata(SyncMetadata& out) {
    out.isbn = "123";
    return true;
  }
};
