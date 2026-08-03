cmake_minimum_required(VERSION 3.16)
project(crosspoint_simulator C CXX)

# Web/host simulator for the CrossPoint firmware. Builds the REAL firmware
# sources (src/ + lib/) against a hardware shim layer:
#   - simulator/shim/include  replaces Arduino/FreeRTOS/ESP-IDF/SDK headers
#   - simulator/hal_sim       replaces lib/hal/*.cpp implementations
#   - simulator/stubs         replaces network clients + activities
# Configure natively for a headless/dev build, or with emcmake for the
# browser build (see simulator/README.md).

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_C_STANDARD 11)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(SIM_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

option(SIM_FETCH_JPEGDEC "Fetch the real JPEGDEC decoder (needs network)" ON)
option(SIM_FETCH_ARDUINOJSON "Fetch the real ArduinoJson (needs network)" ON)

# --- PNGdec + QRCode (real libraries when the network allows) -----------------
set(PNGDEC_SOURCES "")
set(PNGDEC_INCLUDE "${SIM_DIR}/stubs/pngdec_stub")
set(QRCODE_SOURCES "")
set(QRCODE_INCLUDE "${SIM_DIR}/stubs/qrcode_stub")
if(SIM_FETCH_JPEGDEC)  # one switch for all decoder fetches
  include(FetchContent)
  FetchContent_Declare(
    pngdec
    GIT_REPOSITORY https://github.com/bitbank2/PNGdec.git
    GIT_TAG 1.1.6  # same pin as platformio.ini
  )
  FetchContent_GetProperties(pngdec)
  if(NOT pngdec_POPULATED)
    FetchContent_Populate(pngdec)
  endif()
  if(EXISTS "${pngdec_SOURCE_DIR}/src/PNGdec.cpp")
    set(PNGDEC_INCLUDE "${pngdec_SOURCE_DIR}/src")
    file(GLOB PNGDEC_SOURCES "${pngdec_SOURCE_DIR}/src/*.cpp" "${pngdec_SOURCE_DIR}/src/*.c")
  endif()
  FetchContent_Declare(
    qrcodelib
    GIT_REPOSITORY https://github.com/ricmoo/QRCode.git
    GIT_TAG v0.0.1  # same pin as platformio.ini
  )
  FetchContent_GetProperties(qrcodelib)
  if(NOT qrcodelib_POPULATED)
    FetchContent_Populate(qrcodelib)
  endif()
  if(EXISTS "${qrcodelib_SOURCE_DIR}/src/qrcode.c")
    set(QRCODE_INCLUDE "${qrcodelib_SOURCE_DIR}/src")
    set(QRCODE_SOURCES "${qrcodelib_SOURCE_DIR}/src/qrcode.c")
  endif()
endif()

# --- ArduinoJson (real library when the network allows, mini fallback) --------
set(ARDUINOJSON_INCLUDE "${SIM_DIR}/stubs/arduinojson_stub")
if(SIM_FETCH_ARDUINOJSON)
  include(FetchContent)
  FetchContent_Declare(
    arduinojson
    GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
    GIT_TAG v7.4.2  # same pin as platformio.ini
  )
  FetchContent_GetProperties(arduinojson)
  if(NOT arduinojson_POPULATED)
    FetchContent_Populate(arduinojson)
  endif()
  if(EXISTS "${arduinojson_SOURCE_DIR}/src/ArduinoJson.h")
    set(ARDUINOJSON_INCLUDE "${arduinojson_SOURCE_DIR}/src")
  endif()
endif()

# --- JPEGDEC (real decoder when the network allows, stub otherwise) ----------
set(JPEGDEC_SOURCES "")
set(JPEGDEC_INCLUDE "${SIM_DIR}/stubs/jpegdec_stub")
if(SIM_FETCH_JPEGDEC)
  include(FetchContent)
  FetchContent_Declare(
    jpegdec
    GIT_REPOSITORY https://github.com/bitbank2/JPEGDEC.git
    GIT_TAG 86282979224c8a32fd51e091ed5a35b0c699a52b  # same pin as platformio.ini
  )
  FetchContent_GetProperties(jpegdec)
  if(NOT jpegdec_POPULATED)
    FetchContent_Populate(jpegdec)
  endif()
  if(EXISTS "${jpegdec_SOURCE_DIR}/src/JPEGDEC.cpp")
    set(JPEGDEC_INCLUDE "${jpegdec_SOURCE_DIR}/src")
    set(JPEGDEC_SOURCES "${jpegdec_SOURCE_DIR}/src/JPEGDEC.cpp" "${jpegdec_SOURCE_DIR}/src/jpeg.inl")
    list(FILTER JPEGDEC_SOURCES EXCLUDE REGEX "\\.inl$")
  endif()
endif()

# --- generated i18n sources ---------------------------------------------------
# I18nKeys.h / I18nStrings.{h,cpp} are gitignored and generated from the YAML
# translation files (the device build does this in a PlatformIO pre: script).
# Run the generator at configure time so a fresh checkout builds, and so the
# glob below picks up I18nStrings.cpp. The script has no dependencies beyond
# the Python standard library. Re-run cmake after editing translation YAMLs.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(
  COMMAND "${Python3_EXECUTABLE}" "${REPO_ROOT}/scripts/gen_i18n.py"
          "${REPO_ROOT}/lib/I18n/translations" "${REPO_ROOT}/lib/I18n/"
  RESULT_VARIABLE I18N_GEN_RESULT)
