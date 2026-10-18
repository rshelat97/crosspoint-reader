# CbzTest runs the real ZipFile, Bitmap, FsHelpers and Gray16 code against
# in-memory storage and JPEG/PNG converter stubs (see stubs/).
add_executable(CbzTest
  CbzTest.cpp
  ${REPO_ROOT}/lib/Cbz/Cbz.cpp
  ${REPO_ROOT}/lib/Gray16/Gray16.cpp
  ${REPO_ROOT}/lib/ZipFile/ZipFile.cpp
  ${REPO_ROOT}/lib/miniz/src/InflateStream.cpp
  ${REPO_ROOT}/lib/miniz/src/miniz_impl.c
  ${REPO_ROOT}/lib/Memory/BuildScratch.cpp
  ${REPO_ROOT}/lib/hal/HalMemory.cpp
  ${REPO_ROOT}/lib/FsHelpers/FsHelpers.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/lib/GfxRenderer/Bitmap.cpp
  ${REPO_ROOT}/lib/GfxRenderer/BitmapHelpers.cpp
)
target_include_directories(CbzTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/Cbz
  ${REPO_ROOT}/lib/Gray16
  ${REPO_ROOT}/lib/ZipFile
  ${REPO_ROOT}/lib/FsHelpers
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/hal
  ${REPO_ROOT}/lib/miniz/src
)
target_link_libraries(CbzTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(CbzTest)
