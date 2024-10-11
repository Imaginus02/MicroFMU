include(${CMAKE_CURRENT_LIST_DIR}/test-library/micropython.cmake)

# Compilation de micropython-wrap comme un user C module
set(MICROPYTHON_WRAP_DIR ${CMAKE_CURRENT_LIST_DIR}/micropython-wrap-master)

# Compilation des fichiers .c et .cpp pour le module wrap
execute_process(COMMAND make -C ${MICROPYTHON_WRAP_DIR} usercmodule)