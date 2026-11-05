#pragma once

#include <memory>
#include <string>

class Epub;
struct CrossPointPosition;
struct KOReaderProgress;

void populateKOReaderProgressPayload(KOReaderProgress& progress, const CrossPointPosition& position,
                                     const std::string& path, const std::shared_ptr<Epub>& epub);
