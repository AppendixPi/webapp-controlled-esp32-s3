idf_component_register(SRCS "main.c" 
                    INCLUDE_DIRS "."
                    EMBED_FILES "${CMAKE_CURRENT_SOURCE_DIR}/../front/index.html" "${CMAKE_CURRENT_SOURCE_DIR}/../front/script.js" "${CMAKE_CURRENT_SOURCE_DIR}/../front/styles.css")