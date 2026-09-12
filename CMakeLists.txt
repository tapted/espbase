set(ESPBASE_SRCS
    "src/espbase/boot/check_crash_loop.cpp"
    "src/espbase/boot/delayed_pm_enable.cpp"
    "src/espbase/boot/favicon_route.cpp"
    "src/espbase/boot/network_logger.cpp"
    "src/espbase/boot/ota_rollback_watchdog.cpp"
    "src/espbase/circular_history_buffer.cpp"
    "src/espbase/esp_task.cpp"
    "src/espbase/mac_address.cpp"
    "src/espbase/main_loop_task.cpp"
    "src/espbase/nvs_store.cpp"
    "src/espbase/pm_lock.cpp"
    "src/espbase/shutdown_registry.cpp"
    "src/espbase/stack_json/buffer.cpp"
    "src/espbase/stack_json/document.cpp"
    "src/espbase/stack_json/parser.cpp"
    "src/espbase/stack_json/pretty_buffer.cpp"
    "src/espbase/stack_json/printer.cpp"
)

set(ESPBASE_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/espbase_generated")
set(LOGGER_HTML_SRC "${CMAKE_CURRENT_LIST_DIR}/src/espbase/boot/network_logger.html")
set(LOGGER_GZ_OUT "${ESPBASE_GEN_DIR}/network_logger.html.gz")

file(MAKE_DIRECTORY "${ESPBASE_GEN_DIR}")

idf_component_register(
    SRCS ${ESPBASE_SRCS}
    INCLUDE_DIRS "src"
    REQUIRES esp_http_server esp_common esp_timer esp_system freertos log app_update esp_pm nvs_flash esp_driver_gpio
    EMBED_FILES "${LOGGER_GZ_OUT}"
)

add_custom_command(
    OUTPUT "${LOGGER_GZ_OUT}"
    COMMAND python -c "import sys, gzip, shutil; f_in=open(sys.argv[1], 'rb'); f_out=gzip.open(sys.argv[2], 'wb', compresslevel=9); shutil.copyfileobj(f_in, f_out); f_in.close(); f_out.close()" "${LOGGER_HTML_SRC}" "${LOGGER_GZ_OUT}"
    DEPENDS "${LOGGER_HTML_SRC}"
    COMMENT "Compressing network_logger.html to Gzip..."
    VERBATIM
)
add_custom_target(generate_network_logger_br DEPENDS "${LOGGER_GZ_OUT}")

target_include_directories(${COMPONENT_LIB} PRIVATE "${ESPBASE_GEN_DIR}")
add_dependencies(${COMPONENT_LIB} generate_network_logger_br)
