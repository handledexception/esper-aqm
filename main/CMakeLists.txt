set(SEN5X_EMBEDDED_DIR ../thirdparty/esp32-i2c-sen5x)
set(SEN5X_SOURCES
    ${SEN5X_EMBEDDED_DIR}/sen5x_i2c.h
    ${SEN5X_EMBEDDED_DIR}/sen5x_i2c.c
    ${SEN5X_EMBEDDED_DIR}/sensirion_common.h
    ${SEN5X_EMBEDDED_DIR}/sensirion_common.c
    ${SEN5X_EMBEDDED_DIR}/sensirion_i2c.h
    ${SEN5X_EMBEDDED_DIR}/sensirion_i2c.c
    ${SEN5X_EMBEDDED_DIR}/sensirion_i2c_hal.h
    ${SEN5X_EMBEDDED_DIR}/sensirion_i2c_hal.c
)

set(ESPER_AQM_SOURCES
    main.cpp
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
)

idf_component_register(
    SRCS ${TARGET_SOURCES}
    INCLUDE_DIRS ${IDF_INCLUDE_DIRS}
)
