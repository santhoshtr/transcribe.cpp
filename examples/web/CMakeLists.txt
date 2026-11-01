# Browser demo: the C API in transcribe-web.cpp, built as a wasm module
# with Emscripten, or as a native driver for desktop checks.

if(EMSCRIPTEN)
    add_executable(transcribe-web transcribe-web.cpp)
    target_link_libraries(transcribe-web PRIVATE transcribe)
    target_link_options(transcribe-web PRIVATE
        -sMODULARIZE=1
        -sEXPORT_NAME=createTranscribeModule
        -sENVIRONMENT=worker
        -sALLOW_MEMORY_GROWTH=1
        -sMAXIMUM_MEMORY=4GB
        -sFORCE_FILESYSTEM=1
        "-sEXPORTED_FUNCTIONS=_tw_load,_tw_split,_tw_transcribe_chunk,_tw_error,_tw_free,_malloc,_free"
        "-sEXPORTED_RUNTIME_METHODS=ccall,FS,HEAPF32,UTF8ToString"
    )
    # Threads: configure with CMAKE_C_FLAGS and CMAKE_CXX_FLAGS set to
    # -pthread, so ggml and the library also compile with it. Make the
    # workers at start: the module runs in a Web Worker, which cannot wait
    # for a new worker to start while it waits for a thread to join.
    if(CMAKE_CXX_FLAGS MATCHES "-pthread")
        target_link_options(transcribe-web PRIVATE -pthread -sPTHREAD_POOL_SIZE=8)
    endif()
    transcribe_apply_warnings(transcribe-web)
    # Put the page next to transcribe-web.js so the output folder can be
    # served as it is.
    foreach(f index.html worker.js)
        configure_file(www/${f} ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${f} COPYONLY)
    endforeach()
else()
    add_executable(transcribe-web-native native-main.cpp transcribe-web.cpp)
    target_link_libraries(transcribe-web-native PRIVATE transcribe transcribe-common-example)
    transcribe_apply_warnings(transcribe-web-native)
endif()
