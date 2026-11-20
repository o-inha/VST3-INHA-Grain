#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <ole2.h>
#include <objidl.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <gdiplus.h>

#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstevents.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using namespace Steinberg;
using namespace Steinberg::Vst;

constexpr int footerTop = 337;
constexpr int footerMiddle = footerTop + 14;

void require (bool condition, const char* message)
{
    if (! condition) throw std::runtime_error (message);
}

// Only this test process sees the redirection; the plug-in never sees real INHA
// user preferences. Use a separate key from the preferences unit tests.
class IsolatedCurrentUser
{
public:
    IsolatedCurrentUser()
    {
        name = L"Software\\INHA-Editor-Test-" + std::to_wstring (GetCurrentProcessId())
             + L"-" + std::to_wstring (GetTickCount64());
        require (RegCreateKeyExW (HKEY_CURRENT_USER, name.c_str(), 0, nullptr,
                                  REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS,
                                  nullptr, &key, nullptr) == ERROR_SUCCESS,
                 "Could not create isolated UI-test registry key");
        if (RegOverridePredefKey (HKEY_CURRENT_USER, key) != ERROR_SUCCESS)
        {
            RegCloseKey (key);
            RegDeleteTreeW (HKEY_CURRENT_USER, name.c_str());
            throw std::runtime_error ("Could not redirect UI-test preferences");
        }
    }
    ~IsolatedCurrentUser()
    {
        RegOverridePredefKey (HKEY_CURRENT_USER, nullptr);
        RegCloseKey (key);
        RegDeleteTreeW (HKEY_CURRENT_USER, name.c_str());
    }
private:
    HKEY key = nullptr;
    std::wstring name;
};

