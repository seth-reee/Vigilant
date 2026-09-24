cmake_minimum_required(VERSION 3.21)
project(Vigilant VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

qt_add_executable(vigilant main.cpp)
qt_add_resources(vigilant "license" PREFIX "/" FILES LICENSE)
qt_add_resources(vigilant "icon" PREFIX "/" FILES vigilant.png)
target_link_libraries(vigilant PRIVATE Qt6::Widgets)

install(TARGETS vigilant RUNTIME DESTINATION bin)
install(FILES vigilant.png DESTINATION share/pixmaps)
install(FILES vigilant.desktop DESTINATION share/applications)
install(FILES LICENSE DESTINATION share/licenses/vigilant)
