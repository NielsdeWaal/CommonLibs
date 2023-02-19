set(CommonHeaders StreamSocket.h TLSSocket.h UDPSocket.h Util.h Websocket.h TSC.h)
set(CommonSources TSC.cpp)
find_package(Threads REQUIRED)
include(FindOpenSSL)
find_package(OpenSSL REQUIRED)

add_library(Common STATIC ${CommonHeaders} ${CommonSources})
target_link_libraries(Common PRIVATE
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