bool preferenceEnabled (const wchar_t* name)
{
    DWORD value = 0, size = sizeof (value);
    return RegGetValueW (HKEY_CURRENT_USER, L"Software\\INHA\\Grain", name,
                         RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS
        && value == 1;
}

struct GdiSession
{
    ULONG_PTR token = 0;
    GdiSession()
    {
        Gdiplus::GdiplusStartupInput input;
        require (Gdiplus::GdiplusStartup (&token, &input, nullptr) == Gdiplus::Ok,
                 "GDI+ startup failed");
    }
    ~GdiSession() { Gdiplus::GdiplusShutdown (token); }
};

class Module
{
public:
    explicit Module (const wchar_t* path)
    {
        handle = LoadLibraryW (path);
        require (handle != nullptr, "Could not load bundled plug-in for editor smoke test");
        auto init = reinterpret_cast<bool (*)()> (GetProcAddress (handle, "InitDll"));
        exit = reinterpret_cast<bool (*)()> (GetProcAddress (handle, "ExitDll"));
        auto getFactory = reinterpret_cast<IPluginFactory* (PLUGIN_API*)()> (
            GetProcAddress (handle, "GetPluginFactory"));
        require (init && exit && getFactory && init(), "VST3 initialization failed");
        factory = getFactory();
        require (factory != nullptr, "No VST3 factory");
    }
    ~Module()
    {
        if (factory) factory->release();
        if (exit) exit();
        if (handle) FreeLibrary (handle);
    }
    IPluginFactory* factory = nullptr;
private:
    HMODULE handle = nullptr;
    bool (*exit)() = nullptr;
};

struct HostFrame final : IPlugFrame
{
    int resizeCount = 0;
    tresult PLUGIN_API queryInterface (const TUID iid, void** object) override
    {
        if (! object) return kInvalidArgument;
        *object = nullptr;
        if (std::memcmp (iid, IPlugFrame_iid, sizeof (TUID)) != 0
            && std::memcmp (iid, FUnknown_iid, sizeof (TUID)) != 0)
            return kNoInterface;
        *object = static_cast<IPlugFrame*> (this);
        return kResultOk;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
    tresult PLUGIN_API resizeView (IPlugView* view, ViewRect* requested) override
    {
        ++resizeCount;
        return view->onSize (requested);
    }
};

class Editor
{
public:
    explicit Editor (IPluginFactory& factory)
    {
        PClassInfo info {};
        require (factory.getClassInfo (1, &info) == kResultOk, "No controller class");
        require (factory.createInstance (info.cid, IEditController_iid,
                   reinterpret_cast<void**> (&controller)) == kResultOk && controller,
                 "Could not create controller");
        require (controller->initialize (nullptr) == kResultOk, "Controller initialization failed");
        view = controller->createView (ViewType::kEditor);
        require (view != nullptr, "Could not create editor");
        require (view->setFrame (&frame) == kResultOk, "Could not set mock host frame");
        parent = CreateWindowExW (0, L"STATIC", L"Grain editor smoke test", WS_POPUP,
                                  0, 0, 1000, 800, nullptr, nullptr, GetModuleHandleW (nullptr), nullptr);
        require (parent != nullptr, "Could not create hidden host window");
        require (view->attached (parent, kPlatformTypeHWND) == kResultOk, "Editor attach failed");
        window = GetWindow (parent, GW_CHILD);
        require (window != nullptr, "Attached editor has no native child window");
    }
    ~Editor()
    {
        if (view)
        {
            view->removed();
            view->setFrame (nullptr);
            view->release();
        }
        if (parent) DestroyWindow (parent);
        if (controller) { controller->terminate(); controller->release(); }
    }
    ViewRect size() const
    {
        ViewRect result {};
        require (view->getSize (&result) == kResultOk, "Could not get editor size");
        return result;
    }
    void clickFooter (int index)
    {
        moveTo (58, footerMiddle);
        settleFooter();
        moveTo (90 + 32 * index, footerMiddle);
        clickAt (90 + 32 * index, footerMiddle);
        SendMessageW (window, WM_MOUSELEAVE, 0, 0);
        settleFooter();
    }
    void moveTo (int x, int y)
    {
        const auto scale = size().getWidth() >= 660 ? 1.5 : 1.0;
        SendMessageW (window, WM_MOUSEMOVE, 0, MAKELPARAM (
            static_cast<int> (std::lround (x * scale)), static_cast<int> (std::lround (y * scale))));
    }
    void clickAt (int logicalX, int logicalY)
    {
        const auto scale = size().getWidth() >= 660 ? 1.5 : 1.0;
        const auto x = static_cast<int> (std::lround (logicalX * scale));
        const auto y = static_cast<int> (std::lround (logicalY * scale));
        SendMessageW (window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM (x, y));
        SendMessageW (window, WM_LBUTTONUP, 0, MAKELPARAM (x, y));
    }
    void doubleClickAt (int logicalX, int logicalY)
    {
        const auto scale = size().getWidth() >= 660 ? 1.5 : 1.0;
        const auto x = static_cast<int> (std::lround (logicalX * scale));
        const auto y = static_cast<int> (std::lround (logicalY * scale));
        SendMessageW (window, WM_LBUTTONDBLCLK, MK_LBUTTON, MAKELPARAM (x, y));
        SendMessageW (window, WM_LBUTTONUP, 0, MAKELPARAM (x, y));
        SendMessageW (window, WM_TIMER, 1, 0);
    }
    ParamID parameter (const char* name) const
    {
        for (int32 index = 0; index < controller->getParameterCount(); ++index)
        {
            ParameterInfo info {};
            require (controller->getParameterInfo (index, info) == kResultOk,
                     "Cannot inspect editor parameter");
            std::string title;
            for (const auto character : info.title)
            {
                if (character == 0) break;
                title.push_back (static_cast<char> (character));
            }
            if (title == name) return info.id;
        }
        throw std::runtime_error (std::string ("Missing parameter: ") + name);
    }
    double plain (const char* name) const
    {
        const auto id = parameter (name);
        return controller->normalizedParamToPlain (id, controller->getParamNormalized (id));
    }
    void set (const char* name, double value)
    {
        const auto id = parameter (name);
        require (controller->setParamNormalized (id, value) == kResultOk,
                 "Cannot set editor parameter");
        SendMessageW (window, WM_TIMER, 1, 0);
    }
    void dragAt (int logicalX, int logicalY, int logicalEndY)
    {
        const auto scale = size().getWidth() >= 660 ? 1.5 : 1.0;
        const auto point = [scale] (int x, int y)
        {
            return MAKELPARAM (static_cast<int> (std::lround (x * scale)),
                               static_cast<int> (std::lround (y * scale)));
        };
        SendMessageW (window, WM_LBUTTONDOWN, MK_LBUTTON, point (logicalX, logicalY));
        SendMessageW (window, WM_MOUSEMOVE, MK_LBUTTON, point (logicalX, logicalEndY));
        SendMessageW (window, WM_LBUTTONUP, 0, point (logicalX, logicalEndY));
        SendMessageW (window, WM_TIMER, 1, 0);
    }
    void settleFooter()
    {
        advanceAnimation (140);
    }
    void advanceAnimation (DWORD milliseconds = 140)
    {
        Sleep (milliseconds);
        SendMessageW (window, WM_TIMER, 1, 0);
    }
    void refreshPreferences() { SendMessageW (window, WM_TIMER, 1, 0); }
    IEditController* controller = nullptr;
    IPlugView* view = nullptr;
    HWND window = nullptr;
    HostFrame frame;
private:
    HWND parent = nullptr;
};

struct Capture
{
    int width = 0, height = 0;
    std::vector<std::uint32_t> pixels;
    COLORREF pixel (int x, int y) const
    {
        require (x >= 0 && x < width && y >= 0 && y < height, "Pixel outside captured editor");
        const auto value = pixels[static_cast<std::size_t> (y) * width + x];
        return RGB ((value >> 16) & 255, (value >> 8) & 255, value & 255);
    }
    COLORREF logicalPixel (int x, int y) const
    {
        const auto scale = width >= 660 ? 1.5 : 1.0;
        return pixel (static_cast<int> (std::lround (x * scale)),
                      static_cast<int> (std::lround (y * scale)));
    }
    void save (const std::filesystem::path& path)
    {
        UINT bytes = 0, count = 0;
        require (Gdiplus::GetImageEncodersSize (&count, &bytes) == Gdiplus::Ok,
                 "Could not enumerate PNG encoder");
        std::vector<unsigned char> storage (bytes);
        auto* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*> (storage.data());
        require (Gdiplus::GetImageEncoders (count, bytes, codecs) == Gdiplus::Ok,
                 "Could not read PNG encoders");
        for (UINT index = 0; index < count; ++index)
            if (std::wcscmp (codecs[index].MimeType, L"image/png") == 0)
            {
                Gdiplus::Bitmap image (width, height, width * 4, PixelFormat32bppRGB,
                                      reinterpret_cast<BYTE*> (pixels.data()));
                require (image.Save (path.c_str(), &codecs[index].Clsid) == Gdiplus::Ok,
                         "Could not save editor screenshot");
                return;
            }
        throw std::runtime_error ("PNG encoder unavailable");
    }
};

Capture capture (const Editor& editor)
{
    const auto size = editor.size();
    Capture result { size.getWidth(), size.getHeight(), {} };
    BITMAPINFO info {};
    info.bmiHeader.biSize = sizeof (BITMAPINFOHEADER);
    info.bmiHeader.biWidth = result.width;
    info.bmiHeader.biHeight = -result.height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    const auto dc = CreateCompatibleDC (nullptr);
    require (dc != nullptr, "Could not create capture DC");
    void* pixels = nullptr;
    const auto bitmap = CreateDIBSection (dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    require (bitmap && pixels, "Could not create capture bitmap");
    const auto previous = SelectObject (dc, bitmap);
    SendMessageW (editor.window, WM_PRINTCLIENT, reinterpret_cast<WPARAM> (dc), PRF_CLIENT);
    GdiFlush();
    const auto* first = static_cast<const std::uint32_t*> (pixels);
    result.pixels.assign (first, first + static_cast<std::size_t> (result.width) * result.height);
    SelectObject (dc, previous);
    DeleteObject (bitmap);
    DeleteDC (dc);
    return result;
}

void checkRendered (const Capture& image)
{
    require (image.width >= 524 && image.height >= 366, "Unexpected editor dimensions");
    std::size_t visible = 0, transitions = 0;
    for (std::size_t i=1; i<image.pixels.size(); ++i)
    {
        visible += (image.pixels[i] & 0xffffffu) != 0 ? 1u : 0u;
        transitions += image.pixels[i] != image.pixels[i-1] ? 1u : 0u;
    }
    require (visible > image.pixels.size()/4 && transitions > 1000,
             "Native editor screenshot is blank or missing controls");
}

void expandSettings (Editor& editor)
{
    editor.moveTo (58, footerMiddle);
    editor.settleFooter();
}

void clickSetting (Editor& editor, int x)
{
    expandSettings (editor);
    editor.moveTo (x, footerMiddle);
    editor.clickAt (x, footerMiddle);
    editor.settleFooter();
}

void savePreview (Editor& editor, const std::filesystem::path& directory, const wchar_t* filename)
{
    editor.moveTo (200, 330);
    editor.settleFooter();
    auto image = capture (editor);
    checkRendered (image);
    image.save (directory / filename);
}

double motionValue(const Editor& editor,ParamID id)
{
    return editor.controller->getParamNormalized(id);
}

void clickMotionTab(Editor& editor,bool sway)
{
    editor.moveTo(sway?168:77,185);
    editor.clickAt(sway?168:77,185);
    editor.settleFooter();
    require(motionValue(editor,24)==(sway?1.0:0.0),"Motion tab did not select UNDULATION/SWAY");
    const auto image=capture(editor);
    const auto panel=image.logicalPixel(26,203);
    const auto und=image.logicalPixel(26,180);
    const auto sw=image.logicalPixel(198,180);
    require((sway?sw:und)==panel,"Active motion tab does not share its attached panel background");
    require((sway?und:sw)!=panel,"Inactive motion tab did not use the plugin background");
}

void swayPreview(Editor& editor,const std::filesystem::path& directory,const wchar_t* filename)
{
    clickMotionTab(editor,true);
    savePreview(editor,directory,filename);
    clickMotionTab(editor,false);
}

void motionControlBanks(Editor& editor,const std::filesystem::path& directory)
{
    const auto undAmount=std::array<double,2>{motionValue(editor,9),motionValue(editor,10)};
    const auto undSpeed=std::array<double,2>{motionValue(editor,11),motionValue(editor,12)};
    clickMotionTab(editor,true);
    const auto swayAmount=std::array<double,2>{motionValue(editor,25),motionValue(editor,26)};
    const auto swaySpeed=std::array<double,2>{motionValue(editor,27),motionValue(editor,28)};
    editor.dragAt(66,253,235);
    require(motionValue(editor,25)!=swayAmount[0]||motionValue(editor,26)!=swayAmount[1],
            "SWAY AMOUNT knob did not edit its own parameter bank");
    require(motionValue(editor,9)==undAmount[0]&&motionValue(editor,10)==undAmount[1],
            "SWAY AMOUNT drag changed the inactive UNDULATION values");
    editor.dragAt(156,253,235);
    require(motionValue(editor,27)!=swaySpeed[0]||motionValue(editor,28)!=swaySpeed[1],
            "SWAY SPEED knob did not edit its own millisecond parameter bank");
    require(motionValue(editor,11)==undSpeed[0]&&motionValue(editor,12)==undSpeed[1],
            "SWAY SPEED drag changed the inactive UNDULATION values");
    const auto retainedAmount=std::array<double,2>{motionValue(editor,25),motionValue(editor,26)};
    const auto retainedSpeed=std::array<double,2>{motionValue(editor,27),motionValue(editor,28)};
    clickMotionTab(editor,false);
    require(motionValue(editor,9)==undAmount[0]&&motionValue(editor,10)==undAmount[1]
            &&motionValue(editor,11)==undSpeed[0]&&motionValue(editor,12)==undSpeed[1],
            "Changing motion tabs did not retain UNDULATION values");
    editor.dragAt(66,253,235);
    require(motionValue(editor,9)!=undAmount[0]||motionValue(editor,10)!=undAmount[1],
            "UNDULATION AMOUNT knob stopped editing its original parameters");
    editor.dragAt(156,253,235);
    require(motionValue(editor,11)!=undSpeed[0]||motionValue(editor,12)!=undSpeed[1],
            "UNDULATION SPEED knob stopped editing its original parameters");
    require(motionValue(editor,25)==retainedAmount[0]&&motionValue(editor,26)==retainedAmount[1]
            &&motionValue(editor,27)==retainedSpeed[0]&&motionValue(editor,28)==retainedSpeed[1],
            "UNDULATION drags changed the independent SWAY values");
    clickMotionTab(editor,true);
    savePreview(editor,directory,L"grain-sway-edited-100.png");
    clickMotionTab(editor,false);
}

void motionDefaultReset(Editor& editor)
{
    const auto expect=[&](ParamID id,double plain)
    {
        const auto actual=editor.controller->normalizedParamToPlain(id,motionValue(editor,id));
        require(std::abs(actual-plain)<1e-8,"Double-click did not restore the declared control default");
    };
    const auto und=std::array<double,4>{motionValue(editor,9),motionValue(editor,10),
                                      motionValue(editor,11),motionValue(editor,12)};
    clickMotionTab(editor,true);
    editor.doubleClickAt(66,253);expect(25,10);expect(26,30);
    editor.doubleClickAt(156,253);expect(27,500);expect(28,1500);
    for(ParamID id=9;id<=12;++id)
        require(motionValue(editor,id)==und[id-9],"Resetting SWAY changed the inactive UNDULATION range");
    const auto sway=std::array<double,4>{motionValue(editor,25),motionValue(editor,26),
                                       motionValue(editor,27),motionValue(editor,28)};
    clickMotionTab(editor,false);
    editor.doubleClickAt(66,253);expect(9,0);expect(10,50);
    editor.doubleClickAt(156,253);expect(11,50);expect(12,200);
    for(ParamID id=25;id<=28;++id)
        require(motionValue(editor,id)==sway[id-25],"Resetting UNDULATION changed the inactive SWAY range");
    editor.doubleClickAt(156,91);expect(4,2000);
}

void testEditor (Module& module, const std::filesystem::path& directory)
{
    {
        Editor editor (*module.factory);
        const auto initial = editor.size();
        require (initial.getWidth()==786 && initial.getHeight()==549,
                 "New Grain editor must open at 150 percent");
        require (!preferenceEnabled (L"ThemeDark"), "Isolated preferences did not start light");
        savePreview (editor,directory,L"grain-light-150.png");
        swayPreview(editor,directory,L"grain-light-sway-150.png");

        // Footer actions become clickable only when the settings gear is open.
        editor.clickAt (87,footerMiddle);
        require (!preferenceEnabled (L"ThemeDark"), "Collapsed theme action was clickable");
        editor.clickAt (119,footerMiddle);
        require (editor.size().getWidth()==786, "Collapsed zoom action was clickable");
        expandSettings (editor);
        capture(editor).save(directory/L"grain-settings-150.png");

        clickSetting(editor,87);
        require(preferenceEnabled(L"ThemeDark"),"Settings theme control did not persist dark mode");
        savePreview(editor,directory,L"grain-dark-150.png");
        swayPreview(editor,directory,L"grain-dark-sway-150.png");
        clickSetting(editor,119);
        require(editor.size().getWidth()==524 && editor.size().getHeight()==366,
                "Settings zoom control did not resize to 100 percent");
        require(editor.frame.resizeCount>0,"Zoom did not notify the host frame");
        savePreview(editor,directory,L"grain-dark-100.png");
        swayPreview(editor,directory,L"grain-dark-sway-100.png");
        clickSetting(editor,87);
        require(!preferenceEnabled(L"ThemeDark"),"Settings theme did not return to light mode");
        savePreview(editor,directory,L"grain-light-100.png");
        swayPreview(editor,directory,L"grain-light-sway-100.png");
        motionControlBanks(editor,directory);

        // Check meaningful controls through Windows messages, as a VST3 host sees them.
        const auto scatter=editor.plain("Capture scatter");
        editor.dragAt(156,91,70);
        require(editor.plain("Capture scatter")>scatter,"SCATTER knob did not respond to upward drag");
        const auto grainMin=editor.plain("Grain minimum"), grainMax=editor.plain("Grain maximum");
        editor.dragAt(246,91,75);
        require(editor.plain("Grain minimum")!=grainMin || editor.plain("Grain maximum")!=grainMax,
                "GRAIN range knob did not respond to drag");
        require(editor.plain("Grain minimum")<=editor.plain("Grain maximum"),"Range handles crossed");
        motionDefaultReset(editor);

        editor.moveTo(450,14);editor.settleFooter();editor.clickAt(450,14);editor.settleFooter();
        editor.clickAt(450,104);editor.settleFooter();
        require(editor.plain("Trigger mode")==3,"Trigger dropdown did not select VOLUME");
        const auto threshold=editor.plain("Volume threshold");
        editor.dragAt(66,91,70);
        require(editor.plain("Volume threshold")>threshold,"THLD control did not edit volume threshold");
        savePreview(editor,directory,L"grain-volume-100.png");
    }
    // Recreating the view loads the same per-user zoom and theme choices.
    Editor reopened(*module.factory);
    require(reopened.size().getWidth()==524 && !preferenceEnabled(L"ThemeDark"),
            "New editor instance did not retain its appearance preferences");
}
}

int wmain(int argc,wchar_t** argv)
{
    try
    {
        require(argc==3,"Usage: GrainEditorSmoke <fixture-binary> <preview-directory>");
        const std::filesystem::path directory(argv[2]);
        std::filesystem::create_directories(directory);
        IsolatedCurrentUser isolated;
        GdiSession graphics;
        Module module(argv[1]);
        testEditor(module,directory);
        std::cout<<"Grain native editor, settings, controls and appearance persistence passed\n";
        return 0;
    }
    catch(const std::exception& error)
    {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}


