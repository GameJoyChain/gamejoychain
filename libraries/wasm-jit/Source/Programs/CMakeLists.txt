add_executable(gjc-wast2wasm Assemble.cpp CLI.h)
target_link_libraries(gjc-wast2wasm Logging IR WAST WASM)
set_target_properties(gjc-wast2wasm PROPERTIES FOLDER Programs)
INSTALL(TARGETS gjc-wast2wasm DESTINATION ${CMAKE_INSTALL_BINDIR})

add_executable(Disassemble Disassemble.cpp CLI.h)
target_link_libraries(Disassemble Logging IR WAST WASM)
set_target_properties(Disassemble PROPERTIES FOLDER Programs)

add_executable(Test Test.cpp CLI.h)
target_link_libraries(Test Logging IR WAST Runtime)
set_target_properties(Test PROPERTIES FOLDER Programs)

add_executable(wavm wavm.cpp CLI.h)
target_link_libraries(wavm Logging IR WAST WASM Runtime Emscripten)
set_target_properties(wavm PROPERTIES FOLDER Programs)
