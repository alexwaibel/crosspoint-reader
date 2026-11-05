#include "SleepProgressSync.h"

#include <ChapterXPathResolver.h>
#include <Epub.h>
#include <GfxRenderer.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <KOReaderCredentialStore.h>
#include <KOReaderDocumentId.h>
#include <KOReaderProgressPayload.h>
#include <KOReaderSyncClient.h>
#include <Logging.h>
#include <Memory.h>
#include <ProgressComparison.h>
#include <ProgressMapper.h>
#include <WiFi.h>

#include <algorithm>
#include <cmath>
#include <utility>

#include "WifiCredentialStore.h"
#include "activities/RenderLock.h"
#include "activities/reader/EpubReaderUtils.h"
#include "network/WifiPowerSaveGuard.h"

namespace {
std::shared_ptr<Epub> pendingEpub;
std::string pendingRelocatedPath;
CrossPointPosition pendingPosition{};
bool pendingCompleted = false;
constexpr uint32_t JOIN_TIMEOUT_MS = 10000;
constexpr uint32_t SYNC_TIMEOUT_MS = 20000;

struct RemoteProgress {
  KOReaderProgress progress;
  CrossPointPosition position{};
};

std::shared_ptr<Epub> loadBook(const std::string& path) {
  auto loaded = makeUniqueNoThrow<Epub>(path, "/.crosspoint");
  if (!loaded) {
    LOG_ERR("SLPSYNC", "Cannot reload book: %u bytes", static_cast<unsigned>(sizeof(Epub)));
    return nullptr;
  }
  loaded->setupCacheDir();
  if (!loaded->load(false, true)) {
    LOG_ERR("SLPSYNC", "Cannot load cached book for progress sync");
    return nullptr;
  }
  return std::shared_ptr<Epub>(std::move(loaded));
}
}  // namespace

void sleepProgressSync::prepare(const std::shared_ptr<Epub>& epub, const CrossPointPosition& position) {
  if (!KOREADER_STORE.isSleepSyncEnabled() || !KOREADER_STORE.hasCredentials()) return;
  pendingRelocatedPath.clear();
  pendingEpub = epub;
  pendingPosition = position;
  pendingCompleted = epub && position.spineIndex == epub->getSpineItemsCount() && position.spineIndex > 0;
  if (pendingCompleted) {
    pendingPosition.spineIndex--;
    pendingPosition.pageNumber = std::max(position.totalPages - 1, 0);
    pendingPosition.hasVisibleTextOffset = false;
    pendingPosition.hasParagraphIndex = false;
  }
}

void sleepProgressSync::bookMoved(const std::string& source, const std::string& destination) {
  if (pendingEpub && pendingEpub->getPath() == source) pendingRelocatedPath = destination;
}

