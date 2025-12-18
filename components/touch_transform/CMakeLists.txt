set(priv_requires nvs_flash esp_timer touch_orient)

if(CONFIG_TOUCH_TRANSFORM_ENABLE_TESTS)
    list(APPEND priv_requires unity)
endif()

idf_component_register(
    SRCS "touch_transform.c" "touch_transform_storage.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES ${priv_requires}
    REQUIRES touch
)

if(CONFIG_TOUCH_TRANSFORM_ENABLE_TESTS)
    set(TEST_SRC "${CMAKE_CURRENT_LIST_DIR}/test/test_touch_transform.c")
    if(EXISTS "${TEST_SRC}")
        target_sources(${COMPONENT_LIB} PRIVATE "${TEST_SRC}")
        target_compile_definitions(${COMPONENT_LIB} PRIVATE ARS_TOUCH_TRANSFORM_TESTS=1)
    endif()
endif()