if(NOT I18N_GEN_RESULT EQUAL 0)
  message(FATAL_ERROR "gen_i18n.py failed (exit ${I18N_GEN_RESULT})")
endif()

# --- firmware sources ---------------------------------------------------------
file(GLOB_RECURSE SRC_SOURCES "${REPO_ROOT}/src/*.cpp")
list(FILTER SRC_SOURCES EXCLUDE REGEX "/src/network/")
list(FILTER SRC_SOURCES EXCLUDE REGEX "WifiSelectionActivity\\.cpp$")
list(FILTER SRC_SOURCES EXCLUDE REGEX "CrossPointWebServerActivity\\.cpp$")
list(FILTER SRC_SOURCES EXCLUDE REGEX "CalibreConnectActivity\\.cpp$")
list(FILTER SRC_SOURCES EXCLUDE REGEX "OpdsBookBrowserActivity\\.cpp$")
list(FILTER SRC_SOURCES EXCLUDE REGEX "KOReaderSyncActivity\\.cpp$")

set(LIB_DIRS
    EpdFont Epub FsHelpers GfxRenderer I18n InflateReader JsonParser
    KOReaderSync Logging Memory MiniBidi OpdsParser PngToBmpConverter
    JpegToBmpConverter Serialization Txt Utf8 XmlParserUtils Xtc ZipFile)
set(LIB_SOURCES "")
foreach(libdir ${LIB_DIRS})
  file(GLOB_RECURSE one "${REPO_ROOT}/lib/${libdir}/*.cpp" "${REPO_ROOT}/lib/${libdir}/*.c")
  list(APPEND LIB_SOURCES ${one})
endforeach()
list(FILTER LIB_SOURCES EXCLUDE REGEX "KOReaderSyncClient\\.cpp$")

set(EXPAT_SOURCES "${REPO_ROOT}/lib/expat/xmlparse.c" "${REPO_ROOT}/lib/expat/xmlrole.c"
                  "${REPO_ROOT}/lib/expat/xmltok.c")
file(GLOB MINIZ_SOURCES "${REPO_ROOT}/lib/miniz/*.c" "${REPO_ROOT}/lib/miniz/src/*.c"
     "${REPO_ROOT}/lib/miniz/src/*.cpp")