void sleepProgressSync::run(const bool allowNetwork, GfxRenderer& renderer, const std::function<bool()>& shouldAbort) {
  auto epub = std::move(pendingEpub);
  std::string path;
  path.swap(pendingRelocatedPath);
  if (!epub || !allowNetwork || !KOREADER_STORE.isSleepSyncEnabled() || powerManager.getBatteryPercentage() < 20 ||
      shouldAbort())
    return;

  if (!path.empty()) {
    epub.reset();
    epub = loadBook(path);
    if (!epub || shouldAbort()) return;
  } else {
    path = epub->getPath();
  }
  auto progress = makeUniqueNoThrow<KOReaderProgress>();
  if (!progress) {
    LOG_ERR("SLPSYNC", "Cannot allocate progress: %u bytes", static_cast<unsigned>(sizeof(KOReaderProgress)));
    return;
  }
  const auto saved =
      pendingCompleted
          ? SavedProgressPosition{ChapterXPathResolver::findXPathForProgress(epub, pendingPosition.spineIndex, 1.0f),
                                  1.0f}
          : ProgressMapper::toSavedProgress(epub, pendingPosition);
  if (saved.xpath.empty() || !std::isfinite(saved.percentage)) {
    LOG_ERR("SLPSYNC", "Cannot map reading progress; skipping sleep sync");
    return;
  }
  if (shouldAbort()) return;
  progress->progress = saved.xpath;
  progress->percentage = std::clamp(saved.percentage, 0.0f, 1.0f);
  progress->document = KOREADER_STORE.getMatchMethod() == DocumentMatchMethod::FILENAME
                           ? KOReaderDocumentId::calculateFromFilename(path)
                           : KOReaderDocumentId::calculate(path);
  if (progress->document.empty()) {
    LOG_ERR("SLPSYNC", "Cannot identify book; skipping sleep sync");
    return;
  }
  if (shouldAbort()) return;
  const std::string alternateHash = KOREADER_STORE.getMatchMethod() == DocumentMatchMethod::FILENAME
                                        ? KOReaderDocumentId::calculate(path)
                                        : KOReaderDocumentId::calculateFromFilename(path);
  if (alternateHash.empty()) {
    LOG_ERR("SLPSYNC", "Cannot identify alternate document; skipping sleep sync");
    return;
  }
  populateKOReaderProgressPayload(*progress, pendingPosition, path, epub);
  // The reader's metadata must not compete with WiFi and the TLS handshake.
  epub.reset();
  if (shouldAbort()) return;

  if (WiFi.status() != WL_CONNECTED) {
    WIFI_STORE.loadFromFile();
    const auto credential = WIFI_STORE.findCredential(WIFI_STORE.getLastConnectedSsid());
    if (!credential) {
      LOG_INF("SLPSYNC", "No last WiFi network; skipping sleep sync");
      return;
    }
    WiFi.mode(WIFI_STA);
    WiFi.begin(credential->ssid.c_str(), credential->password.c_str());
    const uint32_t started = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - started < JOIN_TIMEOUT_MS) {
      if (shouldAbort()) return;
      delay(10);
    }
    if (shouldAbort()) return;
    if (WiFi.status() != WL_CONNECTED) {
      LOG_INF("SLPSYNC", "WiFi join timed out; skipping sleep sync");
      return;
    }
  }

  WifiPowerSaveGuard powerSave;
  const uint32_t started = millis();
  const auto abortRequest = [&] { return shouldAbort() || millis() - started >= SYNC_TIMEOUT_MS; };
  auto remote = makeUniqueNoThrow<RemoteProgress>();
  auto alternate = makeUniqueNoThrow<RemoteProgress>();
  if (!remote || !alternate) {
    LOG_ERR("SLPSYNC", "Cannot allocate two remote records: %u bytes each",
            static_cast<unsigned>(sizeof(RemoteProgress)));
    return;
  }
  auto result = KOReaderSyncClient::getProgress(progress->document, remote->progress, abortRequest, 1000);
  if (abortRequest()) return;
  if (result != KOReaderSyncClient::OK && result != KOReaderSyncClient::NOT_FOUND) {
    LOG_INF("SLPSYNC", "Progress lookup failed: %s", KOReaderSyncClient::errorString(result));
    return;
  }
  auto alternateResult = KOReaderSyncClient::NOT_FOUND;
  if (alternateHash != progress->document) {
    alternateResult = KOReaderSyncClient::getProgress(alternateHash, alternate->progress, abortRequest, 1000);
    if (abortRequest()) return;
    if (alternateResult != KOReaderSyncClient::OK && alternateResult != KOReaderSyncClient::NOT_FOUND) {
      LOG_INF("SLPSYNC", "Alternate lookup failed: %s", KOReaderSyncClient::errorString(alternateResult));
      return;
    }
  }
  if (result == KOReaderSyncClient::NOT_FOUND && alternateResult == KOReaderSyncClient::OK) {
    remote.swap(alternate);
    result = KOReaderSyncClient::OK;
    alternateResult = KOReaderSyncClient::NOT_FOUND;
  }
  if (result == KOReaderSyncClient::OK) {
    epub = loadBook(path);
    if (!epub || abortRequest()) return;
    {
      RenderLock lock;
#if !BOARD_HAS_PSRAM
      // The panel and saved Quick Resume frame survive this temporary scratch loan.
      GfxRenderer::FrameBufferLoan loan(renderer);
#endif
      remote->position = ProgressMapper::mapRemoteProgress(epub, remote->progress, renderer, pendingPosition);
      if (abortRequest()) return;
      if (alternateResult == KOReaderSyncClient::OK) {
        alternate->position = ProgressMapper::mapRemoteProgress(epub, alternate->progress, renderer, pendingPosition);
        if (selectRemoteRecord(remote->position, remote->progress.percentage, alternate->position,
                               alternate->progress.percentage) == RemoteRecordChoice::Alternate) {
          remote.swap(alternate);
        }
      }
    }
    if (abortRequest()) return;
    switch (compareProgress(pendingPosition, progress->percentage, remote->position, remote->progress.percentage)) {
      case ProgressComparison::Synchronized:
        LOG_INF("SLPSYNC", "Reading progress already synchronized");
        return;
      case ProgressComparison::Unknown:
        LOG_INF("SLPSYNC", "Smart comparison needs confirmation; deferring sleep sync");
        return;
      case ProgressComparison::RemoteAhead: {
        const auto& position = remote->position;
        if (position.spineIndex < 0 || position.spineIndex >= epub->getSpineItemsCount()) {
          LOG_ERR("SLPSYNC", "Remote position has invalid spine");
          return;
        }
        std::optional<uint32_t> offset;
        if (position.hasVisibleTextOffset) offset = position.visibleTextOffset;
        if (!EpubReaderUtils::saveProgress(*epub, position.spineIndex, position.pageNumber, 0, offset)) {
          LOG_ERR("SLPSYNC", "Cannot save remote progress");
          return;
        }
        LOG_INF("SLPSYNC", "Remote reading progress saved for wake");
        return;
      }
      case ProgressComparison::LocalAhead:
        break;
    }
    epub.reset();
  }
  if (abortRequest()) return;
  const auto uploadResult = KOReaderSyncClient::updateProgress(*progress, abortRequest, 1000);
  if (uploadResult == KOReaderSyncClient::OK) {
    LOG_INF("SLPSYNC", "Reading progress uploaded");
  } else {
    LOG_INF("SLPSYNC", "Sleep sync ended: %s", KOReaderSyncClient::errorString(uploadResult));
  }
}
