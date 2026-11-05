#include "KOReaderProgressPayload.h"

#include <Epub.h>
#include <HalStorage.h>
#include <KOReaderCredentialStore.h>
#include <Logging.h>

#include <algorithm>
#include <utility>

#include "CrossPointPosition.h"
#include "KOReaderSyncClient.h"

void populateKOReaderProgressPayload(KOReaderProgress& progress, const CrossPointPosition& position,
                                     const std::string& path, const std::shared_ptr<Epub>& epub) {
  if (KOREADER_STORE.supportsRichProgress()) {
    auto& rich = progress.position.emplace();
    rich.pctQ = static_cast<uint32_t>(std::clamp(progress.percentage, 0.0f, 1.0f) * 1000000.0f + 0.5f);
    rich.spineIndex = static_cast<uint16_t>(position.spineIndex);
    rich.pageNumber = static_cast<uint16_t>(position.pageNumber);
    rich.totalPages = static_cast<uint16_t>(std::max(position.totalPages, 1));
    if (position.hasParagraphIndex) rich.paragraphIndex = position.paragraphIndex;
    rich.xpath = progress.progress;
  }
  if (!KOREADER_STORE.getSendMetadata()) return;

  auto& metadata = progress.metadata.emplace();
  const auto slash = path.rfind('/');
  metadata.filename = slash == std::string::npos ? path : path.substr(slash + 1);
  if (epub) {
    metadata.title = epub->getTitle();
    metadata.authors = epub->getAuthor();
    if (KOREADER_STORE.supportsExtendedMetadata()) {
      Epub::SyncMetadata extended;
      if (epub->loadSyncMetadata(extended)) {
        metadata.isbn = std::move(extended.isbn);
        metadata.asin = std::move(extended.asin);
        metadata.series = std::move(extended.series);
        metadata.seriesIndex = extended.seriesIndex;
      } else {
        LOG_ERR("KOSync", "Cannot read extended metadata; sending core metadata only");
      }
    }
  } else {
    LOG_ERR("KOSync", "Epub unavailable for metadata; sending filename only");
  }
  Storage.readFileToString("KOSync", path + ".meta.json", 2 * 1024, metadata.extraJson);
}
