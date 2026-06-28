file(GLOB_RECURSE SRC_FILES "src/*.cpp")

idf_component_register(
    SRCS ${SRC_FILES}
    INCLUDE_DIRS "src"
    REQUIRES arduino-esp32 Adafruit-GFX-Library
)