add_executable(SleepSyncWakeTest SleepSyncWakeTest.cpp)
target_link_libraries(SleepSyncWakeTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(SleepSyncWakeTest)

set(SLEEP_PROGRESS_SYNC_SOURCES
  SleepProgressSyncTest.cpp
  ${REPO_ROOT}/src/util/SleepProgressSync.cpp
  ${REPO_ROOT}/lib/KOReaderSync/KOReaderProgressPayload.cpp
  ${REPO_ROOT}/lib/KOReaderSync/ProgressComparison.cpp
  ${REPO_ROOT}/lib/KOReaderSync/ChapterXPathResolver.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
)
foreach(SLEEP_SYNC_TARGET SleepProgressSyncTest SleepProgressSyncPsramTest)
  add_executable(${SLEEP_SYNC_TARGET} ${SLEEP_PROGRESS_SYNC_SOURCES})
  target_include_directories(${SLEEP_SYNC_TARGET} BEFORE PRIVATE
    stubs
    ${REPO_ROOT}/lib/KOReaderSync
    ${REPO_ROOT}/lib/Memory
    ${REPO_ROOT}/lib/Epub
    ${REPO_ROOT}/lib/Utf8
    ${REPO_ROOT}/lib/XmlParserUtils
    ${REPO_ROOT}/lib/expat
    ${REPO_ROOT}/test/koreader_xpath_resolver/stubs
  )
  target_compile_definitions(${SLEEP_SYNC_TARGET} PRIVATE XML_CONTEXT_BYTES=0 XML_GE=0
    BOARD_HAS_PSRAM=$<STREQUAL:${SLEEP_SYNC_TARGET},SleepProgressSyncPsramTest>)
  target_link_libraries(${SLEEP_SYNC_TARGET} PRIVATE crosspoint_test_common GTest::gtest_main)
  gtest_discover_tests(${SLEEP_SYNC_TARGET} TEST_PREFIX "${SLEEP_SYNC_TARGET}.")
endforeach()

add_executable(SleepWakeMonitorTest
  SleepWakeMonitorTest.cpp
  ${REPO_ROOT}/lib/hal/HalSleepWake.cpp
)
target_include_directories(SleepWakeMonitorTest BEFORE PRIVATE stubs)
target_link_libraries(SleepWakeMonitorTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(SleepWakeMonitorTest)
