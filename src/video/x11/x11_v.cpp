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

/** @file X11_v.cpp Implementation of the Linux X11 video driver. */

#include "../../stdafx.h"
#include <GL/gl.h>
//-
#include "../../blitter/factory.hpp"
#include "../../core/geometry_func.hpp"
#include "../../core/math_func.hpp"
#include "../../core/random_func.hpp"
#include "../../framerate_type.h"
#include "../../gfx_func.h"
#include "../../progress.h"
#include "../../rev.h"
#include "../../texteff.hpp"
#include "../../thread.h"
#include "../opengl.h"
#include "x11_v.h"
#include "x11_wrapper.h"

static FVideoDriver_X11OpenGL iFVideoDriver_X11OpenGL;
static Palette _local_palette; ///< Current palette to use for drawing.
bool _window_maximize;

static const Dimension default_resolutions[] = {{640, 480},   {800, 600},   {1024, 768},  {1152, 864},
                                                {1280, 800},  {1280, 960},  {1280, 1024}, {1400, 1050},
                                                {1600, 1200}, {1680, 1050}, {1920, 1200}};

static bool _cursor_disable = true;
static bool _cursor_visible = true;

bool MyShowCursor(X11Window *x_window, bool show, bool toggle)
{
    if (toggle)
        _cursor_disable = !_cursor_disable;
    if (_cursor_disable)
        return show;
    if (_cursor_visible == show)
        return show;

    _cursor_visible = show;

    x_window->toggleMouse(show);

    return !show;
}

void FindResolutions(uint8 bpp)
{
    _resolutions.clear();

    Display *display = W_XOpenDisplay();
    int screen_count = W_ScreenCount(display);
    for (int i = 0; i < screen_count; i++)
    {
        int width = W_DisplayWidth(display, i);
        int height = W_DisplayHeight(display, i);
        int depth = W_DefaultDepth(display, i);
        if (depth != bpp || width < 640 || height < 480)
            continue;
        if (std::find(_resolutions.begin(), _resolutions.end(), Dimension(width, height)) != _resolutions.end())
            continue;
        _resolutions.emplace_back(width, height);
    }

    W_XCloseDisplay(display);
    /* We have found no resolutions, show the default list */
    if (_resolutions.empty())
    {
        _resolutions.assign(std::begin(default_resolutions), std::end(default_resolutions));
    }

    SortResolutions();
}

/** Colour depth to use for fullscreen display modes. */
uint8 VideoDriver_X11Base::GetFullscreenBpp()
{
    /* Check modes for the relevant fullscreen bpp */
    return _support8bpp != S8BPP_HARDWARE ? 32 : BlitterFactory::GetCurrentBlitter()->GetScreenDepth();
}

bool VideoDriver_X11Base::MakeWindow(bool full_screen, bool resize)
{
    if (!x_window)
        x_window = new X11Window();
    /* full_screen is whether the new window should be fullscreen,
     * _wnd.fullscreen is whether the current window is. */
    _fullscreen = full_screen;

    /* recreate window? */
    if ((full_screen != this->fullscreen) && x_window->GetWindow())
        x_window->Destroy();

    x_window->Create(fullscreen);

    if (resize)
    {
        x_window->Resize(full_screen ? this->width_org : this->width, full_screen ? this->height_org : this->height);
    }

    BlitterFactory::GetCurrentBlitter()->PostResize();

    GameSizeChanged();
    return true;
}

void VideoDriver_X11Base::Initialize()
{
    this->UpdateAutoResolution();
    FindResolutions(this->GetFullscreenBpp());

    /* fullscreen uses those */
    this->width = this->width_org = _cur_resolution.width;
    this->height = this->height_org = _cur_resolution.height;

    Debug(driver, 2, "Resolution for display: {}x{}", _cur_resolution.width, _cur_resolution.height);
}

bool VideoDriver_X11Base::PollEvent()
{
    return x_window->PollEvent();
}

Dimension VideoDriver_X11Base::GetScreenSize() const
{
    auto data = x_window->GetScreenSize();
    return {data.width, data.height};
}

bool VideoDriver_X11Base::LockVideoBuffer()
{
    if (this->buffer_locked)
        return false;
    this->buffer_locked = true;

    _screen.dst_ptr = this->GetVideoPointer();
    assert(_screen.dst_ptr != nullptr);

    return true;
}

void VideoDriver_X11Base::UnlockVideoBuffer()
{
    assert(_screen.dst_ptr != nullptr);
    if (_screen.dst_ptr != nullptr)
    {
        /* Hand video buffer back to the drawing backend. */
        this->ReleaseVideoPointer();
        _screen.dst_ptr = nullptr;
    }

    this->buffer_locked = false;
}

bool VideoDriver_X11Base::ClaimMousePointer()
{
    MyShowCursor(x_window, false, true);
    return true;
}

bool VideoDriver_X11Base::IsKeyPressed(const int key)
{
    return x_window->IsKeyPressed(key);
}

