# Generated with glad2 2.0.8:
#   glad --api gl:compatibility=4.6 --extensions GL_ARB_buffer_storage,GL_ARB_sync,GL_ARB_framebuffer_object,GL_ARB_vertex_array_bgra,GL_EXT_vertex_array_bgra,GL_EXT_texture_filter_anisotropic,GL_ARB_texture_float,GL_EXT_bgra,GL_KHR_debug,GL_ARB_timer_query --out-path extern/glad c
set(GLAD_SRC "glad/src/gl.c")
set(GLAD_HPP "glad/include/glad/gl.h" "glad/include/KHR/khrplatform.h")

source_group("" FILES ${GLAD_SRC} ${GLAD_HPP})

add_library("glad" STATIC ${GLAD_SRC} ${GLAD_HPP})

set_property(TARGET "glad" PROPERTY FOLDER "External Libraries")

target_include_directories("glad" PUBLIC "glad/include")
