add_compile_options(-std=c++20 -Wall -Wextra -Wshadow -Wnon-virtual-dtor -Wunused -Wpedantic)
set(EventLoopHeaders EventLoop.h File.h UringCommands.h DmaBuffer.h)
set(EventLoopSources EventLoop.cpp)

add_library(EventLoop STATIC ${EventLoopHeaders} ${EventLoopSources})
add_library(EventLoop::EventLoopt ALIAS EventLoop)
target_link_libraries(EventLoop PRIVATE
	Common
	spdlog
	cpptoml
	uring)

target_include_directories(EventLoop
  INTERFACE ./)
