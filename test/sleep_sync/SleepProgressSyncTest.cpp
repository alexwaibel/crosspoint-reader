#include <gtest/gtest.h>

#include <limits>

#include "Epub.h"
#include "GfxRenderer.h"
#include "KOReaderDocumentId.h"
#include "KOReaderProgressPayload.h"
#include "KOReaderSyncClient.h"
#include "TestState.h"
#include "src/util/SleepProgressSync.h"

std::string KOReaderDocumentId::calculateFromFilename(const std::string& path) {
  testState.hashPath = path;
  return testState.hash ? "filename-hash" : "";
}
std::string KOReaderDocumentId::calculate(const std::string& path) {
  testState.hashPath = path;
  return testState.hash ? "binary-hash" : "";
}
KOReaderSyncClient::Error KOReaderSyncClient::getProgress(const std::string& document, KOReaderProgress& out,
                                                          const std::function<bool()>& shouldAbort, uint32_t) {
  EXPECT_EQ(testState.liveEpubs, 0);
  EXPECT_TRUE(testState.powerSave);
  ++testState.lookups;
  if (testState.duringLookup) testState.duringLookup();
  testState.transportAborted = shouldAbort();
  if (testState.transportAborted) return NETWORK_ERROR;
  const bool primary = document == (testState.binary ? "binary-hash" : "filename-hash");
  out = primary ? testState.primaryProgress : testState.alternateProgress;
  out.document = document;
  return primary ? testState.primaryResult : testState.alternateResult;
}
KOReaderSyncClient::Error KOReaderSyncClient::updateProgress(const KOReaderProgress& progress,
                                                             const std::function<bool()>& shouldAbort,
                                                             uint32_t timeoutMs) {
  EXPECT_EQ(testState.liveEpubs, 0);
  EXPECT_TRUE(testState.powerSave);
  testState.uploadTimeout = timeoutMs;
  ++testState.uploads;
  testState.uploaded = progress;
  if (testState.duringUpload) testState.duringUpload();
  testState.transportAborted = shouldAbort();
  return testState.transportAborted ? NETWORK_ERROR : testState.uploadResult;
}
const char* KOReaderSyncClient::errorString(Error) { return "Test result"; }

class SleepProgressSyncTest : public testing::Test {
 protected:
  GfxRenderer renderer;
  void SetUp() override { testState = {}; }
  void TearDown() override {
    sleepProgressSync::run(false, renderer, [] { return false; });
    EXPECT_EQ(testState.liveEpubs, 0);
    EXPECT_FALSE(testState.powerSave);
    EXPECT_FALSE(testState.frameLoanActive);
  }
  void prepare() {
    auto epub = std::make_shared<Epub>();
    CrossPointPosition position{};
    position.spineIndex = 1;
    position.pageNumber = 4;
    position.totalPages = 10;
    position.hasResolvedSpineIndex = true;
    position.hasMappedPage = true;
    sleepProgressSync::prepare(epub, position);
  }
  void run() {
    sleepProgressSync::run(true, renderer, [] { return false; });
  }
  void remote(int spine, int page, float percentage = 0.1f) {
    testState.primaryResult = KOReaderSyncClient::OK;
    testState.primaryProgress.percentage = percentage;
    testState.primaryPosition.spineIndex = spine;
    testState.primaryPosition.pageNumber = page;
    testState.primaryPosition.totalPages = 10;
    testState.primaryPosition.hasResolvedSpineIndex = true;
    testState.primaryPosition.hasMappedPage = true;
    testState.primaryPosition.hasVisibleTextOffset = true;
    testState.primaryPosition.visibleTextOffset = 345;
  }
};

TEST_F(SleepProgressSyncTest, AskModeDoesNotCaptureAndDeactivatesAnExistingSnapshot) {
  testState.smart = false;
  prepare();
  run();
  EXPECT_EQ(testState.lookups, 0);
  testState.smart = true;
  prepare();
  testState.smart = false;
  run();
  EXPECT_EQ(testState.lookups, 0);
  EXPECT_EQ(testState.joins, 0);
}

