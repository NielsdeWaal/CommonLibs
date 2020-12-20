set(StatWriterHeaders StatWriter.h)
set(StatWriterSources StatWriter.cpp)

add_library(StatWriter STATIC ${StatWriterHeaders} ${StatWriterSources})
target_link_libraries(StatWriter PUBLIC 
	EventLoop
	Common
	spdlog
	cpptoml)

target_include_directories(StatWriter
  INTERFACE ./)
