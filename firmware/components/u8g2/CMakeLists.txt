file(GLOB U8G2_SRCS "../../u8g2/csrc/*.c")

idf_component_register(
    SRCS ${U8G2_SRCS}
    INCLUDE_DIRS "../../u8g2/csrc"
)