TEST_F(SleepProgressSyncTest, RemoteAheadSavesContentPositionForWakeWithoutUploading) {
  remote(1, 8);
  prepare();
  run();
  EXPECT_EQ(testState.lookups, 2);
  EXPECT_EQ(testState.uploads, 0);
  EXPECT_EQ(testState.saves, 1);
  EXPECT_EQ(testState.savedSpine, 1);
  EXPECT_EQ(testState.savedPage, 8);
  EXPECT_EQ(testState.savedOffset, 345u);
  EXPECT_EQ(testState.frameLoans, 1 - BOARD_HAS_PSRAM);
}

TEST_F(SleepProgressSyncTest, LocalAheadUploadsEvenWhenRemotePercentageIsHigher) {
  remote(0, 9, 0.9f);
  prepare();
  run();
  EXPECT_EQ(testState.uploads, 1);
  EXPECT_EQ(testState.saves, 0);
  ASSERT_TRUE(testState.uploaded);
  EXPECT_EQ(testState.uploaded->document, "filename-hash");
}

TEST_F(SleepProgressSyncTest, SameMappedPageDoesNotWriteOrUpload) {
  remote(1, 4, 0.9f);
  prepare();
  run();
  EXPECT_EQ(testState.uploads, 0);
  EXPECT_EQ(testState.saves, 0);
}

TEST_F(SleepProgressSyncTest, NewerAlternateProgressWinsOverConfiguredMatchingMethod) {
  remote(0, 9, 0.9f);
  testState.alternateResult = KOReaderSyncClient::OK;
  testState.alternateProgress.percentage = 0.2f;
  testState.alternatePosition = testState.primaryPosition;
  testState.alternatePosition.spineIndex = 2;
  testState.alternatePosition.pageNumber = 3;
  prepare();
  run();
  EXPECT_EQ(testState.uploads, 0);
  EXPECT_EQ(testState.saves, 1);
  EXPECT_EQ(testState.savedSpine, 2);
  EXPECT_EQ(testState.savedPage, 3);
}

TEST_F(SleepProgressSyncTest, MissingPrimaryUsesAlternateRecord) {
  remote(2, 3);
  testState.alternateProgress = testState.primaryProgress;
  testState.alternatePosition = testState.primaryPosition;
  testState.primaryResult = KOReaderSyncClient::NOT_FOUND;
  testState.alternateResult = KOReaderSyncClient::OK;
  prepare();
  run();
  EXPECT_EQ(testState.uploads, 0);
  EXPECT_EQ(testState.saves, 1);
  EXPECT_EQ(testState.savedSpine, 2);
}

TEST_F(SleepProgressSyncTest, FailedLookupNeverOverwritesAnUnseenRemoteRecord) {
  testState.primaryResult = KOReaderSyncClient::AUTH_FAILED;
  prepare();
  run();
  EXPECT_EQ(testState.lookups, 1);
  EXPECT_EQ(testState.uploads, 0);
  testState.primaryResult = KOReaderSyncClient::NOT_FOUND;
  testState.alternateResult = KOReaderSyncClient::SERVER_ERROR;
  prepare();
  run();
  EXPECT_EQ(testState.lookups, 3);
  EXPECT_EQ(testState.uploads, 0);
  EXPECT_EQ(testState.saves, 0);
}

TEST_F(SleepProgressSyncTest, UnknownComparisonDefersWithoutInteraction) {
  testState.primaryResult = KOReaderSyncClient::OK;
  testState.primaryProgress.percentage = std::numeric_limits<float>::quiet_NaN();
  prepare();
  run();
  EXPECT_EQ(testState.uploads, 0);
  EXPECT_EQ(testState.saves, 0);
}

