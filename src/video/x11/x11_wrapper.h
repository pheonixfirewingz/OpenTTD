/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, version 2. OpenTTD is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 * Public License for more details. You should have received a copy of the GNU
 * General Public License along with OpenTTD. If not, see
 * <http://www.gnu.org/licenses/>.
 */

/** @file x11_wrapper.h Base of the linux X11 video driver. */

#ifndef WRAPPER_X11_H
#define WRAPPER_X11_H
#include "../../stdafx.h"
#include <GL/gl.h>
//--
#include "../opengl.h"

struct _XDisplay;
typedef struct _XDisplay Display;
struct __GLXcontextRec;
typedef struct __GLXcontextRec *GLXContext;
struct __GLXFBConfigRec;
typedef struct __GLXFBConfigRec *GLXFBConfig;

struct XSize
{
    int width;
    int height;
};

struct XRect
{
    int x;
    int y;
    int width;
    int height;
};

class X11Window
{
    Display *display;
    unsigned long window;
    int screen;

  public:
    X11Window();
    void getNextEvent(int *type);
    void Create(bool fullscreen);
    void Clean();
    void ChangeResolution(int w, int h, bool fullscreen);
    void Resize(unsigned int w, unsigned int h);
    bool IsKeyPressed(const int key);
    void MoveMouse(int x, int y);
    void toggleMouse(bool show);
    void TranslateMsg(int *button);
    void TranslateMsg2(XRect *rect);
    void TranslateMsg3(XSize *size);
    void TranslateMsg4(XSize *size);
    int ClearArea(int x, int y, unsigned int width, unsigned int height);
    bool PollEvent();
    XSize GetScreenSize();
    void Destroy();

    Display *GetDisplay()
    {
        return display;
    }

    int GetScreen()
    {
        return screen;
    }

    unsigned long GetWindow()
    {
        return window;
    }
    ~X11Window();
};

class X11GLContext
{
    Display* display;
    X11Window *x_window = nullptr;
    GLXContext open_gl_context;
    GLXFBConfig frame_buffer_config;

  public:
    X11GLContext(X11Window *x11_window);
    void SwapBuffers();
    void ToggleVsync(bool toggle);
    static OGLProc GetOGLProcAddressCallback(const char *proc);
    ~X11GLContext();
};

Display *W_XOpenDisplay();
int W_ScreenCount(Display* display);
int W_DisplayWidth(Display *display,int screen);
int W_DisplayHeight(Display *display, int screen);
int W_DefaultDepth(Display *display, int screen);
void W_XCloseDisplay(Display * display);
#endif