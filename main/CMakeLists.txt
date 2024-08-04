set(SEN5X_EMBEDDED_DIR ../thirdparty/embedded-i2c-sen5x)
set(SEN5X_SOURCES
    ${SEN5X_EMBEDDED_DIR}/sen5x_i2c.h
    ${SEN5X_EMBEDDED_DIR}/sen5x_i2c.c
    ${SEN5X_EMBEDDED_DIR}/sensirion_common.h
    ${SEN5X_EMBEDDED_DIR}/sensirion_common.c
    ${SEN5X_EMBEDDED_DIR}/sensirion_i2c.h
    ${SEN5X_EMBEDDED_DIR}/sensirion_i2c.c
    # ${SEN5X_EMBEDDED_DIR}/sample-implementations/esp32/sensirion_i2c_esp32_config.h
    ${SEN5X_EMBEDDED_DIR}/sample-implementations/esp32/sensirion_i2c_hal.c
)

set(ESPER_AQM_SOURCES
    main.cpp
    aqi.h
    aqi.cpp
    system.h
    system.c
    http_server.h
    http_server.c
    lcd_ascii.h
    lcd_ascii.c
    utils.h
    utils.c
    wifi.h
    wifi.c
)

set(TARGET_SOURCES
    ${ESPER_AQM_SOURCES}
    ${SEN5X_SOURCES}
)
set(IDF_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${SEN5X_EMBEDDED_DIR}
    ${SEN5X_EMBEDDED_DIR}/sample-implementations/esp32
)

idf_component_register(
    SRCS ${TARGET_SOURCES}
    INCLUDE_DIRS ${IDF_INCLUDE_DIRS}
)

set(TARGET_COMPILE_DEFS
    -DSEN5X_I2C_ADDRESS=0x69
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE ${TARGET_COMPILE_DEFS})
