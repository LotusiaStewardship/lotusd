# Script to embed HTML/CSS/JS resources into C++ header file
# Usage: cmake -DHTML_FILE=path -DCSS_FILE=path -DJS_FILE=path -DOUTPUT_FILE=path -P embed_resources.cmake

file(READ "${HTML_FILE}" HTML_CONTENT)
file(READ "${CSS_FILE}" CSS_CONTENT)
file(READ "${JS_FILE}" JS_CONTENT)

# Escape special characters for C++ string literals
string(REGEX REPLACE "\\\\" "\\\\\\\\" HTML_CONTENT "${HTML_CONTENT}")
string(REGEX REPLACE "\"" "\\\\\"" HTML_CONTENT "${HTML_CONTENT}")
string(REGEX REPLACE "\n" "\\\\n\"\n\"" HTML_CONTENT "${HTML_CONTENT}")

string(REGEX REPLACE "\\\\" "\\\\\\\\" CSS_CONTENT "${CSS_CONTENT}")
string(REGEX REPLACE "\"" "\\\\\"" CSS_CONTENT "${CSS_CONTENT}")
string(REGEX REPLACE "\n" "\\\\n\"\n\"" CSS_CONTENT "${CSS_CONTENT}")

string(REGEX REPLACE "\\\\" "\\\\\\\\" JS_CONTENT "${JS_CONTENT}")
string(REGEX REPLACE "\"" "\\\\\"" JS_CONTENT "${JS_CONTENT}")
string(REGEX REPLACE "\n" "\\\\n\"\n\"" JS_CONTENT "${JS_CONTENT}")

# Generate the header file
file(WRITE "${OUTPUT_FILE}" "// Auto-generated file - do not edit
// Generated from HTML/CSS/JS resources at build time

#pragma once

namespace explorer_resources {

constexpr const char* HTML = \"${HTML_CONTENT}\";

constexpr const char* CSS = \"${CSS_CONTENT}\";

constexpr const char* JS = \"${JS_CONTENT}\";

} // namespace explorer_resources
")

