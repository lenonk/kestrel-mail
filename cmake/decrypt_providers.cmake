if(NOT DEFINED INPUT OR NOT DEFINED OUTPUT OR NOT DEFINED KEY_FILE)
  message(FATAL_ERROR "decrypt_providers.cmake requires INPUT, OUTPUT, KEY_FILE")
endif()

if(NOT EXISTS "${INPUT}")
  message(FATAL_ERROR "Encrypted providers file not found: ${INPUT}")
endif()

if(NOT EXISTS "${KEY_FILE}")
  message(FATAL_ERROR "Key file not found: ${KEY_FILE}")
endif()

file(READ "${KEY_FILE}" KEY_CONTENT)
string(STRIP "${KEY_CONTENT}" KEY_CONTENT)

if(KEY_CONTENT STREQUAL "")
  message(FATAL_ERROR "Key file is empty: ${KEY_FILE}")
endif()

get_filename_component(OUT_DIR "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${OUT_DIR}")

execute_process(
  COMMAND openssl enc -d -aes-256-cbc -pbkdf2 -iter 200000 -salt
          -in "${INPUT}" -out "${OUTPUT}" -pass "pass:${KEY_CONTENT}"
  RESULT_VARIABLE RC
  OUTPUT_VARIABLE OUT
  ERROR_VARIABLE ERR
)

if(NOT RC EQUAL 0)
  message(FATAL_ERROR "Failed to decrypt providers file: ${ERR}")
endif()
