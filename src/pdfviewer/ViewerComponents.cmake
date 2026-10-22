# Both viewers use these Qt components; they have no Poppler dependency.
if(NOT TARGET document-viewer)
    add_library(document-viewer STATIC
        "${CMAKE_CURRENT_LIST_DIR}/ViewerTools.cpp" "${CMAKE_CURRENT_LIST_DIR}/ViewerTools.h"
        "${CMAKE_CURRENT_LIST_DIR}/ViewerControls.cpp" "${CMAKE_CURRENT_LIST_DIR}/ViewerControls.h")
    target_include_directories(document-viewer PUBLIC "${CMAKE_CURRENT_LIST_DIR}")
    target_link_libraries(document-viewer PUBLIC Qt${QT_VERSION_MAJOR}::Widgets)
endif()
