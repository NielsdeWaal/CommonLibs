add_compile_options(-std=c++20 -g -Wall -Wextra -Wshadow -fno-omit-frame-pointer -Wnon-virtual-dtor -Wunused -Wpedantic)
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
add_executable(cl-tests ${testSources})
# target_compile_options(cl-tests PRIVATE -fno-omit-frame-pointer -fsanitize=address)
# target_link_options(cl-tests PRIVATE -fno-omit-frame-pointer -fsanitize=address)
# target_compile_options(cl-tests PRIVATE -fno-omit-frame-pointer -fsanitize=cfi -fvisibility=hidden -flto)
# target_link_options(cl-tests PRIVATE -fno-omit-frame-pointer -fsanitize=cfi -fvisibility=hidden -flto)
target_link_libraries(cl-tests PUBLIC
	EventLoop

	Common
	spdlog
	cpptoml
	Catch2::Catch2
	)

include(CTest)
include(Catch)
catch_discover_tests(cl-tests)
