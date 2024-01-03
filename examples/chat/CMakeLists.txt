add_executable(chat_server server.cpp common.h)
target_link_libraries(chat_server avizonet)

add_executable(chat_client client.cpp common.h)
target_link_libraries(chat_client avizonet)
