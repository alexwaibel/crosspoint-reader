#pragma once

#include <CrossPointPosition.h>

#include <functional>
#include <memory>
#include <string>

class Epub;
class GfxRenderer;

namespace sleepProgressSync {
void prepare(const std::shared_ptr<Epub>& epub, const CrossPointPosition& position);
void bookMoved(const std::string& source, const std::string& destination);
void run(bool allowNetwork, GfxRenderer& renderer, const std::function<bool()>& shouldAbort);
}  // namespace sleepProgressSync
