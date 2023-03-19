add_compile_options(-std=c++20 -Wall -Wextra -Wshadow -Wnon-virtual-dtor -Wunused -Wpedantic)
# add_link_options(-fsanitize=address)
set(testSources
  main.cpp
  Common.cpp
  tcp.cpp
  websocket.cpp
  io_uring.cpp
  topology.cpp
	file.cpp
	coroutine.cpp
  )

find_package(Catch2 REQUIRED)
add_executable(tests ${testSources})
# target_compile_options(tests PRIVATE -fno-omit-frame-pointer -fsanitize=address)
# target_link_options(tests PRIVATE -fno-omit-frame-pointer -fsanitize=address)
target_link_libraries(tests PUBLIC
	EventLoop

	Common
	spdlog
	cpptoml
	Catch2::Catch2
	)

include(CTest)
include(Catch)
catch_discover_tests(tests)