TEST_F(SleepProgressSyncTest, WakeDuringLookupStopsBeforeFurtherRequestsOrPersistence) {
  remote(2, 3);
  bool wake = false;
  testState.duringLookup = [&] { wake = true; };
  prepare();
  sleepProgressSync::run(true, renderer, [&] { return wake; });
  EXPECT_TRUE(testState.transportAborted);
  EXPECT_EQ(testState.lookups, 1);
  EXPECT_EQ(testState.reloads, 0);
  EXPECT_EQ(testState.saves, 0);
  EXPECT_EQ(testState.uploads, 0);
}

TEST_F(SleepProgressSyncTest, WakeDuringMappingDoesNotSaveRemotePosition) {
  remote(2, 3);
  bool wake = false;
  testState.duringMapping = [&] { wake = true; };
  prepare();
  sleepProgressSync::run(true, renderer, [&] { return wake; });
  EXPECT_EQ(testState.saves, 0);
  EXPECT_EQ(testState.uploads, 0);
  EXPECT_EQ(testState.frameLoans, 1 - BOARD_HAS_PSRAM);
}

TEST_F(SleepProgressSyncTest, SharedDeadlineAlsoStopsRemoteDownload) {
  remote(2, 3);
  testState.duringLookup = [] { testState.now += 20000; };
  prepare();
  run();
  EXPECT_TRUE(testState.transportAborted);
  EXPECT_EQ(testState.lookups, 1);
  EXPECT_EQ(testState.saves, 0);
  EXPECT_EQ(testState.uploads, 0);
}

TEST_F(SleepProgressSyncTest, ReloadFailureDoesNotReplaceSavedProgressOrUpload) {
  remote(2, 3);
  testState.loadSuccess = false;
  prepare();
  run();
  EXPECT_EQ(testState.reloads, 1);
  EXPECT_EQ(testState.frameLoans, 0);
  EXPECT_EQ(testState.saves, 0);
  EXPECT_EQ(testState.uploads, 0);
}

TEST_F(SleepProgressSyncTest, InvalidRemoteSpineCannotBePersisted) {
  remote(3, 0);
  prepare();
  run();
  EXPECT_EQ(testState.saves, 0);
  EXPECT_EQ(testState.uploads, 0);
}

TEST_F(SleepProgressSyncTest, SaveFailureDoesNotFallBackToOverwritingTheServer) {
  remote(2, 3);
  testState.saveSuccess = false;
  prepare();
  run();
  EXPECT_EQ(testState.saves, 1);
  EXPECT_EQ(testState.uploads, 0);
}

TEST_F(SleepProgressSyncTest, UploadsCapturedProgressAndReleasesBookBeforeNetwork) {
  prepare();
  EXPECT_EQ(testState.liveEpubs, 1);
  run();
  ASSERT_EQ(testState.uploads, 1);
  ASSERT_TRUE(testState.uploaded);
  EXPECT_EQ(testState.joins, 1);
  EXPECT_EQ(testState.uploaded->document, "filename-hash");
  EXPECT_FLOAT_EQ(testState.uploaded->percentage, 0.42f);
  ASSERT_TRUE(testState.uploaded->position);
  EXPECT_EQ(testState.uploaded->position->spineIndex, 1);
  EXPECT_EQ(testState.uploaded->position->pageNumber, 4);
  EXPECT_EQ(testState.uploaded->metadata->title, "Test book");
  run();
  EXPECT_EQ(testState.uploads, 1);
}

TEST_F(SleepProgressSyncTest, DisabledSettingDoesNotCaptureOrConnect) {
  testState.enabled = false;
  prepare();
  run();
  EXPECT_EQ(testState.joins, 0);
  EXPECT_EQ(testState.uploads, 0);
}

