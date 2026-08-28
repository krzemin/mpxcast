if(NOT DEFINED INPUT_FILE OR NOT DEFINED OUTPUT_FILE)
    message(FATAL_ERROR "INPUT_FILE and OUTPUT_FILE are required")
endif()

get_filename_component(output_directory "${OUTPUT_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${output_directory}")
file(READ "${INPUT_FILE}" input_hex HEX)
file(SHA256 "${INPUT_FILE}" input_sha256)
string(REGEX REPLACE "([0-9A-Fa-f][0-9A-Fa-f])" "0x\\1, " input_bytes "${input_hex}")
string(REGEX MATCHALL "0x[0-9A-Fa-f][0-9A-Fa-f], " input_bytes "${input_bytes}")

set(formatted_bytes "")
set(line_byte_count 0)
foreach(input_byte IN LISTS input_bytes)
    string(APPEND formatted_bytes "${input_byte}")
    math(EXPR line_byte_count "${line_byte_count} + 1")
    if(line_byte_count EQUAL 12)
        string(APPEND formatted_bytes "\n")
        set(line_byte_count 0)
    endif()
endforeach()

file(WRITE "${OUTPUT_FILE}"
"#include <stddef.h>\n"
"\n"
"const unsigned char mpxcast_cover_png[] = {\n${formatted_bytes}\n};\n"
"const size_t mpxcast_cover_png_size = sizeof(mpxcast_cover_png);\n"
"const char mpxcast_cover_etag[] = \"\\\"${input_sha256}\\\"\";\n")
