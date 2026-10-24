#include "PlayerbotDebugWindow.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/StateTree/StateMachine.h"
#include "playerbot/StateTree/STAI_ActivityLog.h"

#include <atomic>

using namespace ai;

#ifndef _WIN32

PlayerbotDebugWindow& PlayerbotDebugWindow::instance()
{
    static PlayerbotDebugWindow window;
    return window;
}

void PlayerbotDebugWindow::Start(uint32) {}
void PlayerbotDebugWindow::Stop() {}
bool PlayerbotDebugWindow::IsRunning() const { return false; }
void PlayerbotDebugWindow::OnBotUpdate(PlayerbotAI*) {}
void PlayerbotDebugWindow::OnBotRemoved(uint64) {}

#else

#include "PlayerbotDebugIcons.h"

#include "Server/DBCStores.h"

#include <windowsx.h>

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace
{
    // what the bots publish (their own update) and the window reads (its thread), behind hub->lock

    struct BotListing
    {
        std::string name;
        uint32 level = 0;
        uint8 cls = 0;
        std::string zone;
        uint32 seenMs = 0;              // WorldTimer, the bot's last listing
    };

    // the selected bot's state machine and movement
    struct BotView
    {
        uint64 guid = 0;
        uint32 takenMs = 0;             // WorldTimer when it was copied
        StateMachineDebugView machine;
        std::string goal, goalSource, moveState;
        float distance = -1.0f;
        int32 priority = 0;
    };

    // the selected bot's activity log, oldest first
    struct BotHistory
    {
        uint64 guid = 0;
        uint32 takenMs = 0;
        std::vector<STAI_ActivityLog::Entry> entries;
    };

    // a click on a task / node / condition switch, applied on the bot's update
    struct BotSwitch
    {
        uint64 guid = 0;
        StateTreeSwitch what = StateTreeSwitch::Task;
        std::string state, tree;
        int node = -1, index = -1, value = 0;
    };

    struct Hub
    {
        std::atomic<bool> running{ false };
        std::atomic<uint64> selected{ 0 };      // the bot the window shows, 0 none
        std::atomic<bool> publishing{ false };  // the window wants data (not paused, not minimized)

        std::mutex lock;
        std::map<uint64, BotListing> bots;
        std::shared_ptr<const BotView> view;
        std::shared_ptr<const BotHistory> history;
        uint32 version = 0;                     // view or history changed
        std::vector<BotSwitch> switches;
    };

    // never destroyed: bots may still log out while the process shuts down
    Hub* hub = new Hub();

    constexpr uint32 listingIntervalMs = 1000;
    constexpr uint32 viewIntervalMs = 250;
    constexpr uint32 historyIntervalMs = 1000;
    constexpr uint32 listingTimeoutMs = 30000;   // a bot that stopped updating leaves the drop-down

    const char* ClassName(uint8 cls)
    {
        static const char* const names[] = { "", "Warrior", "Paladin", "Hunter", "Rogue", "Priest", "Death Knight",
            "Shaman", "Mage", "Warlock", "Monk", "Druid" };
        return cls < sizeof(names) / sizeof(names[0]) ? names[cls] : "";
    }
}

// per bot timers of the publishing (PlayerbotAI members, read and written on the bot's update only)
static bool Due(uint32& lastMs, uint32 now, uint32 intervalMs)
{
    if (lastMs && WorldTimer::getMSTimeDiff(lastMs, now) < intervalMs)
        return false;
    lastMs = now ? now : 1;
    return true;
}

void PlayerbotDebugWindow::OnBotUpdate(PlayerbotAI* ai)
{
    if (!hub->running)
        return;

    Player* bot = ai->GetBot();
    if (!bot || !bot->IsInWorld())
        return;

    const uint32 now = WorldTimer::getMSTime();
    const uint64 guid = bot->GetObjectGuid().GetRawValue();

    if (Due(ai->debugWindowListedMs, now, listingIntervalMs))
    {
        BotListing listing;
        listing.name = bot->GetName();
        listing.level = bot->GetLevel();
        listing.cls = bot->getClass();
        if (const AreaTableEntry* area = GetAreaEntryByAreaID(bot->GetZoneId()))
            listing.zone = area->area_name[0];
        else if (const MapEntry* map = sMapStore.LookupEntry(bot->GetMapId()))
            listing.zone = map->name[0];
        listing.seenMs = now;

        std::lock_guard<std::mutex> guard(hub->lock);
        hub->bots[guid] = std::move(listing);
    }

    if (hub->selected != guid)
        return;

    // the window's switches of this bot: through the addon's queue (ApplyStateTreeToggles, next update)
    {
        std::lock_guard<std::mutex> guard(hub->lock);
        if (!hub->switches.empty())
        {
            std::lock_guard<std::mutex> toggleGuard(ai->stateTreeToggleLock);
            for (const BotSwitch& item : hub->switches)
            {
                if (item.guid != guid)
                    continue;
                PlayerbotAI::StateTreeToggle toggle;
                toggle.what = item.what;
                toggle.state = item.state;
                toggle.tree = item.tree;
                toggle.node = item.node;
                toggle.index = item.index;
                toggle.value = item.value;
                ai->stateTreeToggles.push_back(toggle);
            }
            hub->switches.clear();
        }
    }

    if (!hub->publishing)
        return;

    std::shared_ptr<BotView> view;
    if (Due(ai->debugWindowViewMs, now, viewIntervalMs))
    {
        view = std::make_shared<BotView>();
        view->guid = guid;
        view->takenMs = now;
        ai->StateMachine.GetDebugView(view->machine);

        ai::PlayerbotMovement& movement = ai->GetMovement();
        const ai::MovementGoalRequest& goal = movement.GetActive();
        const ai::STAI_Location& destination = movement.GetDestination();
        view->goal = goal.name;
        view->goalSource = goal.source;
        view->moveState = movement.GetStateName();
        view->priority = goal.priority;
        view->distance = destination.valid && destination.mapId == bot->GetMapId() ?
            bot->GetDistance(destination.x, destination.y, destination.z) : -1.0f;
    }

    std::shared_ptr<BotHistory> history;
    if (Due(ai->debugWindowHistoryMs, now, historyIntervalMs))
    {
        history = std::make_shared<BotHistory>();
        history->guid = guid;
        history->takenMs = now;
        const std::deque<STAI_ActivityLog::Entry>& entries = ai->StateMachine.GetActivityLog().GetEntries();
        history->entries.assign(entries.begin(), entries.end());
    }

    if (view || history)
    {
        std::lock_guard<std::mutex> guard(hub->lock);
        if (view)
            hub->view = std::move(view);
        if (history)
            hub->history = std::move(history);
        ++hub->version;
    }
}

void PlayerbotDebugWindow::OnBotRemoved(uint64 guid)
{
    if (!hub->running)
        return;

    std::lock_guard<std::mutex> guard(hub->lock);
    hub->bots.erase(guid);
}

//------------------------------------------------------------------------
// the window (its own thread)
//------------------------------------------------------------------------

namespace
{
    constexpr UINT WM_APP_QUIT = WM_APP + 1;
    constexpr UINT WM_APP_ICON = WM_APP + 2;

    enum ControlId : int
    {
        ID_COMBO = 100,
        ID_PAUSE,
        ID_INACTIVE,
        ID_FREEZE,
        ID_TAB_STATES,
        ID_TAB_HISTORY,
    };

    // the addon's colors
    namespace Color
    {
        const COLORREF background = RGB(20, 20, 20);
        const COLORREF canvas = RGB(26, 26, 26);
        const COLORREF rootFill = RGB(31, 92, 102);
        const COLORREF rootBorder = RGB(242, 89, 51);
        const COLORREF activeFill = RGB(41, 158, 168);
        const COLORREF activeBorder = RGB(115, 255, 140);
        const COLORREF childFill = RGB(23, 77, 87);
        const COLORREF stateBorder = RGB(10, 46, 51);
        const COLORREF taskFill = RGB(38, 69, 102);
        const COLORREF taskBorder = RGB(18, 33, 51);
        const COLORREF line = RGB(140, 140, 140);
        const COLORREF white = RGB(255, 255, 255);
        const COLORREF text = RGB(230, 230, 230);
        const COLORREF light = RGB(208, 208, 208);
        const COLORREF dim = RGB(176, 176, 176);
        const COLORREF gray = RGB(160, 160, 160);
        const COLORREF dark = RGB(128, 128, 128);
        const COLORREF darker = RGB(112, 112, 112);
        const COLORREF green = RGB(64, 255, 96);
        const COLORREF brightGreen = RGB(96, 255, 128);
        const COLORREF red = RGB(255, 64, 64);
        const COLORREF lightRed = RGB(255, 128, 128);
        const COLORREF darkRed = RGB(128, 48, 48);
        const COLORREF yellow = RGB(255, 210, 0);
        const COLORREF spell = RGB(113, 213, 255);
        const COLORREF global = RGB(208, 160, 255);
        const COLORREF task = RGB(159, 208, 255);
        const COLORREF marker = RGB(192, 224, 224);
        const COLORREF gold = RGB(255, 210, 0);
    }

