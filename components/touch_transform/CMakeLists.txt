idf_component_register(
    SRCS "touch_transform.c" "touch_transform_storage.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES nvs_flash esp_timer
    REQUIRES touch)

idf_component_get_property(unity_dir unity COMPONENT_DIR)
set(test_src "test/test_touch_transform.c")
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${test_src})
    set_property(TARGET ${COMPONENT_LIB} APPEND PROPERTY INTERFACE_SOURCES "${test_src}")
endif()
