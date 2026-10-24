#ifdef _WIN32

#include "PlayerbotDebugIcons.h"

#include <algorithm>
#include <fstream>
#include <regex>
#include <vector>

#include <objidl.h>
#include <winhttp.h>
// gdiplus.h uses min / max unqualified
namespace Gdiplus
{
    using std::min;
    using std::max;
}
#include <gdiplus.h>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "gdiplus.lib")

using namespace ai;

namespace
{
    // GET https://<host><path>; false on any error or a status other than 200
    bool HttpsGet(HINTERNET session, const wchar_t* host, const std::wstring& path, std::string& body)
    {
        bool ok = false;
        HINTERNET connection = WinHttpConnect(session, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
        HINTERNET request = connection ? WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
        if (request && WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(request, nullptr))
        {
            DWORD status = 0, size = sizeof(status);
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                &status, &size, WINHTTP_NO_HEADER_INDEX);
            if (status == 200)
            {
                body.clear();
                DWORD available = 0;
                ok = true;
                while (WinHttpQueryDataAvailable(request, &available) && available)
                {
                    std::vector<char> buffer(available);
                    DWORD read = 0;
                    if (!WinHttpReadData(request, buffer.data(), available, &read))
                    {
                        ok = false;
                        break;
                    }
                    body.append(buffer.data(), read);
                }
            }
        }
        if (request)
            WinHttpCloseHandle(request);
        if (connection)
            WinHttpCloseHandle(connection);
        return ok && !body.empty();
    }
}

void PlayerbotDebugIcons::Start(HWND window, UINT message)
{
    notifyWindow = window;
    notifyMessage = message;

    Gdiplus::GdiplusStartupInput input;
    Gdiplus::GdiplusStartup(&gdiplusToken, &input, nullptr);
    CreateDirectoryW(L"spellicons", nullptr);

    stopping = false;
    worker = std::thread(&PlayerbotDebugIcons::Run, this);
}

void PlayerbotDebugIcons::Stop()
{
    {
        std::lock_guard<std::mutex> guard(lock);
        stopping = true;
        queue.clear();
    }
    wake.notify_all();
    if (worker.joinable())
        worker.join();

    for (auto& [spellId, icon] : icons)
        delete static_cast<Gdiplus::Bitmap*>(icon.bitmap);
    icons.clear();

    if (gdiplusToken)
        Gdiplus::GdiplusShutdown(gdiplusToken);
    gdiplusToken = 0;
}

std::wstring PlayerbotDebugIcons::FilePath(uint32 spellId)
{
    return L"spellicons\\" + std::to_wstring(spellId) + L".jpg";
}

bool PlayerbotDebugIcons::Draw(HDC dc, int x, int y, int size, uint32 spellId)
{
    Icon* icon = nullptr;
    {
        std::lock_guard<std::mutex> guard(lock);
        auto it = icons.find(spellId);
        if (it == icons.end())
        {
            // cached by an earlier run: no download
            const bool onDisk = GetFileAttributesW(FilePath(spellId).c_str()) != INVALID_FILE_ATTRIBUTES;
            it = icons.emplace(spellId, Icon()).first;
            it->second.state = onDisk ? IconState::Ready : IconState::Queued;
            if (!onDisk)
            {
                queue.push_back(spellId);
                wake.notify_one();
            }
        }
        if (it->second.state != IconState::Ready)
            return false;
        icon = &it->second;     // map nodes stay put; only this thread erases them (Stop)
    }

    if (!icon->bitmap)
    {
        Gdiplus::Bitmap* bitmap = Gdiplus::Bitmap::FromFile(FilePath(spellId).c_str());
        if (!bitmap || bitmap->GetLastStatus() != Gdiplus::Ok)
        {
            delete bitmap;
            std::lock_guard<std::mutex> guard(lock);
            icon->state = IconState::Failed;
            return false;
        }
        icon->bitmap = bitmap;
    }

    Gdiplus::Graphics graphics(dc);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    Gdiplus::Bitmap* bitmap = static_cast<Gdiplus::Bitmap*>(icon->bitmap);
    // the icons have a dark border: cut a few percent like the addon does (SetTexCoord 0.07 - 0.93)
    const Gdiplus::REAL inset = bitmap->GetWidth() * 0.07f;
    graphics.DrawImage(bitmap, Gdiplus::RectF(Gdiplus::REAL(x), Gdiplus::REAL(y), Gdiplus::REAL(size), Gdiplus::REAL(size)),
        inset, inset, bitmap->GetWidth() - 2 * inset, bitmap->GetHeight() - 2 * inset, Gdiplus::UnitPixel);
    return true;
}

void PlayerbotDebugIcons::Run()
{
    HINTERNET session = WinHttpOpen(L"mangosd BotStateTree", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (session)
        WinHttpSetTimeouts(session, 5000, 5000, 5000, 5000);

    while (true)
    {
        uint32 spellId = 0;
        {
            std::unique_lock<std::mutex> guard(lock);
            wake.wait(guard, [this] { return stopping || !queue.empty(); });
            if (stopping)
                break;
            spellId = queue.front();
            queue.pop_front();
        }

        bool ok = false;
        if (session)
        {
            std::string tooltip, image;
            // {"name":"Fireball","icon":"spell_fire_flamebolt",...}
            static const std::regex iconField("\"icon\"\\s*:\\s*\"([A-Za-z0-9_\\-]+)\"");
            std::smatch match;
            if (HttpsGet(session, L"nether.wowhead.com", L"/classic/tooltip/spell/" + std::to_wstring(spellId), tooltip) &&
                std::regex_search(tooltip, match, iconField))
            {
                std::string name = match[1].str();
                std::transform(name.begin(), name.end(), name.begin(), [](char c) { return char(::tolower(c)); });
                if (HttpsGet(session, L"wow.zamimg.com", L"/images/wow/icons/medium/" + std::wstring(name.begin(), name.end()) + L".jpg", image))
                {
                    std::ofstream file(FilePath(spellId), std::ios::binary);
                    file.write(image.data(), image.size());
                    ok = file.good();
                }
            }
        }

        {
            std::lock_guard<std::mutex> guard(lock);
            icons[spellId].state = ok ? IconState::Ready : IconState::Failed;
        }
        if (ok)
            PostMessageW(notifyWindow, notifyMessage, 0, 0);
    }

    if (session)
        WinHttpCloseHandle(session);
}

#endif
