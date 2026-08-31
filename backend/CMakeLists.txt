cmake_minimum_required(VERSION 3.10)
project(WebChatBackend VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra)

find_package(Threads REQUIRED)
find_package(MySQL QUIET)
if (MySQL_FOUND)
    set(MYSQL_LIBS ${MYSQL_LIBRARIES})
else()
    find_library(MYSQLCLIENT_LIB mysqlclient REQUIRED)
    set(MYSQL_LIBS ${MYSQLCLIENT_LIB})
endif()

find_library(HIREDIS_LIB hiredis)
find_library(REDIS_PLUS_PLUS_LIB redis++)
find_path(REDIS_PLUS_PLUS_INCLUDE_DIR sw/redis++/redis++.h)

add_executable(server
    src/main.cpp
    src/config/config.cpp
    src/core/webserver.cpp
    src/buffer/buffer.cpp
    src/core/connection/connection_factory.cpp
    src/core/connection/http_connection.cpp
    src/core/connection/websocket_connection.cpp
    src/core/connection/http_connection_io.cpp
    src/protocol/http/http_request.cpp
    src/protocol/http/http_response.cpp
    src/protocol/websocket/websocket_codec.cpp
    src/module/http/http_utils.cpp
    src/module/http/controller/api_controller.cpp
    src/module/http/service/auth_service.cpp
    src/module/http/service/friend_service.cpp
    src/module/http/repository/user_repository.cpp
    src/module/http/router/http_router.cpp
    src/module/http/router/http_legacy_adapter.cpp
    src/module/http/router/ws_upgrade_handler.cpp
    src/module/chat/chat_room.cpp
    src/module/chat/chat_service.cpp
    src/module/chat/chat_message_bus.cpp
    src/module/chat/message_dispatcher.cpp
    src/module/chat/online_user_manager.cpp
    src/module/chat/message_codec.cpp
    src/utils/timer/lst_timer.cpp
    src/utils/log/log.cpp
    src/utils/sql/sql_connection_pool.cpp
    src/utils/auth/auth_manager.cpp
    src/utils/auth/password_util.cpp
    src/utils/json_util.cpp
    src/utils/redis/redis_client.cpp
)

target_include_directories(server PRIVATE
    src
    src/config
    src/core
    src/core/connection
    src/buffer
    src/protocol/websocket
    ${REDIS_PLUS_PLUS_INCLUDE_DIR}
)
target_link_libraries(server PRIVATE Threads::Threads ${MYSQL_LIBS} crypto ${REDIS_PLUS_PLUS_LIB} ${HIREDIS_LIB})
