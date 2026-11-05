#pragma once

#include <memory>
#include <string>

#include "CrossPointPosition.h"
#include "Epub.h"

struct SavedProgressPosition {
  std::string xpath;
  float percentage;
};
struct ProgressMapper {
  static SavedProgressPosition toSavedProgress(const std::shared_ptr<Epub>& epub, const CrossPointPosition& pos) {
    const bool valid = testState.mapped && pos.spineIndex < epub->getSpineItemsCount();
    const bool complete = pos.spineIndex == epub->getSpineItemsCount() - 1 && pos.pageNumber == pos.totalPages - 1;
    return {valid ? "/body/DocFragment[2]/body/p[1]/text().0" : "", complete ? 1.0f : 0.42f};
  }
  static CrossPointPosition mapRemoteProgress(const std::shared_ptr<Epub>&, const KOReaderProgress& progress,
                                              GfxRenderer&, const CrossPointPosition&) {
    if (testState.duringMapping) testState.duringMapping();
    const std::string primary = testState.binary ? "binary-hash" : "filename-hash";
    return progress.document == primary ? testState.primaryPosition : testState.alternatePosition;
  }
};