TEST_F(SleepProgressSyncTest, CompletedBookUsesLastValidSpineAndFullProgress) {
  auto epub = std::make_shared<Epub>();
  CrossPointPosition position{};
  position.spineIndex = epub->getSpineItemsCount();
  position.totalPages = 10;
  position.hasVisibleTextOffset = true;
  sleepProgressSync::prepare(epub, position);
  epub.reset();
  run();
  ASSERT_TRUE(testState.uploaded);
  EXPECT_FLOAT_EQ(testState.uploaded->percentage, 1.0f);
  ASSERT_TRUE(testState.uploaded->position);
  EXPECT_EQ(testState.uploaded->position->spineIndex, 2);
  EXPECT_EQ(testState.uploaded->position->pageNumber, 9);
  EXPECT_NE(testState.uploaded->progress.find("DocFragment[3]"), std::string::npos);
  EXPECT_NE(testState.uploaded->progress.find("/text()"), std::string::npos);
}

TEST_F(SleepProgressSyncTest, CompletedSinglePageBookUsesTheRealEndOfSpineAnchor) {
  testState.spineCount = 1;
  auto epub = std::make_shared<Epub>();
  CrossPointPosition position{};
  position.spineIndex = 1;
  position.totalPages = 1;
  sleepProgressSync::prepare(epub, position);
  epub.reset();
  run();
  ASSERT_TRUE(testState.uploaded);
  EXPECT_FLOAT_EQ(testState.uploaded->percentage, 1.0f);
  ASSERT_TRUE(testState.uploaded->position);
  EXPECT_EQ(testState.uploaded->position->spineIndex, 0);
  EXPECT_EQ(testState.uploaded->position->pageNumber, 0);
  EXPECT_EQ(testState.uploaded->position->totalPages, 1);
  EXPECT_NE(testState.uploaded->progress.find("DocFragment[1]"), std::string::npos);
  EXPECT_NE(testState.uploaded->progress.find("/text()"), std::string::npos);
}

TEST_F(SleepProgressSyncTest, CompletedMovedBookResolvesItsEndAnchorFromTheNewPath) {
  auto epub = std::make_shared<Epub>();
  CrossPointPosition position{};
  position.spineIndex = epub->getSpineItemsCount();
  position.totalPages = 1;
  sleepProgressSync::prepare(epub, position);
  epub.reset();
  testState.movedFrom = "/books/test.epub";
  sleepProgressSync::bookMoved(testState.movedFrom, "/read/test.epub");
  run();
  ASSERT_TRUE(testState.uploaded);
  EXPECT_FLOAT_EQ(testState.uploaded->percentage, 1.0f);
  EXPECT_NE(testState.uploaded->progress.find("/text()"), std::string::npos);
  EXPECT_EQ(testState.loadedPath, "/read/test.epub");
  EXPECT_EQ(testState.hashPath, "/read/test.epub");
  EXPECT_EQ(testState.reloads, 1);
}

TEST_F(SleepProgressSyncTest, UnrelatedBookMoveDoesNotRetargetTheSnapshot) {
  prepare();
  sleepProgressSync::bookMoved("/books/other.epub", "/read/other.epub");
  run();
  ASSERT_TRUE(testState.uploaded);
  EXPECT_EQ(testState.hashPath, "/books/test.epub");
  EXPECT_EQ(testState.reloads, 0);
}

TEST_F(SleepProgressSyncTest, MissingRelocatedCacheDoesNotConnectOrWriteProgress) {
  prepare();
  sleepProgressSync::bookMoved("/books/test.epub", "/read/test.epub");
  testState.loadSuccess = false;
  run();
  EXPECT_EQ(testState.joins, 0);
  EXPECT_EQ(testState.lookups, 0);
  EXPECT_EQ(testState.saves, 0);
  EXPECT_EQ(testState.uploads, 0);
}

TEST_F(SleepProgressSyncTest, MissingCredentialsDoesNotCaptureOrConnect) {
  testState.credentials = false;
  prepare();
  run();
  EXPECT_EQ(testState.joins, 0);
}

TEST_F(SleepProgressSyncTest, LowBatteryAndRadioStopFailureReleaseSnapshotWithoutConnecting) {
  prepare();
  testState.battery = 19;
  run();
  EXPECT_EQ(testState.joins, 0);
  testState.battery = 20;
  prepare();
  sleepProgressSync::run(false, renderer, [] { return false; });
  EXPECT_EQ(testState.joins, 0);
}

