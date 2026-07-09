cmake_minimum_required(VERSION 3.4.1)

# Declares and names the project.
set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -Wno-unused-variable -Wno-unused-function")
set(QXR_ROOT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../../../../)
set(QXR_EXTERNAL_DIR ${QXR_ROOT_PATH}/External/)

set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE_HO OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(${QXR_EXTERNAL_DIR}/spdlog ${CMAKE_BINARY_DIR}/spdlog)

add_library(lib_xr_loader SHARED IMPORTED)
set_target_properties(lib_xr_loader PROPERTIES IMPORTED_LOCATION
        ${QXR_EXTERNAL_DIR}/OpenXR-SDK/${ANDROID_ABI}/libopenxr_loader.so)

set(THIRDPARTY_OPENXR_SOURCE_DIR
        ${QXR_EXTERNAL_DIR}/OpenXR-SDK/include
        ${QXR_EXTERNAL_DIR}/OpenXR-SDK/src
        ${QXR_EXTERNAL_DIR}/OpenXR-SDK/src/common)
add_library(qxr-thirdparty-openxr INTERFACE)
target_include_directories(qxr-thirdparty-openxr
        INTERFACE ${THIRDPARTY_OPENXR_SOURCE_DIR})

# build native_app_glue as a static lib
set(${CMAKE_C_FLAGS}, "${CMAKE_C_FLAGS}")
add_library(native_app_glue STATIC ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)
target_include_directories(native_app_glue PUBLIC ${ANDROID_NDK}/sources/android/native_app_glue)

# now build app's shared lib
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")

# Export ANativeActivity_onCreate(),
# Refer to: https://github.com/android-ndk/ndk/issues/381.
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -u ANativeActivity_onCreate")

# common-log
set(COMMON_LOG_SOURCE_DIR ${QXR_EXTERNAL_DIR}/Common/Log/)
add_library(qxr-common-log STATIC ${COMMON_LOG_SOURCE_DIR}/LogUtils.c)
target_include_directories(qxr-common-log PUBLIC ${COMMON_LOG_SOURCE_DIR})

# png
#set(png_dir ${CMAKE_CURRENT_SOURCE_DIR}/../../../../png/src/main/cpp)
#add_library(png STATIC IMPORTED)
#set_target_properties(png PROPERTIES IMPORTED_LOCATION
#        ${png_dir}/../../outLibs/${CMAKE_BUILD_TYPE}/${CMAKE_ANDROID_ARCH_ABI}/libsxr_png_static.a)
#target_include_directories(png PUBLIC
#                ${png_dir}/include)

# thirdparty-glm module
set(GLM_SOURCE_DIR ${QXR_EXTERNAL_DIR}/ThirdParty/glm/)
add_library(qxr-thirdparty-glm INTERFACE)
target_include_directories(qxr-thirdparty-glm INTERFACE ${GLM_SOURCE_DIR})

# thirdparty-tinyobj module
set(THIRDPARTY_TINYOBJ_SOURCE_DIR
        ${QXR_EXTERNAL_DIR}/ThirdParty/tinyobjloader/)
set(THIRDPARTY_TINYOBJ_SOURCE_FILES
        ${THIRDPARTY_TINYOBJ_SOURCE_DIR}/tiny_obj_loader.cc)
add_library(qxr-thirdparty-tinyobj STATIC ${THIRDPARTY_TINYOBJ_SOURCE_FILES})
target_include_directories(qxr-thirdparty-tinyobj
        INTERFACE ${THIRDPARTY_TINYOBJ_SOURCE_DIR})

# common-data-structures
set(COMMON_DATA_STRUCTURES_SOURCE_DIR
        ${QXR_EXTERNAL_DIR}/Common/DataStructures/cpp)
add_library(qxr-common-data-structures INTERFACE)
target_include_directories(qxr-common-data-structures
        INTERFACE ${COMMON_DATA_STRUCTURES_SOURCE_DIR})

# common-gl
set(COMMON_GL_SOURCE_DIR ${QXR_EXTERNAL_DIR}/Common/GL/cpp)
file(GLOB COMMON_GL_SOURCE_FILES ${COMMON_GL_SOURCE_DIR}/*.cpp)
add_library(qxr-common-gl STATIC ${COMMON_GL_SOURCE_FILES})
target_include_directories(qxr-common-gl PUBLIC ${COMMON_GL_SOURCE_DIR}/)
target_link_libraries(qxr-common-gl PRIVATE
        log
        qxr-common-log
        qxr-thirdparty-glm
        qxr-thirdparty-tinyobj
        qxr-common-data-structures)

# app-common module
set(APPCOMMON_SOURCE_DIR ${QXR_ROOT_PATH}/External/AppCommon/cpp)
add_library(qxr-app-common STATIC ${APPCOMMON_SOURCE_DIR}/AppCommon.cpp)
target_include_directories(qxr-app-common PUBLIC
        ${APPCOMMON_SOURCE_DIR}/)
target_link_libraries(qxr-app-common PRIVATE
        lib_xr_loader
        EGL
        native_app_glue
        qxr-common-log
        qxr-thirdparty-glm
        qxr-thirdparty-openxr
        )
file(GLOB main_src "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
file(GLOB ble_time_sync_src "${CMAKE_CURRENT_SOURCE_DIR}/ble_time_sync/*.cpp")
list(REMOVE_ITEM main_src
        ${CMAKE_CURRENT_SOURCE_DIR}/BleServiceJni.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/SdkStateBridge.cpp)
list(APPEND main_src
        ${CMAKE_CURRENT_SOURCE_DIR}/BleServiceJni.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/SdkStateBridge.cpp
        ${ble_time_sync_src}
        ${CMAKE_CURRENT_SOURCE_DIR}/packet_codec.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/platform_state_bridge.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/protocol_adapter.cpp)
add_library(mixedreality SHARED ${main_src})
target_include_directories(mixedreality PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set(LINKED_LIBRARIES
        mixedreality
        android
        native_app_glue
        EGL
        GLESv3
        log
        mediandk
        aaudio
        lib_xr_loader
        qxr-thirdparty-openxr
        qxr-common-log
        qxr-app-common
        qxr-common-gl
        qxr-common-data-structures
        qxr-thirdparty-tinyobj
        qxr-thirdparty-glm
        spdlog::spdlog_header_only)

target_link_libraries(${LINKED_LIBRARIES})