file(GLOB UZLIB_SOURCES "${REPO_ROOT}/lib/uzlib/*.c" "${REPO_ROOT}/lib/uzlib/src/*.c")
set(FREEINKUI_SOURCES "${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/src/FreeInkUI.cpp")

file(GLOB HAL_SIM_SOURCES "${SIM_DIR}/hal_sim/*.cpp")
file(GLOB SIM_SOURCES "${SIM_DIR}/sim/*.cpp")
set(SHIM_SOURCES "${SIM_DIR}/shim/shim.cpp")
set(STUB_SOURCES "${SIM_DIR}/stubs/net_client_stubs.cpp" "${SIM_DIR}/stubs/net_activity_stubs.cpp")

add_executable(crosspoint_sim
    ${SRC_SOURCES} ${LIB_SOURCES} ${EXPAT_SOURCES} ${MINIZ_SOURCES} ${UZLIB_SOURCES}
    ${HAL_SIM_SOURCES} ${SIM_SOURCES} ${SHIM_SOURCES} ${STUB_SOURCES} ${JPEGDEC_SOURCES}
    ${PNGDEC_SOURCES} ${QRCODE_SOURCES} ${FREEINKUI_SOURCES})

target_include_directories(crosspoint_sim BEFORE PRIVATE
    "${SIM_DIR}/shim/include"
    "${JPEGDEC_INCLUDE}"
    "${PNGDEC_INCLUDE}"
    "${QRCODE_INCLUDE}"
    "${ARDUINOJSON_INCLUDE}")

target_include_directories(crosspoint_sim PRIVATE
    "${REPO_ROOT}/src"
    "${REPO_ROOT}/lib/hal")
foreach(libdir ${LIB_DIRS})
  target_include_directories(crosspoint_sim PRIVATE "${REPO_ROOT}/lib/${libdir}")
endforeach()
target_include_directories(crosspoint_sim PRIVATE
    "${REPO_ROOT}/lib/expat"
    "${REPO_ROOT}/lib/miniz"
    "${REPO_ROOT}/lib/miniz/src"
    "${REPO_ROOT}/lib/uzlib"
    "${REPO_ROOT}/lib/uzlib/src"
    "${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/include"
    "${REPO_ROOT}/freeink-sdk/libs/assets/Icons/include")

target_compile_definitions(crosspoint_sim PRIVATE
    CROSSPOINT_SIMULATOR=1
    CROSSPOINT_VERSION="simulator"
    ENABLE_SERIAL_LOG
    LOG_LEVEL=2
    EINK_DISPLAY_SINGLE_BUFFER_MODE=1
    DESTRUCTOR_CLOSES_FILE=1
    XML_CONTEXT_BYTES=1024
    XML_GE=0
    USE_UTF8_LONG_NAMES=1
    MINIZ_NO_ZLIB_COMPATIBLE_NAMES=1)

target_compile_options(crosspoint_sim PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>
    -Wno-unused-parameter)

if(EMSCRIPTEN)
  set_target_properties(crosspoint_sim PROPERTIES SUFFIX ".js")
  target_link_options(crosspoint_sim PRIVATE
      -sALLOW_MEMORY_GROWTH=1
      -sINITIAL_MEMORY=134217728
      # HEAPU8 must be exported explicitly on newer emsdks (older ones always
      # attach it to Module); the frontend reads the RGBA framebuffer from it.
      -sEXPORTED_RUNTIME_METHODS=['FS','ccall','cwrap','HEAPU8']
      -sEXPORTED_FUNCTIONS=['_main','_sim_button','_sim_touch','_sim_fb','_sim_fb_width','_sim_fb_height','_sim_fb_frame','_sim_refresh_mode','_sim_save_fs','_sim_fs_ready']
      -lidbfs.js
      -sFORCE_FILESYSTEM=1)
  # Make the build directory directly servable: static frontend + wasm output.
  add_custom_command(TARGET crosspoint_sim POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory "${SIM_DIR}/web" "$<TARGET_FILE_DIR:crosspoint_sim>")
endif()