TEST_F(SleepProgressSyncTest, MissingWifiSkipsUpload) {
  testState.savedWifi = false;
  prepare();
  run();
  EXPECT_EQ(testState.joins, 0);
  EXPECT_EQ(testState.uploads, 0);
}

TEST_F(SleepProgressSyncTest, FailedJoinStopsAtTenSecondsWithoutRetry) {
  testState.connect = false;
  prepare();
  run();
  EXPECT_EQ(testState.now, 10000u);
  EXPECT_EQ(testState.joins, 1);
  EXPECT_EQ(testState.uploads, 0);
}

TEST_F(SleepProgressSyncTest, WakeCancelsJoinWithinOnePoll) {
  testState.connect = false;
  prepare();
  sleepProgressSync::run(true, renderer, [] { return testState.now >= 30; });
  EXPECT_EQ(testState.now, 30u);
  EXPECT_EQ(testState.uploads, 0);
}

TEST_F(SleepProgressSyncTest, WakeBeforeMappingDoesNotConnect) {
  prepare();
  sleepProgressSync::run(true, renderer, [] { return true; });
  EXPECT_EQ(testState.joins, 0);
}

TEST_F(SleepProgressSyncTest, WakeAndDeadlineReachUploadTransport) {
  prepare();
  bool wake = false;
  testState.duringUpload = [&] { wake = true; };
  sleepProgressSync::run(true, renderer, [&] { return wake; });
  EXPECT_EQ(testState.uploads, 1);
  EXPECT_TRUE(testState.transportAborted);
  prepare();
  testState.duringUpload = [] { testState.now += 20000; };
  run();
  EXPECT_EQ(testState.uploads, 2);
  EXPECT_TRUE(testState.transportAborted);
}

TEST_F(SleepProgressSyncTest, UsesExistingConnectionAndStrictServerSettings) {
  testState.wifiStatus = 1;
  testState.rich = false;
  testState.metadata = false;
  testState.binary = true;
  prepare();
  run();
  EXPECT_EQ(testState.joins, 0);
  ASSERT_TRUE(testState.uploaded);
  EXPECT_EQ(testState.uploaded->document, "binary-hash");
  EXPECT_FALSE(testState.uploaded->position);
  EXPECT_FALSE(testState.uploaded->metadata);
}

TEST_F(SleepProgressSyncTest, MappingAndHashFailuresDoNotConnect) {
  testState.mapped = false;
  prepare();
  run();
  EXPECT_EQ(testState.joins, 0);
  testState.mapped = true;
  testState.hash = false;
  prepare();
  run();
  EXPECT_EQ(testState.joins, 0);
}

TEST_F(SleepProgressSyncTest, ServerFailureDoesNotRetry) {
  testState.uploadResult = KOReaderSyncClient::SERVER_ERROR;
  prepare();
  run();
  EXPECT_EQ(testState.uploads, 1);
}

TEST_F(SleepProgressSyncTest, SharedPayloadPreservesFilenameWhenMetadataLoadFails) {
  KOReaderProgress progress{};
  progress.percentage = 0.42f;
  CrossPointPosition position{};
  populateKOReaderProgressPayload(progress, position, "/books/test.epub", nullptr);
  ASSERT_TRUE(progress.metadata);
  EXPECT_EQ(progress.metadata->filename, "test.epub");
  EXPECT_TRUE(progress.metadata->title.empty());
}

TEST_F(SleepProgressSyncTest, SharedPayloadIncludesExtendedMetadataOnlyForSupportedProfiles) {
  testState.extended = true;
  prepare();
  run();
  ASSERT_TRUE(testState.uploaded);
  ASSERT_TRUE(testState.uploaded->metadata);
  EXPECT_EQ(testState.uploaded->metadata->isbn, "123");
}