void VideoDriver_X11Base::InputLoop()
{
    bool old_ctrl_pressed = _ctrl_pressed;
    _ctrl_pressed = this->has_focus && this->IsKeyPressed(0xffe3);
    _shift_pressed = this->has_focus && this->IsKeyPressed(0xffe1);

#if defined(_DEBUG)
    this->fast_forward_key_pressed = _shift_pressed;
#else
    /* Speedup when pressing tab, except when using ALT+TAB
     * to switch to another application. */
    this->fast_forward_key_pressed = this->has_focus && this->IsKeyPressed(XK_Tab) && !this->IsKeyPressed(0xffe9);
#endif

    /* Determine which directional keys are down. */
    if (this->has_focus)
    {
        _dirkeys = (this->IsKeyPressed(0xff51) ? 1 : 0) + (this->IsKeyPressed(0xff52) ? 2 : 0) +
                   (this->IsKeyPressed(0xff53) ? 4 : 0) + (this->IsKeyPressed(0xff54) ? 8 : 0);
    }
    else
    {
        _dirkeys = 0;
    }

    if (old_ctrl_pressed != _ctrl_pressed)
        HandleCtrlChanged();
}

void VideoDriver_X11Base::CheckPaletteAnim()
{
    if (!CopyPalette(_local_palette))
        return;
    this->MakeDirty(0, 0, _screen.width, _screen.height);
}

void VideoDriver_X11Base::ClientSizeChanged(int w, int h, bool force)
{
    /* Allocate backing store of the new size. */
    if (this->AllocateBackingStore(w, h, force))
    {
        CopyPalette(_local_palette, true);

        BlitterFactory::GetCurrentBlitter()->PostResize();

        GameSizeChanged();
    }
}

void VideoDriver_X11Base::MakeDirty(int left, int top, int width, int height)
{
    Rect r = {left, top, left + width, top + height};
    this->dirty_rect = BoundingRect(this->dirty_rect, r);
}

void VideoDriver_X11Base::MainLoop()
{
    this->StartGameThread();
    for (;;)
    {
        if (_exit_game && x_window)
            break;
        int type = 0;
        x_window->getNextEvent(&type);
        switch (type)
        {
        case 12: {
            XRect expose = {0, 0, 0, 0};
            x_window->TranslateMsg2(&expose);
            /* Make the exposed area dirty */
            this->MakeDirty(expose.x, expose.y, expose.width, expose.height);
            /* Clear the exposed area */
            x_window->ClearArea(expose.x, expose.y, expose.width, expose.height);
        }
        break;
        case 8: {
            UndrawMouseCursor();
            _cursor.in_window = false;

            if (!_left_button_down && !_right_button_down)
                MyShowCursor(x_window, true, false);
        }
        break;
        case 6: {
            XSize move = {0, 0};
            x_window->TranslateMsg3(&move);

            if (!_cursor.in_window)
            {
                _cursor.in_window = true;
            }

            if (_cursor.UpdateCursorPosition(move.width, move.height, false))
                x_window->MoveMouse(move.width, move.height);
            MyShowCursor(x_window, false, false);
            HandleMouseEvents();
        }
        break;
        case 4: {
            int button = 0;
            x_window->TranslateMsg(&button);
            if (button == 1)
            {
                _left_button_down = true;
                //_left_button_clicked = true;
            }
            else if (button == 3)
            {
                _right_button_down = true;
                _right_button_clicked = true;
            }
            HandleMouseEvents();
        }
        break;
        case 2: {
            unsigned int button = 0;
            x_window->TranslateMsg5(&button);
            HandleKeypress(button, 0);
        }
        break;
        case 5: {
            int button = 0;
            x_window->TranslateMsg(&button);
            if (button == 1)
            {
                _left_button_down = false;
                _left_button_clicked = false;
            }
            else if (button == 3)
            {
                _right_button_down = false;
                _right_button_clicked = false;
            }
            HandleMouseEvents();
        }
        break;
        case 22: {
            XSize configure_event = {0, 0};
            x_window->TranslateMsg4(&configure_event);
            AdjustGUIZoom(true);
            this->ClientSizeChanged(configure_event.width, configure_event.height);
        }
        break;
        }
        if (type == 0)
        {
            MarkWholeScreenDirty();
        }

        this->Tick();
        this->SleepTillNextTick();
    }
    this->StopGameThread();
}

bool VideoDriver_X11Base::ChangeResolution(int w, int h)
{
    x_window->ChangeResolution(w, h, _window_maximize);
    this->width = this->width_org = w;
    this->height = this->height_org = h;
    return this->MakeWindow(_fullscreen);
}

bool VideoDriver_X11Base::ToggleFullscreen(bool fullscreen)
{
    bool res = this->MakeWindow(fullscreen);
    return res;
}

void VideoDriver_X11Base::Stop()
{
    MyShowCursor(x_window, true, false);
    if (x_window)
        delete x_window;
}

