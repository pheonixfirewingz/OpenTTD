#include "x11_wrapper.h"
#include "../../debug.h"
#include "../../rev.h"
#include "../../stdafx.h"
#include <GL/glx.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xinerama.h>
#include <string>
#define MASKS                                                                                                \
    ExposureMask | KeyPressMask | KeyReleaseMask | PointerMotionMask | ButtonPressMask | ButtonReleaseMask | \
        LeaveWindowMask | StructureNotifyMask

PFNGLXCREATECONTEXTATTRIBSARBPROC _glXCreateContextAttribsARB;
PFNGLXSWAPINTERVALEXTPROC _glXSwapIntervalEXT;
OGLProc X11GLContext::GetOGLProcAddressCallback(const char *proc)
{
    return reinterpret_cast<OGLProc>(glXGetProcAddress((const GLubyte *)proc));
}

XEvent xev;

X11Window::X11Window()
{
    display = XOpenDisplay(0);
}

int type_last = 0;
void X11Window::getNextEvent(int *type)
{
    if (XCheckMaskEvent(display, MASKS, &xev))
    {
        *type = xev.type;
    }
    else
    {
        *type = 0;
    }

    /*if (xev.type != type_last)
    {
        printf("Event ID: %d\n", xev.type);
        type_last = xev.type;
    }*/
}

void X11Window::ChangeResolution(int w, int h, bool fullscreen)
{
    if (fullscreen)
        XMapWindow(display, window);

    XResizeWindow(display, window, w, h);
}

void X11Window::Resize(unsigned int w, unsigned int h)
{
    XResizeWindow(display, window, w, h);
    XFlush(display);
}

bool X11Window::IsKeyPressed(const int key)
{
    char keymap[32];
    XQueryKeymap(display, keymap);

    if (key == NoSymbol)
    {
        return false;
    }

    KeyCode keycode = XKeysymToKeycode(display, key);
    if (keycode == 0)
    {
        return false;
    }

    int byte = keycode / 8;
    int bit = keycode % 8;

    return keymap[byte] & (1 << bit);
}

void X11Window::MoveMouse(int x, int y)
{
    XWarpPointer(display, None, window, 0, 0, 0, 0, x, y);
}

void X11Window::toggleMouse(bool show)
{
    if (show)
    {
        XUndefineCursor(display, window);
    }
    else
    {
        Pixmap blank;
        XColor dummy;
        char data[1] = {0};
        Cursor invisible;

        blank = XCreateBitmapFromData(display, window, data, 1, 1);
        if (blank == None)
            return;

        invisible = XCreatePixmapCursor(display, blank, blank, &dummy, &dummy, 0, 0);
        XFreePixmap(display, blank);

        XDefineCursor(display, window, invisible);
    }
}

void X11Window::TranslateMsg(int *button)
{
    XButtonEvent button_event = xev.xbutton;
    *button = button_event.button;
}

void X11Window::TranslateMsg2(XRect *rect)
{
    XExposeEvent expose = xev.xexpose;
    *rect = {expose.x, expose.y, expose.width, expose.height};
}

void X11Window::TranslateMsg3(XSize *size)
{
    XMotionEvent move = xev.xmotion;
    *size = {move.x, move.y};
}

void X11Window::TranslateMsg4(XSize *size)
{
    XConfigureEvent configure_event = xev.xconfigure;
    *size = {configure_event.width, configure_event.height};
}

void X11Window::TranslateMsg5(unsigned int *button)
{
    XKeyEvent key_event = xev.xkey;
    *button = key_event.keycode;
}

int X11Window::ClearArea(int x, int y, unsigned int width, unsigned int height)
{
    return XClearArea(display, window, x, y, width, height, false);
}

void X11Window::Create(bool full_screen)
{
    screen = DefaultScreen(display);

    if (full_screen)
    {
        window = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, DisplayWidth(display, screen),
                                     DisplayHeight(display, screen), 0, BlackPixel(display, screen),
                                     BlackPixel(display, screen));
        XSelectInput(display, window, MASKS);
        XMapWindow(display, window);
        XFlush(display);
    }
    else
    {
        window = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, DisplayWidth(display, screen),
                                     DisplayHeight(display, screen), 0, BlackPixel(display, screen),
                                     BlackPixel(display, screen));
        XSelectInput(display, window, MASKS);
        XMapWindow(display, window);
        XStoreName(display, window, std::string(std::string("OpenTTD ") += _openttd_revision).c_str());
        XFlush(display);
    }
}

void X11Window::Clean()
{
    XClearWindow(display, window);
}

bool X11Window::PollEvent()
{
    XEvent event;
    if (!XCheckMaskEvent(display, MASKS, &event))
        return false;

    XFlush(display);
    return true;
}