    std::wstring Wide(const std::string& text)
    {
        if (text.empty())
            return std::wstring();
        const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
        std::wstring result(size, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), result.data(), size);
        return result;
    }

    std::wstring FormatSeconds(uint32 ms)
    {
        wchar_t buffer[64];
        const uint32 seconds = ms / 1000;
        if (seconds < 60)
            swprintf(buffer, 64, L"%.1fs", ms / 1000.0);
        else if (seconds < 3600)
            swprintf(buffer, 64, L"%um %02us", seconds / 60, seconds % 60);
        else
            swprintf(buffer, 64, L"%uh %02um", seconds / 3600, seconds / 60 % 60);
        return buffer;
    }

    std::wstring FormatDuration(uint32 ms)
    {
        if (ms < 1000)
            return std::to_wstring(ms) + L"ms";
        return FormatSeconds(ms);
    }

    std::wstring SpellTooltip(uint32 spellId)
    {
        std::wstring text;
        if (const SpellEntry* spell = sSpellTemplate.LookupEntry<SpellEntry>(spellId))
        {
            text = Wide(spell->SpellName[0] ? spell->SpellName[0] : "");
            if (spell->Rank[0] && *spell->Rank[0])
                text += L" (" + Wide(spell->Rank[0]) + L")";
        }
        if (text.empty())
            text = L"spell";
        return text + L"\nid " + std::to_wstring(spellId);
    }

    std::wstring SpellName(uint32 spellId)
    {
        if (const SpellEntry* spell = sSpellTemplate.LookupEntry<SpellEntry>(spellId))
            if (spell->SpellName[0] && *spell->SpellName[0])
                return Wide(spell->SpellName[0]);
        return L"spell " + std::to_wstring(spellId);
    }

    // a piece of a text line: text in a color, or a spell's icon
    struct TextRun
    {
        std::wstring text;
        COLORREF color = Color::text;
        uint32 spell = 0;               // an icon instead of text
        bool bold = false;
    };
    using Runs = std::vector<TextRun>;

    // {spell:<id>} in a task's info -> the spell's icon and name
    void AppendMarkup(Runs& runs, const std::string& text, COLORREF color)
    {
        size_t start = 0;
        while (start < text.size())
        {
            const size_t open = text.find("{spell:", start);
            const size_t close = open == std::string::npos ? std::string::npos : text.find('}', open);
            if (close == std::string::npos)
            {
                runs.push_back({ Wide(text.substr(start)), color });
                return;
            }
            if (open > start)
                runs.push_back({ Wide(text.substr(start, open - start)), color });
            const uint32 spellId = uint32(strtoul(text.c_str() + open + 7, nullptr, 10));
            runs.push_back({ L"", color, spellId });
            runs.push_back({ SpellName(spellId), Color::spell });
            start = close + 1;
        }
    }

    // a clickable / hoverable area of the canvas, in client pixels
    struct Hit
    {
        RECT rect;
        std::function<void()> click;
        std::function<void()> doubleClick;
        std::wstring tooltip;
    };

    class DebugWindow
    {
    public:
        void Run();

        HWND main = nullptr;

    private:
        static LRESULT CALLBACK MainProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
        static LRESULT CALLBACK CanvasProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
        LRESULT OnMain(UINT message, WPARAM wParam, LPARAM lParam);
        LRESULT OnCanvas(UINT message, WPARAM wParam, LPARAM lParam);

        void Create();
        void Layout();
        void OnTimer();
        void RefreshBots();
        void SelectBot(uint64 guid);
        void UpdateScroll();
        void ScrollTo(int y);
        void PaintCanvas();
        void PaintStatus(HDC dc);

        // drawing in logical units (scaled by the screen's dpi), y relative to the canvas content
        int S(int value) const { return MulDiv(value, dpi, 96); }
        int Dev(int x) const { return S(x); }
        int DevY(int y) const { return S(y - scrollY); }
        int TextWidth(const std::wstring& text, HFONT font);
        int RunsWidth(const Runs& runs, int iconSize = 14);
        int DrawLabel(int x, int y, const std::wstring& text, COLORREF color, HFONT font = nullptr);
        int DrawRuns(int x, int y, const Runs& runs, int iconSize = 14);
        int DrawWrapped(int x, int y, int width, const std::wstring& text, COLORREF color);
        void Fill(int x, int y, int width, int height, COLORREF color);
        void Frame(int x, int y, int width, int height, COLORREF color, int thickness = 1);
        int SpellIcon(int x, int y, uint32 spellId, int size);
        int SpellIcons(int x, int y, const std::vector<uint32>& spells, int size);
        RECT Box(int x, int y, const Runs& runs, COLORREF fill, COLORREF border, int height, bool glow = false);
        void Toggle(int x, int y, bool disabled, const wchar_t* what, std::function<void()> click);
        void Override(int x, int y, int value, std::function<void()> click);
        void AddHit(int x, int y, int width, int height, std::function<void()> click, std::function<void()> doubleClick = nullptr,
            std::wstring tooltip = L"");

        void RenderMessage(const std::wstring& text);
        void RenderStates();
        int RenderState(const StateMachineDebugView::State& state, int y);
        int RenderSubtree(const StateNodeDebugTree& tree, int x, int y, const std::string& keyPrefix);
        void RenderStack(const std::vector<const StateMachineDebugView::State*>& states);
        void RenderHistory();

        // switches: the snapshot's value, or a click not in a snapshot yet (3s at most)
        std::string SwitchKey(char kind, const std::string& state, const std::string& tree, int node, int index) const;
        int SwitchValue(const std::string& key, int reported);
        void SendSwitch(char kind, StateTreeSwitch what, const std::string& state, const std::string& tree, int node, int index, int value);

        HWND combo = nullptr, pauseCheck = nullptr, inactiveCheck = nullptr, freezeCheck = nullptr;
        HWND statesTab = nullptr, historyTab = nullptr, canvas = nullptr;
        HFONT font = nullptr, bold = nullptr, smallFont = nullptr, controlFont = nullptr;
        HBRUSH backgroundBrush = nullptr;
        int dpi = 96;

        std::vector<uint64> comboGuids;
        std::vector<std::wstring> comboTexts;
        ULONGLONG lastBotRefresh = 0;
        size_t botCount = 0;
        bool botActive = false;

        uint64 selected = 0;
        bool paused = false, showInactive = false, freeze = false, historyMode = false;
        std::shared_ptr<const BotView> view;
        std::shared_ptr<const BotHistory> history;
        uint32 version = 0;
        ULONGLONG viewTick = 0, historyTick = 0;    // GetTickCount64 when they arrived

        std::map<std::string, bool> expanded;       // "<tree>/<node>": set by double click (or kept with freeze)
        std::string lastActive;                     // active state of the last view: nodes collapse when it changes
        std::map<std::string, std::pair<int, ULONGLONG>> pending;

        // painting
        HDC dc = nullptr;
        int scrollY = 0, contentHeight = 0, viewHeight = 0, viewWidth = 0;
        int leftX = 0;                              // the canvas content starts here (right of the tree stack)
        bool inStack = false;                       // drawing the fixed tree stack panel (not scrolled)
        std::vector<Hit> hits;
        std::wstring hoverTooltip;
        POINT hoverPoint{};
        bool tracking = false;

        PlayerbotDebugIcons icons;
    };

    DebugWindow* window = nullptr;
    std::thread windowThread;
    std::atomic<HWND> windowHandle{ nullptr };     // for Stop, from another thread

    LRESULT CALLBACK DebugWindow::MainProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (!window->main)
            window->main = hwnd;    // WM_GETMINMAXINFO comes before WM_NCCREATE
        return window->OnMain(message, wParam, lParam);
    }

    LRESULT CALLBACK DebugWindow::CanvasProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (!window->canvas)
            window->canvas = hwnd;
        return window->OnCanvas(message, wParam, lParam);
    }

    void DebugWindow::Run()
    {
        // crisp text on scaled screens (Windows 10 1607+, looked up: the SDK may not declare it for this target)
        using SetDpiContext = HANDLE(WINAPI*)(HANDLE);
        if (auto setDpiContext = (SetDpiContext)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetThreadDpiAwarenessContext"))
            setDpiContext((HANDLE)-2);  // DPI_AWARENESS_CONTEXT_SYSTEM_AWARE
        Create();
        icons.Start(canvas, WM_APP_ICON);
        SetTimer(main, 1, 250, nullptr);

        MSG message;
        while (GetMessageW(&message, nullptr, 0, 0) > 0)
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        icons.Stop();
        for (HGDIOBJ object : { (HGDIOBJ)font, (HGDIOBJ)bold, (HGDIOBJ)smallFont, (HGDIOBJ)controlFont, (HGDIOBJ)backgroundBrush })
            DeleteObject(object);
    }

    void DebugWindow::Create()
    {
        HINSTANCE instance = GetModuleHandleW(nullptr);

        HDC screen = GetDC(nullptr);
        dpi = GetDeviceCaps(screen, LOGPIXELSY);
        ReleaseDC(nullptr, screen);

        auto makeFont = [this](int points, int weight)
        {
            return CreateFontW(-MulDiv(points, dpi, 72), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        };
        font = makeFont(9, FW_NORMAL);
        bold = makeFont(9, FW_SEMIBOLD);
        smallFont = makeFont(8, FW_NORMAL);
        controlFont = makeFont(9, FW_NORMAL);
        backgroundBrush = CreateSolidBrush(Color::background);

        WNDCLASSEXW mainClass = { sizeof(mainClass) };
        mainClass.lpfnWndProc = MainProc;
        mainClass.hInstance = instance;
        mainClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
        mainClass.hbrBackground = backgroundBrush;
        mainClass.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
        mainClass.lpszClassName = L"MangosBotStateTree";
        RegisterClassExW(&mainClass);

        WNDCLASSEXW canvasClass = { sizeof(canvasClass) };
        canvasClass.style = CS_DBLCLKS;
        canvasClass.lpfnWndProc = CanvasProc;
        canvasClass.hInstance = instance;
        canvasClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
        canvasClass.lpszClassName = L"MangosBotStateTreeCanvas";
        RegisterClassExW(&canvasClass);

        CreateWindowExW(0, mainClass.lpszClassName, L"Bot State Tree - mangosd", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
            CW_USEDEFAULT, CW_USEDEFAULT, S(1000), S(780), nullptr, nullptr, instance, nullptr);

        auto control = [&](const wchar_t* className, const wchar_t* text, DWORD style, int id)
        {
            HWND child = CreateWindowExW(0, className, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, main, (HMENU)(INT_PTR)id, instance, nullptr);
            SendMessageW(child, WM_SETFONT, (WPARAM)controlFont, TRUE);
            return child;
        };
        combo = control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, ID_COMBO);
        pauseCheck = control(L"BUTTON", L"Pause", BS_AUTOCHECKBOX, ID_PAUSE);
        inactiveCheck = control(L"BUTTON", L"Show inactive StateTrees", BS_AUTOCHECKBOX, ID_INACTIVE);
        freezeCheck = control(L"BUTTON", L"dont expand/collapse", BS_AUTOCHECKBOX, ID_FREEZE);
        statesTab = control(L"BUTTON", L"States", BS_AUTORADIOBUTTON | BS_PUSHLIKE | WS_GROUP, ID_TAB_STATES);
        historyTab = control(L"BUTTON", L"History", BS_AUTORADIOBUTTON | BS_PUSHLIKE, ID_TAB_HISTORY);
        SendMessageW(statesTab, BM_SETCHECK, BST_CHECKED, 0);

        CreateWindowExW(0, canvasClass.lpszClassName, L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL, 0, 0, 10, 10, main, nullptr, instance, nullptr);

        Layout();
        ShowWindow(main, SW_SHOWNOACTIVATE);
        windowHandle = main;
        hub->publishing = true;
    }

    void DebugWindow::Layout()
    {
        RECT client;
        GetClientRect(main, &client);
        const int width = client.right;
        int x = S(8);
        MoveWindow(combo, x, S(8), S(340), S(400), TRUE);
        x += S(352);
        MoveWindow(pauseCheck, x, S(10), S(70), S(20), TRUE);
        x += S(76);
        MoveWindow(inactiveCheck, x, S(10), S(180), S(20), TRUE);
        x += S(186);
        MoveWindow(freezeCheck, x, S(10), S(170), S(20), TRUE);
        MoveWindow(statesTab, S(8), S(40), S(96), S(24), TRUE);
        MoveWindow(historyTab, S(106), S(40), S(96), S(24), TRUE);
        MoveWindow(canvas, S(8), S(70), (std::max)(width - S(16), 10), (std::max)(int(client.bottom) - S(70) - S(24), 10), TRUE);
        InvalidateRect(main, nullptr, TRUE);
    }

    LRESULT DebugWindow::OnMain(UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
            case WM_SIZE:
                if (canvas)
                    Layout();
                hub->publishing = wParam != SIZE_MINIMIZED && !paused;
                return 0;
            case WM_GETMINMAXINFO:
                reinterpret_cast<MINMAXINFO*>(lParam)->ptMinTrackSize = { S(640), S(360) };
                return 0;
            case WM_TIMER:
                OnTimer();
                return 0;
            case WM_CTLCOLORSTATIC:
            case WM_CTLCOLORBTN:
                SetTextColor((HDC)wParam, Color::light);
                SetBkColor((HDC)wParam, Color::background);
                return (LRESULT)backgroundBrush;
            case WM_MOUSEWHEEL:
                return SendMessageW(canvas, message, wParam, lParam);
            case WM_COMMAND:
            {
                const int id = LOWORD(wParam);
                if (id == ID_COMBO && HIWORD(wParam) == CBN_SELCHANGE)
                {
                    const LRESULT index = SendMessageW(combo, CB_GETCURSEL, 0, 0);
                    if (index >= 0 && size_t(index) < comboGuids.size())
                        SelectBot(comboGuids[index]);
                    SetFocus(canvas);   // the wheel scrolls the canvas, not the drop-down
                }
                else if (id == ID_PAUSE)
                {
                    paused = SendMessageW(pauseCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
                    hub->publishing = !paused && !IsIconic(main);
                }
                else if (id == ID_INACTIVE)
                    showInactive = SendMessageW(inactiveCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
                else if (id == ID_FREEZE)
                    freeze = SendMessageW(freezeCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
                else if (id == ID_TAB_STATES || id == ID_TAB_HISTORY)
                {
                    historyMode = id == ID_TAB_HISTORY;
                    scrollY = 0;
                }
                if (id != ID_COMBO)
                    SetFocus(canvas);
                InvalidateRect(canvas, nullptr, FALSE);
                return 0;
            }
            case WM_PAINT:
            {
                PAINTSTRUCT paint;
                HDC paintDc = BeginPaint(main, &paint);
                PaintStatus(paintDc);
                EndPaint(main, &paint);
                return 0;
            }
            case WM_CLOSE:
                // the window lives as long as the server: closing minimizes it
                ShowWindow(main, SW_MINIMIZE);
                return 0;
            case WM_APP_QUIT:
                DestroyWindow(main);
                return 0;
            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;
        }
        return DefWindowProcW(main, message, wParam, lParam);
    }

    void DebugWindow::PaintStatus(HDC paintDc)
    {
        RECT client;
        GetClientRect(main, &client);
        RECT bar = { 0, client.bottom - S(22), client.right, client.bottom };
        FillRect(paintDc, &bar, backgroundBrush);

        std::wstring text;
        if (paused)
            text = L"Paused";
        else
            text = L"Live every 0.25s";
        const ULONGLONG received = historyMode ? historyTick : viewTick;
        if (received && (historyMode ? history != nullptr : view != nullptr))
            text += L"  \u00B7  updated " + FormatSeconds(uint32(GetTickCount64() - received)) + L" ago";
        if (selected && !botActive)
            text += L"  \u00B7  the bot is not active any more";
        text += L"  \u00B7  " + std::to_wstring(botCount) + L" active bots";
        if (historyMode)
            text += L"  \u00B7  history kept " + std::to_wstring(STAI_ActivityLog::GetRetention() / 60000) + L" min";

        SelectObject(paintDc, smallFont);
        SetBkMode(paintDc, TRANSPARENT);
        SetTextColor(paintDc, Color::gray);
        TextOutW(paintDc, S(10), bar.top + S(4), text.c_str(), int(text.size()));
    }

    void DebugWindow::OnTimer()
    {
        const ULONGLONG now = GetTickCount64();
        if (now - lastBotRefresh >= 1000)
        {
            lastBotRefresh = now;
            RefreshBots();
        }

        {
            std::lock_guard<std::mutex> guard(hub->lock);
            if (hub->version != version && !paused)
            {
                version = hub->version;
                if (hub->view && hub->view->guid == selected && hub->view != view)
                {
                    view = hub->view;
                    viewTick = now;
                }
                if (hub->history && hub->history->guid == selected && hub->history != history)
                {
                    history = hub->history;
                    historyTick = now;
                }
            }
        }

        if (!IsIconic(main))
        {
            InvalidateRect(canvas, nullptr, FALSE);
            RECT client;
            GetClientRect(main, &client);
            RECT bar = { 0, client.bottom - S(22), client.right, client.bottom };
            InvalidateRect(main, &bar, FALSE);
        }
    }

    // the drop-down: every bot listed in the last 30s, by name; rebuilt only when it changed (and is closed)
    void DebugWindow::RefreshBots()
    {
        std::vector<std::pair<std::wstring, uint64>> list;
        {
            std::lock_guard<std::mutex> guard(hub->lock);
            const uint32 nowMs = WorldTimer::getMSTime();
            for (auto it = hub->bots.begin(); it != hub->bots.end();)
            {
                if (WorldTimer::getMSTimeDiff(it->second.seenMs, nowMs) > listingTimeoutMs)
                {
                    it = hub->bots.erase(it);
                    continue;
                }
                const BotListing& bot = it->second;
                std::wstring text = Wide(bot.name) + L"   (" + std::to_wstring(bot.level) + L" " + Wide(ClassName(bot.cls));
                if (!bot.zone.empty())
                    text += L", " + Wide(bot.zone);
                text += L")";
                list.emplace_back(std::move(text), it->first);
                ++it;
            }
        }
        std::sort(list.begin(), list.end());

        botCount = list.size();
        botActive = std::any_of(list.begin(), list.end(), [this](const auto& item) { return item.second == selected; });

        std::vector<std::wstring> texts;
        std::vector<uint64> guids;
        for (const auto& [text, guid] : list)
        {
            texts.push_back(text);
            guids.push_back(guid);
        }
        if (texts == comboTexts || SendMessageW(combo, CB_GETDROPPEDSTATE, 0, 0))
            return;

        comboTexts = std::move(texts);
        comboGuids = std::move(guids);
        SendMessageW(combo, WM_SETREDRAW, FALSE, 0);
        SendMessageW(combo, CB_RESETCONTENT, 0, 0);
        int selection = -1;
        for (size_t i = 0; i < comboTexts.size(); ++i)
        {
            SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)comboTexts[i].c_str());
            if (comboGuids[i] == selected)
                selection = int(i);
        }
        SendMessageW(combo, CB_SETCURSEL, selection, 0);
        SendMessageW(combo, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(combo, nullptr, TRUE);
    }

    void DebugWindow::SelectBot(uint64 guid)
    {
        if (guid == selected)
            return;
        selected = guid;
        hub->selected = guid;
        view.reset();
        history.reset();
        expanded.clear();
        pending.clear();
        lastActive.clear();
        scrollY = 0;
        botActive = true;
        InvalidateRect(canvas, nullptr, FALSE);
    }

    LRESULT DebugWindow::OnCanvas(UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
            case WM_PAINT:
                PaintCanvas();
                return 0;
            case WM_ERASEBKGND:
                return 1;
            case WM_SIZE:
                UpdateScroll();
                InvalidateRect(canvas, nullptr, FALSE);
                return 0;
            case WM_APP_ICON:
                InvalidateRect(canvas, nullptr, FALSE);
                return 0;
            case WM_MOUSEWHEEL:
                ScrollTo(scrollY - GET_WHEEL_DELTA_WPARAM(wParam) * 60 / WHEEL_DELTA);
                return 0;
            case WM_VSCROLL:
            {
                SCROLLINFO info = { sizeof(info), SIF_ALL };
                GetScrollInfo(canvas, SB_VERT, &info);
                int y = scrollY;
                switch (LOWORD(wParam))
                {
                    case SB_LINEUP: y -= 20; break;
                    case SB_LINEDOWN: y += 20; break;
                    case SB_PAGEUP: y -= int(info.nPage); break;
                    case SB_PAGEDOWN: y += int(info.nPage); break;
                    case SB_THUMBTRACK:
                    case SB_THUMBPOSITION: y = info.nTrackPos; break;
                    case SB_TOP: y = 0; break;
                    case SB_BOTTOM: y = contentHeight; break;
                }
                ScrollTo(y);
                return 0;
            }
            case WM_MOUSEMOVE:
            {
                if (!tracking)
                {
                    TRACKMOUSEEVENT track = { sizeof(track), TME_LEAVE, canvas, 0 };
                    tracking = TrackMouseEvent(&track) != FALSE;
                }
                POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                std::wstring tooltip;
                for (auto it = hits.rbegin(); it != hits.rend(); ++it)
                    if (!it->tooltip.empty() && PtInRect(&it->rect, point))
                    {
                        tooltip = it->tooltip;
                        break;
                    }
                if (tooltip != hoverTooltip || (!tooltip.empty() && (point.x != hoverPoint.x || point.y != hoverPoint.y)))
                {
                    hoverTooltip = tooltip;
                    hoverPoint = point;
                    InvalidateRect(canvas, nullptr, FALSE);
                }
                return 0;
            }
            case WM_MOUSELEAVE:
                tracking = false;
                if (!hoverTooltip.empty())
                {
                    hoverTooltip.clear();
                    InvalidateRect(canvas, nullptr, FALSE);
                }
                return 0;
            case WM_LBUTTONDOWN:
            case WM_LBUTTONDBLCLK:
            {
                SetFocus(canvas);
                POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                for (auto it = hits.rbegin(); it != hits.rend(); ++it)
                {
                    if (!PtInRect(&it->rect, point))
                        continue;
                    // a double click on a switch is not a second click
                    std::function<void()> action = message == WM_LBUTTONDBLCLK ? it->doubleClick : it->click;
                    if (!action && !it->click && !it->doubleClick)
                        continue;
                    if (action)
                    {
                        action();
                        InvalidateRect(canvas, nullptr, FALSE);
                    }
                    break;
                }
                return 0;
            }
        }
        return DefWindowProcW(canvas, message, wParam, lParam);
    }

    void DebugWindow::UpdateScroll()
    {
        RECT client;
        GetClientRect(canvas, &client);
        viewHeight = MulDiv(client.bottom, 96, dpi);
        viewWidth = MulDiv(client.right, 96, dpi);
        scrollY = (std::max)(0, (std::min)(scrollY, contentHeight - viewHeight));

        SCROLLINFO info = { sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS };
        info.nMin = 0;
        info.nMax = (std::max)(contentHeight - 1, 0);
        info.nPage = UINT((std::max)(viewHeight, 0));
        info.nPos = scrollY;
        SetScrollInfo(canvas, SB_VERT, &info, TRUE);
    }

    void DebugWindow::ScrollTo(int y)
    {
        y = (std::max)(0, (std::min)(y, contentHeight - viewHeight));
        if (y == scrollY)
            return;
        scrollY = y;
        UpdateScroll();
        InvalidateRect(canvas, nullptr, FALSE);
    }

    void DebugWindow::PaintCanvas()
    {
        PAINTSTRUCT paint;
        HDC paintDc = BeginPaint(canvas, &paint);
        RECT client;
        GetClientRect(canvas, &client);
        viewHeight = MulDiv(client.bottom, 96, dpi);
        viewWidth = MulDiv(client.right, 96, dpi);

        HDC memory = CreateCompatibleDC(paintDc);
        HBITMAP bitmap = CreateCompatibleBitmap(paintDc, (std::max)(int(client.right), 1), (std::max)(int(client.bottom), 1));
        HGDIOBJ oldBitmap = SelectObject(memory, bitmap);
        dc = memory;
        SetBkMode(dc, TRANSPARENT);
        HBRUSH canvasBrush = CreateSolidBrush(Color::canvas);
        FillRect(dc, &client, canvasBrush);
        DeleteObject(canvasBrush);

        hits.clear();
        const int oldHeight = contentHeight;
        if (!selected)
            RenderMessage(L"Select a bot in the drop-down above.");
        else if (historyMode)
            RenderHistory();
        else
            RenderStates();

        // the hovered spell / switch
        if (!hoverTooltip.empty())
        {
            SelectObject(dc, smallFont);
            RECT size = { 0, 0, S(400), 0 };
            ::DrawTextW(dc, hoverTooltip.c_str(), -1, &size, DT_CALCRECT | DT_NOPREFIX | DT_WORDBREAK);
            RECT box = { hoverPoint.x + S(14), hoverPoint.y + S(16), 0, 0 };
            box.right = box.left + size.right + S(12);
            box.bottom = box.top + size.bottom + S(8);
            if (box.right > client.right)
                OffsetRect(&box, client.right - box.right - S(2), 0);
            if (box.bottom > client.bottom)
                OffsetRect(&box, 0, hoverPoint.y - box.bottom - S(4));
            HBRUSH tipBrush = CreateSolidBrush(RGB(12, 12, 16));
            FillRect(dc, &box, tipBrush);
            DeleteObject(tipBrush);
            HBRUSH tipBorder = CreateSolidBrush(RGB(90, 90, 110));
            FrameRect(dc, &box, tipBorder);
            DeleteObject(tipBorder);
            RECT textRect = { box.left + S(6), box.top + S(4), box.right - S(6), box.bottom - S(4) };
            SetTextColor(dc, Color::white);
            ::DrawTextW(dc, hoverTooltip.c_str(), -1, &textRect, DT_NOPREFIX | DT_WORDBREAK);
        }

        BitBlt(paintDc, 0, 0, client.right, client.bottom, memory, 0, 0, SRCCOPY);
        SelectObject(memory, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(memory);
        dc = nullptr;
        EndPaint(canvas, &paint);

        if (contentHeight != oldHeight)
            UpdateScroll();
    }

    //--------------------------------------------------------------------
    // drawing helpers
    //--------------------------------------------------------------------

    int DebugWindow::TextWidth(const std::wstring& text, HFONT textFont)
    {
        SelectObject(dc, textFont ? textFont : font);
        SIZE size = {};
        GetTextExtentPoint32W(dc, text.c_str(), int(text.size()), &size);
        return MulDiv(size.cx, 96, dpi);
    }

    int DebugWindow::RunsWidth(const Runs& runs, int iconSize)
    {
        int width = 0;
        for (const TextRun& run : runs)
            width += run.spell ? iconSize + 3 : TextWidth(run.text, run.bold ? bold : font);
        return width;
    }

    int DebugWindow::DrawLabel(int x, int y, const std::wstring& text, COLORREF color, HFONT textFont)
    {
        SelectObject(dc, textFont ? textFont : font);
        SetTextColor(dc, color);
        const int devY = inStack ? S(y) : DevY(y);
        TextOutW(dc, Dev(x), devY, text.c_str(), int(text.size()));
        return x + TextWidth(text, textFont);
    }

    int DebugWindow::DrawRuns(int x, int y, const Runs& runs, int iconSize)
    {
        for (const TextRun& run : runs)
        {
            if (run.spell)
                x = SpellIcon(x, y, run.spell, iconSize) + 1;
            else
                x = DrawLabel(x, y, run.text, run.color, run.bold ? bold : font);
        }
        return x;
    }

    // wrapped at width: the y below it
    int DebugWindow::DrawWrapped(int x, int y, int width, const std::wstring& text, COLORREF color)
    {
        SelectObject(dc, font);
        SetTextColor(dc, color);
        RECT rect = { Dev(x), DevY(y), Dev(x + width), DevY(y) + S(2000) };
        RECT measure = rect;
        ::DrawTextW(dc, text.c_str(), int(text.size()), &measure, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        ::DrawTextW(dc, text.c_str(), int(text.size()), &rect, DT_WORDBREAK | DT_NOPREFIX);
        return y + (std::max)(15, MulDiv(measure.bottom - measure.top, 96, dpi) + 1);
    }

    void DebugWindow::Fill(int x, int y, int width, int height, COLORREF color)
    {
        const int top = inStack ? S(y) : DevY(y);
        RECT rect = { Dev(x), top, Dev(x) + (std::max)(S(width), 1), top + (std::max)(S(height), 1) };
        HBRUSH brush = CreateSolidBrush(color);
        FillRect(dc, &rect, brush);
        DeleteObject(brush);
    }

    void DebugWindow::Frame(int x, int y, int width, int height, COLORREF color, int thickness)
    {
        Fill(x, y, width, thickness, color);
        Fill(x, y + height - thickness, width, thickness, color);
        Fill(x, y, thickness, height, color);
        Fill(x + width - thickness, y, thickness, height, color);
    }

    void DebugWindow::AddHit(int x, int y, int width, int height, std::function<void()> click, std::function<void()> doubleClick,
        std::wstring tooltip)
    {
        const int top = inStack ? S(y) : DevY(y);
        Hit hit;
        hit.rect = { Dev(x), top, Dev(x + width), top + S(height) };
        hit.click = std::move(click);
        hit.doubleClick = std::move(doubleClick);
        hit.tooltip = std::move(tooltip);
        hits.push_back(std::move(hit));
    }

    // the icon (or a ? while it is downloaded / unknown) with the spell's name on hover; the x right of it
    int DebugWindow::SpellIcon(int x, int y, uint32 spellId, int size)
    {
        const int top = inStack ? S(y) : DevY(y);
        if (top > -S(size) && top < S(viewHeight))
        {
            if (!icons.Draw(dc, Dev(x), top, S(size), spellId))
            {
                Fill(x, y, size, size, RGB(50, 50, 60));
                Frame(x, y, size, size, RGB(90, 90, 110));
                SelectObject(dc, smallFont);
                SetTextColor(dc, Color::gray);
                RECT rect = { Dev(x), top, Dev(x) + S(size), top + S(size) };
                ::DrawTextW(dc, L"?", 1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
        }
        AddHit(x, y, size, size, nullptr, nullptr, SpellTooltip(spellId));
        return x + size + 2;
    }

    int DebugWindow::SpellIcons(int x, int y, const std::vector<uint32>& spells, int size)
    {
        for (uint32 spellId : spells)
            if (spellId)
                x = SpellIcon(x, y, spellId, size);
        return x;
    }

    RECT DebugWindow::Box(int x, int y, const Runs& runs, COLORREF fill, COLORREF border, int height, bool glow)
    {
        const int width = (std::max)(RunsWidth(runs) + 16, 60);
        if (glow)
            Frame(x - 3, y - 3, width + 6, height + 6, RGB(40, 110, 60), 2);
        Fill(x, y, width, height, fill);
        Frame(x, y, width, height, border);
        DrawRuns(x + 6, y + (height - 15) / 2, runs);
        return { x, y, x + width, y + height };
    }

    // check / cross right of a task or a node: check runs, cross is switched off
    void DebugWindow::Toggle(int x, int y, bool disabled, const wchar_t* what, std::function<void()> click)
    {
        const int size = 16;
        Fill(x, y, size, size, Color::taskFill);
        Frame(x, y, size, size, disabled ? RGB(153, 38, 38) : Color::taskBorder);

        const int top = DevY(y);
        HPEN pen = CreatePen(PS_SOLID, (std::max)(S(2), 1), disabled ? RGB(230, 60, 60) : RGB(80, 220, 90));
        HGDIOBJ oldPen = SelectObject(dc, pen);
        if (disabled)
        {
            MoveToEx(dc, Dev(x + 4), top + S(4), nullptr);
            LineTo(dc, Dev(x + 12), top + S(12));
            MoveToEx(dc, Dev(x + 12), top + S(4), nullptr);
            LineTo(dc, Dev(x + 4), top + S(12));
        }
        else
        {
            MoveToEx(dc, Dev(x + 3), top + S(8), nullptr);
            LineTo(dc, Dev(x + 7), top + S(12));
            LineTo(dc, Dev(x + 13), top + S(4));
        }
        SelectObject(dc, oldPen);
        DeleteObject(pen);

        std::wstring tooltip = disabled ? std::wstring(L"Switched off: the bot skips this ") + what : std::wstring(L"Runs; click to switch it off");
        tooltip += L"\nRuntime only: reset when the bot despawns or the server restarts";
        AddHit(x, y, size, size, std::move(click), nullptr, tooltip);
    }

    // square left of a condition: blank checked normally, green always passes, red always fails; click cycles
    void DebugWindow::Override(int x, int y, int value, std::function<void()> click)
    {
        const int size = 11;
        if (value == 1)
            Fill(x, y, size, size, RGB(51, 204, 77));
        else if (value == 2)
            Fill(x, y, size, size, RGB(204, 38, 38));
        Frame(x, y, size, size, value == 1 ? Color::activeBorder : value == 2 ? RGB(255, 102, 102) : RGB(140, 140, 140));
        const wchar_t* text = value == 1 ? L"Forced to pass" : value == 2 ? L"Forced to fail" : L"Checked normally";
        AddHit(x, y, size, size, std::move(click), nullptr,
            std::wstring(text) + L"\nClick: checked > pass > fail. Runtime only: reset when the bot despawns or the server restarts");
    }

    void DebugWindow::RenderMessage(const std::wstring& text)
    {
        DrawWrapped(12, 12, (std::max)(viewWidth - 24, 50), text, Color::dark);
        contentHeight = 60;
    }

    //--------------------------------------------------------------------
    // switches
    //--------------------------------------------------------------------

    std::string DebugWindow::SwitchKey(char kind, const std::string& state, const std::string& tree, int node, int index) const
    {
        return std::string(1, kind) + "/" + state + "/" + tree + "/" + std::to_string(node) + "/" + std::to_string(index);
    }

    int DebugWindow::SwitchValue(const std::string& key, int reported)
    {
        auto it = pending.find(key);
        if (it == pending.end())
            return reported;
        if (it->second.first == reported || GetTickCount64() - it->second.second > 3000)
        {
            pending.erase(it);
            return reported;
        }
        return it->second.first;
    }

    void DebugWindow::SendSwitch(char kind, StateTreeSwitch what, const std::string& state, const std::string& tree,
        int node, int index, int value)
    {
        if (!selected)
            return;
        BotSwitch item;
        item.guid = selected;
        item.what = what;
        item.state = state;
        item.tree = tree;
        item.node = node;
        item.index = index;
        item.value = value;
        {
            std::lock_guard<std::mutex> guard(hub->lock);
            hub->switches.push_back(std::move(item));
        }
        pending[SwitchKey(kind, state, tree, node, index)] = { value, GetTickCount64() };
    }

    //--------------------------------------------------------------------
    // States tab
    //--------------------------------------------------------------------

    constexpr int stackWidth = 220;

    void DebugWindow::RenderStates()
    {
        if (!view)
        {
            RenderMessage(L"Waiting for the bot's state tree...");
            return;
        }

        const StateMachineDebugView& machine = view->machine;
        if (machine.active != lastActive)
        {
            // the bot changed its state: nodes collapse again (not with dont expand/collapse)
            if (!freeze)
                expanded.clear();
            lastActive = machine.active;
        }

        // the active main state (NonCombat / Combat / Dead) and Reaction, which always runs next to it
        const StateMachineDebugView::State* state = nullptr;
        const StateMachineDebugView::State* reaction = nullptr;
        for (const StateMachineDebugView::State& node : machine.states)
        {
            if (node.type == "Reaction")
                reaction = &node;
            else if (node.type == machine.active)
                state = &node;
        }
        std::vector<const StateMachineDebugView::State*> shown;
        if (state)
            shown.push_back(state);
        if (reaction)
            shown.push_back(reaction);

        leftX = stackWidth + 10;
        int y = 8;

        // the bot's movement first (independent of the states): the goal driving it and where it comes from
        Runs movement;
        movement.push_back({ L"Movement:  ", Color::light });
        movement.push_back({ Wide(view->goal), Color::white, 0, true });
        const COLORREF moveColor = view->moveState == "blocked" ? RGB(255, 96, 96) : view->moveState == "moving" ? Color::yellow : Color::brightGreen;
        movement.push_back({ L"  " + Wide(view->moveState), moveColor });
        if (view->distance >= 0.0f)
        {
            wchar_t distance[32];
            swprintf(distance, 32, L"  %.1f yd", view->distance);
            movement.push_back({ distance, Color::gray });
        }
        movement.push_back({ L"  " + Wide(view->goalSource) + L"  (priority " + std::to_wstring(view->priority) + L")", Color::dark });
        DrawRuns(leftX, y, movement);
        y += 22;

        auto separator = [&]()
        {
            y += 10;
            Fill(leftX, y, (std::max)(viewWidth - leftX - 8, 10), 1, Color::line);
            y += 10;
        };

        for (size_t i = 0; i < shown.size(); ++i)
        {
            if (i)
                separator();
            y = RenderState(*shown[i], y);
        }
        if (showInactive)
            for (const StateMachineDebugView::State& other : machine.states)
                if (&other != state && &other != reaction)
                {
                    separator();
                    y = RenderState(other, y);
                }

        contentHeight = y + 12;
        RenderStack(shown);
    }

    // a state's top tree from Root down; the y below it
    int DebugWindow::RenderState(const StateMachineDebugView::State& state, int startY)
    {
        const int width = viewWidth;
        const int rootX = leftX;
        int y = startY;

        const StateNodeDebugTree* top = state.stack.empty() ? nullptr : &state.stack.back();
        const std::string treeName = top ? top->name : state.name;

        Runs rootRuns = { { L"\u00A4 ", Color::gold }, { L"Root  ", Color::text }, { Wide(treeName), Color::white, 0, true } };
        const RECT root = Box(rootX, y, rootRuns, Color::rootFill, Color::rootBorder, 24);

        const bool active = state.running && state.type != "Reaction";
        Runs stateRuns;
        if (state.running)
        {
            stateRuns.push_back({ Wide(state.type), Color::brightGreen, 0, true });
            if (active)
            {
                const uint32 since = uint32(GetTickCount64() - viewTick);
                stateRuns.push_back({ L"  " + FormatSeconds(view->machine.msInState + since), Color::text });
            }
        }
        else
        {
            stateRuns.push_back({ Wide(state.type), Color::text, 0, true });
            stateRuns.push_back({ L"  (inactive)", Color::dark });
        }
        const int infoX = root.right + 10;
        DrawRuns(infoX, y - 1, stateRuns);
        DrawLabel(infoX, y + 12, Wide(state.name), Color::gray, smallFont);
        y += 24 + 8;

        const int trunkX = rootX + 10;
        const int trunkTop = startY + 24;
        int lastStubY = trunkTop;
        const int childX = rootX + 26;

        static const std::vector<StateNodeDebugChild> noNodes;
        const std::vector<StateNodeDebugChild>& nodes = top ? top->nodes : noNodes;
        if (nodes.empty())
        {
            DrawLabel(childX, y, L"(no nodes)", Color::dark);
            y += 16;
        }

        for (size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex)
        {
            const StateNodeDebugChild& child = nodes[nodeIndex];
            const int node = int(nodeIndex);
            y += 4;
            const bool childActive = child.active && state.running;
            const std::string key = treeName + "/" + child.name;
            auto found = expanded.find(key);
            bool isExpanded = found != expanded.end() ? found->second : childActive;
            if (found == expanded.end() && freeze)
                expanded[key] = isExpanded;     // dont expand/collapse: the layout seen now is kept

            const bool nodeDisabled = SwitchValue(SwitchKey('n', state.type, treeName, node, 0), child.disabled ? 1 : 0) != 0;
            auto taskDisabled = [&](size_t task)
            {
                const bool reported = task < child.tasksDisabled.size() && child.tasksDisabled[task];
                return SwitchValue(SwitchKey('t', state.type, treeName, node, int(task)), reported ? 1 : 0) != 0;
            };

            Runs caption;
            if (childActive)
                caption.push_back({ L">> ", Color::brightGreen, 0, true });
            else
                caption.push_back({ isExpanded ? L"\u2212 " : L"+ ", Color::marker });
            if (nodeDisabled)
                caption.push_back({ Wide(child.name) + L"  (switched off)", Color::gray });
            else
                caption.push_back({ Wide(child.name), Color::white });
            if (!isExpanded)
            {
                caption.push_back({ L"  ", Color::dark });
                for (size_t task = 0; task < child.tasks.size(); ++task)
                {
                    if (task)
                        caption.push_back({ L" > ", Color::dark });
                    if (taskDisabled(task))
                        caption.push_back({ L"x " + Wide(child.tasks[task]), Color::darkRed });
                    else
                        caption.push_back({ Wide(child.tasks[task]), Color::dark });
                }
            }

            const RECT box = Box(childX, y, caption, childActive ? Color::activeFill : Color::childFill,
                childActive ? Color::activeBorder : Color::stateBorder, 20, childActive);
            AddHit(childX, y, box.right - box.left, 20, nullptr, [this, key, isExpanded]() { expanded[key] = !isExpanded; },
                L"Double-click to " + std::wstring(isExpanded ? L"collapse" : L"expand"));
            // check / cross right of the node: a switched off node is never selected (left when it runs)
            const std::string stateType = state.type;   // the view may be replaced before a click
            Toggle(box.right + 4, y + 2, nodeDisabled, L"node", [this, stateType, treeName, node, nodeDisabled]()
            {
                SendSwitch('n', StateTreeSwitch::Node, stateType, treeName, node, 0, nodeDisabled ? 0 : 1);
            });
            Fill(trunkX, y + 9, childX - trunkX, 2, childActive ? Color::activeBorder : Color::line);
            lastStubY = y + 9;
            y += 20 + 3;

            if (!isExpanded)
                continue;

            const int contentX = childX + 14;
            if (child.conditions.empty())
            {
                DrawRuns(contentX, y, { { L"IF  ", Color::dim }, { L"(always)", Color::darker } });
                y += 15;
            }
            else
            {
                // the square left of each condition overrides it
                for (size_t condition = 0; condition < child.conditions.size(); ++condition)
                {
                    const auto& [text, result] = child.conditions[condition];
                    const int reported = condition < child.conditionOverrides.size() ? child.conditionOverrides[condition] : 0;
                    const int value = SwitchValue(SwitchKey('c', state.type, treeName, node, int(condition)), reported);
                    Override(contentX, y + 2, value, [this, stateType, treeName, node, condition, value]()
                    {
                        SendSwitch('c', StateTreeSwitch::Condition, stateType, treeName, node, int(condition), (value + 1) % 3);
                    });
                    const wchar_t* prefix = condition == 0 ? L"IF" : child.conditionsAny ? L"OR" : L"AND";
                    const int textX = DrawLabel(contentX + 15, y, prefix, Color::dim) + 6;
                    const COLORREF color = result == StateNodeConditionResult::Passed ? Color::green :
                        result == StateNodeConditionResult::Failed ? Color::red : Color::dark;
                    std::wstring line = Wide(text);
                    if (value == 1)
                        line += L"  (forced pass)";
                    else if (value == 2)
                        line += L"  (forced fail)";
                    y = DrawWrapped(textX, y, (std::max)(width - textX - 8, 60), line, color);
                }
            }
            y += 2;

            // tasks run one after another, each with what it does; the check / cross right of each switches it
            const int running = childActive ? child.currentTask + 1 : 0;
            for (size_t task = 0; task < child.tasks.size(); ++task)
            {
                const int index = int(task) + 1;
                const bool disabled = taskDisabled(task);
                const bool current = index == running || (running == 0 && index == 1);
                std::string info = task < child.taskInfos.size() ? child.taskInfos[task] : "";
                if (info.empty() && current)
                    info = child.info;

                Runs runs;
                runs.push_back(index == running ? TextRun{ L"> ", Color::brightGreen, 0, true } : TextRun{ L"T ", Color::task, 0, true });
                if (child.tasks.size() > 1)
                    runs.push_back({ std::to_wstring(index) + L". ", Color::dark });
                if (disabled)
                    runs.push_back({ Wide(child.tasks[task]) + L"  (switched off)", Color::darker });
                else
                {
                    runs.push_back({ Wide(child.tasks[task]), Color::white });
                    if (!info.empty())
                    {
                        runs.push_back({ L"  ", Color::gray });
                        AppendMarkup(runs, info, Color::gray);
                    }
                }
                const RECT taskBox = Box(contentX, y, runs, Color::taskFill, index == running ? Color::activeBorder : Color::taskBorder, 18);
                Toggle(taskBox.right + 4, y + 1, disabled, L"task", [this, stateType, treeName, node, task, disabled]()
                {
                    SendSwitch('t', StateTreeSwitch::Task, stateType, treeName, node, int(task), disabled ? 0 : 1);
                });
                if (current && !disabled && !child.spells.empty())
                    SpellIcons(taskBox.right + 4 + 16 + 6, y + 1, child.spells, 16);
                y += 18 + 2;

                // the tree the task runs while it does not run, down to the leaves
                for (const StateNodeDebugTree& subtree : child.subtrees)
                    if (subtree.task == int(task))
                        y = RenderSubtree(subtree, contentX + 14, y, key + "#" + std::to_string(task));
            }

            // where the tree goes when the node finished (none: back to the first node)
            for (const std::string& transition : child.transitions)
            {
                std::string text = transition;
                std::replace(text.begin(), text.end(), '|', '/');
                const size_t space = text.find(' ');
                const int textX = DrawLabel(contentX, y, Wide(space == std::string::npos ? text : text.substr(0, space)), Color::dim);
                y = DrawWrapped(textX, y, (std::max)(width - textX - 8, 60),
                    L"  \u00BB " + (space == std::string::npos ? std::wstring() : Wide(text.substr(space + 1))), Color::light);
            }
        }

        // vertical trunk from Root down to the last node
        Fill(trunkX, trunkTop, 2, lastStubY - trunkTop + 2, Color::line);
        return y;
    }

    // a tree a task runs while it does not run (Task_RunSubtree): its file's nodes, collapsed until double-clicked,
    // with the trees their tasks run; nothing to switch (the bot has no contexts for it until the task runs);
    // the y below it
    int DebugWindow::RenderSubtree(const StateNodeDebugTree& tree, int x, int y, const std::string& keyPrefix)
    {
        const int width = viewWidth;
        y = DrawWrapped(x, y, (std::max)(width - x - 8, 60), L"¤ " + Wide(tree.name), Color::gold) + 2;

        const int trunkX = x + 4;
        const int trunkTop = y;
        int lastStubY = trunkTop;
        const int childX = x + 18;
        if (tree.nodes.empty())
        {
            DrawLabel(childX, y, L"(no nodes)", Color::dark);
            y += 16;
        }

        for (const StateNodeDebugChild& child : tree.nodes)
        {
            const std::string key = keyPrefix + "/" + tree.name + "/" + child.name;
            auto found = expanded.find(key);
            const bool isExpanded = found != expanded.end() && found->second;

            Runs caption;
            caption.push_back({ isExpanded ? L"− " : L"+ ", Color::marker });
            caption.push_back({ Wide(child.name), Color::text });
            if (!isExpanded)
            {
                caption.push_back({ L"  ", Color::dark });
                for (size_t task = 0; task < child.tasks.size(); ++task)
                    caption.push_back({ (task ? L" > " : L"") + Wide(child.tasks[task]), Color::dark });
            }
            const RECT box = Box(childX, y, caption, Color::childFill, Color::stateBorder, 18);
            AddHit(childX, y, box.right - box.left, 18, nullptr, [this, key, isExpanded]() { expanded[key] = !isExpanded; },
                L"Double-click to " + std::wstring(isExpanded ? L"collapse" : L"expand"));
            Fill(trunkX, y + 8, childX - trunkX, 1, Color::line);
            lastStubY = y + 8;
            y += 18 + 3;

            if (!isExpanded)
                continue;

            const int contentX = childX + 14;
            if (child.conditions.empty())
            {
                DrawRuns(contentX, y, { { L"IF  ", Color::dim }, { L"(always)", Color::darker } });
                y += 15;
            }
            for (size_t condition = 0; condition < child.conditions.size(); ++condition)
            {
                const wchar_t* prefix = condition == 0 ? L"IF" : child.conditionsAny ? L"OR" : L"AND";
                const int textX = DrawLabel(contentX, y, prefix, Color::dim) + 6;
                y = DrawWrapped(textX, y, (std::max)(width - textX - 8, 60), Wide(child.conditions[condition].first), Color::dark);
            }
            y += 2;

            for (size_t task = 0; task < child.tasks.size(); ++task)
            {
                Runs runs;
                runs.push_back({ L"T ", Color::task, 0, true });
                if (child.tasks.size() > 1)
                    runs.push_back({ std::to_wstring(task + 1) + L". ", Color::dark });
                runs.push_back({ Wide(child.tasks[task]), Color::white });
                const std::string info = task < child.taskInfos.size() ? child.taskInfos[task] : "";
                if (!info.empty())
                {
                    runs.push_back({ L"  ", Color::gray });
                    AppendMarkup(runs, info, Color::gray);
                }
                Box(contentX, y, runs, Color::taskFill, Color::taskBorder, 18);
                y += 18 + 2;

                for (const StateNodeDebugTree& subtree : child.subtrees)
                    if (subtree.task == int(task))
                        y = RenderSubtree(subtree, contentX + 14, y, key + "#" + std::to_string(task));
            }

            for (const std::string& transition : child.transitions)
            {
                std::string text = transition;
                std::replace(text.begin(), text.end(), '|', '/');
                const size_t space = text.find(' ');
                const int textX = DrawLabel(contentX, y, Wide(space == std::string::npos ? text : text.substr(0, space)), Color::dim);
                y = DrawWrapped(textX, y, (std::max)(width - textX - 8, 60),
                    L"  » " + (space == std::string::npos ? std::wstring() : Wide(text.substr(space + 1))), Color::light);
            }
        }

        Fill(trunkX, trunkTop, 1, lastStubY - trunkTop + 1, Color::line);
        return y + 2;
    }

    // tree stacks of the shown states, bottom (initial tree) to top, with their global tasks; fixed left of the canvas
    void DebugWindow::RenderStack(const std::vector<const StateMachineDebugView::State*>& states)
    {
        inStack = true;
        Fill(0, 0, stackWidth, (std::max)(viewHeight, 1), RGB(22, 22, 22));
        Fill(stackWidth, 0, 1, (std::max)(viewHeight, 1), RGB(48, 48, 48));

        int y = 8;
        for (const StateMachineDebugView::State* state : states)
        {
            DrawLabel(8, y, Wide(state->type), Color::light, bold);
            y += 18;
            if (state->stack.empty())
            {
                DrawLabel(8, y, L"(no tree)", Color::dark);
                y += 16;
            }
            for (size_t depth = 0; depth < state->stack.size(); ++depth)
            {
                const StateNodeDebugTree& tree = state->stack[depth];
                const bool top = depth + 1 == state->stack.size();
                DrawLabel(8, y, (top ? L">> " : L"") + Wide(tree.name), top ? Color::brightGreen : tree.running ? Color::white : Color::gray);
                y += 16;
                for (size_t global = 0; global < tree.globalTasks.size(); ++global)
                {
                    Runs runs = { { L"G ", Color::global, 0, true }, { Wide(tree.globalTasks[global]), Color::text } };
                    const int boxWidth = (std::min)((std::max)(RunsWidth(runs) + 16, 60), stackWidth - 24);
                    Fill(16, y, boxWidth, 18, Color::taskFill);
                    Frame(16, y, boxWidth, 18, Color::taskBorder);
                    DrawRuns(22, y + 1, runs);
                    if (global < tree.globalSpells.size() && !tree.globalSpells[global].empty())
                    {
                        // icons wrap below the box when they do not fit the panel
                        int iconX = 16 + boxWidth + 4;
                        for (uint32 spellId : tree.globalSpells[global])
                        {
                            if (iconX + 16 > stackWidth - 4)
                            {
                                iconX = 16;
                                y += 20;
                            }
                            iconX = SpellIcon(iconX, y + 1, spellId, 16);
                        }
                    }
                    y += 20;
                }
                y += 4;
            }
            y += 8;
        }
        inStack = false;
    }

    //--------------------------------------------------------------------
    // History tab
    //--------------------------------------------------------------------

    void DebugWindow::RenderHistory()
    {
        if (!history)
        {
            RenderMessage(L"Waiting for the bot's activity...");
            return;
        }

        leftX = 0;
        const int width = viewWidth;
        const int whenX = 10, nameX = 104;
        const int sourceX = (std::max)(nameX + 240, width * 52 / 100);
        const uint32 since = uint32(GetTickCount64() - historyTick);
        const std::vector<STAI_ActivityLog::Entry>& entries = history->entries;

        int y = 8;
        DrawLabel(whenX, y, L"When", Color::light, bold);
        DrawLabel(nameX, y, L"Task / Action", Color::light, bold);
        DrawLabel(sourceX, y, L"Node / Strategy  (time)", Color::light, bold);
        y += 18;
        Fill(8, y, (std::max)(width - 16, 10), 1, Color::line);
        y += 5;

        if (entries.empty())
        {
            DrawLabel(whenX, y, L"no activity yet", Color::dark);
            y += 16;
        }

        // newest first; only the rows on screen are drawn (an hour may hold thousands)
        const int rowHeight = 16;
        const int firstY = y;
        for (size_t row = 0; row < entries.size(); ++row, y += rowHeight)
        {
            if (y + rowHeight < scrollY || y > scrollY + viewHeight)
                continue;

            const STAI_ActivityLog::Entry& entry = entries[entries.size() - 1 - row];
            const uint32 ageAtTaken = WorldTimer::getMSTimeDiff(entry.startMs, history->takenMs);
            const bool running = entry.open || WorldTimer::getMSTimeDiff(entry.startMs, entry.endMs) > ageAtTaken;
            const uint32 duration = running ? ageAtTaken + since : WorldTimer::getMSTimeDiff(entry.startMs, entry.endMs);

            if (row % 2)
                Fill(8, y - 1, (std::max)(width - 16, 10), rowHeight, RGB(30, 30, 30));

            DrawLabel(whenX, y, FormatSeconds(ageAtTaken + since) + L" ago", Color::gray);

            Runs name;
            AppendMarkup(name, entry.name, Color::white);
            if (entry.count > 1)
                name.push_back({ L"  x" + std::to_wstring(entry.count), Color::yellow });
            // clipped at the source column
            HRGN clip = CreateRectRgn(Dev(nameX), 0, Dev(sourceX - 6), S(viewHeight));
            SelectClipRgn(dc, clip);
            DrawRuns(nameX, y, name);
            SelectClipRgn(dc, nullptr);
            DeleteObject(clip);

            Runs source = { { Wide(entry.source), Color::text } };
            if (entry.result == 1)
                source.push_back({ L"  Successful", Color::green });
            else if (entry.result == 0)
                source.push_back({ L"  Fail", Color::red });
            if (entry.count > 1 && entry.failures > 0)
                source.push_back({ L"  " + std::to_wstring(entry.failures) + L" failed", Color::lightRed });
            source.push_back({ L"  (", Color::gray });
            source.push_back({ FormatDuration(duration) + (running ? L"..." : L""), running ? Color::yellow : Color::gray });
            if (entry.count > 1)
                source.push_back({ L", " + FormatDuration(entry.doneMs + duration) + L" total", Color::light });
            source.push_back({ L")", Color::gray });
            DrawRuns(sourceX, y, source);
        }
        if (entries.empty())
            y = firstY + 16;

        contentHeight = y + 12;
    }

    void RunWindow()
    {
        window->Run();
    }
}

PlayerbotDebugWindow& PlayerbotDebugWindow::instance()
{
    static PlayerbotDebugWindow instance;
    return instance;
}

void PlayerbotDebugWindow::Start(uint32 historyMinutes)
{
    if (hub->running)
        return;

    STAI_ActivityLog::SetRetention(historyMinutes * 60000);
    window = new DebugWindow();
    hub->running = true;
    windowThread = std::thread(RunWindow);
    sLog.outString("PlayerbotDebugWindow: opened, bots keep %u minutes of activity", historyMinutes);
}

void PlayerbotDebugWindow::Stop()
{
    if (!hub->running)
        return;

    hub->running = false;
    hub->publishing = false;
    hub->selected = 0;
    // the window may still be being created: wait for its handle
    for (int i = 0; i < 100 && !windowHandle; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    if (HWND handle = windowHandle.exchange(nullptr))
        PostMessageW(handle, WM_APP_QUIT, 0, 0);
    if (windowThread.joinable())
        windowThread.join();
    delete window;
    window = nullptr;
}

bool PlayerbotDebugWindow::IsRunning() const
{
    return hub->running;
}

#endif
