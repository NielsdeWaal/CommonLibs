set(CommonHeaders StreamSocket.h TLSSocket.h UDPSocket.h Util.h Websocket.h)
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

add_library(Common INTERFACE ${CommonLibsHeaders})
target_link_libraries(Common INTERFACE 
	EventLoop
	spdlog
	cpptoml

	OpenSSL::SSL
	OpenSSL::Crypto)

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
