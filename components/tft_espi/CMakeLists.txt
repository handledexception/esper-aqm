# Builds the TFT_eSPI submodule (thirdparty/TFT_eSPI) with this project's
# board setup in tft_setup.h. Wrapping it here leaves the library unmodified
# and skips its Kconfig, whose pin limits don't allow the T-Display-S3's pins.
set(TFT_ESPI_DIR ${CMAKE_CURRENT_LIST_DIR}/../../thirdparty/TFT_eSPI)

idf_component_register(SRCS "${TFT_ESPI_DIR}/TFT_eSPI.cpp"
                       INCLUDE_DIRS "." "${TFT_ESPI_DIR}"
                       REQUIRES arduino)
