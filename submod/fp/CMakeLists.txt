file(GLOB_RECURSE fp_srcs CONFIGURE_DEPENDS src/*.cpp)
add_library(fp ${fp_srcs})
add_library(ptxsim::fp ALIAS fp)
register_headers(fp fp PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")