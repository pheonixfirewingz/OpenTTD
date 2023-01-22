add_files(
    x11_wrapper.cpp
    x11_wrapper.h
    x11_v.cpp
    x11_v.h
    CONDITION X11_FOUND AND OPENGL_FOUND
)