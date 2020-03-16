set(EventLoopHeaders EventLoop.h)
set(EventLoopSources EventLoop.cpp)

add_library(EventLoop STATIC ${EventLoopHeaders} ${EventLoopSources})
add_library(EventLoop::EventLoopt ALIAS EventLoop)
target_link_libraries(EventLoop PRIVATE
	Common
	spdlog
	cpptoml)

target_include_directories(EventLoop
  INTERFACE ./)