XSize X11Window::GetScreenSize()
{
    int num_screens;
    XineramaScreenInfo *screens = XineramaQueryScreens(display, &num_screens);

    int main_screen = 0;
    for (int i = 0; i < num_screens; i++)
    {
        if (screens[i].screen_number == this->screen)
        {
            main_screen = i;
            break;
        }
    }
    return {screens[main_screen].width, screens[main_screen].height};
}

void X11Window::Destroy()
{
    XDestroyWindow(display, window);
    window = 0;
}

X11Window::~X11Window()
{
    XDestroyWindow(display, window);
    XCloseDisplay(display);
}

X11GLContext::X11GLContext(X11Window *x_window_in)
    : display(x_window_in->GetDisplay())
    , x_window(x_window_in)
{
    _glXCreateContextAttribsARB =
        (PFNGLXCREATECONTEXTATTRIBSARBPROC)glXGetProcAddressARB((const GLubyte *)"glXCreateContextAttribsARB");
    if (!_glXCreateContextAttribsARB)
        return;

    _glXSwapIntervalEXT = (PFNGLXSWAPINTERVALEXTPROC)glXGetProcAddressARB((const GLubyte *)"glXSwapIntervalEXT");
    if (!_glXSwapIntervalEXT)
        return;

    int context_attribs[] = {GLX_CONTEXT_MAJOR_VERSION_ARB,
                             4,
                             GLX_CONTEXT_MINOR_VERSION_ARB,
                             5,
                             GLX_CONTEXT_FLAGS_ARB,
                             _debug_driver_level >= 8 ? GLX_CONTEXT_DEBUG_BIT_ARB : 0,
                             GLX_CONTEXT_PROFILE_MASK_ARB,
                             GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
                             None};

    int attribs[] = {GLX_RENDER_TYPE,
                     GLX_RGBA_BIT,
                     GLX_DRAWABLE_TYPE,
                     GLX_WINDOW_BIT,
                     GLX_DOUBLEBUFFER,
                     True,
                     GLX_RED_SIZE,
                     8,
                     GLX_GREEN_SIZE,
                     8,
                     GLX_BLUE_SIZE,
                     8,
                     GLX_ALPHA_SIZE,
                     8,
                     GLX_DEPTH_SIZE,
                     24,
                     None};

    int num_configs;
    GLXFBConfig *fbc = glXChooseFBConfig(display, x_window->GetScreen(), attribs, &num_configs);
    this->frame_buffer_config = fbc[0];

    GLXContext rc = nullptr;
    if (_glXCreateContextAttribsARB)
    {
        rc = _glXCreateContextAttribsARB(display, this->frame_buffer_config, 0, true, context_attribs);

        if (rc == nullptr)
        {
            /* Try again for a 3.2 context. */
            attribs[1] = 3;
            attribs[3] = 2;
            rc = _glXCreateContextAttribsARB(display, fbc[0], 0, true, context_attribs);
        }
    }

    if (rc == nullptr)
    {
        /* Old OpenGL or old driver, let's hope for the best. */
        rc = glXCreateContext(display, glXGetVisualFromFBConfig(display, this->frame_buffer_config), 0, GL_TRUE);
        if (rc == nullptr)
            return;
    }
    if (!glXMakeCurrent(display, x_window->GetWindow(), rc))
        return;
}

void X11GLContext::SwapBuffers()
{
    glXSwapBuffers(display, x_window->GetWindow());
}

void X11GLContext::ToggleVsync(bool toggle)
{
    if (_glXSwapIntervalEXT != nullptr)
    {
        _glXSwapIntervalEXT(display, x_window->GetWindow(), toggle);
    }
    else if (toggle)
    {
        Debug(driver, 0, "OpenGL: Vsync requested, but not supported by driver");
    }
}

X11GLContext::~X11GLContext()
{
    glXMakeCurrent(display, None, nullptr);
    if (this->open_gl_context != nullptr)
    {
        glXDestroyContext(display, this->open_gl_context);
        this->open_gl_context = nullptr;
    }
}

Display *W_XOpenDisplay()
{
    return XOpenDisplay(0);
}

int W_ScreenCount(Display *display)
{
    return ScreenCount(display);
}

int W_DisplayWidth(Display *display, int screen)
{
    return DisplayWidth(display, screen);
}

int W_DisplayHeight(Display *display, int screen)
{
    return DisplayHeight(display, screen);
}

int W_DefaultDepth(Display *display, int screen)
{
    return DefaultDepth(display, screen);
}

void W_XCloseDisplay(Display *display)
{
    XCloseDisplay(display);
}
