#pragma once

#ifdef _WIN32

#include "Platform/Define.h"

#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>

namespace ai
{
    // Spell icons of the server's debug window (PlayerbotDebugWindow). The server has no icon textures, so an icon
    // is looked up on wowhead (nether.wowhead.com/classic/tooltip/spell/<id> names it), downloaded from its image
    // host and kept in spellicons/<spell id>.jpg next to the server; later runs load the file. Downloads run on a
    // worker thread; the window is told with notifyMessage when one is ready and draws it on its next paint.
    // Get / Draw are called on the window's thread only.
    class PlayerbotDebugIcons
    {
    public:
        void Start(HWND notifyWindow, UINT notifyMessage);
        void Stop();

        // draws the spell's icon into x, y, size x size; false while it is downloaded or when there is none (the
        // caller draws a placeholder)
        bool Draw(HDC dc, int x, int y, int size, uint32 spellId);

    private:
        enum class IconState : uint8 { Queued, Ready, Failed };

        struct Icon
        {
            IconState state = IconState::Queued;
            void* bitmap = nullptr;     // Gdiplus::Bitmap, loaded from the file on the window's thread
        };

        void Run();
        bool Fetch(uint32 spellId);
        static std::wstring FilePath(uint32 spellId);

        HWND notifyWindow = nullptr;
        UINT notifyMessage = 0;
        ULONG_PTR gdiplusToken = 0;

        std::mutex lock;                // states and queue, shared with the worker
        std::condition_variable wake;
        std::map<uint32, Icon> icons;
        std::deque<uint32> queue;
        bool stopping = false;
        std::thread worker;
    };
}

#endif
