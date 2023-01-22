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

/** @file x11_v.h Base of the linux X11 video driver. */

#ifndef VIDEO_X11_H
#define VIDEO_X11_H

#include "../video_driver.hpp"
#include "x11_wrapper.h"
struct X11Window;

/** Base class for linux X11 video drivers. */
class VideoDriver_X11Base : public VideoDriver
{
  public:
    VideoDriver_X11Base()
        : x_window(new X11Window())
        , fullscreen(false)
        , buffer_locked(false)
    {
    }

    void Stop() override;

    void MakeDirty(int left, int top, int width, int height) override;

    void MainLoop() override;

    bool ChangeResolution(int w, int h) override;

    bool ToggleFullscreen(bool fullscreen) override;

    bool ClaimMousePointer() override;

  protected:
    X11Window *x_window;
    bool fullscreen;
    bool has_focus = false;
    Rect dirty_rect;
    int width = 0;
    int height = 0;
    int width_org = 0;
    int height_org = 0;

    bool buffer_locked;

    Dimension GetScreenSize() const override;
    void InputLoop() override;
    bool LockVideoBuffer() override;
    void UnlockVideoBuffer() override;
    void CheckPaletteAnim() override;
    bool PollEvent() override;

    void Initialize();
    bool MakeWindow(bool full_screen, bool resize = true);
    void ClientSizeChanged(int w, int h, bool force = false);

    /** Get screen depth to use for fullscreen mode. */
    virtual uint8 GetFullscreenBpp();
    /** (Re-)create the backing store. */
    virtual bool AllocateBackingStore(int w, int h, bool force = false) = 0;
    /** Get a pointer to the video buffer. */
    virtual void *GetVideoPointer() = 0;
    /** Hand video buffer back to the painting backend. */
    virtual void ReleaseVideoPointer()
    {
    }

  private:
    friend int EventLoop(void *);
    bool IsKeyPressed(const int key);
};

/** The OpenGL video driver for X11 linux. */
class VideoDriver_X11OpenGL : public VideoDriver_X11Base
{
  public:
    VideoDriver_X11OpenGL()
        : anim_buffer(nullptr)
        , driver_info(this->GetName())
    {
    }
    const char *Start(const StringList &param) override;
    void Stop() override;
    bool ToggleFullscreen(bool fullscreen) override;
    bool AfterBlitterChange() override;
    bool HasEfficient8Bpp() const override
    {
        return true;
    }
    bool UseSystemCursor() override
    {
        return true;
    }
    void PopulateSystemSprites() override;
    void ClearSystemSprites() override;
    bool HasAnimBuffer() override
    {
        return true;
    }
    uint8 *GetAnimBuffer() override
    {
        return this->anim_buffer;
    }
    void ToggleVsync(bool vsync) override;
    const char *GetName() const override
    {
        return "x11-opengl";
    }
    const char *GetInfoString() const override
    {
        return this->driver_info.c_str();
    }

  protected:
    X11GLContext *gl_context = nullptr;
    uint8_t *anim_buffer = nullptr;
    std::string driver_info;

    uint8 GetFullscreenBpp() override
    {
        return 32;
    } // OpenGL is always 32 bpp.
    void Paint() override;
    bool AllocateBackingStore(int w, int h, bool force = false) override;
    void *GetVideoPointer() override;
    void ReleaseVideoPointer() override;

    const char *AllocateContext();
    void DestroyContext();
};

/** The factory for linux X11 OpenGL video driver. */
class FVideoDriver_X11OpenGL : public DriverFactoryBase
{
  public:
    FVideoDriver_X11OpenGL()
        : DriverFactoryBase(Driver::DT_VIDEO, 9, "x11", "X11 OpenGL Video Driver")
    {
    }
    Driver *CreateInstance() const override
    {
        return new VideoDriver_X11OpenGL();
    }
};
#endif
