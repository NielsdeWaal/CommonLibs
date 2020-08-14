add_compile_options(-std=c++17 -Wall -Wextra -Wshadow -Wnon-virtual-dtor -Wunused -Wpedantic)
find_package(Threads REQUIRED) #threading
find_package(OpenSSL REQUIRED)

add_subdirectory(Common)
add_subdirectory(EventLoop)
add_subdirectory(Statwriter)

add_executable(TestingCommonLibs
	main.cpp
	Example.h)

target_link_libraries(TestingCommonLibs
	EventLoop
	Common
	StatWriter

	json)
