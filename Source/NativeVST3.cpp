#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <ole2.h>
#include <objidl.h>
#include <windowsx.h>
#include <wtypes.h>
#include <shellapi.h>
#include <gdiplus.h>
#include <commctrl.h>

#include "AutomationTimeline.h"
#include "ProcessorParameterState.h"
#include "ParameterDisplay.h"
#include "GrainEngine.h"
#include "GrainTiming.h"
#include "Version.h"
#include "FooterIcons.h"
#include "GrainIcons.h"
#include "GrainPreferences.h"

#include <inha/license/LicenseManager.h>
#include <inha/parameter/Curve.h>
#include <inha/platform/WindowsResources.h>
#include <inha/ui/ControlGestures.h>
#include <inha/ui/Theme.h>

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/ivstevents.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
#include <xmmintrin.h>
#include <cstdlib>
#include <cwchar>
#include <cwctype>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace
{
HINSTANCE moduleInstance = nullptr;
ULONG_PTR gdiplusToken = 0;
inline constexpr auto& licenseHelpUrl = InhaLicense::helpUrl;
using LicenseStatus = InhaLicense::Status;

std::wstring bundleResourcePath (const wchar_t* filename)
{
    return Inha::Platform::bundleResourcePath (moduleInstance, filename);
}

Inha::Ui::ModifierKeys currentModifiers() noexcept
{
    return {
        (GetKeyState (VK_SHIFT) & 0x8000) != 0,
        (GetKeyState (VK_CONTROL) & 0x8000) != 0,
        (GetKeyState (VK_MENU) & 0x8000) != 0
    };
}

std::wstring cleanLicenseMessage (const std::wstring& message)
{
    return InhaLicense::cleanMessage (message);
}

std::wstring licenseMessageWithHelp (const std::wstring& message)
{
    return InhaLicense::messageWithHelp (message);
}


constexpr TUID componentCID = INLINE_UID (0x494E4841, 0x47524149, 0x4E465831, 0x7DA92E01);
constexpr TUID controllerCID = INLINE_UID (0x494E4841, 0x47524149, 0x4E435431, 0x7DA92E02);
enum Parameter : ParamID {
 triggerMode, amount, sensitivity, threshold, scatter, grainMinimum, grainMaximum,
 envMinimum, envMaximum, undulationMinimum, undulationMaximum, speedMinimum, speedMaximum,
 attackMinimum, attackMaximum, releaseMinimum, releaseMaximum, duckDry, duckPrevious,
 dryLevel, wetLevel, seedHigh, seedLow, bypass,
 motionMode, swayAmountMinimum, swayAmountMaximum, swaySpeedMinimum, swaySpeedMaximum,
 tempoSync, mass, panMinimum, panMaximum, parameterCount
};
struct ParamSpec { const char* title; const char* shortTitle; const char* unit;
 double minimum, maximum, defaultValue; bool time; };
constexpr std::array<ParamSpec,parameterCount> specs {{
 {"Trigger mode","Trigger","",0,3,1,false},
 {"MIDI trigger probability","Amount","%",0,100,50,false},
 {"Transient pickup","Pickup","%",0,100,50,false},
 {"Volume threshold","Threshold","dB",-60,0,-24,false},
 {"Capture buffer","Buffer","ms",40,6000,2000,true},
 {"Grain minimum","Grain min","ms",40,6000,200,true},
 {"Grain maximum","Grain max","ms",40,6000,1000,true},
 {"Envelope minimum","Env min","%",0,100,0,false},
 {"Envelope maximum","Env max","%",0,100,50,false},
 {"Undulation amount minimum","Undl min","%",-100,100,0,false},
 {"Undulation amount maximum","Undl max","%",-100,100,50,false},
 {"Undulation duration minimum","Speed min","%",1,400,50,false},
 {"Undulation duration maximum","Speed max","%",1,400,200,false},
 {"Event attack minimum","Attack min","ms",40,6000,500,true},
 {"Event attack maximum","Attack max","ms",40,6000,1000,true},
 {"Event release minimum","Release min","ms",40,6000,800,true},
 {"Event release maximum","Release max","ms",40,6000,6000,true},
 {"Duck dry","Duck dry","%",0,100,90,false},
 {"Duck previous event","Duck previous","%",0,100,90,false},
 {"Dry level","Dry","dB",-60,6,0,false},
 {"Wet level","Wet","dB",-60,6,0,false},
 {"Seed high word","Seed high","",0,65535,3887,false},
 {"Seed low word","Seed low","",0,65535,5271,false},
 {"Bypass","Bypass","",0,1,0,false},
 {"Grain motion mode","Motion","",0,1,0,false},
 {"Sway amount minimum","Sway min","%",0,100,10,false},
 {"Sway amount maximum","Sway max","%",0,100,30,false},
 {"Sway duration minimum","Sway speed min","ms",40,6000,500,true},
 {"Sway duration maximum","Sway speed max","ms",40,6000,1500,true},
 {"Tempo sync","Sync","",0,1,0,false},
 {"Grain mass","Mass","x",1,8,1,false},
 {"Grain pan minimum","Pan min","%",-100,100,0,false},
 {"Grain pan maximum","Pan max","%",-100,100,0,false}
}};
bool iidEqual (const TUID a, const TUID b) noexcept
{
    return std::memcmp (a, b, sizeof (TUID)) == 0;
}

template <std::size_t Size>
void copyAscii (char (&destination)[Size], const char* source)
{
    std::memset (destination, 0, Size);
    if (source != nullptr)
        std::strncpy (destination, source, Size - 1);
}

template <std::size_t Size>
void copyUtf16 (char16 (&destination)[Size], const char* source)
{
    std::fill (std::begin (destination), std::end (destination), static_cast<char16> (0));
    if (source == nullptr)
        return;

    for (std::size_t i = 0; i + 1 < Size && source[i] != 0; ++i)
        destination[i] = static_cast<char16> (static_cast<unsigned char> (source[i]));
}

void copyUtf16 (String128 destination, const char* source)
{
    std::fill (destination, destination + 128, static_cast<char16> (0));
    if (source == nullptr)
        return;

    for (std::size_t i = 0; i < 127 && source[i] != 0; ++i)
        destination[i] = static_cast<char16> (static_cast<unsigned char> (source[i]));
}

double clampNormal (double value) noexcept
{
    return std::clamp (std::isfinite (value) ? value : 0.0, 0.0, 1.0);
}


bool isButtonParameter(ParamID id) noexcept { return id==triggerMode || id==motionMode || id==tempoSync || id==mass || id==bypass || id==seedHigh || id==seedLow; }
double quantizeNormal(ParamID id,double x) noexcept {
 x=clampNormal(x); if(id==triggerMode) return std::round(x*3.0)/3.0;
 if(id==bypass || id==motionMode || id==tempoSync) return x>=0.5?1.0:0.0;
 if(id==seedHigh || id==seedLow) return std::round(x*65535.0)/65535.0;
 if(id==mass) return std::round(x*7.0)/7.0;
 return x;
}
double toPlain(ParamID id,double x) noexcept {
 if(id>=parameterCount) return 0; x=quantizeNormal(id,x); const auto& p=specs[id];
 if(p.time) x=x*x*x;
 return p.minimum+(p.maximum-p.minimum)*x;
}
double toNormal(ParamID id,double x) noexcept {
 if(id>=parameterCount) return 0; const auto& p=specs[id];
 x=clampNormal((x-p.minimum)/(p.maximum-p.minimum));
 return quantizeNormal(id,p.time?std::cbrt(x):x);
}
double defaultStateNormalized(ParamID id) noexcept { return toNormal(id,specs[id].defaultValue); }
const char* choiceName(ParamID id,double plain) {
 if(id==triggerMode) { constexpr const char* names[]{"NONE","TRANSIENTS","MIDI NOTES","VOLUME"};
 return names[std::clamp(static_cast<int>(std::lround(plain)),0,3)]; }
 if(id==motionMode) return plain>=0.5?"SWAY":"UNDULATION";
 return id==bypass||id==tempoSync?(plain>=0.5?"ON":"OFF"):nullptr;
}
std::wstring displayValue(ParamID id,double value) {
 wchar_t text[64]{};
 if(specs[id].time) {
  if(value>999.0) std::swprintf(text,64,L"%.2f S",value/1000.0);
  else std::swprintf(text,64,L"%.0f MS",value);
 } else if(id==dryLevel || id==wetLevel) {
  if(value<=-59.9) return L"-INF";
  std::swprintf(text,64,L"%.1f dB",value);
 } else if(id==threshold) std::swprintf(text,64,L"%.1f dB",value);
 else if(id==undulationMinimum || id==undulationMaximum) std::swprintf(text,64,L"%+.0f%%",value);
 else if(id==mass) std::swprintf(text,64,L"%.0fx",value);
 else if(id==panMinimum || id==panMaximum) {
  if(std::abs(value)<0.5)return L"0";
  std::swprintf(text,64,L"%.0f %lc",std::abs(value),value<0?L'L':L'R');
 }
 else std::swprintf(text,64,L"%.0f%%",value);
 return text;
}
struct SavedStateHeader { std::uint32_t magic=0x47524149u,version=3; };
constexpr std::size_t stateV1ParameterCount = 24, stateV2ParameterCount = 29;
static_assert (bypass == 23 && motionMode == stateV1ParameterCount && tempoSync == stateV2ParameterCount && parameterCount == 33);
constexpr std::size_t maximumAutomationPoints=2048;
using ParameterTimeline=InhaAutomation::Timeline<parameterCount,maximumAutomationPoints>;
constexpr int sidePanelLogicalWidth=13, contentEditorWidth=474, contentLeft=13,
 contentRight=contentLeft+contentEditorWidth, baseEditorWidth=contentEditorWidth+26,
 headerHeight=28, footerHeight=29, controlsBottom=337, baseEditorHeight=controlsBottom+footerHeight,
 zoomedEditorWidth=baseEditorWidth*3/2, zoomedEditorHeight=baseEditorHeight*3/2,
 footerButtonWidth=28,footerButtonStride=32,triggerMenuRowHeight=22,triggerMenuTop=headerHeight-1;
constexpr float hoverAnimationDurationMs=120.0f,hoverSlideDistance=6.0f;
constexpr RECT footerSettingsRect{contentLeft+31,controlsBottom,contentLeft+59,baseEditorHeight};
constexpr RECT footerButtonRect(int i) {return {footerSettingsRect.right+1+i*footerButtonStride,controlsBottom,
 footerSettingsRect.right+1+i*footerButtonStride+footerButtonWidth,baseEditorHeight};}
constexpr RECT footerActionsArea{footerSettingsRect.left,controlsBottom,contentLeft+126,baseEditorHeight};
constexpr RECT syncRect{contentRight-58,0,contentRight,headerHeight};
constexpr RECT triggerSelectorRect{contentRight-134,0,syncRect.left,headerHeight};
constexpr RECT triggerMenuRect{contentRight-134,triggerMenuTop,contentRight,triggerMenuTop+4*triggerMenuRowHeight};
constexpr RECT triggerMenuItemRect(int i){return {triggerMenuRect.left,triggerMenuTop+i*triggerMenuRowHeight,triggerMenuRect.right,triggerMenuTop+(i+1)*triggerMenuRowHeight};}
constexpr RECT seedRefreshRect{triggerSelectorRect.left-29,0,triggerSelectorRect.left-5,headerHeight};
constexpr RECT seedValueRect{seedRefreshRect.left-78,0,seedRefreshRect.left-1,headerHeight};
constexpr RECT logoHitRect{contentLeft,0,contentLeft+125,headerHeight};
constexpr std::array<RECT,2> motionTabRects{{
 {contentLeft+8,174,contentLeft+120,196},
 {contentLeft+120,174,contentLeft+177,196}
}};
constexpr ParamID symmetricRangeDrag=parameterCount+1,translateRangeDrag=parameterCount+2,pendingRangeDrag=parameterCount+3;
struct Control {ParamID minimum,maximum;int x,y;const wchar_t* label;bool slider;};
constexpr std::array<Control,14> controls{{
 {mass,parameterCount,contentLeft+48,91,L"MASS",false},
 {amount,parameterCount,contentLeft+124,91,L"AMOUNT",false},
 {scatter,parameterCount,contentLeft+199,91,L"BUFFER",false},
 {grainMinimum,grainMaximum,contentLeft+275,91,L"GRAIN",false},
 {envMinimum,envMaximum,contentLeft+350,91,L"ENV",false},
 {panMinimum,panMaximum,contentLeft+426,91,L"PAN",false},
 {undulationMinimum,undulationMaximum,contentLeft+48,253,L"AMOUNT",false},
 {speedMinimum,speedMaximum,contentLeft+130,253,L"SPEED",false},
 {attackMinimum,attackMaximum,contentLeft+207,253,L"A",true},
 {releaseMinimum,releaseMaximum,contentLeft+250,253,L"R",true},
 {duckDry,parameterCount,contentLeft+301,253,L"DRY",true},
 {duckPrevious,parameterCount,contentLeft+339,253,L"PRV",true},
 {dryLevel,parameterCount,contentLeft+397,253,L"DRY",true},
 {wetLevel,parameterCount,contentLeft+443,253,L"WET",true}
}};
class PluginInstance;
class NativeView final : public IPlugView
{
public:
    explicit NativeView (PluginInstance&);
    ~NativeView();

    tresult PLUGIN_API queryInterface (const TUID, void**) override;
    uint32 PLUGIN_API addRef() override { return ++references; }
    uint32 PLUGIN_API release() override;

    tresult PLUGIN_API isPlatformTypeSupported (FIDString type) override;
    tresult PLUGIN_API attached (void* parent, FIDString type) override;
    tresult PLUGIN_API removed() override;
    tresult PLUGIN_API onWheel (float) override { return kResultFalse; }
    tresult PLUGIN_API onKeyDown (char16, int16, int16) override;
    tresult PLUGIN_API onKeyUp (char16, int16, int16) override
    {
        return seedEditing ? kResultTrue : kResultFalse;
    }
    tresult PLUGIN_API getSize (ViewRect* size) override;
    tresult PLUGIN_API onSize (ViewRect* size) override;
    tresult PLUGIN_API onFocus (TBool state) override;
    tresult PLUGIN_API setFrame (IPlugFrame* newFrame) override;
    tresult PLUGIN_API canResize() override { return kResultFalse; }
    tresult PLUGIN_API checkSizeConstraint (ViewRect* rect) override;

    void invalidate();

private:
    static LRESULT CALLBACK windowProcedure (HWND, UINT, WPARAM, LPARAM);
    LRESULT handleMessage (HWND, UINT, WPARAM, LPARAM);
    void paint (HDC);
    bool updateParameterDisplay();
    void mouseDown (int x, int y, bool rightButton = false);
    void mouseDoubleClick (int x, int y);
    void mouseDrag (int x, int y);
    void mouseUp();
    void toggleZoom();
    bool setZoom (bool);
    void toggleTheme();
    void pollPreferences();
    void showPreferenceError (const std::wstring&);
    void updateFooterTooltips();
    void updateFooterHover (int, int);
    void setFooterExpanded (bool);
    void updateFooterAnimation();
    void invalidateFooter();
    void updateTriggerMenuHover (int, int);
    void setTriggerMenuOpen (bool, bool dismissUntilLeave = false);
    void updateTriggerMenuAnimation();
    [[nodiscard]] int triggerMenuSlidePixels() const;
    [[nodiscard]] int triggerMenuItemAt (int, int) const;
    void beginSeedEdit();
    void editSeedCharacter (wchar_t character);
    void commitSeed();
    [[nodiscard]] RECT knobFor (ParamID id) const;
    [[nodiscard]] RECT verticalSliderFor (ParamID id) const;
    [[nodiscard]] RECT verticalLabelFor (ParamID id) const;
    [[nodiscard]] RECT verticalValueFor (ParamID id) const;
    [[nodiscard]] bool pointIn (RECT rect, int x, int y) const;
    [[nodiscard]] double knobValue (RECT knob, int x, int y) const;
    [[nodiscard]] ParamID firstControlParameter() const;
    [[nodiscard]] bool controlEnabled (ParamID) const;
    [[nodiscard]] ParamID visibleControlParameter (ParamID) const;
    [[nodiscard]] HCURSOR cursorFor (int x, int y) const;

    std::atomic<uint32> references { 1 };
    PluginInstance& owner;
    HWND window = nullptr;
    HWND footerTooltips = nullptr;
    bool footerExpanded = false;
    bool triggerMenuOpen = false;
    int triggerMenuHover = -1;
    bool triggerMenuDismissed = false;
    int triggerPointerX = -1;
    int triggerPointerY = -1;
    float triggerMenuReveal = 0.0f;
    float triggerMenuAnimationFrom = 0.0f;
    ULONGLONG triggerMenuAnimationStart = 0;
    float footerReveal = 0.0f;
    float footerAnimationFrom = 0.0f;
    ULONGLONG footerAnimationStart = 0;
    IPlugFrame* frame = nullptr;
    ViewRect viewSize { 0, 0, baseEditorWidth, baseEditorHeight };
    ParamID dragParameter = parameterCount;
    int dragStartX = 0;
    int dragStartY = 0;
    double dragStartValue = 0.0;
    ParamID rangeDragMinimumId = parameterCount;
    ParamID rangeDragMaximumId = parameterCount;
    double rangeDragMinimumStart = 0.0;
    double rangeDragMaximumStart = 0.0;
    bool rangeDragHorizontal = false;
    bool seedEditing = false;
    bool seedSelectAll = false;
    std::wstring seedText;
    std::wstring loadedFontPath;
    Gdiplus::Image* headerLogo = nullptr;
    Gdiplus::Image* woodLeft = nullptr;
    Gdiplus::Image* woodRight = nullptr;
    bool lastActivity = false;
    int lastMotionMode = -1;
    int lastTriggerMode = -1;
    LicenseStatus lastLicenseStatus = LicenseStatus::checking;
    RECT licenseHelpRect {};
    bool zoomed = false;
    bool darkTheme = false;
    bool observedGlobalZoom = false;
    bool nativeDialogOpen = false;
    ULONGLONG lastPreferencesPoll = 0;
    InhaAutomation::ParameterDisplay<parameterCount> parameterDisplay;
    ULONGLONG lastDisplayTick = 0;
    std::uint32_t lastDisplayRevision = 0;
};

class PluginInstance final : public IComponent, public IAudioProcessor, public IEditController,
                             public IProcessContextRequirements {
public:
 PluginInstance() {
  std::array<double,parameterCount> initial{};
  for(ParamID id=0;id<parameterCount;++id) {
   const auto v=defaultStateNormalized(id); initial[id]=v; values[id].store(v);
   localEditValues[id].store(v); localEditing[id].store(false);
  }
  processorParameters.commit(initial); license.start(moduleInstance);
 }
 ~PluginInstance() { if(componentHandler) componentHandler->release(); if(componentHandler2) componentHandler2->release(); }
 tresult PLUGIN_API queryInterface(const TUID iid,void** object) override {
  if(!object) return kInvalidArgument; *object=nullptr;
  if(iidEqual(iid,FUnknown_iid)||iidEqual(iid,IPluginBase_iid)||iidEqual(iid,IComponent_iid)) *object=static_cast<IComponent*>(this);
  else if(iidEqual(iid,IAudioProcessor_iid)) *object=static_cast<IAudioProcessor*>(this);
  else if(iidEqual(iid,IProcessContextRequirements_iid)) *object=static_cast<IProcessContextRequirements*>(this);
  else if(iidEqual(iid,IEditController_iid)) *object=static_cast<IEditController*>(this);
  else return kNoInterface;
  addRef();return kResultOk;
 }
 uint32 PLUGIN_API addRef() override{return ++references;}
 uint32 PLUGIN_API release() override{auto r=--references;if(!r)delete this;return r;}
 tresult PLUGIN_API initialize(FUnknown*) override{return kResultOk;}
 tresult PLUGIN_API terminate() override{return kResultOk;}
 tresult PLUGIN_API getControllerClassId(TUID id) override{std::memcpy(id,controllerCID,sizeof(TUID));return kResultOk;}
 tresult PLUGIN_API setIoMode(IoMode) override{return kResultOk;}
 int32 PLUGIN_API getBusCount(MediaType type,BusDirection direction) override{
  return type==kAudio?1:type==kEvent&&direction==kInput?1:0;
 }
 tresult PLUGIN_API getBusInfo(MediaType type,BusDirection direction,int32 index,BusInfo& info) override{
  if(index!=0 || !getBusCount(type,direction))return kInvalidArgument;
  std::memset(&info,0,sizeof(info));info.mediaType=type;info.direction=direction;
  info.channelCount=type==kAudio?SpeakerArr::getChannelCount(arrangement):16;
  copyUtf16(info.name,type==kEvent?"MIDI input":direction==kInput?"Input":"Output");
  info.busType=kMain;info.flags=BusInfo::kDefaultActive;return kResultOk;
 }
 tresult PLUGIN_API getRoutingInfo(RoutingInfo& input,RoutingInfo& output) override{
  if(input.mediaType!=kAudio || input.busIndex!=0)return kResultFalse;output=input;return kResultOk;
 }
 tresult PLUGIN_API activateBus(MediaType type,BusDirection direction,int32 index,TBool) override{
  return index==0&&getBusCount(type,direction)?kResultOk:kInvalidArgument;
 }
 tresult PLUGIN_API setActive(TBool state) override{if(state&&prepared)resetEngine();return kResultOk;}
 tresult PLUGIN_API setState(IBStream* stream) override{return readState(stream);}
 tresult PLUGIN_API getState(IBStream* stream) override{return writeState(stream);}
 tresult PLUGIN_API setBusArrangements(SpeakerArrangement* inputs,int32 ni,SpeakerArrangement* outputs,int32 no) override{
  if(!inputs||!outputs||ni!=1||no!=1||inputs[0]!=outputs[0]||(inputs[0]!=SpeakerArr::kMono&&inputs[0]!=SpeakerArr::kStereo))return kResultFalse;
  arrangement=inputs[0];return kResultTrue;
 }
 tresult PLUGIN_API getBusArrangement(BusDirection,int32 index,SpeakerArrangement& result) override{
  if(index!=0)return kInvalidArgument;result=arrangement;return kResultOk;
 }
 tresult PLUGIN_API canProcessSampleSize(int32 size) override{return size==kSample32||size==kSample64?kResultTrue:kResultFalse;}
 uint32 PLUGIN_API getLatencySamples() override{return 0;}
 uint32 PLUGIN_API getProcessContextRequirements() override{return IProcessContextRequirements::kNeedTransportState|IProcessContextRequirements::kNeedTempo;}
 tresult PLUGIN_API setupProcessing(ProcessSetup& setup) override{
  if((setup.symbolicSampleSize!=kSample32&&setup.symbolicSampleSize!=kSample64)||!std::isfinite(setup.sampleRate)||setup.sampleRate<8000||setup.sampleRate>384000)return kResultFalse;
  sampleRate=setup.sampleRate;
  try{engine.prepare(sampleRate);}catch(...){prepared=false;return kOutOfMemory;}
  prepared=true;resetEngine();return kResultOk;
 }
 tresult PLUGIN_API setProcessing(TBool state) override{
  processingForView.store(state!=0);
  if(!state)for(ParamID id=0;id<parameterCount;++id)values[id].store(processorParameters.valueForState(id));
  if(prepared)resetEngine();return kResultOk;
 }
 tresult PLUGIN_API process(ProcessData& data) override {
  struct DenormalGuard { unsigned previous=_mm_getcsr(); DenormalGuard(){_mm_setcsr(previous|0x8040u);} ~DenormalGuard(){_mm_setcsr(previous);} } denormalGuard;
  prepareAutomationTimeline(data.inputParameterChanges,std::max(0,data.numSamples));
  if(data.numSamples<=0||data.numInputs<1||data.numOutputs<1||!data.inputs||!data.outputs){finishAutomationBlock(std::max(0,data.numSamples));return kResultOk;}
  if(!prepared)return kResultFalse;
  if(data.symbolicSampleSize!=kSample32&&data.symbolicSampleSize!=kSample64)return kResultFalse;
  const auto& input=data.inputs[0];auto& output=data.outputs[0];
  const int channels=std::min({input.numChannels,output.numChannels,2});
  if(channels<1)return kInvalidArgument;
  if(data.symbolicSampleSize==kSample32&&(!input.channelBuffers32||!output.channelBuffers32))return kInvalidArgument;
  if(data.symbolicSampleSize==kSample64&&(!input.channelBuffers64||!output.channelBuffers64))return kInvalidArgument;
  for(int c=0;c<channels;++c)if(data.symbolicSampleSize==kSample32?(!input.channelBuffers32[c]||!output.channelBuffers32[c]):(!input.channelBuffers64[c]||!output.channelBuffers64[c]))return kInvalidArgument;
  if(data.processContext&&(data.processContext->state&ProcessContext::kTempoValid)&&std::isfinite(data.processContext->tempo)&&data.processContext->tempo>0)
   hostTempo.store(InhaGrain::Timing::tempo(data.processContext->tempo),std::memory_order_relaxed);
  const bool playing=data.processContext&&(data.processContext->state&ProcessContext::kPlaying);
  const auto timeline=data.processContext&&playing?data.processContext->projectTimeSamples:freeRunningSample;
  const bool discontinuity=playing&&(!hostWasPlaying||!haveExpectedSample||std::llabs(timeline-expectedSample)>1);
  if(needsReset.exchange(false)||discontinuity)engine.reset(timeline);
  hostWasPlaying=playing;haveExpectedSample=playing;expectedSample=timeline+data.numSamples;freeRunningSample=timeline+data.numSamples;
  std::array<int,maximumAutomationPoints+2> boundaries{};
  const auto count=automationTimeline.boundaries(boundaries);
  int eventIndex=0;const int eventCount=data.inputEvents?data.inputEvents->getEventCount():0;
  Event nextEvent{};bool haveEvent=eventCount>0&&data.inputEvents->getEvent(0,nextEvent)==kResultOk;
  const bool denied=license.denied();
  uint64 silentChannels=(uint64{1}<<channels)-1;
  for(std::size_t boundary=0;boundary+1<count;++boundary){
   const int start=boundaries[boundary],end=boundaries[boundary+1];if(end<=start)continue;
   auto current=automationValuesAt(start),last=automationValuesAt(end);
   std::array<double,parameterCount> increment{};bool ramps=false;
   for(ParamID id=0;id<parameterCount;++id){if(!isButtonParameter(id))increment[id]=(last[id]-current[id])/(end-start);ramps=ramps||increment[id]!=0;}
   engine.setParameters(makeSettings(current));
   for(int i=start;i<end;++i){
    if(i!=start&&ramps){for(ParamID id=0;id<parameterCount;++id)current[id]+=increment[id];engine.setParameters(makeSettings(current));}
    while(haveEvent&&nextEvent.sampleOffset<=i){
     if(nextEvent.sampleOffset==i&&nextEvent.type==Event::kNoteOnEvent&&nextEvent.noteOn.velocity>0&&!denied)engine.noteOn();
     ++eventIndex;haveEvent=eventIndex<eventCount&&data.inputEvents->getEvent(eventIndex,nextEvent)==kResultOk;
    }
    const auto read=[&](int c)->double{
     if(input.silenceFlags&(uint64{1}<<c))return 0.0;
     return data.symbolicSampleSize==kSample32?static_cast<double>(input.channelBuffers32[c][i]):input.channelBuffers64[c][i];
    };
    const std::array<double,2> raw{{read(0),channels>1?read(1):read(0)}};
    const std::array<float,2> source{{static_cast<float>(raw[0]),static_cast<float>(raw[1])}};
    const auto wet=denied?source:engine.process(source[0],source[1]);
    const float bypassMix=denied?1.0f:engine.bypassMix();
    for(int c=0;c<channels;++c){
     double result=wet[c];
     if(std::isfinite(raw[c])){
      if(bypassMix>=1.0f)result=raw[c];
      else if(bypassMix>0&&std::isfinite(source[c]))result+=(raw[c]-static_cast<double>(source[c]))*bypassMix;
     }
     if(!std::isfinite(result))result=0.0;
     if(result!=0.0)silentChannels&=~(uint64{1}<<c);
     if(data.symbolicSampleSize==kSample32)output.channelBuffers32[c][i]=static_cast<float>(result);
     else output.channelBuffers64[c][i]=result;
    }
   }
  }
  finishAutomationBlock(data.numSamples);grainActive.store(!denied&&engine.isActive());output.silenceFlags=silentChannels;return kResultOk;
 }
 uint32 PLUGIN_API getTailSamples() override{return static_cast<uint32>(std::min(sampleRate*(InhaGrain::Timing::maximumCaptureMilliseconds*0.002+0.1),4294967294.0));}
 tresult PLUGIN_API setComponentState(IBStream* state) override{return readState(state);}
 int32 PLUGIN_API getParameterCount() override{return parameterCount;}
 tresult PLUGIN_API getParameterInfo(int32 index,ParameterInfo& info) override{
  if(index<0||index>=parameterCount)return kInvalidArgument;const auto& p=specs[index];std::memset(&info,0,sizeof(info));
  info.id=index;copyUtf16(info.title,p.title);copyUtf16(info.shortTitle,p.shortTitle);copyUtf16(info.units,p.unit);
  info.stepCount=index==triggerMode?3:index==bypass||index==motionMode||index==tempoSync?1:index==mass?7:index==seedHigh||index==seedLow?65535:0;
  info.defaultNormalizedValue=defaultStateNormalized(index);info.unitId=0;
  info.flags=ParameterInfo::kCanAutomate|((index==triggerMode||index==motionMode||index==tempoSync)?ParameterInfo::kIsList:0)|(index==bypass?ParameterInfo::kIsBypass:0);return kResultOk;
 }
 tresult PLUGIN_API getParamStringByValue(ParamID id,ParamValue normalized,String128 text) override{
  if(id>=parameterCount)return kInvalidArgument;const double p=toPlain(id,normalized);
  if(const char* name=choiceName(id,p)){copyUtf16(text,name);return kResultOk;}
  const auto value=id==seedHigh||id==seedLow?std::to_wstring(static_cast<unsigned>(std::lround(p))):
   specs[id].time&&plain(tempoSync)>=0.5?std::wstring(InhaGrain::Timing::syncLabel(normalized)):displayValue(id,p);
  for(std::size_t i=0;i<128;++i)text[i]=i<value.size()?static_cast<char16>(value[i]):0;
  return kResultOk;
 }
 tresult PLUGIN_API getParamValueByString(ParamID id,TChar* text,ParamValue& normalized) override{
  if(id>=parameterCount||!text)return kInvalidArgument;
  char b[128]{};for(int i=0;i<127&&text[i];++i)b[i]=static_cast<char>(text[i]);
  if(id==triggerMode||id==motionMode)for(int m=0;m<(id==triggerMode?4:2);++m)if(_stricmp(b,choiceName(id,m))==0){normalized=toNormal(id,m);return kResultOk;}
  if((id==bypass||id==tempoSync)&&(_stricmp(b,"ON")==0||_stricmp(b,"OFF")==0)){normalized=_stricmp(b,"ON")==0?1:0;return kResultOk;}
  if((id==dryLevel||id==wetLevel)&&_strnicmp(b,"-INF",4)==0){normalized=0;return kResultOk;}
  if(specs[id].time&&plain(tempoSync)>=0.5){
   std::wstring label;for(int i=0;text[i]&&i<127;++i)label.push_back(static_cast<wchar_t>(std::towupper(text[i])));
   const auto note=InhaGrain::Timing::syncNormalizedFromLabel(label);
   if(note>=0){normalized=note;return kResultOk;}
   if(label.find(L'/')!=std::wstring::npos)return kResultFalse;
  }
  char* end=nullptr;auto value=std::strtod(b,&end);if(end==b||!std::isfinite(value))return kResultFalse;
  while(*end==' ')++end;if(specs[id].time&&(*end=='s'||*end=='S'))value*=1000;
  if(id==panMinimum||id==panMaximum){if(*end=='l'||*end=='L')value=-std::abs(value);else if(*end=='r'||*end=='R')value=std::abs(value);}
  normalized=plainParamToNormalized(id,value);return kResultOk;
 }
 ParamValue PLUGIN_API normalizedParamToPlain(ParamID id,ParamValue value) override{
  return id<parameterCount&&specs[id].time&&plain(tempoSync)>=0.5
   ?InhaGrain::Timing::syncMilliseconds(value,tempoForView()):toPlain(id,value);
 }
 ParamValue PLUGIN_API plainParamToNormalized(ParamID id,ParamValue value) override{
  if(id<parameterCount&&specs[id].time&&plain(tempoSync)>=0.5){
   std::size_t nearest=0;double distance=std::numeric_limits<double>::max();
   for(std::size_t i=0;i<InhaGrain::Timing::beats.size();++i){
    const auto d=std::abs(InhaGrain::Timing::syncMilliseconds(InhaGrain::Timing::syncNormalized(i),tempoForView())-value);
    if(d<distance){nearest=i;distance=d;}
   }
   return InhaGrain::Timing::syncNormalized(nearest);
  }
  return toNormal(id,value);
 }
 ParamValue PLUGIN_API getParamNormalized(ParamID id) override{return id<parameterCount?values[id].load():0;}
 tresult PLUGIN_API setParamNormalized(ParamID id,ParamValue value) override{if(id>=parameterCount)return kInvalidArgument;values[id].store(quantizeNormal(id,value));return kResultOk;}
    tresult PLUGIN_API setComponentHandler (IComponentHandler* handler) override
    {
        if (handler != nullptr)
            handler->addRef();
        IComponentHandler2* handler2 = nullptr;
        if (handler != nullptr)
            (void) handler->queryInterface (IComponentHandler2_iid,
                                            reinterpret_cast<void**> (&handler2));
        if (componentHandler != nullptr)
            componentHandler->release();
        if (componentHandler2 != nullptr)
            componentHandler2->release();
        componentHandler = handler;
        componentHandler2 = handler2;
        return kResultOk;
    }

    IPlugView* PLUGIN_API createView (FIDString name) override
    {
        if (name == nullptr || std::strcmp (name, ViewType::kEditor) != 0)
            return nullptr;
        auto* result = new NativeView (*this);
        view = result;
        return result;
    }

    void viewDestroyed (NativeView* destroyed)
    {
        if (view == destroyed)
            view = nullptr;
    }
    void invalidateView();
    void editFromView (ParamID id, double normalized, int gesture)
    {
        if (id >= parameterCount)
            return;
        normalized = quantizeNormal (id, normalized);
        if (specs[id].time && plain(tempoSync)>=0.5)
            normalized=InhaGrain::Timing::syncNormalized(InhaGrain::Timing::syncIndex(normalized));
        values[id].store (normalized, std::memory_order_relaxed);
        localEditValues[id].store (normalized, std::memory_order_relaxed);
        localEditing[id].store (gesture <= 0, std::memory_order_release);
        processorParameters.queueEdit (id, normalized);
        if (componentHandler != nullptr)
        {
            if (gesture < 0 || gesture == 2)
                componentHandler->beginEdit (id);
            componentHandler->performEdit (id, normalized);
            if (gesture > 0)
                componentHandler->endEdit (id);
        }
        invalidateView();
    }
    [[nodiscard]] double normalized (ParamID id) const
    {
        if (id >= parameterCount)
            return 0.0;
        if (localEditing[id].load (std::memory_order_acquire))
            return localEditValues[id].load (std::memory_order_relaxed);
        return processingForView.load (std::memory_order_acquire)
            ? processorParameters.valueForState (id)
            : values[id].load (std::memory_order_relaxed);
    }
    [[nodiscard]] bool editingForView (ParamID id) const
    {
        return localEditing[id].load (std::memory_order_acquire);
    }
    bool snapshotForView (std::array<double, parameterCount>& snapshot) const
    {
        if (processingForView.load (std::memory_order_acquire))
        {
            if (! processorParameters.snapshotForDisplay (snapshot))
                return false;
        }
        else
            for (ParamID id = 0; id < parameterCount; ++id)
                snapshot[id] = values[id].load (std::memory_order_relaxed);
        for (ParamID id = 0; id < parameterCount; ++id)
            if (localEditing[id].load (std::memory_order_acquire))
                snapshot[id] = localEditValues[id].load (std::memory_order_relaxed);
        return true;
    }
    [[nodiscard]] std::uint32_t displayRevision() const
    {
        return displayStateRevision.load (std::memory_order_acquire);
    }
    [[nodiscard]] double plain (ParamID id) const { return toPlain (id, normalized (id)); }
    [[nodiscard]] double tempoForView() const { return hostTempo.load(std::memory_order_relaxed); }
    void toggleLevelFromView (ParamID id)
    {
        if (id != dryLevel && id != wetLevel)
            return;
        auto& remembered = id == dryLevel ? rememberedDryLevel : rememberedWetLevel;
        const auto current = normalized (id);
        if (plain (id) <= -59.9)
            editFromView (id, remembered.load (std::memory_order_relaxed), 2);
        else
        {
            remembered.store (current, std::memory_order_relaxed);
            editFromView (id, 0.0, 2);
        }
    }
    void resetLevelFromView (ParamID id)
    {
        if (id != dryLevel && id != wetLevel)
            return;
        const auto zero = toNormal (id, 0.0);
        (id == dryLevel ? rememberedDryLevel : rememberedWetLevel).store (
            zero, std::memory_order_relaxed);
        editFromView (id, zero, 2);
    }
    void resetControlFromView (ParamID minimum, ParamID maximum)
    {
        if (minimum >= parameterCount)
            return;
        if (minimum == dryLevel || minimum == wetLevel)
        {
            resetLevelFromView (minimum);
            return;
        }
        if (maximum >= parameterCount)
        {
            editFromView (minimum, defaultStateNormalized (minimum), 2);
        }
        else
        {
            const bool grouped = componentHandler2 != nullptr
                && componentHandler2->startGroupEdit() == kResultOk;
            editFromView (minimum, defaultStateNormalized (minimum), -1);
            editFromView (maximum, defaultStateNormalized (maximum), -1);
            if (componentHandler != nullptr)
            {
                componentHandler->endEdit (minimum);
                componentHandler->endEdit (maximum);
            }
            localEditing[minimum].store (false, std::memory_order_release);
            localEditing[maximum].store (false, std::memory_order_release);
            if (grouped)
                componentHandler2->finishGroupEdit();
        }
        displayStateRevision.fetch_add (1, std::memory_order_release);
        invalidateView();
    }
    [[nodiscard]] bool isGrainingForView() const noexcept
    {
        return grainActive.load (std::memory_order_acquire);
    }
    [[nodiscard]] LicenseStatus licenseStatusForView() const noexcept
    {
        return license.status();
    }
    [[nodiscard]] std::wstring licenseMessageForView() const
    {
        return license.message();
    }
    [[nodiscard]] std::uint32_t seed() const
    {
        return (static_cast<std::uint32_t> (std::lround (plain (seedHigh))) << 16u)
             | static_cast<std::uint32_t> (std::lround (plain (seedLow)));
    }
    void setSeedFromView (std::uint32_t newSeed)
    {
        editFromView (seedHigh, toNormal (seedHigh, static_cast<double> (newSeed >> 16u)), -1);
        editFromView (seedLow, toNormal (seedLow, static_cast<double> (newSeed & 0xffffu)), -1);
        if (componentHandler != nullptr)
        {
            componentHandler->endEdit (seedHigh);
            componentHandler->endEdit (seedLow);
        }
        localEditing[seedHigh].store (false, std::memory_order_release);
        localEditing[seedLow].store (false, std::memory_order_release);
    }


private:
 tresult readState(IBStream* stream){
  if(!stream)return kInvalidArgument;SavedStateHeader h{};int32 bytes=0;
  if(stream->read(&h,sizeof(h),&bytes)!=kResultOk||bytes!=sizeof(h)||h.magic!=0x47524149u||(h.version!=1&&h.version!=2&&h.version!=3))return kResultFalse;
  std::array<double,parameterCount> p{};
  for(ParamID id=0;id<parameterCount;++id)p[id]=defaultStateNormalized(id);
  const auto count=h.version==1?stateV1ParameterCount:h.version==2?stateV2ParameterCount:static_cast<std::size_t>(parameterCount);
  const int32 size=static_cast<int32>(count*sizeof(double));
  if(stream->read(p.data(),size,&bytes)!=kResultOk||bytes!=size)return kResultFalse;
  if(h.version<3)for(ParamID id=0;id<count;++id)if(specs[id].time){
   const double n=clampNormal(p[id]);
   const double minimum=id==scatter?10:id==attackMinimum||id==attackMaximum?1:id==swaySpeedMinimum||id==swaySpeedMaximum?25:5;
   const double maximum=id==scatter||id==swaySpeedMinimum||id==swaySpeedMaximum?20000:id==releaseMinimum||id==releaseMaximum?30000:10000;
   p[id]=toNormal(id,minimum+(maximum-minimum)*n*n*n);
  }
  for(ParamID id=0;id<parameterCount;++id){p[id]=quantizeNormal(id,p[id]);values[id].store(p[id]);localEditing[id].store(false);}
  processorParameters.restore(p);displayStateRevision.fetch_add(1);needsReset.store(true);return kResultOk;
 }
 tresult writeState(IBStream* stream){
  if(!stream)return kInvalidArgument;SavedStateHeader h{};int32 bytes=0;
  if(stream->write(&h,sizeof(h),&bytes)!=kResultOk||bytes!=sizeof(h))return kResultFalse;
  std::array<double,parameterCount> p{};for(ParamID id=0;id<parameterCount;++id)p[id]=processorParameters.valueForState(id);
  const int32 size=static_cast<int32>(sizeof(p));return stream->write(p.data(),size,&bytes)==kResultOk&&bytes==size?kResultOk:kResultFalse;
 }
    bool prepareAutomationTimeline (IParameterChanges* changes, int32 blockSize)
    {
        std::array<bool, parameterCount> automated {};
        const int32 queueCount = changes == nullptr ? 0 : std::clamp (
            changes->getParameterCount(), 0, static_cast<int32> (parameterCount));
        std::size_t activeQueues = 0;
        for (int32 index = 0; index < queueCount; ++index)
        {
            auto* queue = changes->getParameterData (index);
            if (queue != nullptr && queue->getParameterId() < parameterCount
                && queue->getPointCount() > 0)
            {
                automated[queue->getParameterId()] = true;
                ++activeQueues;
            }
        }
        const auto startValues = processorParameters.consumePending (automated);
        automationTimeline.reset (startValues, blockSize);
        // Decide the budget before reading any host points. Ordinary lanes
        // retain all points. Exceptionally dense lanes are evenly sampled,
        // always retaining their first and final values; callback work and
        // storage remain bounded even for a malformed oversized host queue.
        const auto pointBudget = maximumAutomationPoints / std::max (std::size_t { 1 }, activeQueues);
        for (int32 index = 0; index < queueCount; ++index)
        {
            auto* queue = changes->getParameterData (index);
            if (queue == nullptr || queue->getParameterId() >= parameterCount
                || queue->getPointCount() <= 0)
                continue;
            const auto id = queue->getParameterId();
            const auto sourceCount = static_cast<std::size_t> (queue->getPointCount());
            const auto retainedCount = std::min (sourceCount, pointBudget);
            for (std::size_t point = 0; point < retainedCount; ++point)
            {
                const auto source = sourceCount <= retainedCount ? point
                    : ParameterTimeline::sourcePointIndex (point, retainedCount, sourceCount);
                int32 offset = 0;
                ParamValue value = 0.0;
                if (queue->getPoint (static_cast<int32> (source), offset, value) == kResultOk)
                    (void) automationTimeline.addPoint (id, offset, quantizeNormal (id, value));
            }
        }
        return false;
    }

    [[nodiscard]] std::array<double, parameterCount> automationValuesAt (
        int offset) const noexcept
    {
        std::array<double, parameterCount> result {};
        for (ParamID id = 0; id < parameterCount; ++id)
            result[id] = automationTimeline.valueAt (
                id, offset, isButtonParameter (id));
        return result;
    }

    void finishAutomationBlock (int32 blockSize) noexcept
    {
        std::array<double, parameterCount> finalValues {};
        for (ParamID id = 0; id < parameterCount; ++id)
            finalValues[id] = quantizeNormal (id,
                automationTimeline.valueAt (id, blockSize,
                                            isButtonParameter (id)));
        processorParameters.commit (finalValues);
        if (! processingForView.load (std::memory_order_acquire))
            for (ParamID id = 0; id < parameterCount; ++id)
                values[id].store (finalValues[id], std::memory_order_relaxed);
    }

    static std::uint32_t seedFromValues (
        const std::array<double, parameterCount>& parameters) noexcept
    {
        return (static_cast<std::uint32_t> (std::lround (
                    toPlain (seedHigh, parameters[seedHigh]))) << 16u)
             | static_cast<std::uint32_t> (std::lround (
                    toPlain (seedLow, parameters[seedLow])));
    }


 InhaGrain::Parameters makeSettings(const std::array<double,parameterCount>& p) const noexcept {
  const bool synced=toPlain(tempoSync,p[tempoSync])>=0.5;
  const auto bpm=hostTempo.load(std::memory_order_relaxed);
  const auto v=[&](ParamID id){return synced&&specs[id].time?InhaGrain::Timing::syncMilliseconds(p[id],bpm):toPlain(id,p[id]);};InhaGrain::Parameters result;
  result.synced=synced;result.tempoBpm=static_cast<float>(bpm);
  result.durationMinimumMs=InhaGrain::Timing::minimumDuration(synced,bpm);
  result.durationMaximumMs=InhaGrain::Timing::maximumDuration(synced,bpm);
  result.mode=static_cast<int>(std::lround(v(triggerMode)));
  result.motionMode=result.mode==0?1:static_cast<int>(std::lround(v(motionMode)));
  result.swayAmountMinPercent=v(swayAmountMinimum);result.swayAmountMaxPercent=v(swayAmountMaximum);
  result.swaySpeedMinMs=v(swaySpeedMinimum);result.swaySpeedMaxMs=v(swaySpeedMaximum);
  result.amount=v(amount);result.sensitivity=v(sensitivity);result.thresholdDb=v(threshold);result.scatterMs=v(scatter);
  result.grainMinMs=v(grainMinimum);result.grainMaxMs=v(grainMaximum);result.envMinPercent=v(envMinimum);result.envMaxPercent=v(envMaximum);
  result.undulationMinPercent=v(undulationMinimum);result.undulationMaxPercent=v(undulationMaximum);
  result.speedMinPercent=v(speedMinimum);result.speedMaxPercent=v(speedMaximum);
  result.attackMinMs=v(attackMinimum);result.attackMaxMs=v(attackMaximum);result.releaseMinMs=v(releaseMinimum);result.releaseMaxMs=v(releaseMaximum);
  result.duckDryPercent=v(duckDry);result.duckPreviousPercent=result.mode==0?0:v(duckPrevious);result.dryDb=v(dryLevel);result.wetDb=v(wetLevel);
  result.mass=static_cast<int>(std::lround(v(mass)));result.panMinPercent=v(panMinimum);result.panMaxPercent=v(panMaximum);
  result.seed=seedFromValues(p);result.bypass=v(bypass)>=0.5;return result;
 }
 void resetEngine(){
  const auto p=processorParameters.consumePending();processorParameters.commit(p);engine.setParameters(makeSettings(p));engine.reset(0);
  grainActive.store(false);hostWasPlaying=false;haveExpectedSample=false;expectedSample=0;freeRunningSample=0;
 }
 std::atomic<uint32> references{1};
 std::array<std::atomic<double>,parameterCount> values;
 std::atomic<bool> processingForView{false};
 std::array<std::atomic<bool>,parameterCount> localEditing;
 std::array<std::atomic<double>,parameterCount> localEditValues;
 std::atomic<std::uint32_t> displayStateRevision{0};
 InhaAutomation::ProcessorParameterState<parameterCount> processorParameters;
 ParameterTimeline automationTimeline;
 std::atomic<double> rememberedDryLevel{toNormal(dryLevel,0)},rememberedWetLevel{toNormal(wetLevel,0)};
 IComponentHandler* componentHandler=nullptr;IComponentHandler2* componentHandler2=nullptr;NativeView* view=nullptr;
 InhaGrain::GrainEngine engine;SpeakerArrangement arrangement=SpeakerArr::kStereo;double sampleRate=44100;
 bool prepared=false,hostWasPlaying=false,haveExpectedSample=false;TSamples expectedSample=0,freeRunningSample=0;
 std::atomic<double> hostTempo{120.0};
 std::atomic<bool> needsReset{false},grainActive{false};
 InhaLicense::Manager license{{"inha-grain",L"INHA Grain",InhaGrainVersion::ascii,"INHA-Grain-device-v1:"}};
};
void PluginInstance::invalidateView()
{
    if (view != nullptr)
        view->invalidate();
}

NativeView::NativeView (PluginInstance& instance) : owner (instance)
{
    owner.addRef();
    const auto preferences = GrainPreferences::read();
    darkTheme = preferences.darkTheme;
    zoomed = observedGlobalZoom = preferences.zoomed;
    viewSize = { 0, 0, zoomed ? zoomedEditorWidth : baseEditorWidth,
                 zoomed ? zoomedEditorHeight : baseEditorHeight };
    loadedFontPath = bundleResourcePath (L"Cousine-Regular.ttf");
    if (! loadedFontPath.empty() && AddFontResourceExW (loadedFontPath.c_str(), FR_PRIVATE, nullptr) == 0)
        loadedFontPath.clear();

    const auto loadImage = [] (const std::wstring& path) -> Gdiplus::Image*
    {
        if (path.empty())
            return nullptr;
        auto* image = Gdiplus::Image::FromFile (path.c_str(), FALSE);
        if (image != nullptr && image->GetLastStatus() != Gdiplus::Ok)
        {
            delete image;
            return nullptr;
        }
        return image;
    };
    headerLogo = loadImage (bundleResourcePath (L"INHA-Grain-Logo.png"));
    woodLeft = loadImage (bundleResourcePath (L"wood-left-20px.jpg"));
    woodRight = loadImage (bundleResourcePath (L"wood-right-20px.jpg"));
}

NativeView::~NativeView()
{
    removed();
    delete headerLogo;
    delete woodLeft;
    delete woodRight;
    if (! loadedFontPath.empty())
        RemoveFontResourceExW (loadedFontPath.c_str(), FR_PRIVATE, nullptr);
    if (frame != nullptr)
        frame->release();
    owner.viewDestroyed (this);
    owner.release();
}

tresult PLUGIN_API NativeView::queryInterface (const TUID requestedIid, void** object)
{
    if (object == nullptr)
        return kInvalidArgument;
    *object = nullptr;
    if (iidEqual (requestedIid, FUnknown_iid)
        || iidEqual (requestedIid, IPlugView_iid))
    {
        *object = static_cast<IPlugView*> (this);
        addRef();
        return kResultOk;
    }
    return kNoInterface;
}

uint32 PLUGIN_API NativeView::release()
{
    const auto remaining = --references;
    if (remaining == 0)
        delete this;
    return remaining;
}

tresult PLUGIN_API NativeView::isPlatformTypeSupported (FIDString type)
{
    return type != nullptr && std::strcmp (type, kPlatformTypeHWND) == 0 ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API NativeView::attached (void* parent, FIDString type)
{
    if (parent == nullptr || isPlatformTypeSupported (type) != kResultTrue || window != nullptr)
        return kInvalidArgument;

    WNDCLASSEXW windowClass {};
    windowClass.cbSize = sizeof (windowClass);
    windowClass.style = CS_DBLCLKS;
    windowClass.lpfnWndProc = windowProcedure;
    windowClass.hInstance = moduleInstance;
    windowClass.hCursor = LoadCursorW (nullptr, MAKEINTRESOURCEW (32512));
    windowClass.lpszClassName = L"InhaGrainView_v103";
    RegisterClassExW (&windowClass);

    window = CreateWindowExW (0, windowClass.lpszClassName, L"INHA Grain",
                              WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                              0, 0, viewSize.getWidth(), viewSize.getHeight(),
                              static_cast<HWND> (parent), nullptr, moduleInstance, this);
    if (window != nullptr)
    {
        lastActivity = owner.isGrainingForView();
        SetTimer (window, 1, 16, nullptr);
        updateFooterTooltips();
    }
    return window != nullptr ? kResultOk : kResultFalse;
}

tresult PLUGIN_API NativeView::removed()
{
    if (window != nullptr)
    {
        if (footerTooltips != nullptr)
        {
            DestroyWindow (footerTooltips);
            footerTooltips = nullptr;
        }
        const auto oldWindow = window;
        KillTimer (oldWindow, 1);
        window = nullptr;
        DestroyWindow (oldWindow);
    }
    return kResultOk;
}

tresult PLUGIN_API NativeView::getSize (ViewRect* size)
{
    if (size == nullptr)
        return kInvalidArgument;
    *size = viewSize;
    return kResultOk;
}

tresult PLUGIN_API NativeView::onSize (ViewRect* size)
{
    if (size == nullptr)
        return kInvalidArgument;
    viewSize = *size;
    zoomed = viewSize.getWidth() >= zoomedEditorWidth;
    setTriggerMenuOpen (false, true);
    setFooterExpanded (false);
    if (window != nullptr)
        SetWindowPos (window, nullptr, 0, 0, viewSize.getWidth(), viewSize.getHeight(),
                      SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    updateFooterTooltips();
    return kResultOk;
}

tresult PLUGIN_API NativeView::onFocus (TBool state)
{
    if (! state) { setFooterExpanded (false); setTriggerMenuOpen (false, true); }
    if (state && window != nullptr)
        SetFocus (window);
    return kResultOk;
}

tresult PLUGIN_API NativeView::onKeyDown (char16 key, int16 keyCode, int16)
{
    if (triggerMenuOpen && (key == 27 || keyCode == VK_ESCAPE))
    {
        setTriggerMenuOpen (false, true);
        return kResultTrue;
    }
    if (! seedEditing)
        return kResultFalse;

    auto character = static_cast<wchar_t> (key);
    if (character == 0 && keyCode >= L'0' && keyCode <= L'9')
        character = static_cast<wchar_t> (keyCode);

    if (character == L'\x1b' || keyCode == VK_ESCAPE)
    {
        seedEditing = false;
        seedSelectAll = false;
        invalidate();
        return kResultTrue;
    }
    if (character == L'\b' || keyCode == VK_BACK)
    {
        editSeedCharacter (L'\b');
        return kResultTrue;
    }
    if (character == L'\r' || keyCode == VK_RETURN)
    {
        commitSeed();
        return kResultTrue;
    }
    if (character >= L'0' && character <= L'9')
    {
        editSeedCharacter (character);
        return kResultTrue;
    }
    return kResultFalse;
}

tresult PLUGIN_API NativeView::setFrame (IPlugFrame* newFrame)
{
    if (newFrame != nullptr)
        newFrame->addRef();
    if (frame != nullptr)
        frame->release();
    frame = newFrame;
    return kResultOk;
}

tresult PLUGIN_API NativeView::checkSizeConstraint (ViewRect* rect)
{
    if (rect == nullptr)
        return kInvalidArgument;
    rect->right = rect->left + (zoomed ? zoomedEditorWidth : baseEditorWidth);
    rect->bottom = rect->top + (zoomed ? zoomedEditorHeight : baseEditorHeight);
    return kResultOk;
}

void NativeView::invalidate()
{
    if (window != nullptr)
        InvalidateRect (window, nullptr, FALSE);
}

LRESULT CALLBACK NativeView::windowProcedure (HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    NativeView* self = reinterpret_cast<NativeView*> (GetWindowLongPtrW (hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        const auto* create = reinterpret_cast<CREATESTRUCTW*> (lParam);
        self = static_cast<NativeView*> (create->lpCreateParams);
        SetWindowLongPtrW (hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR> (self));
    }
    return self != nullptr ? self->handleMessage (hwnd, message, wParam, lParam)
                           : DefWindowProcW (hwnd, message, wParam, lParam);
}

LRESULT NativeView::handleMessage (HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    RECT client {};
    GetClientRect (hwnd, &client);
    const auto clientWidth = std::max (1L, client.right - client.left);
    const auto clientHeight = std::max (1L, client.bottom - client.top);
    const auto baseX = [clientWidth] (int x)
    {
        return static_cast<int> (static_cast<long long> (x)
                               * baseEditorWidth / clientWidth);
    };
    const auto baseY = [clientHeight] (int y)
    {
        return static_cast<int> (static_cast<long long> (y)
                               * baseEditorHeight / clientHeight);
    };

    switch (message)
    {
        case WM_PRINTCLIENT:
            paint (reinterpret_cast<HDC> (wParam));
            return 0;
        case WM_PAINT:
        {
            PAINTSTRUCT paintInfo {};
            const auto dc = BeginPaint (hwnd, &paintInfo);
            paint (dc);
            EndPaint (hwnd, &paintInfo);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_LBUTTONDOWN:
            SetFocus (hwnd);
            SetCapture (hwnd);
            mouseDown (baseX (GET_X_LPARAM (lParam)),
                       baseY (GET_Y_LPARAM (lParam)), false);
            return 0;
        case WM_LBUTTONDBLCLK:
            SetFocus (hwnd);
            mouseDoubleClick (baseX (GET_X_LPARAM (lParam)),
                              baseY (GET_Y_LPARAM (lParam)));
            return 0;
        case WM_RBUTTONDOWN:
            SetFocus (hwnd);
            mouseDown (baseX (GET_X_LPARAM (lParam)),
                       baseY (GET_Y_LPARAM (lParam)), true);
            return 0;
        case WM_MOUSEMOVE:
        {
            TRACKMOUSEEVENT tracking { sizeof (tracking), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent (&tracking);
            if ((wParam & MK_LBUTTON) != 0)
                mouseDrag (baseX (GET_X_LPARAM (lParam)),
                           baseY (GET_Y_LPARAM (lParam)));
            else
            {
                updateTriggerMenuHover (baseX (GET_X_LPARAM (lParam)),
                                        baseY (GET_Y_LPARAM (lParam)));
                updateFooterHover (baseX (GET_X_LPARAM (lParam)),
                                   baseY (GET_Y_LPARAM (lParam)));
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            triggerPointerX = triggerPointerY = -1;
            triggerMenuDismissed = false;
            setTriggerMenuOpen (false);
            setFooterExpanded (false);
            return 0;
        case WM_LBUTTONUP:
            mouseUp();
            ReleaseCapture();
            return 0;
        case WM_CAPTURECHANGED:
            mouseUp();
            return 0;
        case WM_TIMER:
            if (wParam == 1)
            {
                updateFooterAnimation();
                updateTriggerMenuAnimation();
                pollPreferences();
                if(lastMotionMode!=static_cast<int>(std::lround(owner.plain(motionMode)))
                   ||lastTriggerMode!=static_cast<int>(std::lround(owner.plain(triggerMode))))
                {
                    mouseUp();
                    updateFooterTooltips();
                }
                const auto active = owner.isGrainingForView();
                const auto license = owner.licenseStatusForView();
                const auto parametersChanged = updateParameterDisplay();
                if (parametersChanged || active != lastActivity
                    || license != lastLicenseStatus)
                {
                    lastActivity = active;
                    lastLicenseStatus = license;
                    InvalidateRect (hwnd, nullptr, FALSE);
                }
                return 0;
            }
            break;
        case WM_CHAR:
            if (seedEditing)
            {
                editSeedCharacter (static_cast<wchar_t> (wParam));
                return 0;
            }
            break;
        case WM_KEYDOWN:
            if (triggerMenuOpen && wParam == VK_ESCAPE)
            {
                setTriggerMenuOpen (false, true); return 0;
            }
            if (seedEditing && wParam == VK_ESCAPE)
            {
                seedEditing = false;
                seedSelectAll = false;
                invalidate();
                return 0;
            }
            break;
        case WM_KILLFOCUS:
            setTriggerMenuOpen (false, true);
            setFooterExpanded (false);
            if (seedEditing)
                commitSeed();
            break;
        case WM_SETCURSOR:
            if (LOWORD (lParam) == HTCLIENT)
            {
                POINT point {};
                GetCursorPos (&point);
                ScreenToClient (hwnd, &point);
                SetCursor (cursorFor (baseX (point.x), baseY (point.y)));
                return TRUE;
            }
            break;
        default:
            break;
    }
    return DefWindowProcW (hwnd, message, wParam, lParam);
}

bool NativeView::setZoom (bool next)
{
    if (next == zoomed) return true;
    const auto previous = zoomed;
    ViewRect requested { 0, 0, next ? zoomedEditorWidth : baseEditorWidth,
                         next ? zoomedEditorHeight : baseEditorHeight };
    zoomed = next;
    if (frame != nullptr && frame->resizeView (this, &requested) != kResultOk)
    {
        zoomed = previous;
        return false;
    }
    viewSize = requested;
    if (window != nullptr)
        SetWindowPos (window, nullptr, 0, 0, viewSize.getWidth(), viewSize.getHeight(),
                      SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    updateFooterTooltips();
    invalidate();
    return true;
}

void NativeView::toggleZoom()
{
    if (! setZoom (! zoomed)) return;
    std::wstring error;
    if (! GrainPreferences::setZoomed (zoomed, error))
        showPreferenceError (error);
    else
        observedGlobalZoom = zoomed;
}

void NativeView::toggleTheme()
{
    std::wstring error;
    if (! GrainPreferences::setDarkTheme (! darkTheme, error))
        showPreferenceError (error);
    else
        darkTheme = ! darkTheme;
    invalidate();
}

void NativeView::pollPreferences()
{
    const auto now = GetTickCount64();
    if (now - lastPreferencesPoll < 1000) return;
    // Do not resize a control underneath an ongoing edit or native dialog.
    if (GetCapture() == window || seedEditing || nativeDialogOpen) return;
    lastPreferencesPoll = now;
    const auto preferences = GrainPreferences::read();
    const auto themeChanged = darkTheme != preferences.darkTheme;
    darkTheme = preferences.darkTheme;
    if (preferences.zoomed != observedGlobalZoom)
    {
        observedGlobalZoom = preferences.zoomed;
        setZoom (preferences.zoomed);
    }
    if (themeChanged) invalidate();
}

void NativeView::updateFooterTooltips()
{
    if (window == nullptr) return;
    const auto create = footerTooltips == nullptr;
    if (create)
    {
        INITCOMMONCONTROLSEX controls { sizeof (controls), ICC_WIN95_CLASSES };
        InitCommonControlsEx (&controls);
        footerTooltips = CreateWindowExW (WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
            WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT,
            CW_USEDEFAULT, CW_USEDEFAULT, window, nullptr, moduleInstance, nullptr);
        if (footerTooltips == nullptr) return;
        SendMessageW (footerTooltips, TTM_SETMAXTIPWIDTH, 0, zoomed ? 480 : 320);
    }
    constexpr std::array<const wchar_t*, 3> labels {{
        L"Settings", L"Toggle light / dark theme",
        L"Toggle zoom: 100% / 150%"
    }};
    const auto scale = zoomed ? 1.5 : 1.0;
    const auto scaled = [scale] (LONG value) { return static_cast<LONG> (std::lround (value * scale)); };
    for (int index = -1; index < 2; ++index)
    {
        const auto rect = index < 0 ? footerSettingsRect
            : footerExpanded ? footerButtonRect (index) : RECT {};
        TOOLINFOW tool {};
        tool.cbSize = sizeof (tool);
        tool.uFlags = TTF_SUBCLASS;
        tool.hwnd = window;
        tool.uId = static_cast<UINT_PTR> (index + 2);
        tool.rect = { scaled (rect.left), scaled (rect.top), scaled (rect.right), scaled (rect.bottom) };
        tool.lpszText = const_cast<wchar_t*> (labels[index + 1]);
        SendMessageW (footerTooltips, create ? TTM_ADDTOOLW : TTM_NEWTOOLRECTW, 0,
                      reinterpret_cast<LPARAM> (&tool));
    }
    lastMotionMode = static_cast<int> (std::lround (owner.plain (motionMode)));
    lastTriggerMode = static_cast<int> (std::lround (owner.plain (triggerMode)));
    const bool sway = owner.plain(triggerMode)<0.5 || lastMotionMode == 1;
    const std::array<const wchar_t*, 4> motionLabels {{
        owner.plain(triggerMode)<0.5
            ? L"UNDULATION is unavailable with trigger NONE. SWAY supplies continuous grain-size movement."
            : L"UNDULATION: one-way grain-size movement during each event. Negative AMOUNT shrinks grains; positive AMOUNT grows them. SPEED is a percentage of the event's attack + release duration.",
        L"SWAY: continuous movement toward seeded random grain-size targets on the DAW timeline. AMOUNT is relative to the base grain size. SPEED is time between targets, independent of attack and release.",
        sway ? L"SWAY AMOUNT: depth relative to the base grain size. 20% allows targets from 80% to 120% of that size. The two handles set the randomized depth range."
             : L"UNDULATION AMOUNT: signed grain-size movement for each event. Negative shrinks; positive grows. The two handles set the randomized amount range.",
        sway ? L"SWAY SPEED: time between new random targets, in milliseconds / seconds. Independent of event attack and release. The two handles set the duration range."
             : L"UNDULATION SPEED: duration as a percentage of the event's attack + release. 100% uses their combined duration; 200% takes twice as long."
    }};
    const std::array<RECT, 4> motionRects {{
        motionTabRects[0], motionTabRects[1],
        {contentLeft+9,196,contentLeft+87,319},
        {contentLeft+91,196,contentLeft+169,319}
    }};
    for (std::size_t index = 0; index < motionRects.size(); ++index)
    {
        const auto rect = motionRects[index];
        TOOLINFOW tool {};
        tool.cbSize = sizeof (tool);
        tool.uFlags = TTF_SUBCLASS;
        tool.hwnd = window;
        tool.uId = static_cast<UINT_PTR> (100 + index);
        tool.rect = {scaled(rect.left), scaled(rect.top), scaled(rect.right), scaled(rect.bottom)};
        tool.lpszText = const_cast<wchar_t*> (motionLabels[index]);
        SendMessageW (footerTooltips, create ? TTM_ADDTOOLW : TTM_NEWTOOLRECTW, 0,
                      reinterpret_cast<LPARAM> (&tool));
        if (!create)
            SendMessageW (footerTooltips, TTM_UPDATETIPTEXTW, 0,
                          reinterpret_cast<LPARAM> (&tool));
    }
}

void NativeView::invalidateFooter()
{
    if (window == nullptr) return;
    const auto scale = zoomed ? 1.5 : 1.0;
    RECT area { 0, static_cast<LONG> (std::floor (controlsBottom * scale)),
                viewSize.getWidth(), viewSize.getHeight() };
    InvalidateRect (window, &area, FALSE);
}

void NativeView::setFooterExpanded (bool expanded)
{
    if (footerExpanded == expanded) return;
    updateFooterAnimation();
    footerExpanded = expanded;
    footerAnimationFrom = footerReveal;
    footerAnimationStart = GetTickCount64();
    if (! expanded && footerTooltips != nullptr)
        SendMessageW (footerTooltips, TTM_POP, 0, 0);
    updateFooterTooltips();
    invalidateFooter();
}

void NativeView::updateFooterAnimation()
{
    if (footerAnimationStart == 0) return;
    const auto progress = std::min (1.0f,
        static_cast<float> (GetTickCount64() - footerAnimationStart) / hoverAnimationDurationMs);
    const auto remaining = 1.0f - progress;
    const auto eased = 1.0f - remaining * remaining * remaining;
    footerReveal = footerAnimationFrom
        + ((footerExpanded ? 1.0f : 0.0f) - footerAnimationFrom) * eased;
    if (progress >= 1.0f) footerAnimationStart = 0;
    invalidateFooter();
}

void NativeView::updateFooterHover (int x, int y)
{
    const auto status = owner.licenseStatusForView();
    const auto available = ! nativeDialogOpen
        && status != InhaLicense::Status::denied
        && status != InhaLicense::Status::invalidLocalKey;
    setFooterExpanded (available && (pointIn (footerSettingsRect, x, y)
        || ((footerExpanded || footerReveal > 0.0f) && pointIn (footerActionsArea, x, y))));
}

void NativeView::setTriggerMenuOpen (bool open, bool dismissUntilLeave)
{
    if (dismissUntilLeave && (triggerMenuOpen || triggerMenuReveal > 0.0f))
        triggerMenuDismissed = true;
    if (triggerMenuOpen == open) return;
    updateTriggerMenuAnimation();
    triggerMenuOpen = open;
    triggerMenuAnimationFrom = triggerMenuReveal;
    triggerMenuAnimationStart = GetTickCount64();
    if (! open) triggerMenuHover = -1;
    invalidate();
}

int NativeView::triggerMenuSlidePixels() const
{
    const auto scale = static_cast<double> (viewSize.getWidth()) / baseEditorWidth;
    return static_cast<int> (std::lround (-hoverSlideDistance * (1.0f - triggerMenuReveal) * scale));
}

int NativeView::triggerMenuItemAt (int x, int y) const
{
    if (pointIn (triggerSelectorRect, x, y)
        || x < triggerMenuRect.left || x >= triggerMenuRect.right) return -1;
    const auto scale = static_cast<double> (viewSize.getWidth()) / baseEditorWidth;
    const auto rowPosition = y - triggerMenuTop - triggerMenuSlidePixels() / scale;
    return rowPosition >= 0.0 && rowPosition < 4 * triggerMenuRowHeight
        ? static_cast<int> (rowPosition / triggerMenuRowHeight) : -1;
}

void NativeView::updateTriggerMenuAnimation()
{
    if (triggerMenuAnimationStart == 0) return;
    const auto progress = std::min (1.0f,
        static_cast<float> (GetTickCount64() - triggerMenuAnimationStart) / hoverAnimationDurationMs);
    const auto remaining = 1.0f - progress;
    const auto eased = 1.0f - remaining * remaining * remaining;
    triggerMenuReveal = triggerMenuAnimationFrom
        + ((triggerMenuOpen ? 1.0f : 0.0f) - triggerMenuAnimationFrom) * eased;
    triggerMenuHover = triggerMenuOpen ? triggerMenuItemAt (triggerPointerX, triggerPointerY) : -1;
    if (progress >= 1.0f) triggerMenuAnimationStart = 0;
    invalidate();
}

void NativeView::updateTriggerMenuHover (int x, int y)
{
    triggerPointerX = x;
    triggerPointerY = y;
    const auto inSelector = pointIn (triggerSelectorRect, x, y);
    const auto inMenu = pointIn (triggerMenuRect, x, y);
    if (! inSelector && ! inMenu) triggerMenuDismissed = false;
    const auto status = owner.licenseStatusForView();
    const auto available = ! nativeDialogOpen
        && status != InhaLicense::Status::denied
        && status != InhaLicense::Status::invalidLocalKey;
    // Retain the full menu's hover area throughout the slide, including when
    // reversing a close. Hidden menus cannot open by hovering the controls below.
    setTriggerMenuOpen (available && ! triggerMenuDismissed
        && (inSelector || ((triggerMenuOpen || triggerMenuReveal > 0.0f) && inMenu)));
    const auto hover = triggerMenuOpen ? triggerMenuItemAt (x, y) : -1;
    if (hover != triggerMenuHover) { triggerMenuHover = hover; invalidate(); }
}

void NativeView::showPreferenceError (const std::wstring& error)
{
    nativeDialogOpen = true;
    MessageBoxW (window, error.c_str(), L"INHA Grain — could not save preferences",
                 MB_OK | MB_ICONERROR);
    nativeDialogOpen = false;
}

namespace Drawing
{
constexpr COLORREF panel = static_cast<COLORREF> (Inha::Ui::Theme::panel.nativeColourRef());
constexpr COLORREF ink = static_cast<COLORREF> (Inha::Ui::Theme::ink.nativeColourRef());
constexpr COLORREF muted = static_cast<COLORREF> (Inha::Ui::Theme::muted.nativeColourRef());
constexpr COLORREF lineColour = static_cast<COLORREF> (Inha::Ui::Theme::line.nativeColourRef());
constexpr COLORREF rangeFill = static_cast<COLORREF> (Inha::Ui::Theme::rangeFill.nativeColourRef());
constexpr COLORREF disabledInk = static_cast<COLORREF> (Inha::Ui::Theme::disabledInk.nativeColourRef());
constexpr COLORREF disabledFill = static_cast<COLORREF> (Inha::Ui::Theme::disabledFill.nativeColourRef());
constexpr COLORREF activeGreen = static_cast<COLORREF> (Inha::Ui::Theme::activeGreen.nativeColourRef());
constexpr COLORREF activeGlow = static_cast<COLORREF> (Inha::Ui::Theme::activeGlow.nativeColourRef());
// Retain the family palette for consistent light and dark appearance.
constexpr COLORREF manualRegionColour = RGB (53, 50, 43);
// The old grid colours composited at 25% over the panel background. This is
// equivalent to reducing their opacity by 75% without an alpha drawing pass.
constexpr COLORREF gridBarColour = RGB (184, 179, 165);
constexpr COLORREF gridBeatColour = RGB (211, 204, 189);
constexpr COLORREF gridSubdivisionColour = RGB (230, 222, 200);
struct Palette
{
    COLORREF panel, ink, muted, lineColour, rangeFill, disabledInk, disabledFill;
    COLORREF activeGlow, manualRegionColour, gridBarColour, gridBeatColour, gridSubdivisionColour;
};
constexpr Palette paletteFor (bool dark) noexcept
{
    return dark ? Palette { RGB (29, 29, 28), RGB (225, 225, 224), RGB (139, 139, 137),
                            RGB (73, 73, 71), RGB (40, 40, 39), RGB (94, 94, 92), RGB (40, 40, 39),
                            RGB (45, 65, 42), RGB (225, 225, 224), RGB (78, 78, 77),
                            RGB (57, 57, 55), RGB (40, 40, 39) }
                : Palette { panel, ink, muted, lineColour, rangeFill, disabledInk, disabledFill,
                            activeGlow, manualRegionColour, gridBarColour, gridBeatColour,
                            gridSubdivisionColour };
}
constexpr double pi = 3.14159265358979323846;
thread_local double renderScale = 1.0;

void setRenderScale (double scale)
{
    renderScale = std::max (1.0, scale);
}

int scaled (int value)
{
    return static_cast<int> (std::lround (static_cast<double> (value) * renderScale));
}

Gdiplus::REAL scaledReal (float value)
{
    return static_cast<Gdiplus::REAL> (static_cast<double> (value) * renderScale);
}

RECT scaled (RECT rect)
{
    return RECT { scaled (static_cast<int> (rect.left)), scaled (static_cast<int> (rect.top)),
                  scaled (static_cast<int> (rect.right)), scaled (static_cast<int> (rect.bottom)) };
}

Gdiplus::Color smoothColour (COLORREF colour, BYTE alpha = 255)
{
    return Gdiplus::Color (alpha, GetRValue (colour), GetGValue (colour), GetBValue (colour));
}

HFONT makeFont (int pixels, bool underline = false)
{
    return CreateFontW (-scaled (pixels), 0, 0, 0, FW_NORMAL, FALSE, underline, FALSE, DEFAULT_CHARSET,
                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                        FIXED_PITCH | FF_MODERN, L"Cousine");
}

HFONT makeSymbolFont (int pixels)
{
    return CreateFontW (-scaled (pixels), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Symbol");
}

void fill (HDC dc, RECT rect, COLORREF colour, int verticalPixelOffset = 0)
{
    rect = scaled (rect);
    OffsetRect (&rect, 0, verticalPixelOffset);
    const auto brush = CreateSolidBrush (colour);
    FillRect (dc, &rect, brush);
    DeleteObject (brush);
}

void fillRightTriangle (HDC dc, RECT rect, COLORREF colour)
{
    Gdiplus::Graphics graphics (dc);
    graphics.SetSmoothingMode (Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::SolidBrush brush (smoothColour (colour));
    const std::array<Gdiplus::PointF, 3> points {{
        { scaledReal (static_cast<float> (rect.left)),
          scaledReal (static_cast<float> (rect.top)) },
        { scaledReal (static_cast<float> (rect.left)),
          scaledReal (static_cast<float> (rect.bottom)) },
        { scaledReal (static_cast<float> (rect.right)),
          scaledReal (static_cast<float> (rect.bottom)) }
    }};
    graphics.FillPolygon (&brush, points.data(), static_cast<INT> (points.size()));
}

void tintedImage (HDC dc, Gdiplus::Image* image, RECT rect, COLORREF colour)
{
    if (image == nullptr || rect.right <= rect.left || rect.bottom <= rect.top)
        return;

    const auto red = static_cast<float> (GetRValue (colour)) / 255.0f;
    const auto green = static_cast<float> (GetGValue (colour)) / 255.0f;
    const auto blue = static_cast<float> (GetBValue (colour)) / 255.0f;
    Gdiplus::ColorMatrix colourMatrix {{
        { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f },
        { red, green, blue, 0.0f, 1.0f }
    }};
    Gdiplus::ImageAttributes attributes;
    attributes.SetColorMatrix (&colourMatrix);

    const auto target = scaled (rect);
    Gdiplus::Graphics graphics (dc);
    graphics.SetInterpolationMode (Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetPixelOffsetMode (Gdiplus::PixelOffsetModeHighQuality);
    graphics.DrawImage (
        image,
        Gdiplus::Rect (target.left, target.top,
                       target.right - target.left, target.bottom - target.top),
        0, 0, static_cast<INT> (image->GetWidth()),
        static_cast<INT> (image->GetHeight()), Gdiplus::UnitPixel, &attributes);
}

void text (HDC dc, HFONT font, COLORREF colour, const std::wstring& value, RECT rect,
           UINT flags, int verticalPixelOffset = 0)
{
    rect = scaled (rect);
    OffsetRect (&rect, 0, verticalPixelOffset);
    const auto previous = SelectObject (dc, font);
    SetBkMode (dc, TRANSPARENT);
    SetTextColor (dc, colour);
    DrawTextW (dc, value.c_str(), static_cast<int> (value.size()), &rect,
               flags | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject (dc, previous);
}

RECT licenseNotice (HDC dc, HFONT bodyFont, HFONT linkFont, COLORREF colour,
                    const std::wstring& message, RECT bounds)
{
    const auto separator = std::wstring (L"\n\n") + licenseHelpUrl;
    const auto separatorPosition = message.rfind (separator);
    const auto body = separatorPosition == std::wstring::npos
        ? cleanLicenseMessage (message)
        : message.substr (0, separatorPosition);
    const auto link = separatorPosition == std::wstring::npos
        ? std::wstring {} : std::wstring (licenseHelpUrl);

    const auto physicalBounds = scaled (bounds);
    RECT bodyMeasure { 0, 0, physicalBounds.right - physicalBounds.left, 0 };
    auto previous = SelectObject (dc, bodyFont);
    DrawTextW (dc, body.c_str(), static_cast<int> (body.size()), &bodyMeasure,
               DT_CALCRECT | DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
    TEXTMETRICW bodyMetrics {};
    GetTextMetricsW (dc, &bodyMetrics);
    SelectObject (dc, previous);

    RECT linkMeasure { 0, 0, physicalBounds.right - physicalBounds.left, 0 };
    previous = SelectObject (dc, linkFont);
    DrawTextW (dc, link.c_str(), static_cast<int> (link.size()), &linkMeasure,
               DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject (dc, previous);

    const auto bodyHeight = std::max<LONG> (1, bodyMeasure.bottom - bodyMeasure.top);
    const auto blankHeight = std::max<LONG> (1, bodyMetrics.tmHeight);
    const auto linkHeight = link.empty()
        ? 0L : std::max<LONG> (1, linkMeasure.bottom - linkMeasure.top);
    const auto totalHeight = bodyHeight + (link.empty() ? 0L : blankHeight + linkHeight);
    const auto top = physicalBounds.top
                   + std::max<LONG> (0, (physicalBounds.bottom - physicalBounds.top - totalHeight) / 2);

    RECT bodyRect { physicalBounds.left, top, physicalBounds.right, top + bodyHeight };
    previous = SelectObject (dc, bodyFont);
    SetBkMode (dc, TRANSPARENT);
    SetTextColor (dc, colour);
    DrawTextW (dc, body.c_str(), static_cast<int> (body.size()), &bodyRect,
               DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject (dc, previous);

    if (link.empty())
        return {};

    const auto linkWidth = std::max<LONG> (1, linkMeasure.right - linkMeasure.left);
    const auto linkLeft = physicalBounds.left
                        + (physicalBounds.right - physicalBounds.left - linkWidth) / 2;
    RECT linkRect { linkLeft, top + bodyHeight + blankHeight,
                    linkLeft + linkWidth, top + bodyHeight + blankHeight + linkHeight };
    previous = SelectObject (dc, linkFont);
    SetBkMode (dc, TRANSPARENT);
    SetTextColor (dc, colour);
    DrawTextW (dc, link.c_str(), static_cast<int> (link.size()), &linkRect,
               DT_SINGLELINE | DT_NOPREFIX);
    SelectObject (dc, previous);

    const auto logicalFloor = [] (LONG value)
    {
        return static_cast<LONG> (std::floor (static_cast<double> (value) / renderScale));
    };
    const auto logicalCeil = [] (LONG value)
    {
        return static_cast<LONG> (std::ceil (static_cast<double> (value) / renderScale));
    };
    return RECT { logicalFloor (linkRect.left), logicalFloor (linkRect.top),
                  logicalCeil (linkRect.right), logicalCeil (linkRect.bottom) };
}

void trackedText (HDC dc, HFONT font, COLORREF colour, const std::wstring& value,
                  RECT rect, UINT flags, int tracking, int verticalPixelOffset = 0)
{
    if (value.empty())
        return;

    rect = scaled (rect);
    const auto previous = SelectObject (dc, font);
    SetBkMode (dc, TRANSPARENT);
    SetTextColor (dc, colour);

    double width = 0.0;
    std::vector<int> advances;
    advances.reserve (value.size());
    for (const auto character : value)
    {
        SIZE size {};
        GetTextExtentPoint32W (dc, &character, 1, &size);
        advances.push_back (size.cx);
        width += size.cx;
    }
    const auto physicalTracking = static_cast<double> (tracking) * renderScale;
    width += static_cast<double> (value.size() - 1) * physicalTracking;

    auto x = static_cast<double> (rect.left);
    if ((flags & DT_CENTER) != 0)
        x = static_cast<double> (rect.left) + (static_cast<double> (rect.right - rect.left) - width) / 2.0;
    else if ((flags & DT_RIGHT) != 0)
        x = static_cast<double> (rect.right) - width;

    TEXTMETRICW metrics {};
    GetTextMetricsW (dc, &metrics);

    // Font line boxes contain unequal invisible padding above and below the
    // actual glyphs. Centre the visible ink instead, so different fonts and
    // symbols share one optical centreline at both native render scales.
    MAT2 identity {};
    identity.eM11.value = 1;
    identity.eM22.value = 1;
    auto inkTop = 0;
    auto inkBottom = 0;
    auto hasInkBounds = false;
    for (const auto character : value)
    {
        GLYPHMETRICS glyph {};
        if (GetGlyphOutlineW (dc, static_cast<UINT> (character), GGO_METRICS,
                              &glyph, 0, nullptr, &identity) == GDI_ERROR
            || glyph.gmBlackBoxY == 0)
            continue;

        const auto top = -static_cast<int> (glyph.gmptGlyphOrigin.y);
        const auto bottom = top + static_cast<int> (glyph.gmBlackBoxY);
        if (! hasInkBounds)
        {
            inkTop = top;
            inkBottom = bottom;
            hasInkBounds = true;
        }
        else
        {
            inkTop = std::min (inkTop, top);
            inkBottom = std::max (inkBottom, bottom);
        }
    }

    const auto visualCentre = (static_cast<double> (rect.top) + rect.bottom) / 2.0;
    const auto baseline = hasInkBounds
        ? visualCentre - (static_cast<double> (inkTop) + inkBottom) / 2.0
        : visualCentre + (static_cast<double> (metrics.tmAscent) - metrics.tmDescent) / 2.0;
    const auto y = static_cast<int> (std::lround (baseline - metrics.tmAscent))
                   + verticalPixelOffset;
    for (std::size_t i = 0; i < value.size(); ++i)
    {
        TextOutW (dc, static_cast<int> (std::lround (x)), y, &value[i], 1);
        x += static_cast<double> (advances[i]) + physicalTracking;
    }
    SelectObject (dc, previous);
}

void line (HDC dc, int x1, int y1, int x2, int y2, COLORREF colour, int width = 1)
{
    const auto pen = CreatePen (PS_SOLID, std::max (1, scaled (width)), colour);
    const auto oldPen = SelectObject (dc, pen);
    MoveToEx (dc, scaled (x1), scaled (y1), nullptr);
    LineTo (dc, scaled (x2), scaled (y2));
    SelectObject (dc, oldPen);
    DeleteObject (pen);
}

void circle (HDC dc, float x, float y, int radius, COLORREF colour)
{
    Gdiplus::Graphics graphics (dc);
    graphics.SetSmoothingMode (Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode (Gdiplus::PixelOffsetModeHighQuality);
    Gdiplus::SolidBrush brush (smoothColour (colour));
    graphics.FillEllipse (&brush, scaledReal (static_cast<float> (x - radius)),
                          scaledReal (static_cast<float> (y - radius)),
                          scaledReal (static_cast<float> (radius * 2)),
                          scaledReal (static_cast<float> (radius * 2)));
}

void circleOutline (HDC dc, float x, float y, int radius, COLORREF outline, COLORREF fillColour)
{
    Gdiplus::Graphics graphics (dc);
    graphics.SetSmoothingMode (Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode (Gdiplus::PixelOffsetModeHighQuality);
    Gdiplus::SolidBrush brush (smoothColour (fillColour));
    Gdiplus::Pen pen (smoothColour (outline), scaledReal (1.0f));
    const Gdiplus::RectF bounds (scaledReal (static_cast<float> (x - radius)),
                                 scaledReal (static_cast<float> (y - radius)),
                                 scaledReal (static_cast<float> (radius * 2)),
                                 scaledReal (static_cast<float> (radius * 2)));
    graphics.FillEllipse (&brush, bounds);
    graphics.DrawEllipse (&pen, bounds);
}

void smoothLine (HDC dc, int x1, int y1, int x2, int y2, COLORREF colour, float width = 1.0f)
{
    Gdiplus::Graphics graphics (dc);
    graphics.SetSmoothingMode (Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode (Gdiplus::PixelOffsetModeHighQuality);
    Gdiplus::Pen pen (smoothColour (colour), scaledReal (width));
    graphics.DrawLine (&pen, scaledReal (static_cast<float> (x1)), scaledReal (static_cast<float> (y1)),
                       scaledReal (static_cast<float> (x2)), scaledReal (static_cast<float> (y2)));
}

void statusLight (HDC dc, float x, float y, bool active, const Palette& palette, bool enabled = true)
{
    if (! enabled)
    {
        circleOutline (dc, x, y, 3, palette.disabledInk, palette.panel);
    }
    else if (active)
    {
        circle (dc, x, y, 7, palette.activeGlow);
        circle (dc, x, y, 3, activeGreen);
    }
    else
    {
        circleOutline (dc, x, y, 3, palette.lineColour, palette.panel);
    }
}

POINT pointOnKnob (int x, int y, double radius, double normalized)
{
    const auto angle = (135.0 + 270.0 * clampNormal (normalized)) * pi / 180.0;
    return POINT {
        x + static_cast<int> (std::lround (std::cos (angle) * radius)),
        y + static_cast<int> (std::lround (std::sin (angle) * radius))
    };
}

void arc (HDC dc, int x, int y, int radius, double start, double end, COLORREF colour, float width)
{
    start = clampNormal (start);
    end = clampNormal (end);
    if (end < start)
        std::swap (start, end);

    Gdiplus::Graphics graphics (dc);
    graphics.SetSmoothingMode (Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode (Gdiplus::PixelOffsetModeHighQuality);
    Gdiplus::Pen pen (smoothColour (colour), scaledReal (width));
    graphics.DrawArc (&pen,
                      scaledReal (static_cast<float> (x - radius)),
                      scaledReal (static_cast<float> (y - radius)),
                      scaledReal (static_cast<float> (radius * 2)),
                      scaledReal (static_cast<float> (radius * 2)),
                      static_cast<Gdiplus::REAL> (135.0 + 270.0 * start),
                      static_cast<Gdiplus::REAL> (270.0 * (end - start)));
}

void fillSector (HDC dc, int x, int y, int radius, double start, double end, COLORREF colour)
{
    start = clampNormal (start);
    end = clampNormal (end);
    if (end < start)
        std::swap (start, end);
    if (end <= start)
        return;

    Gdiplus::Graphics graphics (dc);
    graphics.SetSmoothingMode (Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode (Gdiplus::PixelOffsetModeHighQuality);
    Gdiplus::SolidBrush brush (smoothColour (colour));
    graphics.FillPie (&brush,
                      scaledReal (static_cast<float> (x - radius)),
                      scaledReal (static_cast<float> (y - radius)),
                      scaledReal (static_cast<float> (radius * 2)),
                      scaledReal (static_cast<float> (radius * 2)),
                      static_cast<Gdiplus::REAL> (135.0 + 270.0 * start),
                      static_cast<Gdiplus::REAL> (270.0 * (end - start)));
}

void tick (HDC dc, int x, int y, double normalized, int innerRadius, int outerRadius,
           COLORREF colour)
{
    const auto inner = pointOnKnob (x, y, static_cast<double> (innerRadius), normalized);
    const auto outer = pointOnKnob (x, y, static_cast<double> (outerRadius), normalized);
    smoothLine (dc, inner.x, inner.y, outer.x, outer.y, colour);
}
}

ParamID NativeView::firstControlParameter() const {
 const auto mode=static_cast<int>(std::lround(owner.plain(triggerMode)));
 return mode==1?sensitivity:mode==3?threshold:amount;
}
bool NativeView::controlEnabled (ParamID id) const {
 return owner.plain(triggerMode)>=0.5 || (id!=amount && id!=duckPrevious);
}
ParamID NativeView::visibleControlParameter (ParamID id) const {
 if(id==amount)return firstControlParameter();
 if(owner.plain(triggerMode)>=0.5&&owner.plain(motionMode)<0.5)return id;
 if(id==undulationMinimum)return swayAmountMinimum;
 if(id==undulationMaximum)return swayAmountMaximum;
 if(id==speedMinimum)return swaySpeedMinimum;
 if(id==speedMaximum)return swaySpeedMaximum;
 return id;
}
RECT NativeView::knobFor(ParamID id) const {
 if(id==swayAmountMinimum)id=undulationMinimum;
 else if(id==swayAmountMaximum)id=undulationMaximum;
 else if(id==swaySpeedMinimum)id=speedMinimum;
 else if(id==swaySpeedMaximum)id=speedMaximum;
 for(const auto& c:controls)if(c.minimum==id||c.maximum==id||(c.minimum==amount&&(id==sensitivity||id==threshold)))return {c.x-38,c.y-38,c.x+38,c.y+38};
 return {};
}
RECT NativeView::verticalSliderFor(ParamID id) const {
 for(const auto& c:controls)if(c.slider&&(c.minimum==id||c.maximum==id))return {c.x-17,c.y-38,c.x+17,c.y+38};
 return {};
}
RECT NativeView::verticalLabelFor(ParamID id) const {
 const auto r=knobFor(id);const LONG x=(r.left+r.right)/2,y=(r.top+r.bottom)/2;
 return {x-22,y-53,x+22,y-39};
}
RECT NativeView::verticalValueFor(ParamID id) const {
 const auto r=knobFor(id);const LONG x=(r.left+r.right)/2,y=(r.top+r.bottom)/2;
 RECT result{x-23,y+39,x+23,y+63};
 // Narrow neighboring value hit areas at their midpoint, so densely spaced
 // sliders never claim a click belonging to the adjacent value.
 for(const auto& c:controls)if(c.y==y){
  if(c.x<x)result.left=std::max(result.left,static_cast<LONG>((c.x+x)/2));
  else if(c.x>x)result.right=std::min(result.right,static_cast<LONG>((c.x+x)/2));
 }
 return result;
}
bool NativeView::pointIn(RECT r,int x,int y) const {return r.right>r.left&&r.bottom>r.top&&x>=r.left&&x<r.right&&y>=r.top&&y<r.bottom;}
double NativeView::knobValue(RECT r,int x,int y) const {
 const auto cx=(r.left+r.right)/2,cy=(r.top+r.bottom)/2;
 auto a=std::atan2(static_cast<double>(y-cy),static_cast<double>(x-cx))*180.0/Drawing::pi;
 if(a<0)a+=360;if(a>=45&&a<135)return x<cx?0:1;if(a<135)a+=360;return clampNormal((a-135)/270.0);
}
HCURSOR NativeView::cursorFor(int x,int y) const {
 const auto hand=LoadCursorW(nullptr,MAKEINTRESOURCEW(32649)),arrow=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));
 if(pointIn(licenseHelpRect,x,y)||pointIn(footerSettingsRect,x,y)||pointIn(logoHitRect,x,y)||pointIn(seedValueRect,x,y)||pointIn(seedRefreshRect,x,y)||pointIn(triggerSelectorRect,x,y)||pointIn(syncRect,x,y))return hand;
 for(int i=0;i<2;++i)if((i==1||owner.plain(triggerMode)>=0.5)&&pointIn(motionTabRects[i],x,y))return hand;
 for(int i=0;footerExpanded&&i<2;++i)if(pointIn(footerButtonRect(i),x,y))return hand;
 if(triggerMenuOpen&&triggerMenuItemAt(x,y)>=0)return hand;
 for(const auto& c:controls)if(controlEnabled(c.minimum)&&(pointIn(knobFor(c.minimum),x,y)||pointIn(verticalValueFor(c.minimum),x,y)))return hand;
 return arrow;
}
bool NativeView::updateParameterDisplay(){
 std::array<double,parameterCount> target{};if(!owner.snapshotForView(target))return false;
 std::array<bool,parameterCount> immediate{};for(ParamID id=0;id<parameterCount;++id)immediate[id]=isButtonParameter(id)||owner.editingForView(id);
 const auto now=GetTickCount64();const double elapsed=lastDisplayTick?static_cast<double>(now-lastDisplayTick)*0.001:0;
 const auto revision=owner.displayRevision();const bool changed=parameterDisplay.update(target,immediate,elapsed,revision!=lastDisplayRevision);
 lastDisplayTick=now;lastDisplayRevision=revision;return changed;
}
void NativeView::paint (HDC windowDC)
{
    using namespace Drawing;
    const auto palette = paletteFor (darkTheme);
    const auto panel = palette.panel;
    const auto ink = palette.ink;
    const auto muted = palette.muted;
    const auto lineColour = palette.lineColour;
    const auto rangeFill = palette.rangeFill;
    const auto disabledInk = palette.disabledInk;
    (void) updateParameterDisplay();
    const auto& displayed = parameterDisplay.values();
    const bool synced=toPlain(tempoSync,displayed[tempoSync])>=0.5;
    const auto normalized = [&] (ParamID id) { return synced&&specs[id].time?InhaGrain::Timing::syncNormalized(InhaGrain::Timing::syncIndex(displayed[id])):displayed[id]; };
    const auto plain = [&] (ParamID id) { return toPlain (id, displayed[id]); };
    RECT client {};
    GetClientRect (window, &client);
    const auto width = std::max (1L, client.right - client.left);
    const auto height = std::max (1L, client.bottom - client.top);
    setRenderScale (static_cast<double> (width) / baseEditorWidth);

    const auto dc = CreateCompatibleDC (windowDC);
    const auto bitmap = CreateCompatibleBitmap (windowDC, width, height);
    if (dc == nullptr || bitmap == nullptr)
    {
        if (bitmap != nullptr) DeleteObject (bitmap);
        if (dc != nullptr) DeleteDC (dc);
        return;
    }
    const auto previousBitmap = SelectObject (dc, bitmap);
    const auto background = CreateSolidBrush (panel);
    FillRect (dc, &client, background);
    DeleteObject (background);

    const auto smallFont = makeFont (10);
    const auto valueFont = makeFont (11);
    const auto syncedSliderValueFont = makeFont (9);
    const auto linkFont = makeFont (11, true);
    const auto footerFont = makeFont (9);
    const auto sectionFont = makeFont (10);
    const auto symbolFont = makeSymbolFont (16);

    // Invert the artwork's RGB channels while retaining its original alpha.
    // This keeps the supplied logo usable in both themes.
    Gdiplus::ImageAttributes imageAttributes;
    const Gdiplus::ColorMatrix invertedImageMatrix {
        -1.0f,  0.0f,  0.0f, 0.0f, 0.0f,
         0.0f, -1.0f,  0.0f, 0.0f, 0.0f,
         0.0f,  0.0f, -1.0f, 0.0f, 0.0f,
         0.0f,  0.0f,  0.0f, 1.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 0.0f, 1.0f
    };
    if (darkTheme) imageAttributes.SetColorMatrix (&invertedImageMatrix);
    const auto* artworkAttributes = darkTheme ? &imageAttributes : nullptr;

    const auto drawWoodPanel = [&] (Gdiplus::Image* image, int targetX, int targetWidth)
    {
        if (image == nullptr || targetWidth <= 0)
            return;
        const auto sourceWidth = static_cast<float> (image->GetWidth());
        const auto sourceHeight = static_cast<float> (image->GetHeight());
        const auto visibleSourceHeight = std::min (
            sourceHeight, static_cast<float> (height) * sourceWidth
                        / static_cast<float> (targetWidth));
        const auto sourceTop = (sourceHeight - visibleSourceHeight) * 0.5f;
        Gdiplus::Graphics graphics (dc);
        graphics.SetInterpolationMode (Gdiplus::InterpolationModeHighQualityBicubic);
        graphics.SetPixelOffsetMode (Gdiplus::PixelOffsetModeHighQuality);
        graphics.DrawImage (image, Gdiplus::Rect (targetX, 0, targetWidth, height),
                            0.0f, sourceTop, sourceWidth, visibleSourceHeight,
                            Gdiplus::UnitPixel);
    };
    if (! darkTheme)
    {
        const auto woodWidth = zoomed ? 20 : sidePanelLogicalWidth;
        drawWoodPanel (woodLeft, 0, woodWidth);
        drawWoodPanel (woodRight, width - woodWidth, woodWidth);
    }
    fill (dc, { contentLeft, 0, contentRight, baseEditorHeight }, panel);

    line (dc, contentLeft, headerHeight, contentRight, headerHeight, lineColour);
    line (dc, contentLeft, controlsBottom, contentRight, controlsBottom, lineColour);

    if (headerLogo != nullptr)
    {
        constexpr float sourceArtworkScale = 1.5f;
        const auto logoWidth = static_cast<float> (headerLogo->GetWidth())
                             / sourceArtworkScale;
        const auto logoHeight = static_cast<float> (headerLogo->GetHeight())
                              / sourceArtworkScale;
        const auto logoTop = (static_cast<float> (headerHeight) - logoHeight) * 0.5f;
        Gdiplus::Graphics graphics (dc);
        graphics.SetInterpolationMode (Gdiplus::InterpolationModeHighQualityBicubic);
        graphics.SetPixelOffsetMode (Gdiplus::PixelOffsetModeHighQuality);
        graphics.DrawImage (headerLogo,
            Gdiplus::RectF (scaledReal (static_cast<float> (contentLeft + 10)),
                            scaledReal (logoTop), scaledReal (logoWidth),
                            scaledReal (logoHeight)),
            0.0f, 0.0f, static_cast<float> (headerLogo->GetWidth()),
            static_cast<float> (headerLogo->GetHeight()), Gdiplus::UnitPixel,
            artworkAttributes);
    }
    else
    {
        trackedText (dc, sectionFont, ink, L"INHA GRAIN",
                     { contentLeft + 10, 0, contentLeft + 145, headerHeight },
                     DT_LEFT, 1);
    }

    const auto frameSeed = (static_cast<std::uint32_t> (
        std::lround (plain (seedHigh))) << 16u)
        | static_cast<std::uint32_t> (std::lround (plain (seedLow)));
    const auto shownSeed = seedEditing ? seedText + L"|" : std::to_wstring (frameSeed);
    trackedText (dc, smallFont, seedEditing ? ink : muted, shownSeed,
                 seedValueRect, DT_RIGHT, 0);
    trackedText (dc, symbolFont, ink, L"\x21bb",
                 { seedRefreshRect.left, seedRefreshRect.top - 1,
                   seedRefreshRect.right, seedRefreshRect.bottom - 1 },
                 DT_RIGHT, 0);

    const auto mode=std::clamp(static_cast<int>(std::lround(plain(triggerMode))),0,3);
    constexpr std::array<const wchar_t*,4> shortModeNames{{L"NONE",L"TRNS",L"MIDI",L"VOL"}};
    const auto modeName=shortModeNames[mode];
    const auto previousFont=SelectObject(dc,sectionFont);SIZE modeTextSize{};
    GetTextExtentPoint32W(dc,modeName,static_cast<int>(std::wcslen(modeName)),&modeTextSize);SelectObject(dc,previousFont);
    const auto modeTextWidth=static_cast<int>(std::lround(modeTextSize.cx/renderScale));
    const auto groupWidth=19+modeTextWidth;const auto groupLeft=(triggerSelectorRect.left+triggerSelectorRect.right-groupWidth)/2;
    {Gdiplus::Graphics graphics(dc);inha::grainIcons::draw(graphics,inha::grainIcons::Icon::trigger,
      scaledReal(static_cast<float>(groupLeft)),scaledReal(7.0f),scaledReal(14.0f),smoothColour(ink));}
    text(dc,sectionFont,ink,modeName,{groupLeft+19,0,groupLeft+groupWidth+1,headerHeight},DT_CENTER|DT_VCENTER,2);
    trackedText(dc,sectionFont,synced?ink:muted,L"SYNC",syncRect,DT_CENTER,1);
    if(synced)line(dc,syncRect.left+10,headerHeight-5,syncRect.right-10,headerHeight-5,ink);
    const auto undulationPanel=darkTheme?RGB(40,40,39):RGB(232,225,206);
    const auto duckPanel=darkTheme?RGB(35,35,34):RGB(226,219,200);
    const auto selectedMotion=mode==0||plain(motionMode)>=0.5?1:0;
    fill(dc,{contentLeft+8,196,contentLeft+177,330},undulationPanel);
    constexpr std::array<const wchar_t*,2> motionNames{{L"UNDULATION",L"SWAY"}};
    for(int index=0;index<2;++index){
     fill(dc,motionTabRects[index],index==selectedMotion?undulationPanel:panel);
     text(dc,sectionFont,index==0&&mode==0?disabledInk:index==selectedMotion?ink:muted,motionNames[index],motionTabRects[index],DT_CENTER|DT_VCENTER);
    }
    fill(dc,{contentLeft+279,174,contentLeft+366,330},duckPanel);
    text(dc,sectionFont,muted,L"DUCK",{contentLeft+287,177,contentLeft+359,195},DT_LEFT|DT_VCENTER);
    constexpr int radius=30;
    for(const auto& c:controls){
      const auto lo=visibleControlParameter(c.minimum);
      const auto hi=visibleControlParameter(c.maximum);
      const bool enabled=controlEnabled(c.minimum);
      const auto label=c.minimum==amount?(mode==1?L"PICKUP":mode==3?L"THLD":L"AMOUNT"):c.label;
      const auto primary=enabled?ink:disabledInk,secondary=enabled?lineColour:disabledInk;
      const bool ranged=hi<parameterCount;
      const auto a=std::min(normalized(lo),ranged?normalized(hi):normalized(lo));
      const auto b=std::max(normalized(lo),ranged?normalized(hi):normalized(lo));
      const bool grainRange=c.minimum==grainMinimum;
      const auto effectiveA=grainRange?std::min(a,normalized(scatter)):a;
      const auto effectiveB=grainRange?std::min(b,normalized(scatter)):b;
      const auto pink=darkTheme?RGB(255,108,168):RGB(220,48,116);
      const auto bg=c.minimum==undulationMinimum||c.minimum==speedMinimum?undulationPanel:
        c.minimum==duckDry||c.minimum==duckPrevious?duckPanel:panel;
      if(!c.slider){
       if(ranged)fillSector(dc,c.x,c.y,radius-1,effectiveA,effectiveB,rangeFill);
       arc(dc,c.x,c.y,radius,0,1,secondary,1.0f);
       tick(dc,c.x,c.y,0,radius+1,radius+6,secondary);tick(dc,c.x,c.y,1,radius+1,radius+6,secondary);
       if(ranged){
        arc(dc,c.x,c.y,radius,effectiveA,effectiveB,primary,1.5f);const auto pa=pointOnKnob(c.x,c.y,radius,a),pb=pointOnKnob(c.x,c.y,radius,b);
        circleOutline(dc,pa.x,pa.y,4,enabled?muted:disabledInk,bg);circleOutline(dc,pb.x,pb.y,4,primary,bg);
       }else{
        arc(dc,c.x,c.y,radius,0,a,primary,2.0f);const auto p=pointOnKnob(c.x,c.y,radius-8,a);smoothLine(dc,c.x,c.y,p.x,p.y,primary,1.25f);
       }
       circle(dc,c.x,c.y,2,primary);
      }else{
       const int top=c.y-radius,bottom=c.y+radius,ya=bottom-static_cast<int>(std::lround(a*2*radius)),yb=bottom-static_cast<int>(std::lround(b*2*radius));
       line(dc,c.x,top,c.x,bottom,secondary);
       if(ranged){fill(dc,{c.x-2,yb,c.x+2,ya+1},rangeFill);line(dc,c.x,yb,c.x,ya,primary,2);smoothLine(dc,c.x-6,ya,c.x+6,ya,muted,1.5f);}
       else line(dc,c.x,yb,c.x,bottom+1,primary,2);
       smoothLine(dc,c.x-7,yb,c.x+7,yb,primary,1.5f);
      }
      const int half=c.slider?22:39;
      trackedText(dc,smallFont,enabled?muted:disabledInk,label,{c.x-half,c.y-53,c.x+half,c.y-39},DT_CENTER, c.slider?0:1);
      const auto compact=[&](ParamID id){
       const auto effective=grainRange?std::min(normalized(id),normalized(scatter)):normalized(id);
       auto value=synced&&specs[id].time?std::wstring(InhaGrain::Timing::syncLabel(effective))
         :displayValue(id,grainRange?std::min(plain(id),plain(scatter)):plain(id));
       if(id==dryLevel||id==wetLevel){const auto space=value.find(L' ');if(space!=std::wstring::npos)value.resize(space);}
       if(c.slider&&specs[id].time&&!synced){
        wchar_t duration[32]{};
        const auto milliseconds=plain(id);
        if(milliseconds<=999.0)std::swprintf(duration,32,L"%.0f",milliseconds);
        else {
         std::swprintf(duration,32,L"%.1f",milliseconds/1000.0);
         value=duration;
         if(value.size()>=2&&value.compare(value.size()-2,2,L".0")==0)value.resize(value.size()-2);
         return value+L" S";
        }
        value=duration;
       }
       return value;
      };
      const auto controlValueFont=c.slider&&specs[lo].time&&synced?syncedSliderValueFont:valueFont;
      text(dc,controlValueFont,grainRange&&normalized(lo)>normalized(scatter)+1e-8?pink:primary,compact(lo),{c.x-half,c.y+39,c.x+half,c.y+51},DT_CENTER|DT_VCENTER);
      const auto second=ranged?compact(hi):c.minimum==amount&&mode==2?std::wstring(L"/ NOTE"):(c.minimum==dryLevel||c.minimum==wetLevel)?std::wstring(L"dB"):std::wstring{};
      text(dc,controlValueFont,grainRange&&normalized(hi)>normalized(scatter)+1e-8?pink:primary,second,{c.x-half,c.y+51,c.x+half,c.y+63},DT_CENTER|DT_VCENTER);
    }
    trackedText (dc, footerFont, muted,
                 L"v" + std::wstring (InhaGrainVersion::wide) + L" \xa9 2026 O. Inha / inha.jp",
                 { contentLeft + 140, controlsBottom, contentRight - 10, baseEditorHeight },
                 DT_RIGHT, 1);
    {
        using inha::footerIcons::Icon;
        const std::array<Icon, 2> icons {{
            darkTheme ? Icon::sun : Icon::moon, zoomed ? Icon::zoomOut : Icon::zoomIn }};
        const auto lightX = static_cast<float> (contentLeft + 17);
        const auto lightY = static_cast<float> (controlsBottom) + footerHeight * 0.5f;
        statusLight (dc, lightX, lightY, lastActivity, palette);
        Gdiplus::Graphics graphics (dc);
        inha::footerIcons::draw (graphics, Icon::settings,
            scaledReal (static_cast<float> (footerSettingsRect.left + footerSettingsRect.right) * 0.5f - 7.0f),
            scaledReal (lightY - 7.0f), scaledReal (14.0f), smoothColour (muted));
        for (int index = 0; footerReveal > 0.0f && index < 2; ++index)
        {
            const auto rect = footerButtonRect (index);
            // Icon-only footer buttons never acquire a fill, including active states.
            inha::footerIcons::draw (graphics, icons[index],
                scaledReal (static_cast<float> (rect.left + rect.right) * 0.5f - 7.0f
                            - hoverSlideDistance * (1.0f - footerReveal)),
                scaledReal (static_cast<float> (rect.top + rect.bottom) * 0.5f - 7.0f),
                scaledReal (14.0f), smoothColour (muted, static_cast<BYTE> (std::lround (255.0f * footerReveal))));
        }
    }

    if (triggerMenuReveal > 0.0f)
    {
        constexpr std::array<const wchar_t*, 4> names {{
            L"NONE", L"TRANSIENTS", L"MIDI NOTES", L"VOLUME" }};
        const auto slide = triggerMenuSlidePixels();
        const auto bounds = scaled (RECT { triggerMenuRect.left - 6, triggerMenuRect.top - 12,
                                           triggerMenuRect.right + 10, triggerMenuRect.bottom + 10 });
        const auto layerWidth = bounds.right - bounds.left;
        const auto layerHeight = bounds.bottom - bounds.top;
        // Save only the small popup area while it animates. Blending its original
        // background back over the drawing fades the text, fill and shadow together.
        const auto backdropDC = triggerMenuReveal < 1.0f ? CreateCompatibleDC (dc) : nullptr;
        const auto backdrop = backdropDC != nullptr
            ? CreateCompatibleBitmap (dc, layerWidth, layerHeight) : nullptr;
        HGDIOBJ previousBackdrop = nullptr;
        if (backdrop != nullptr)
        {
            previousBackdrop = SelectObject (backdropDC, backdrop);
            BitBlt (backdropDC, 0, 0, layerWidth, layerHeight, dc, bounds.left, bounds.top, SRCCOPY);
        }
        // Soft, offset shadow behind the borderless menu, drawn after the
        // editor so the menu also covers the header separator beneath it.
        {
            Gdiplus::Graphics graphics (dc);
            Gdiplus::SolidBrush shadow (Gdiplus::Color (darkTheme ? 16 : 10, 0, 0, 0));
            for (int spread = 6; spread >= 0; --spread)
                graphics.FillRectangle (&shadow,
                    scaledReal (static_cast<float> (triggerMenuRect.left + 2 - spread)),
                    scaledReal (static_cast<float> (triggerMenuRect.top + 3 - spread)) + slide,
                    scaledReal (static_cast<float> (triggerMenuRect.right - triggerMenuRect.left + spread * 2)),
                    scaledReal (static_cast<float> (triggerMenuRect.bottom - triggerMenuRect.top + spread * 2)));
        }
        const auto menuHover = RGB ((GetRValue (ink) * 7 + GetRValue (panel)) / 8,
                                    (GetGValue (ink) * 7 + GetGValue (panel)) / 8,
                                    (GetBValue (ink) * 7 + GetBValue (panel)) / 8);
        fill (dc, triggerMenuRect, ink, slide);
        for (int index = 0; index < 4; ++index)
        {
            const auto row = triggerMenuItemRect (index);
            if (index == triggerMenuHover) fill (dc, row, menuHover, slide);
            text (dc, sectionFont, panel, names[static_cast<std::size_t> (index)], row,
                  DT_CENTER | DT_VCENTER, slide);
        }
        if (backdrop != nullptr)
        {
            const BLENDFUNCTION blend { AC_SRC_OVER, 0,
                static_cast<BYTE> (std::lround (255.0f * (1.0f - triggerMenuReveal))), 0 };
            AlphaBlend (dc, bounds.left, bounds.top, layerWidth, layerHeight,
                        backdropDC, 0, 0, layerWidth, layerHeight, blend);
            SelectObject (backdropDC, previousBackdrop);
            DeleteObject (backdrop);
        }
        if (backdropDC != nullptr) DeleteDC (backdropDC);
    }
    const auto license = owner.licenseStatusForView();
    licenseHelpRect = {};
    if (license == LicenseStatus::invalidLocalKey || license == LicenseStatus::denied)
    {
        fill (dc, { contentLeft, headerHeight, contentRight, controlsBottom }, panel);
        const auto message = license == LicenseStatus::invalidLocalKey
            ? licenseMessageWithHelp (L"Something went wrong, please reinstall plugin.")
            : owner.licenseMessageForView();
        licenseHelpRect = licenseNotice (
            dc, valueFont, linkFont, ink, message,
            { contentLeft + 28, headerHeight + 14,
              contentRight - 28, controlsBottom - 14 });
    }

    DeleteObject (smallFont);
    DeleteObject (valueFont);
    DeleteObject (syncedSliderValueFont);
    DeleteObject (linkFont);
    DeleteObject (footerFont);
    DeleteObject (sectionFont);
    DeleteObject (symbolFont);
    BitBlt (windowDC, 0, 0, width, height, dc, 0, 0, SRCCOPY);
    SelectObject (dc, previousBitmap);
    DeleteObject (bitmap);
    DeleteDC (dc);
}

void NativeView::mouseDown(int x,int y,bool rightButton){
 const auto license=owner.licenseStatusForView();
 if(license==LicenseStatus::invalidLocalKey||license==LicenseStatus::denied){if(pointIn(licenseHelpRect,x,y))ShellExecuteW(window,L"open",licenseHelpUrl,nullptr,nullptr,SW_SHOWNORMAL);return;}
 if(!rightButton&&pointIn(syncRect,x,y)){mouseUp();if(seedEditing)commitSeed();setTriggerMenuOpen(false,true);owner.editFromView(tempoSync,owner.plain(tempoSync)>=0.5?0:1,2);return;}
 if(!rightButton&&pointIn(triggerSelectorRect,x,y)){if(seedEditing)commitSeed();triggerMenuDismissed=false;updateTriggerMenuHover(x,y);return;}
 if(triggerMenuOpen||triggerMenuReveal>0){const auto index=triggerMenuOpen?triggerMenuItemAt(x,y):-1;if(!rightButton&&index>=0)owner.editFromView(triggerMode,toNormal(triggerMode,index),2);setTriggerMenuOpen(false,true);return;}
 if(seedEditing&&!pointIn(seedValueRect,x,y))commitSeed();
 if(rightButton)return;
 if(pointIn(footerSettingsRect,x,y)){setFooterExpanded(true);return;}
 for(int i=0;footerExpanded&&i<2;++i)if(pointIn(footerButtonRect(i),x,y)){mouseUp();ReleaseCapture();if(i==0)toggleTheme();else toggleZoom();return;}
 if(pointIn(logoHitRect,x,y)){toggleZoom();return;}
 if(pointIn(seedValueRect,x,y)){beginSeedEdit();return;}
 if(pointIn(seedRefreshRect,x,y)){LARGE_INTEGER ticks{};QueryPerformanceCounter(&ticks);auto seed=static_cast<std::uint32_t>(ticks.QuadPart^GetTickCount64());seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;owner.setSeedFromView(seed);return;}
 for(int index=0;index<2;++index)if((index==1||owner.plain(triggerMode)>=0.5)&&pointIn(motionTabRects[index],x,y)){
  mouseUp();owner.editFromView(motionMode,toNormal(motionMode,index),2);updateFooterTooltips();return;
 }
 for(auto id:{dryLevel,wetLevel})if(pointIn(verticalLabelFor(id),x,y)){owner.toggleLevelFromView(id);return;}
 for(const auto& c:controls){
  if(!controlEnabled(c.minimum))continue;
  const auto lo=visibleControlParameter(c.minimum);
  const auto hi=visibleControlParameter(c.maximum);
  const bool hitValue=pointIn(verticalValueFor(lo),x,y);
  const auto bounds=c.slider?verticalSliderFor(lo):knobFor(lo);
  if(!hitValue&&!pointIn(bounds,x,y))continue;
  dragStartX=x;dragStartY=y;rangeDragMinimumId=lo;rangeDragMaximumId=hi;
  rangeDragMinimumStart=owner.normalized(lo);rangeDragMaximumStart=hi<parameterCount?owner.normalized(hi):rangeDragMinimumStart;
  if(hi<parameterCount){
   const auto kind=Inha::Ui::rangeDragMode(currentModifiers());
   if(kind!=Inha::Ui::RangeDragMode::handle){
    dragParameter=kind==Inha::Ui::RangeDragMode::translate?translateRangeDrag:symmetricRangeDrag;
    rangeDragMinimumStart=std::min(owner.normalized(lo),owner.normalized(hi));rangeDragMaximumStart=std::max(owner.normalized(lo),owner.normalized(hi));
    owner.editFromView(lo,rangeDragMinimumStart,-1);owner.editFromView(hi,rangeDragMaximumStart,-1);return;
   }
   if(hitValue)dragParameter=y>=c.y+51?hi:lo;
   else if(std::abs(rangeDragMaximumStart-rangeDragMinimumStart)<1e-9){dragParameter=pendingRangeDrag;return;}
   else {const double click=c.slider?clampNormal(static_cast<double>(c.y+30-y)/60):knobValue(bounds,x,y);
    dragParameter=std::abs(click-rangeDragMaximumStart)<std::abs(click-rangeDragMinimumStart)?hi:lo;}
  }else dragParameter=lo;
  dragStartValue=owner.normalized(dragParameter);
  owner.editFromView(dragParameter,dragStartValue,-1);return;
 }
}
void NativeView::mouseDoubleClick(int x,int y){
 const auto status=owner.licenseStatusForView();
 if(status==LicenseStatus::invalidLocalKey||status==LicenseStatus::denied||triggerMenuOpen||triggerMenuReveal>0){mouseDown(x,y,false);return;}
 for(auto id:{dryLevel,wetLevel}){
  if(pointIn(verticalValueFor(id),x,y)){mouseUp();owner.resetLevelFromView(id);return;}
  if(pointIn(verticalLabelFor(id),x,y))return;
 }
 for(const auto& control:controls){
  if(!controlEnabled(control.minimum))continue;
  const auto lo=visibleControlParameter(control.minimum),hi=visibleControlParameter(control.maximum);
  const auto area=control.slider?verticalSliderFor(lo):knobFor(lo);
  if(pointIn(area,x,y)||pointIn(verticalValueFor(lo),x,y)){
   mouseUp();owner.resetControlFromView(lo,hi);return;
  }
 }
 mouseDown(x,y,false);
}
void NativeView::mouseDrag(int,int y){
 const auto delta=static_cast<double>(dragStartY-y);
 if(dragParameter==pendingRangeDrag){if(std::abs(delta)<2)return;dragParameter=delta>0?rangeDragMaximumId:rangeDragMinimumId;dragStartValue=owner.normalized(dragParameter);owner.editFromView(dragParameter,dragStartValue,-1);}
 if(dragParameter==translateRangeDrag||dragParameter==symmetricRangeDrag){
  const Inha::Ui::RangeValues start{rangeDragMinimumStart,rangeDragMaximumStart};
  const auto r=dragParameter==translateRangeDrag?Inha::Ui::translateRange(start,delta,currentModifiers()):Inha::Ui::symmetricRange(start,delta,currentModifiers());
  owner.editFromView(rangeDragMinimumId,r.minimum,0);owner.editFromView(rangeDragMaximumId,r.maximum,0);return;
 }
 if(dragParameter>=parameterCount)return;
 auto value=Inha::Ui::draggedValue(dragStartValue,delta,currentModifiers());
 if(rangeDragMaximumId<parameterCount){if(dragParameter==rangeDragMinimumId)value=std::min(value,owner.normalized(rangeDragMaximumId));else value=std::max(value,owner.normalized(rangeDragMinimumId));}
 owner.editFromView(dragParameter,value,0);
}
void NativeView::mouseUp(){
 if(dragParameter==translateRangeDrag||dragParameter==symmetricRangeDrag){owner.editFromView(rangeDragMinimumId,owner.normalized(rangeDragMinimumId),1);owner.editFromView(rangeDragMaximumId,owner.normalized(rangeDragMaximumId),1);}
 else if(dragParameter<parameterCount)owner.editFromView(dragParameter,owner.normalized(dragParameter),1);
 dragParameter=parameterCount;rangeDragMinimumId=rangeDragMaximumId=parameterCount;
}
void NativeView::beginSeedEdit()
{
    seedEditing = true;
    seedSelectAll = true;
    seedText = std::to_wstring (owner.seed());
    invalidate();
}

void NativeView::editSeedCharacter (wchar_t character)
{
    if (character == L'\r')
    {
        commitSeed();
        return;
    }
    if (character == L'\b')
    {
        if (seedSelectAll)
            seedText.clear();
        else if (! seedText.empty())
            seedText.pop_back();
        seedSelectAll = false;
    }
    else if (character >= L'0' && character <= L'9')
    {
        if (seedSelectAll)
            seedText.clear();
        seedSelectAll = false;
        if (seedText.size() < 10)
            seedText.push_back (character);
    }
    invalidate();
}

void NativeView::commitSeed()
{
    std::uint64_t parsed = 0;
    for (const auto character : seedText)
        if (character >= L'0' && character <= L'9')
            parsed = std::min<std::uint64_t> (4294967295ull, parsed * 10u + static_cast<unsigned> (character - L'0'));
    owner.setSeedFromView (static_cast<std::uint32_t> (parsed));
    seedEditing = false;
    seedSelectAll = false;
    invalidate();
}

class PluginFactory final : public IPluginFactory3
{
public:
    tresult PLUGIN_API queryInterface (const TUID requestedIid,
                                       void** object) override
    {
        if (object == nullptr)
            return kInvalidArgument;
        *object = nullptr;
        if (iidEqual (requestedIid, FUnknown_iid)
            || iidEqual (requestedIid, IPluginFactory_iid)
            || iidEqual (requestedIid, IPluginFactory2_iid)
            || iidEqual (requestedIid, IPluginFactory3_iid))
        {
            *object = static_cast<IPluginFactory3*> (this);
            addRef();
            return kResultOk;
        }
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return ++references; }
    uint32 PLUGIN_API release() override
    {
        const auto current = references.load();
        return current > 0 ? --references : 0;
    }
    tresult PLUGIN_API getFactoryInfo (PFactoryInfo* info) override
    {
        if (info == nullptr)
            return kInvalidArgument;
        std::memset (info, 0, sizeof (*info));
        copyAscii (info->vendor, "INHA");
        copyAscii (info->url, "https://inha.jp");
        copyAscii (info->email, "vst@inha.jp");
        info->flags = PFactoryInfo::kUnicode;
        return kResultOk;
    }
    int32 PLUGIN_API countClasses() override { return 2; }
    tresult PLUGIN_API getClassInfo (int32 index, PClassInfo* info) override
    {
        if (info == nullptr || index < 0 || index > 1)
            return kInvalidArgument;
        std::memset (info, 0, sizeof (*info));
        std::memcpy (info->cid, index == 0 ? componentCID : controllerCID, sizeof (TUID));
        info->cardinality = PClassInfo::kManyInstances;
        copyAscii (info->category, index == 0 ? kVstAudioEffectClass : kVstComponentControllerClass);
        copyAscii (info->name, index == 0 ? "INHA Grain" : "INHA Grain Controller");
        return kResultOk;
    }
    tresult PLUGIN_API getClassInfo2 (int32 index, PClassInfo2* info) override
    {
        if (info == nullptr || index < 0 || index > 1)
            return kInvalidArgument;
        std::memset (info, 0, sizeof (*info));
        std::memcpy (info->cid, index == 0 ? componentCID : controllerCID, sizeof (TUID));
        info->cardinality = PClassInfo::kManyInstances;
        copyAscii (info->category, index == 0 ? kVstAudioEffectClass : kVstComponentControllerClass);
        copyAscii (info->name, index == 0 ? "INHA Grain" : "INHA Grain Controller");
        info->classFlags = 0;
        copyAscii (info->subCategories, index == 0 ? "Fx|Delay" : "");
        copyAscii (info->vendor, "INHA");
        copyAscii (info->version, InhaGrainVersion::ascii);
        copyAscii (info->sdkVersion, "VST 3.7");
        return kResultOk;
    }
    tresult PLUGIN_API getClassInfoUnicode (int32 index, PClassInfoW* info) override
    {
        if (info == nullptr || index < 0 || index > 1)
            return kInvalidArgument;
        std::memset (info, 0, sizeof (*info));
        std::memcpy (info->cid, index == 0 ? componentCID : controllerCID, sizeof (TUID));
        info->cardinality = PClassInfo::kManyInstances;
        copyAscii (info->category, index == 0 ? kVstAudioEffectClass : kVstComponentControllerClass);
        copyUtf16 (info->name, index == 0 ? "INHA Grain" : "INHA Grain Controller");
        copyAscii (info->subCategories, index == 0 ? "Fx|Delay" : "");
        copyUtf16 (info->vendor, "INHA");
        copyUtf16 (info->version, InhaGrainVersion::ascii);
        copyUtf16 (info->sdkVersion, "VST 3.7");
        return kResultOk;
    }
    tresult PLUGIN_API setHostContext (FUnknown*) override { return kResultOk; }
    tresult PLUGIN_API createInstance (FIDString cid, FIDString requestedIid,
                                       void** object) override
    {
        if (cid == nullptr || requestedIid == nullptr || object == nullptr)
            return kInvalidArgument;
        *object = nullptr;
        if (std::memcmp (cid, componentCID, sizeof (TUID)) != 0
            && std::memcmp (cid, controllerCID, sizeof (TUID)) != 0)
            return kNoInterface;
        auto* instance = new PluginInstance();
        const auto result = instance->queryInterface (requestedIid, object);
        instance->release();
        return result;
    }

private:
    std::atomic<uint32> references { 1 };
};

PluginFactory factory;
}

extern "C" SMTG_EXPORT_SYMBOL IPluginFactory* PLUGIN_API GetPluginFactory()
{
    factory.addRef();
    return &factory;
}

extern "C" SMTG_EXPORT_SYMBOL bool InitDll()
{
    if (gdiplusToken != 0)
        return true;
    Gdiplus::GdiplusStartupInput input;
    return Gdiplus::GdiplusStartup (&gdiplusToken, &input, nullptr) == Gdiplus::Ok;
}

extern "C" SMTG_EXPORT_SYMBOL bool ExitDll()
{
    if (gdiplusToken != 0)
    {
        Gdiplus::GdiplusShutdown (gdiplusToken);
        gdiplusToken = 0;
    }
    return true;
}

BOOL WINAPI DllMain (HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        moduleInstance = instance;
        DisableThreadLibraryCalls (instance);
    }
    return TRUE;
}