const char *VideoDriver_X11OpenGL::Start(const StringList &param)
{
    if (BlitterFactory::GetCurrentBlitter()->GetScreenDepth() == 0)
        return "Only real blitters supported";

    Dimension old_res = _cur_resolution; // Save current screen resolution in case of errors, as
                                         // MakeWindow invalidates it.

    this->Initialize();
    this->MakeWindow(_fullscreen);

    /* Create and initialize OpenGL context. */
    const char *err = this->AllocateContext();
    if (err != nullptr)
    {
        this->Stop();
        _cur_resolution = old_res;
        return err;
    }

    this->driver_info = GetName();
    this->driver_info += " (";
    this->driver_info += OpenGLBackend::Get()->GetDriverName();
    this->driver_info += ")";

    this->ClientSizeChanged(this->width, this->height, true);
    /* We should have a valid screen buffer now. If not, something went wrong and we should abort. */
    if (_screen.dst_ptr == nullptr)
    {
        this->Stop();
        _cur_resolution = old_res;
        return "Can't get pointer to screen buffer";
    }
    /* Main loop expects to start with the buffer unmapped. */
    this->ReleaseVideoPointer();

    MarkWholeScreenDirty();

    this->is_game_threaded = !GetDriverParamBool(param, "no_threads") && !GetDriverParamBool(param, "no_thread");

    return nullptr;
}

void VideoDriver_X11OpenGL::Stop()
{
    this->DestroyContext();
    this->VideoDriver_X11Base::Stop();
}

void VideoDriver_X11OpenGL::DestroyContext()
{
    OpenGLBackend::Destroy();
    if (gl_context)
        delete gl_context;
}

void VideoDriver_X11OpenGL::ToggleVsync(bool vsync)
{
    gl_context->ToggleVsync(vsync);
}

const char *VideoDriver_X11OpenGL::AllocateContext()
{
    if (gl_context)
        delete gl_context;

    gl_context = new X11GLContext(x_window);

    this->ToggleVsync(_video_vsync);

    return OpenGLBackend::Create(&X11GLContext::GetOGLProcAddressCallback, this->GetScreenSize());
}

bool VideoDriver_X11OpenGL::ToggleFullscreen(bool full_screen)
{
    if (_screen.dst_ptr != nullptr)
        this->ReleaseVideoPointer();
    this->DestroyContext();
    bool res = this->VideoDriver_X11Base::ToggleFullscreen(full_screen);
    res &= this->AllocateContext() == nullptr;
    this->ClientSizeChanged(this->width, this->height, true);
    return res;
}

bool VideoDriver_X11OpenGL::AfterBlitterChange()
{
    assert(BlitterFactory::GetCurrentBlitter()->GetScreenDepth() != 0);
    this->ClientSizeChanged(this->width, this->height, true);
    return true;
}

void VideoDriver_X11OpenGL::PopulateSystemSprites()
{
    OpenGLBackend::Get()->PopulateCursorCache();
}

void VideoDriver_X11OpenGL::ClearSystemSprites()
{
    OpenGLBackend::Get()->ClearCursorCache();
}

bool VideoDriver_X11OpenGL::AllocateBackingStore(int w, int h, bool force)
{
    if (!force && w == _screen.width && h == _screen.height)
        return false;

    this->width = w = std::max(w, 64);
    this->height = h = std::max(h, 64);

    if (this->gl_context == nullptr)
        return false;

    if (_screen.dst_ptr != nullptr)
        this->ReleaseVideoPointer();

    this->dirty_rect = {};
    bool res = OpenGLBackend::Get()->Resize(w, h, force);
    gl_context->SwapBuffers();
    _screen.dst_ptr = this->GetVideoPointer();

    return res;
}

void *VideoDriver_X11OpenGL::GetVideoPointer()
{
    if (BlitterFactory::GetCurrentBlitter()->NeedsAnimationBuffer())
    {
        this->anim_buffer = OpenGLBackend::Get()->GetAnimBuffer();
    }
    return OpenGLBackend::Get()->GetVideoBuffer();
}

void VideoDriver_X11OpenGL::ReleaseVideoPointer()
{
    if (this->anim_buffer != nullptr)
        OpenGLBackend::Get()->ReleaseAnimBuffer(this->dirty_rect);
    OpenGLBackend::Get()->ReleaseVideoBuffer(this->dirty_rect);
    this->dirty_rect = {};
    _screen.dst_ptr = nullptr;
    this->anim_buffer = nullptr;
}

void VideoDriver_X11OpenGL::Paint()
{
    PerformanceMeasurer framerate(PFE_VIDEO);

    if (_local_palette.count_dirty != 0)
    {
        Blitter *blitter = BlitterFactory::GetCurrentBlitter();

        /* Always push a changed palette to OpenGL. */
        OpenGLBackend::Get()->UpdatePalette(_local_palette.palette, _local_palette.first_dirty,
                                            _local_palette.count_dirty);
        if (blitter->UsePaletteAnimation() == Blitter::PALETTE_ANIMATION_BLITTER)
        {
            blitter->PaletteAnimate(_local_palette);
        }

        _local_palette.count_dirty = 0;
    }

    OpenGLBackend::Get()->Paint();
    OpenGLBackend::Get()->DrawMouseCursor();

    gl_context->SwapBuffers();
}
