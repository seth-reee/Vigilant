cmake_minimum_required(VERSION 3.21)
project(Bootkeeper VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

qt_add_executable(bootkeeper main.cpp)
qt_add_resources(bootkeeper "license" PREFIX "/" FILES LICENSE)
qt_add_resources(bootkeeper "icon" PREFIX "/" FILES bootkeeper.png)
target_link_libraries(bootkeeper PRIVATE Qt6::Widgets)

install(TARGETS bootkeeper RUNTIME DESTINATION bin)
install(FILES bootkeeper.png DESTINATION share/pixmaps)
install(FILES bootkeeper.desktop DESTINATION share/applications)
install(FILES LICENSE DESTINATION share/licenses/bootkeeper)
