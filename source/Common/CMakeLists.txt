set(CommonHeaders StreamSocket.h TLSSocket.h UDPSocket.h Util.h Websocket.h)
find_package(Threads REQUIRED)

add_library(Common INTERFACE ${CommonLibsHeaders})
target_link_libraries(Common INTERFACE 
	EventLoop
	spdlog
	cpptoml)

if(COMMONLIBS_TRACE)
	target_link_libraries(
		Common
		INTERFACE

		tracy
		Threads::Threads
		pthread dl)
endif()

target_include_directories(Common
	INTERFACE ./)
