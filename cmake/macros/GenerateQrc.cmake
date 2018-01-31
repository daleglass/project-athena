
function(GENERATE_QRC)
  set(oneValueArgs OUTPUT PREFIX PATH)
  set(multiValueArgs CUSTOM_PATHS GLOBS)
  cmake_parse_arguments(GENERATE_QRC "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN} )
  if ("${GENERATE_QRC_PREFIX}" STREQUAL "")
    set(QRC_PREFIX_PATH /)
  else()
    set(QRC_PREFIX_PATH ${GENERATE_QRC_PREFIX})
  endif()
  
  foreach(GLOB ${GENERATE_QRC_GLOBS})
    file(GLOB_RECURSE FOUND_FILES RELATIVE ${GENERATE_QRC_PATH} ${GLOB})
    foreach(FILENAME ${FOUND_FILES})
      if (${FILENAME} MATCHES "^\\.\\.")
        continue()
      endif()
      list(APPEND ALL_FILES "${GENERATE_QRC_PATH}/${FILENAME}") 
      set(QRC_CONTENTS "${QRC_CONTENTS}<file alias=\"${FILENAME}\">${GENERATE_QRC_PATH}/${FILENAME}</file>\n")
    endforeach() 
  endforeach()

  foreach(CUSTOM_PATH ${GENERATE_QRC_CUSTOM_PATHS})
    string(REPLACE "=" ";" CUSTOM_PATH ${CUSTOM_PATH})
    list(GET CUSTOM_PATH 0 IMPORT_PATH)
    list(GET CUSTOM_PATH 1 LOCAL_PATH)
    set(QRC_CONTENTS "${QRC_CONTENTS}<file alias=\"${LOCAL_PATH}\">${IMPORT_PATH}</file>\n")
  endforeach()

  set(GENERATE_QRC_DEPENDS ${ALL_FILES} PARENT_SCOPE)  
  configure_file("${HF_CMAKE_DIR}/templates/resources.qrc.in" ${GENERATE_QRC_OUTPUT})
endfunction()
