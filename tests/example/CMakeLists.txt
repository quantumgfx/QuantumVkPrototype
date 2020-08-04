project(Example LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_executable(Example "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp")
target_link_libraries(Example PRIVATE QuantumVk)
target_include_directories(Example PUBLIC ${QM_VK_INCLUDE_DIR})