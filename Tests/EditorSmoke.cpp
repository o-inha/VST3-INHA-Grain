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
    return RegGetValueW (HKEY_CURRENT_USER, L"Software\\INHA\\Grate", name,
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
        parent = CreateWindowExW (0, L"STATIC", L"Grate editor smoke test", WS_POPUP,
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
    require (image.width >= 546 && image.height >= 366, "Unexpected editor dimensions");
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
    const auto sw=image.logicalPixel(180,180);
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
    editor.dragAt(61,253,235);
    require(motionValue(editor,25)!=swayAmount[0]||motionValue(editor,26)!=swayAmount[1],
            "SWAY AMOUNT knob did not edit its own parameter bank");
    require(motionValue(editor,9)==undAmount[0]&&motionValue(editor,10)==undAmount[1],
            "SWAY AMOUNT drag changed the inactive UNDULATION values");
    editor.dragAt(143,253,235);
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
    editor.dragAt(61,253,235);
    require(motionValue(editor,9)!=undAmount[0]||motionValue(editor,10)!=undAmount[1],
            "UNDULATION AMOUNT knob stopped editing its original parameters");
    editor.dragAt(143,253,235);
    require(motionValue(editor,11)!=undSpeed[0]||motionValue(editor,12)!=undSpeed[1],
            "UNDULATION SPEED knob stopped editing its original parameters");
    require(motionValue(editor,25)==retainedAmount[0]&&motionValue(editor,26)==retainedAmount[1]
            &&motionValue(editor,27)==retainedSpeed[0]&&motionValue(editor,28)==retainedSpeed[1],
            "UNDULATION drags changed the independent SWAY values");
    clickMotionTab(editor,true);
    savePreview(editor,directory,L"grate-sway-edited-100.png");
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
    editor.doubleClickAt(61,253);expect(25,10);expect(26,30);
    editor.doubleClickAt(143,253);expect(27,500);expect(28,1500);
    for(ParamID id=9;id<=12;++id)
        require(motionValue(editor,id)==und[id-9],"Resetting SWAY changed the inactive UNDULATION range");
    const auto sway=std::array<double,4>{motionValue(editor,25),motionValue(editor,26),
                                       motionValue(editor,27),motionValue(editor,28)};
    clickMotionTab(editor,false);
    editor.doubleClickAt(61,253);expect(9,0);expect(10,50);
    editor.doubleClickAt(143,253);expect(11,50);expect(12,200);
    for(ParamID id=25;id<=28;++id)
        require(motionValue(editor,id)==sway[id-25],"Resetting UNDULATION changed the inactive SWAY range");
    editor.doubleClickAt(231,91);expect(4,2000);
}

void bufferCapAndNewControls(Editor& editor,const std::filesystem::path& directory)
{
    const auto setPlain=[&](const char* name,double value)
    {
        const auto id=editor.parameter(name);
        editor.set(name,editor.controller->plainParamToNormalized(id,value));
    };
    setPlain("Tempo sync",0);
    setPlain("Capture buffer",500);
    setPlain("Grain minimum",2000);setPlain("Grain maximum",4000);
    editor.settleFooter();
    const auto requested=std::array<double,2>{motionValue(editor,5),motionValue(editor,6)};
    const auto pinkValues=[](const Capture& image)
    {
        int pink=0;
        for(int y=130;y<159;++y)for(int x=276;x<355;++x)
        {
            const auto colour=image.logicalPixel(x,y);
            // ClearType gives dark text magenta fringes; warning pink is bright.
            pink+=GetRValue(colour)>180&&GetRValue(colour)>GetGValue(colour)+25
                &&GetBValue(colour)>GetGValue(colour)+10;
        }
        return pink;
    };
    require(pinkValues(capture(editor))>5,"BUFFER-limited GRAIN values did not turn pink");
    savePreview(editor,directory,L"grate-buffer-capped-100.png");
    setPlain("Capture buffer",6000);editor.settleFooter();
    require(motionValue(editor,5)==requested[0]&&motionValue(editor,6)==requested[1],
            "Raising BUFFER lost the requested GRAIN handle positions");
    auto uncapped=capture(editor);
    uncapped.save(directory/L"grate-buffer-allowed-100.png");
    require(pinkValues(uncapped)==0,"GRAIN values remained pink after BUFFER allowed the requested sizes");
    const auto mass=editor.plain("Grain mass");
    editor.dragAt(61,91,50);
    require(editor.plain("Grain mass")>mass,"MASS knob did not change parallel stream count");
    const auto pan=std::array<double,2>{editor.plain("Grain pan minimum"),editor.plain("Grain pan maximum")};
    editor.dragAt(485,91,65);
    require(editor.plain("Grain pan minimum")!=pan[0]||editor.plain("Grain pan maximum")!=pan[1],
            "PAN range knob did not respond to dragging");
    const auto dry=editor.plain("Dry level"),wet=editor.plain("Wet level");
    editor.dragAt(456,253,270);editor.dragAt(502,253,270);
    require(editor.plain("Dry level")<dry&&editor.plain("Wet level")<wet,
            "Bottom DRY/WET sliders did not change output levels");
    editor.clickAt(504,14);editor.settleFooter();
    require(editor.plain("Tempo sync")==1,"Header SYNC button did not enable tempo synchronization");
    savePreview(editor,directory,L"grate-sync-100.png");
    editor.clickAt(504,14);editor.settleFooter();
    require(editor.plain("Tempo sync")==0,"Header SYNC button did not disable tempo synchronization");
    editor.doubleClickAt(61,91);editor.doubleClickAt(485,91);
    editor.doubleClickAt(456,253);editor.doubleClickAt(502,253);
    editor.doubleClickAt(231,91);editor.doubleClickAt(315,91);
}

int colourDistance(COLORREF a,COLORREF b);

void fixedEqControls(Editor& editor,const std::filesystem::path& directory,const std::wstring& prefix)
{
    const auto low=editor.parameter("EQ low cut"),high=editor.parameter("EQ high cut");
    const auto saved=std::array<double,2>{motionValue(editor,low),motionValue(editor,high)};
    editor.set("EQ low cut",0);editor.set("EQ high cut",1);editor.settleFooter();
    require(editor.plain("EQ low cut")==20&&editor.plain("EQ high cut")==20000,
            "Default EQ endpoints were not a neutral full-band range");
    const auto initial=capture(editor);const auto panel=initial.logicalPixel(390,250);
    require(colourDistance(initial.logicalPixel(405,223),panel)>40
            &&colourDistance(initial.logicalPixel(405,283),panel)>40,
            "Fixed EQ slider did not show separate upper/high-cut and lower/low-cut handles");
    editor.dragAt(410,283,260);
    require(editor.plain("EQ low cut")>20&&editor.plain("EQ high cut")==20000,
            "Lower EQ handle did not edit the low-cut frequency independently");
    const auto retainedLow=editor.plain("EQ low cut");
    editor.dragAt(410,223,245);
    require(editor.plain("EQ high cut")<20000&&editor.plain("EQ low cut")==retainedLow,
            "Upper EQ handle did not edit the high-cut frequency independently");
    savePreview(editor,directory,(prefix+L"-eq-active.png").c_str());
    editor.set("EQ low cut",0.35);editor.set("EQ high cut",0.65);editor.settleFooter();
    editor.dragAt(410,262,180);
    require(motionValue(editor,low)<=motionValue(editor,high),"EQ lower handle crossed above its upper handle");
    editor.set("EQ low cut",0.35);editor.set("EQ high cut",0.65);editor.settleFooter();
    editor.dragAt(410,244,330);
    require(motionValue(editor,low)<=motionValue(editor,high),"EQ upper handle crossed below its lower handle");
    editor.doubleClickAt(410,253);
    require(editor.plain("EQ low cut")==20&&editor.plain("EQ high cut")==20000,
            "Double-clicking EQ did not restore both neutral endpoints");
    for(const auto id:{low,high}){
        ParameterInfo info{};require(editor.controller->getParameterInfo(id,info)==kResultOk,"Cannot inspect EQ automation parameter");
        require(info.stepCount==0,"Fixed EQ frequency unexpectedly became a discrete or randomized parameter");
    }
    editor.set("EQ low cut",saved[0]);editor.set("EQ high cut",saved[1]);editor.settleFooter();
}

void noneModeDisablesEventControls(Editor& editor,const std::filesystem::path& directory)
{
    editor.set("Trigger mode",0.0);
    editor.set("Grain motion mode",1.0);
    const auto previous=editor.plain("Duck previous event");
    editor.dragAt(352,253,225);
    require(editor.plain("Duck previous event")==previous,"DUCK PRV remained editable in NONE mode");
    editor.clickAt(77,185);editor.settleFooter();
    require(motionValue(editor,24)==1.0,"Disabled UNDULATION tab remained selectable in NONE mode");
    const auto undulation=std::array<double,2>{motionValue(editor,9),motionValue(editor,10)};
    const auto sway=std::array<double,2>{motionValue(editor,25),motionValue(editor,26)};
    editor.dragAt(61,253,235);
    require(motionValue(editor,9)==undulation[0]&&motionValue(editor,10)==undulation[1],
            "NONE mode edited event UNDULATION instead of continuous SWAY");
    require(motionValue(editor,25)!=sway[0]||motionValue(editor,26)!=sway[1],
            "NONE mode did not expose working SWAY controls");
    savePreview(editor,directory,L"grate-none-sway-100.png");
    editor.set("Trigger mode",1.0/3.0);
    clickMotionTab(editor,false);
    editor.doubleClickAt(61,253);
}

int colourDistance(COLORREF a,COLORREF b)
{
    return std::abs(static_cast<int>(GetRValue(a))-GetRValue(b))
        +std::abs(static_cast<int>(GetGValue(a))-GetGValue(b))
        +std::abs(static_cast<int>(GetBValue(a))-GetBValue(b));
}

void tripletControlAppearance(Editor& editor,const std::filesystem::path& directory,const std::wstring& prefix,bool dark)
{
    const auto scale=editor.size().getWidth()>=660?1.5:1.0;
    const auto physical=[&](int value){return static_cast<int>(std::lround(value*scale));};
    const auto set=[&](ParamID id,double value)
    {
        require(editor.controller->setParamNormalized(id,value)==kResultOk,"Cannot configure triplet UI parameter");
        SendMessageW(editor.window,WM_TIMER,1,0);
    };
    const auto value=[&](ParamID id){return editor.controller->getParamNormalized(id);};
    const auto saved=std::array<double,6>{value(4),value(5),value(6),value(29),value(33),value(34)};
    set(29,0);set(33,0);set(34,0);
    editor.clickAt(256,45);editor.clickAt(336,45);
    require(value(33)==0&&value(34)==0,"Disabled T buttons remained clickable without SYNC");
    editor.clickAt(504,14);editor.moveTo(200,330);editor.settleFooter();
    auto inactive=capture(editor);
    editor.clickAt(256,45);
    require(value(33)==1&&value(34)==0,"BUFFER T button changed the wrong triplet bank");
    editor.clickAt(336,45);editor.moveTo(200,330);editor.settleFooter();
    require(value(33)==1&&value(34)==1,"GRAIN T button did not enable its independent triplet bank");
    auto active=capture(editor);active.save(directory/(prefix+L"-triplets-on.png"));
    const auto ink=dark?RGB(225,225,224):RGB(16,16,16);
    const auto panel=inactive.logicalPixel(200,35);
    std::array<double,2> labelGaps{};
    for(int index=0;index<2;++index)
    {
        const int faceLeft=index==0?251:331,centre=index==0?231:315;
        require(active.logicalPixel(faceLeft+1,49)==ink
                &&inactive.logicalPixel(faceLeft+1,49)==panel,
                "T button did not use the INHA filled/outlined toggle face");
        require(inactive.logicalPixel(faceLeft,43)!=panel,"Inactive T button outline was missing");
        int inversePixels=0;
        for(int y=physical(41);y<physical(50);++y)
            for(int x=physical(faceLeft+1);x<physical(faceLeft+9);++x)
                inversePixels+=colourDistance(active.pixel(x,y),panel)<180;
        require(inversePixels>2,"Active T button did not invert its text over the filled face");
        int labelRight=0;
        for(int y=physical(38);y<physical(52);++y)
            for(int x=physical(centre-39);x<physical(faceLeft);++x)
                if(colourDistance(inactive.pixel(x,y),panel)>60)labelRight=std::max(labelRight,x);
        labelGaps[index]=faceLeft-static_cast<double>(labelRight)/scale;
        require(labelRight>0&&labelGaps[index]>=2&&labelGaps[index]<=9,
                "BUFFER/GRAIN label did not retain normal spacing before its T button");
    }
    require(std::abs(labelGaps[0]-labelGaps[1])<=1.5,"BUFFER and GRAIN T buttons use inconsistent label gaps");
    editor.clickAt(504,14);editor.clickAt(256,45);editor.clickAt(336,45);
    require(value(29)==0&&value(33)==1&&value(34)==1,"Disabling SYNC lost retained triplet settings");
    editor.moveTo(200,330);editor.settleFooter();
    auto disabled=capture(editor);disabled.save(directory/(prefix+L"-triplets-disabled.png"));

    // Compare musical durations, not normalized positions from different pools.
    const auto pinkValues=[](const Capture& image)
    {
        int count=0;
        for(int y=130;y<159;++y)for(int x=276;x<355;++x)
        {
            const auto colour=image.logicalPixel(x,y);
            count+=GetRValue(colour)>180&&GetRValue(colour)>GetGValue(colour)+25
                &&GetBValue(colour)>GetGValue(colour)+10;
        }
        return count;
    };
    set(29,1);set(33,0);set(34,1);set(4,0.1);set(5,0.1);set(6,0.1);
    editor.settleFooter();
    require(pinkValues(capture(editor))==0,"Allowed GRAIN triplet was incorrectly capped by straight BUFFER");
    set(33,1);set(34,0);editor.settleFooter();
    auto limited=capture(editor);limited.save(directory/(prefix+L"-triplet-buffer-cap.png"));
    require(pinkValues(limited)>5,"Straight GRAIN exceeding triplet BUFFER did not show its capped value");
    require(value(5)==0.1&&value(6)==0.1,"Triplet BUFFER cap destroyed the requested GRAIN positions");
    for(std::size_t i=0;i<saved.size();++i)set(std::array<ParamID,6>{4,5,6,29,33,34}[i],saved[i]);
    editor.settleFooter();
}

void headerAppearance(Editor& editor,const std::filesystem::path& directory,const wchar_t* appearance,bool dark)
{
    constexpr int syncLeft=486,syncRight=533,menuLeft=424,menuRight=507;
    const auto scale=editor.size().getWidth()>=660?1.5:1.0;
    const auto physical=[&](int value){return static_cast<int>(std::lround(value*scale));};
    const auto savedMode=editor.controller->getParamNormalized(editor.parameter("Trigger mode"));
    const std::wstring prefix=L"grate-header-"+std::wstring(appearance);
    editor.set("Tempo sync",0);editor.moveTo(200,330);editor.settleFooter();
    auto off=capture(editor);off.save(directory/(prefix+L"-sync-off.png"));
    editor.clickAt(504,14);editor.moveTo(200,330);editor.settleFooter();
    require(editor.plain("Tempo sync")==1,"Header SYNC toggle did not activate");
    auto on=capture(editor);on.save(directory/(prefix+L"-sync-on.png"));
    const auto offBackground=off.logicalPixel(syncLeft+2,4),onBackground=on.logicalPixel(syncLeft+2,4);
    require(offBackground!=onBackground,"Active SYNC did not change its button background");
    require(on.logicalPixel(syncLeft,4)==onBackground&&on.logicalPixel(syncRight-1,4)==onBackground,
            "SYNC background did not fill its compact right-aligned button");
    require(on.logicalPixel(syncLeft-1,4)==off.logicalPixel(syncLeft-1,4),
            "SYNC background crossed into the adjacent trigger selector");
    for(int x=syncLeft+4;x<syncRight-4;++x)
        require(on.logicalPixel(x,23)==onBackground,"Active SYNC still draws an underline");

    // Compare foreground coverage after subtracting each background. ClearType
    // changes fringe RGB values when the background changes, even for fixed ink.
    const auto ink=dark?RGB(225,225,224):RGB(16,16,16);
    const auto channel=[](COLORREF colour,int index)->double
    {
        return index==0?GetRValue(colour):index==1?GetGValue(colour):GetBValue(colour);
    };
    double coverageDifference=0,offCoverage=0,onCoverage=0;
    std::size_t foregroundChannels=0;
    int rightmostInk=0;
    for(int y=physical(5);y<physical(22);++y)
        for(int x=physical(syncLeft);x<physical(syncRight);++x)
        {
            const auto a=off.pixel(x,y),b=on.pixel(x,y);
            if(colourDistance(a,offBackground)>45)rightmostInk=std::max(rightmostInk,x);
            for(int c=0;c<3;++c)
            {
                const auto alphaOff=(channel(a,c)-channel(offBackground,c))/(channel(ink,c)-channel(offBackground,c));
                const auto alphaOn=(channel(b,c)-channel(onBackground,c))/(channel(ink,c)-channel(onBackground,c));
                if(std::abs(alphaOff)<0.05&&std::abs(alphaOn)<0.05)continue;
                coverageDifference+=std::abs(alphaOff-alphaOn);
                offCoverage+=alphaOff;onCoverage+=alphaOn;++foregroundChannels;
            }
        }
    require(foregroundChannels>30&&offCoverage>10&&onCoverage>10,"SYNC text was missing in one toggle state");
    require(coverageDifference/foregroundChannels<0.12,"SYNC changed foreground ink between its active and inactive states");
    const auto rightPadding=syncRight-static_cast<double>(rightmostInk)/scale;
    require(rightPadding>=8&&rightPadding<=13,"SYNC text did not use the normal right-side padding");

    editor.clickAt(504,14);editor.moveTo(485,14);editor.settleFooter();
    require(editor.plain("Tempo sync")==0,"Trigger/SYNC boundary unexpectedly toggled synchronization");
    auto menu=capture(editor);menu.save(directory/(prefix+L"-trigger-menu.png"));
    const auto menuBackground=menu.logicalPixel(menuLeft+2,30);
    require(menu.logicalPixel(menuLeft,30)==menuBackground&&menu.logicalPixel(menuRight-1,30)==menuBackground,
            "Trigger selector does not abut SYNC or popup does not use its compact width");
    require(menu.logicalPixel(menuLeft-1,30)!=menuBackground&&menu.logicalPixel(menuRight,30)!=menuBackground,
            "Trigger popup retained excess horizontal padding");
    std::array<double,4> widths{};
    for(int row=0;row<4;++row)
    {
        int left=physical(menuRight),right=physical(menuLeft),pixels=0;
        for(int y=physical(27+row*22+3);y<physical(27+(row+1)*22-3);++y)
            for(int x=physical(menuLeft);x<physical(menuRight);++x)
                if(colourDistance(menu.pixel(x,y),menuBackground)>75)
                {left=std::min(left,x);right=std::max(right,x);++pixels;}
        require(pixels>15,"A full trigger-menu entry was missing");
        require(static_cast<double>(left)/scale-menuLeft>=7
                &&menuRight-static_cast<double>(right)/scale>=7,
                "A full trigger-menu entry was clipped or lacked normal padding");
        widths[row]=static_cast<double>(right-left+1)/scale;
    }
    require(widths[1]>widths[0]+15&&widths[1]>widths[3]+10&&widths[2]>widths[3]+5,
            "Trigger popup replaced full menu entries with compact header labels");
    editor.moveTo(200,330);editor.settleFooter();
    editor.set("Trigger mode",savedMode);
    tripletControlAppearance(editor,directory,prefix,dark);
    fixedEqControls(editor,directory,prefix);
}

void testEditor (Module& module, const std::filesystem::path& directory)
{
    {
        Editor editor (*module.factory);
        const auto initial = editor.size();
        require (initial.getWidth()==819 && initial.getHeight()==549,
                 "New Grate editor must open at 150 percent");
        require (!preferenceEnabled (L"ThemeDark"), "Isolated preferences did not start light");
        require(editor.plain("Duck wet")==0&&editor.plain("Duck previous event")==50,"New DUCK defaults did not use WET0/PREV50");
        savePreview (editor,directory,L"grate-light-150.png");
        swayPreview(editor,directory,L"grate-light-sway-150.png");
        headerAppearance(editor,directory,L"light-150",false);

        // Footer actions become clickable only when the settings gear is open.
        editor.clickAt (87,footerMiddle);
        require (!preferenceEnabled (L"ThemeDark"), "Collapsed theme action was clickable");
        editor.clickAt (119,footerMiddle);
        require (editor.size().getWidth()==819, "Collapsed zoom action was clickable");
        expandSettings (editor);
        capture(editor).save(directory/L"grate-settings-150.png");

        clickSetting(editor,87);
        require(preferenceEnabled(L"ThemeDark"),"Settings theme control did not persist dark mode");
        savePreview(editor,directory,L"grate-dark-150.png");
        swayPreview(editor,directory,L"grate-dark-sway-150.png");
        headerAppearance(editor,directory,L"dark-150",true);
        clickSetting(editor,119);
        require(editor.size().getWidth()==546 && editor.size().getHeight()==366,
                "Settings zoom control did not resize to 100 percent");
        require(editor.frame.resizeCount>0,"Zoom did not notify the host frame");
        savePreview(editor,directory,L"grate-dark-100.png");
        swayPreview(editor,directory,L"grate-dark-sway-100.png");
        headerAppearance(editor,directory,L"dark-100",true);
        clickSetting(editor,87);
        require(!preferenceEnabled(L"ThemeDark"),"Settings theme did not return to light mode");
        savePreview(editor,directory,L"grate-light-100.png");
        swayPreview(editor,directory,L"grate-light-sway-100.png");
        headerAppearance(editor,directory,L"light-100",false);
        motionControlBanks(editor,directory);
        bufferCapAndNewControls(editor,directory);
        noneModeDisablesEventControls(editor,directory);

        // Check meaningful controls through Windows messages, as a VST3 host sees them.
        const auto buffer=editor.plain("Capture buffer");
        editor.dragAt(231,91,70);
        require(editor.plain("Capture buffer")>buffer,"BUFFER knob did not respond to upward drag");
        const auto grainMin=editor.plain("Grain minimum"), grainMax=editor.plain("Grain maximum");
        editor.dragAt(315,91,75);
        require(editor.plain("Grain minimum")!=grainMin || editor.plain("Grain maximum")!=grainMax,
                "GRAIN range knob did not respond to drag");
        require(editor.plain("Grain minimum")<=editor.plain("Grain maximum"),"Range handles crossed");
        motionDefaultReset(editor);

        editor.moveTo(436,14);editor.settleFooter();editor.clickAt(436,14);editor.settleFooter();
        editor.clickAt(436,104);editor.settleFooter();
        require(editor.plain("Trigger mode")==3,"Trigger dropdown did not select VOLUME");
        const auto threshold=editor.plain("Volume threshold");
        editor.dragAt(146,91,70);
        require(editor.plain("Volume threshold")>threshold,"THLD control did not edit volume threshold");
        savePreview(editor,directory,L"grate-volume-100.png");
    }
    // Recreating the view loads the same per-user zoom and theme choices.
    Editor reopened(*module.factory);
    require(reopened.size().getWidth()==546 && !preferenceEnabled(L"ThemeDark"),
            "New editor instance did not retain its appearance preferences");
}
}

int wmain(int argc,wchar_t** argv)
{
    try
    {
        require(argc==3,"Usage: GrateEditorSmoke <fixture-binary> <preview-directory>");
        const std::filesystem::path directory(argv[2]);
        std::filesystem::create_directories(directory);
        IsolatedCurrentUser isolated;
        GdiSession graphics;
        Module module(argv[1]);
        testEditor(module,directory);
        std::cout<<"Grate native editor, settings, controls and appearance persistence passed\n";
        return 0;
    }
    catch(const std::exception& error)
    {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}


