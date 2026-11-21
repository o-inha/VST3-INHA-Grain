#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace
{
void require (bool value, const char* message) { if (!value) throw std::runtime_error (message); }
bool same (const TUID a, const TUID b) { return std::memcmp (a,b,sizeof (TUID)) == 0; }
class MemoryStream final : public IBStream
{
public:
    tresult PLUGIN_API queryInterface (const TUID iid, void** p) override {
        if (!p) return kInvalidArgument; *p=nullptr;
        if (!same(iid,FUnknown_iid) && !same(iid,IBStream_iid)) return kNoInterface;
        *p=static_cast<IBStream*>(this); return kResultOk;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
    tresult PLUGIN_API read (void* p,int32 n,int32* readCount) override {
        if (!p || n<0) return kInvalidArgument;
        const auto count=std::min<std::size_t>(static_cast<std::size_t>(n),position<bytes.size()?bytes.size()-position:0);
        if(count) std::memcpy(p,bytes.data()+position,count); position+=count;
        if(readCount) *readCount=static_cast<int32>(count);
        return count==static_cast<std::size_t>(n)?kResultOk:kResultFalse;
    }
    tresult PLUGIN_API write (void* p,int32 n,int32* written) override {
        if(!p || n<0) return kInvalidArgument;
        if(position+static_cast<std::size_t>(n)>bytes.size()) bytes.resize(position+n);
        if(n) std::memcpy(bytes.data()+position,p,n); position+=n;
        if(written) *written=n; return kResultOk;
    }
    tresult PLUGIN_API seek (int64 offset,int32 mode,int64* result) override {
        const int64 base=mode==0?0:mode==1?static_cast<int64>(position):mode==2?static_cast<int64>(bytes.size()):-1;
        if(base<0 || offset < -base) return kInvalidArgument;
        position=static_cast<std::size_t>(base+offset); if(result) *result=static_cast<int64>(position); return kResultOk;
    }
    tresult PLUGIN_API tell (int64* result) override {if(!result)return kInvalidArgument;*result=static_cast<int64>(position);return kResultOk;}
    std::vector<std::uint8_t> bytes;
    std::size_t position=0;
};

class Queue final : public IParamValueQueue
{
public:
    explicit Queue(ParamID v):id(v){}
    tresult PLUGIN_API queryInterface(const TUID iid,void** p) override {
        if(!p)return kInvalidArgument;*p=nullptr;
        if(!same(iid,FUnknown_iid)&&!same(iid,IParamValueQueue_iid))return kNoInterface;
        *p=static_cast<IParamValueQueue*>(this);return kResultOk;
    }
    uint32 PLUGIN_API addRef() override{return 1;}
    uint32 PLUGIN_API release() override{return 1;}
    ParamID PLUGIN_API getParameterId() override{return id;}
    int32 PLUGIN_API getPointCount() override{return static_cast<int32>(points.size());}
    tresult PLUGIN_API getPoint(int32 i,int32& offset,ParamValue& value) override {
        if(i<0||static_cast<std::size_t>(i)>=points.size())return kInvalidArgument;
        offset=points[i].first;value=points[i].second;return kResultOk;
    }
    tresult PLUGIN_API addPoint(int32 offset,ParamValue value,int32& i) override {
        i=static_cast<int32>(points.size());points.emplace_back(offset,value);return kResultOk;
    }
    ParamID id;std::vector<std::pair<int32,ParamValue>> points;
};
class Changes final : public IParameterChanges
{
public:
    tresult PLUGIN_API queryInterface(const TUID iid,void** p) override {
        if(!p)return kInvalidArgument;*p=nullptr;
        if(!same(iid,FUnknown_iid)&&!same(iid,IParameterChanges_iid))return kNoInterface;
        *p=static_cast<IParameterChanges*>(this);return kResultOk;
    }
    uint32 PLUGIN_API addRef() override{return 1;}
    uint32 PLUGIN_API release() override{return 1;}
    int32 PLUGIN_API getParameterCount() override{return static_cast<int32>(queues.size());}
    IParamValueQueue* PLUGIN_API getParameterData(int32 i) override {
        return i>=0&&static_cast<std::size_t>(i)<queues.size()?queues[i].get():nullptr;
    }
    IParamValueQueue* PLUGIN_API addParameterData(const ParamID& id,int32& index) override {
        for(std::size_t i=0;i<queues.size();++i)if(queues[i]->id==id){index=static_cast<int32>(i);return queues[i].get();}
        index=static_cast<int32>(queues.size());queues.push_back(std::make_unique<Queue>(id));return queues.back().get();
    }
    void add(ParamID id,int32 offset,double value) {int32 q=0,p=0;addParameterData(id,q)->addPoint(offset,value,p);}
private: std::vector<std::unique_ptr<Queue>> queues;
};
class Events final : public IEventList
{
public:
    tresult PLUGIN_API queryInterface(const TUID iid,void** p) override {
        if(!p)return kInvalidArgument;*p=nullptr;
        if(!same(iid,FUnknown_iid)&&!same(iid,IEventList_iid))return kNoInterface;
        *p=static_cast<IEventList*>(this);return kResultOk;
    }
    uint32 PLUGIN_API addRef() override{return 1;}
    uint32 PLUGIN_API release() override{return 1;}
    int32 PLUGIN_API getEventCount() override{return static_cast<int32>(events.size());}
    tresult PLUGIN_API getEvent(int32 i,Event& e) override {
        if(i<0||static_cast<std::size_t>(i)>=events.size())return kInvalidArgument;e=events[i];return kResultOk;
    }
    tresult PLUGIN_API addEvent(Event& e) override {events.push_back(e);return kResultOk;}
    void note(int offset) {Event e{};e.type=Event::kNoteOnEvent;e.sampleOffset=offset;e.busIndex=0;e.noteOn.pitch=60;e.noteOn.velocity=1.0f;e.noteOn.noteId=-1;events.push_back(e);}
    std::vector<Event> events;
};

constexpr TUID grateComponentCID=INLINE_UID(0x494E4841,0x47524154,0x45465831,0x7DA92E11);
constexpr TUID grateControllerCID=INLINE_UID(0x494E4841,0x47524154,0x45435431,0x7DA92E12);
constexpr TUID previousComponentCID=INLINE_UID(0x494E4841,0x47524149,0x4E465831,0x7DA92E01);
constexpr TUID previousControllerCID=INLINE_UID(0x494E4841,0x47524149,0x4E435431,0x7DA92E02);

void requireCurrentBranding(const std::filesystem::path& path,bool productBinary=false)
{
    std::ifstream stream(path,std::ios::binary);
    require(stream.good(),"Cannot inspect product binary or resource branding");
    const std::string bytes((std::istreambuf_iterator<char>(stream)),std::istreambuf_iterator<char>());
    if(productBinary)
        require(bytes.find("inha-grate")!=std::string::npos
                &&bytes.find("INHA-Grate-device-v1:")!=std::string::npos,
                "New Grate licence product identity was missing from the binary");
    for(const char* previous:{"INHA Grain","inha-grain","INHA-Grain-device-v1:"})
    {
        require(bytes.find(previous)==std::string::npos,"Previous product branding remained in the binary or resources");
        std::string utf16;
        for(const char* p=previous;*p;++p){utf16.push_back(*p);utf16.push_back(0);}
        require(bytes.find(utf16)==std::string::npos,"Previous wide-string product branding remained in the binary or resources");
    }
}

class Module
{
public:
    explicit Module(const wchar_t* path) {
        library=LoadLibraryW(path);require(library!=nullptr,"LoadLibrary failed");
        auto init=reinterpret_cast<bool(*)()>(GetProcAddress(library,"InitDll"));
        exit=reinterpret_cast<bool(*)()>(GetProcAddress(library,"ExitDll"));
        auto factoryFn=reinterpret_cast<IPluginFactory*(PLUGIN_API*)()>(GetProcAddress(library,"GetPluginFactory"));
        require(init&&exit&&factoryFn&&init(),"Required VST3 exports/initialization failed");
        factory=factoryFn();require(factory&&factory->countClasses()==2,"Expected component and controller factory classes");
        PFactoryInfo info{};require(factory->getFactoryInfo(&info)==kResultOk,"Factory metadata failed");
        require(std::string(info.vendor)=="INHA","Wrong vendor identity");
        for(int i=0;i<2;++i)
        {
            PClassInfo c{};require(factory->getClassInfo(i,&c)==kResultOk,"Class metadata failed");
            require(std::string(c.name)==(i==0?"INHA Grate":"INHA Grate Controller"),"Wrong Grate product identity");
            require(same(c.cid,i==0?grateComponentCID:grateControllerCID),"Factory did not expose the new Grate class IDs");
        }
        for(int i=0;i<2;++i)
        {
            void* oldInstance=nullptr;
            require(factory->createInstance(i==0?previousComponentCID:previousControllerCID,
                    i==0?IComponent_iid:IEditController_iid,&oldInstance)!=kResultOk&&oldInstance==nullptr,
                    "The renamed plugin still exposed a previous product class ID");
        }
        const std::filesystem::path binary(path);
        requireCurrentBranding(binary,true);
        const auto resources=binary.parent_path().parent_path()/L"Resources";
        require(std::filesystem::is_directory(resources),"The VST3 resource directory was missing");
        for(const auto& entry:std::filesystem::recursive_directory_iterator(resources))
        {
            const auto name=entry.path().filename().string();
            require(name.find("Grain")==std::string::npos&&name.find("grain")==std::string::npos,
                    "Previous product name remained in a resource filename");
            if(entry.is_regular_file())requireCurrentBranding(entry.path());
        }
    }
    ~Module(){if(factory)factory->release();if(exit)exit();if(library)FreeLibrary(library);}
    IPluginFactory* factory=nullptr;
private: HMODULE library=nullptr;bool(*exit)()=nullptr;
};
class Instance
{
public:
    explicit Instance(Module& module) {
        PClassInfo info{};require(module.factory->getClassInfo(0,&info)==kResultOk,"Component class missing");
        require(module.factory->createInstance(info.cid,IComponent_iid,reinterpret_cast<void**>(&component))==kResultOk&&component,"Component creation failed");
        require(component->initialize(nullptr)==kResultOk,"Component initialize failed");
        TUID controllerId{};
        require(component->getControllerClassId(controllerId)==kResultOk&&same(controllerId,grateControllerCID),
                "Component retained the previous controller identity");
        require(component->queryInterface(IAudioProcessor_iid,reinterpret_cast<void**>(&audio))==kResultOk&&audio,"Audio processor interface missing");
        require(component->queryInterface(IEditController_iid,reinterpret_cast<void**>(&controller))==kResultOk&&controller,"Controller interface missing");
        require(controller->getParameterCount()==37,"Parameter inventory changed unexpectedly");
        require(audio->canProcessSampleSize(kSample32)==kResultTrue&&audio->canProcessSampleSize(kSample64)==kResultTrue,"Both sample formats required");
        IProcessContextRequirements* contextRequirements=nullptr;
        require(component->queryInterface(IProcessContextRequirements_iid,reinterpret_cast<void**>(&contextRequirements))==kResultOk&&contextRequirements,"VST3 process context requirements interface missing");
        require((contextRequirements->getProcessContextRequirements()&IProcessContextRequirements::kNeedTransportState)!=0,"Transport state requirement missing");
        require((contextRequirements->getProcessContextRequirements()&IProcessContextRequirements::kNeedTempo)!=0,"Tempo context requirement missing");
        contextRequirements->release();
        require(component->getBusCount(kAudio,kInput)==1&&component->getBusCount(kAudio,kOutput)==1,"Expected one input/output audio bus");
        require(component->getBusCount(kEvent,kInput)==1,"MIDI event input missing");
    }
    ~Instance(){if(processing)audio->setProcessing(false);if(active)component->setActive(false);if(controller)controller->release();if(audio)audio->release();if(component){component->terminate();component->release();}}
    void setup(int channels,int symbolic) {
        SpeakerArrangement arrangement=channels==1?SpeakerArr::kMono:SpeakerArr::kStereo;
        require(audio->setBusArrangements(&arrangement,1,&arrangement,1)==kResultOk,"Mono/stereo negotiation failed");
        ProcessSetup setup{};setup.processMode=kRealtime;setup.symbolicSampleSize=symbolic;setup.maxSamplesPerBlock=256;setup.sampleRate=48000;
        require(audio->setupProcessing(setup)==kResultOk,"Processing setup failed");
        require(component->setActive(true)==kResultOk,"setActive failed");active=true;
        require(audio->setProcessing(true)==kResultOk,"setProcessing failed");processing=true;
    }
    double normalized(ParamID id,double plain) {return controller->plainParamToNormalized(id,plain);}
    void stateRoundTrip() {
        MemoryStream before;require(component->getState(&before)==kResultOk&&!before.bytes.empty(),"getState failed");
        before.position=0;require(component->setState(&before)==kResultOk,"setState failed");
        MemoryStream after;require(component->getState(&after)==kResultOk,"State re-save failed");
        require(before.bytes==after.bytes,"State round trip changed bytes");
        before.position=0;require(controller->setComponentState(&before)==kResultOk,"Controller state load failed");
    }
    IComponent* component=nullptr;IAudioProcessor* audio=nullptr;IEditController* controller=nullptr;
private:bool active=false,processing=false;
};

std::uint32_t stateWord(const MemoryStream& state,std::size_t offset)
{
    require(offset+sizeof(std::uint32_t)<=state.bytes.size(),"State header is truncated");
    std::uint32_t value=0;std::memcpy(&value,state.bytes.data()+offset,sizeof(value));return value;
}
double stateValue(const MemoryStream& state,ParamID id)
{
    const auto offset=8+static_cast<std::size_t>(id)*sizeof(double);
    require(offset+sizeof(double)<=state.bytes.size(),"State parameter payload is truncated");
    double value=0;std::memcpy(&value,state.bytes.data()+offset,sizeof(value));return value;
}
void replaceStateWord(MemoryStream& state,std::size_t offset,std::uint32_t value)
{
    require(offset+sizeof(value)<=state.bytes.size(),"Cannot replace missing state header");
    std::memcpy(state.bytes.data()+offset,&value,sizeof(value));
}
void replaceStateValue(MemoryStream& state,ParamID id,double value)
{
    const auto offset=8+static_cast<std::size_t>(id)*sizeof(double);
    require(offset+sizeof(value)<=state.bytes.size(),"Cannot replace missing state parameter");
    std::memcpy(state.bytes.data()+offset,&value,sizeof(value));
}
void stateCompatibility(Module& module)
{
    Instance instance(module);
    ParameterInfo motionInfo{},syncInfo{},massInfo{},tripletInfo{},eqInfo{},duckInfo{};
    require(instance.controller->getParameterInfo(24,motionInfo)==kResultOk&&motionInfo.id==24
            &&motionInfo.stepCount==1&&(motionInfo.flags&ParameterInfo::kIsList)!=0
            &&(motionInfo.flags&ParameterInfo::kCanAutomate)!=0,
            "Motion mode must retain its automatable two-item host list");
    require(instance.controller->getParameterInfo(29,syncInfo)==kResultOk&&syncInfo.id==29
            &&syncInfo.stepCount==1&&(syncInfo.flags&ParameterInfo::kCanAutomate)!=0,
            "SYNC must be an appended automatable switch");
    require(instance.controller->getParameterInfo(30,massInfo)==kResultOk&&massInfo.id==30
            &&massInfo.stepCount==7,"MASS must expose eight integer voice counts");
    for(const ParamID id:{33u,34u})
        require(instance.controller->getParameterInfo(id,tripletInfo)==kResultOk&&tripletInfo.id==id
                &&tripletInfo.stepCount==1&&(tripletInfo.flags&ParameterInfo::kCanAutomate)!=0,
                "Triplet toggles must be appended automatable switches");
    for(const ParamID id:{35u,36u})
    {
        require(instance.controller->getParameterInfo(id,eqInfo)==kResultOk&&eqInfo.id==id
                &&eqInfo.stepCount==0&&(eqInfo.flags&ParameterInfo::kCanAutomate)!=0,
                "Wet EQ cutoffs must be appended continuous automatable parameters");
        require(eqInfo.defaultNormalizedValue==(id==35?0.0:1.0),"Default EQ endpoints were not neutral");
    }
    require(instance.controller->getParameterInfo(17,duckInfo)==kResultOk,"Wet duck parameter missing");
    const std::array<char16,9> wetDuckTitle{{'D','u','c','k',' ','w','e','t',0}};
    require(std::equal(wetDuckTitle.begin(),wetDuckTitle.end(),duckInfo.title),"Old duck automation ID did not expose Duck wet");
    String128 motionText{};
    require(instance.controller->getParamStringByValue(24,1.0,motionText)==kResultOk,
            "Could not format SWAY mode for host");
    const std::array<char16,5> expectedSway{{'S','W','A','Y',0}};
    require(std::equal(expectedSway.begin(),expectedSway.end(),motionText),"Host motion list does not label SWAY correctly");
    MemoryStream saved;
    require(instance.component->getState(&saved)==kResultOk,"Could not save initial v5 state");
    require(saved.bytes.size()==8+37*sizeof(double)&&stateWord(saved,0)==0x47524154u&&stateWord(saved,4)==5,
            "Expected v5 state with 37 parameter values");
    require(stateValue(saved,35)==0.0&&stateValue(saved,36)==1.0,"Fresh project EQ was not neutral");
    require(stateValue(saved,17)==instance.normalized(17,0)&&stateValue(saved,18)==instance.normalized(18,50),
            "New projects did not use zero wet duck and fifty-percent previous-event duck");
    const auto setPlain=[&](ParamID id,double plain){replaceStateValue(saved,id,instance.normalized(id,plain));};
    // Independent motion banks and new controls must survive a full project save.
    setPlain(4,500);setPlain(5,2000);setPlain(6,4000); // Preserve requests exceeding BUFFER.
    setPlain(9,-35);setPlain(10,45);setPlain(11,75);setPlain(12,180);
    setPlain(24,1);setPlain(25,17);setPlain(26,63);setPlain(27,250);setPlain(28,3500);
    setPlain(29,1);setPlain(30,6);setPlain(31,-70);setPlain(32,85);setPlain(33,1);setPlain(34,1);setPlain(35,180);setPlain(36,8500);
    saved.position=0;
    require(instance.component->setState(&saved)==kResultOk,"Could not restore v5 state");
    MemoryStream restored;
    require(instance.component->getState(&restored)==kResultOk&&saved.bytes==restored.bytes,
            "v5 state round trip lost requested ranges or new controls");
    saved.position=0;
    require(instance.controller->setComponentState(&saved)==kResultOk,"Controller rejected v5 state");
    for(ParamID id:{4u,5u,6u,9u,10u,11u,12u,24u,25u,26u,27u,28u,29u,30u,31u,32u,33u,34u,35u,36u})
        require(instance.controller->getParamNormalized(id)==stateValue(saved,id),"Controller lost an independent parameter");

    // Construct actual old encodings, rather than relabelling current normalized values.
    const auto oldTime=[](double value,double minimum,double maximum){return std::cbrt((value-minimum)/(maximum-minimum));};
    MemoryStream migrated;
    for(const std::uint32_t version:{1u,2u})
    {
        MemoryStream legacy;
        legacy.bytes=saved.bytes;legacy.bytes.resize(8+(version==1?24:29)*sizeof(double));
        replaceStateWord(legacy,4,version);replaceStateWord(legacy,0,0x47524149u);
        replaceStateValue(legacy,4,oldTime(2000,10,20000));
        replaceStateValue(legacy,5,oldTime(20,5,10000)); // New minimum clips to 40 ms.
        replaceStateValue(legacy,6,oldTime(1200,5,10000));
        replaceStateValue(legacy,13,oldTime(500,1,10000));
        replaceStateValue(legacy,14,oldTime(1000,1,10000));
        replaceStateValue(legacy,15,oldTime(800,5,30000));
        replaceStateValue(legacy,16,oldTime(10000,5,30000)); // New maximum clips to 6 s.
        if(version==2){replaceStateValue(legacy,27,oldTime(250,25,20000));replaceStateValue(legacy,28,oldTime(3500,25,20000));}
        require(instance.component->setState(&legacy)==kResultOk,"Existing project version is not compatible");
        migrated.bytes.clear();migrated.position=0;
        require(instance.component->getState(&migrated)==kResultOk&&stateWord(migrated,4)==5&&stateWord(migrated,0)==0x47524154u,
                "Legacy project did not migrate to v5 state");
        for(const auto expected:std::array<std::pair<ParamID,double>,7>{{{4,2000},{5,40},{6,1200},{13,500},{14,1000},{15,800},{16,6000}}})
            require(std::abs(stateValue(migrated,expected.first)-instance.normalized(expected.first,expected.second))<1e-10,
                    "Migration did not preserve and clip old milliseconds");
        for(ParamID id:{0u,1u,2u,3u,7u,8u,9u,10u,11u,12u,17u,18u,19u,20u,21u,22u,23u})
            require(stateValue(migrated,id)==stateValue(legacy,id),"Migration changed a non-time legacy parameter");
        if(version==1)
        {
            require(stateValue(migrated,24)==0.0,"Legacy project did not default to UNDULATION");
            for(const auto expected:std::array<std::pair<ParamID,double>,4>{{{25,10},{26,30},{27,500},{28,1500}}})
                require(std::abs(stateValue(migrated,expected.first)-instance.normalized(expected.first,expected.second))<1e-10,
                        "Legacy project did not initialize its SWAY defaults");
        }
        else
        {
            require(stateValue(migrated,24)==stateValue(legacy,24),"Migration lost selected motion mode");
            require(std::abs(stateValue(migrated,27)-instance.normalized(27,250))<1e-10
                    &&std::abs(stateValue(migrated,28)-instance.normalized(28,3500))<1e-10,"v2 SWAY milliseconds were not migrated");
        }
        require(stateValue(migrated,29)==instance.normalized(29,0)
                &&stateValue(migrated,30)==instance.normalized(30,1)
                &&stateValue(migrated,31)==instance.normalized(31,0)
                &&stateValue(migrated,32)==instance.normalized(32,0)
                &&stateValue(migrated,33)==0.0&&stateValue(migrated,34)==0.0
                &&stateValue(migrated,35)==0.0&&stateValue(migrated,36)==1.0,"Legacy project did not initialize new controls safely");
    }
    // v3 already uses the current time encoding and retains all 33 old lanes.
    MemoryStream v3;
    v3.bytes=saved.bytes;v3.bytes.resize(8+33*sizeof(double));replaceStateWord(v3,4,3);replaceStateWord(v3,0,0x47524149u);
    require(instance.component->setState(&v3)==kResultOk,"Existing v3 projects are not compatible");
    migrated.bytes.clear();migrated.position=0;
    require(instance.component->getState(&migrated)==kResultOk&&stateWord(migrated,4)==5&&stateWord(migrated,0)==0x47524154u,
            "v3 project did not migrate to v5 state");
    for(ParamID id=0;id<33;++id)
        require(stateValue(migrated,id)==stateValue(v3,id),"v3 migration changed an existing parameter");
    require(stateValue(migrated,33)==0.0&&stateValue(migrated,34)==0.0,
            "v3 project did not default both triplet switches off");
    require(stateValue(migrated,35)==0.0&&stateValue(migrated,36)==1.0,"v3 project did not load neutral EQ");

    // Both prior Grate v4 projects and manually imported Grain v4 presets keep
    // their 35 existing controls and receive neutral values for the two new cuts.
    for(const auto magic:{0x47524154u,0x47524149u})
    {
        MemoryStream previousV4;
        previousV4.bytes=saved.bytes;previousV4.bytes.resize(8+35*sizeof(double));
        replaceStateWord(previousV4,0,magic);replaceStateWord(previousV4,4,4);
        require(instance.component->setState(&previousV4)==kResultOk,"Import of a previous v4 project/preset failed");
        migrated.bytes.clear();migrated.position=0;
        require(instance.component->getState(&migrated)==kResultOk&&stateWord(migrated,4)==5
                &&stateWord(migrated,0)==0x47524154u,"v4 project did not write the new state identity");
        for(ParamID id=0;id<35;++id)
            require(stateValue(migrated,id)==stateValue(previousV4,id),"v4 import lost an existing parameter");
        require(stateValue(migrated,35)==0.0&&stateValue(migrated,36)==1.0,"v4 import did not initialize neutral EQ");
    }

    const auto rejected=[&](MemoryStream& invalid,const char* message)
    {
        invalid.position=0;
        require(instance.component->setState(&invalid)!=kResultOk,message);
        MemoryStream after;
        require(instance.component->getState(&after)==kResultOk&&after.bytes==migrated.bytes,
                "Rejected state partially changed the plugin");
    };
    MemoryStream invalid;invalid.bytes=saved.bytes;replaceStateWord(invalid,0,0);rejected(invalid,"Invalid state magic accepted");
    invalid.bytes=saved.bytes;replaceStateWord(invalid,4,99);rejected(invalid,"Unknown future state version accepted");
    invalid.bytes=saved.bytes;invalid.bytes.pop_back();rejected(invalid,"Truncated v5 parameter payload accepted");
    invalid.bytes.assign(4,0);rejected(invalid,"Truncated state header accepted");
    require(instance.component->setState(nullptr)!=kResultOk,"Null state stream accepted");
    std::cout<<"v1-v4 migration, v5 wet EQ state, new controls and invalid-state rejection passed\n";
}

template<typename Sample> void formatSmoke(Module& module,int channels,int symbolic) {
    Instance instance(module);instance.stateRoundTrip();instance.setup(channels,symbolic);
    Changes parameters;parameters.add(23,0,1.0); // Bypass: exact unity dry after its click-free ramp.
    std::array<std::array<Sample,256>,2> input{},output{};
    Sample* ins[2]{input[0].data(),input[1].data()};Sample* outs[2]{output[0].data(),output[1].data()};
    AudioBusBuffers in{},out{};in.numChannels=out.numChannels=channels;
    if constexpr(std::is_same_v<Sample,float>){in.channelBuffers32=ins;out.channelBuffers32=outs;}
    else {in.channelBuffers64=ins;out.channelBuffers64=outs;}
    ProcessContext context{};context.sampleRate=48000;context.state=ProcessContext::kPlaying;
    ProcessData data{};data.processMode=kRealtime;data.symbolicSampleSize=symbolic;data.numSamples=256;data.numInputs=data.numOutputs=1;data.inputs=&in;data.outputs=&out;data.processContext=&context;
    for(int block=0;block<32;++block){
        for(int i=0;i<256;++i){input[0][i]=static_cast<Sample>(0.15*std::sin((block*256+i)*0.057));input[1][i]=static_cast<Sample>(0.13*std::cos((block*256+i)*0.031));}
        context.projectTimeSamples=block*256;data.inputParameterChanges=block==0?&parameters:nullptr;
        require(instance.audio->process(data)==kResultOk,"Audio callback failed");
        for(int c=0;c<channels;++c)for(int i=0;i<256;++i){require(std::isfinite(output[c][i]),"Non-finite host audio");if(block>=24)require(std::abs(output[c][i]-input[c][i])<1e-6,"Bypass is not unity dry");}
    }
    std::cout<<channels<<" channel / "<<(symbolic==kSample32?32:64)<<" bit passed\n";
}

std::vector<float> renderFixture(Module& module,int noteOffset,int wetAutomationOffset=-1,
                                 int bypassAutomationOffset=-1,int initialMotionMode=0,
                                 int motionAutomationOffset=-1) {
    Instance instance(module);instance.setup(2,kSample32);
    Changes controls;
    const auto set=[&](ParamID id,double value){controls.add(id,0,instance.normalized(id,value));};
    set(0,2);set(1,100);set(4,500);set(5,100);set(6,100);set(7,0);set(8,0);
    set(9,0);set(10,0);set(13,40);set(14,40);set(15,1200);set(16,1200);
    set(17,0);set(18,0);set(19,0);set(20,0);
    set(24,initialMotionMode);set(25,60);set(26,60);set(27,40);set(28,40);
    std::array<std::array<float,256>,2> input{},output{};
    float* ins[2]{input[0].data(),input[1].data()};float* outs[2]{output[0].data(),output[1].data()};
    AudioBusBuffers in{},out{};in.numChannels=out.numChannels=2;in.channelBuffers32=ins;out.channelBuffers32=outs;
    ProcessContext context{};context.sampleRate=48000;context.state=ProcessContext::kPlaying;
    ProcessData data{};data.processMode=kRealtime;data.symbolicSampleSize=kSample32;data.numSamples=256;data.numInputs=data.numOutputs=1;data.inputs=&in;data.outputs=&out;data.processContext=&context;
    std::vector<float> result;result.reserve(32768);
    for(int block=0;block<128;++block){
        for(int i=0;i<256;++i)input[0][i]=input[1][i]=0.15f*std::sin(static_cast<float>(block*256+i)*0.027f);
        Events events;if(block==0&&noteOffset>=0)events.note(noteOffset);
        Changes automation;if(block==32&&wetAutomationOffset>=0){automation.add(20,0,instance.normalized(20,0));automation.add(20,wetAutomationOffset,instance.normalized(20,-60));}
        if(block==32&&bypassAutomationOffset>=0)automation.add(23,bypassAutomationOffset,1.0);
        if(block==32&&motionAutomationOffset>=0)automation.add(24,motionAutomationOffset,1.0);
        const bool automate=block==32&&(wetAutomationOffset>=0||bypassAutomationOffset>=0||motionAutomationOffset>=0);
        data.inputEvents=&events;data.inputParameterChanges=block==0?&controls:(automate?&automation:nullptr);context.projectTimeSamples=block*256;
        require(instance.audio->process(data)==kResultOk,"Fixture render callback failed");
        for(float value:output[0]){require(std::isfinite(value),"Fixture produced non-finite audio");result.push_back(value);}
    }
    return result;
}

void transportRestartRepeatability(Module& module)
{
    for(const int mode:{1,2,3})
    {
        Instance instance(module);
        MemoryStream configured;
        require(instance.component->getState(&configured)==kResultOk,"Cannot configure repeatability render");
        const auto set=[&](ParamID id,double value){replaceStateValue(configured,id,instance.normalized(id,value));};
        set(0,mode);set(1,100);set(2,75);set(3,-30);set(4,300);set(5,60);set(6,100);
        set(9,0);set(10,0);set(13,40);set(14,40);set(15,500);set(16,500);
        set(17,0);set(18,0);set(19,-60);set(20,-6);set(30,3);set(31,-70);set(32,70);
        configured.position=0;
        require(instance.component->setState(&configured)==kResultOk,"Cannot restore repeatability settings");
        instance.setup(2,kSample32);
        std::array<std::array<float,256>,2> input{},output{};
        float* ins[2]{input[0].data(),input[1].data()};float* outs[2]{output[0].data(),output[1].data()};
        AudioBusBuffers in{},out{};in.numChannels=out.numChannels=2;in.channelBuffers32=ins;out.channelBuffers32=outs;
        ProcessContext context{};context.sampleRate=48000;
        ProcessData data{};data.processMode=kRealtime;data.symbolicSampleSize=kSample32;data.numSamples=256;
        data.numInputs=data.numOutputs=1;data.inputs=&in;data.outputs=&out;data.processContext=&context;
        const auto render=[&](bool playing,int blocks)
        {
            std::vector<float> result;
            result.reserve(static_cast<std::size_t>(blocks)*512);
            context.state=playing?ProcessContext::kPlaying|ProcessContext::kCycleActive:0;
            for(int block=0;block<blocks;++block)
            {
                for(int i=0;i<256;++i)
                {
                    const auto n=block*256+i;
                    const auto gate=n%7200<3600?1.0:0.0;
                    input[0][i]=playing?static_cast<float>(gate*(0.16*std::sin(n*0.031)+0.08*std::cos(n*0.067))):0.0f;
                    input[1][i]=playing?static_cast<float>(gate*(0.13*std::sin(n*0.047)+0.06*std::cos(n*0.083))):0.0f;
                }
                Events events;if(playing&&block%24==0)events.note(128);
                context.projectTimeSamples=123456+(playing?block*256:0);
                data.inputEvents=&events;
                require(instance.audio->process(data)==kResultOk,"Transport repeatability callback failed");
                for(int i=0;i<256;++i){result.push_back(output[0][i]);result.push_back(output[1][i]);}
            }
            return result;
        };
        const auto first=render(true,128);
        const auto cycled=render(true,128);
        require(first==cycled,"Continuous loop rewind changed seeded output with identical source/history");
        render(false,16); // Hosts continue calling process with a stationary timeline while stopped.
        const auto restarted=render(true,128);
        require(first==restarted,"Pause/play at the same timeline changed seeded grain audio");
        double energy=0;for(const auto sample:first){require(std::isfinite(sample),"Restart render was non-finite");energy+=sample*sample;}
        require(energy>0.001,"Repeatability render did not exercise triggered grain audio");
    }
    std::cout<<"Transient/MIDI/volume continuous loops and stopped-callback restarts repeat bit-exactly\n";
}

void syncAndPanHost(Module& module)
{
    const auto render=[&](double bpm,bool sync)
    {
        Instance instance(module);
        MemoryStream configured;
        require(instance.component->getState(&configured)==kResultOk,"Cannot configure sync/pan render");
        const auto set=[&](ParamID id,double value){replaceStateValue(configured,id,instance.normalized(id,value));};
        set(0,2);set(1,100);set(9,0);set(10,0);set(17,0);set(18,0);set(19,-60);set(20,-6);
        set(29,sync?1:0);set(30,1);set(31,100);set(32,100);
        // Shared normalized time positions select exact note lengths when synced.
        for(ParamID id:{4u,15u,16u})replaceStateValue(configured,id,0.8); // One bar.
        for(ParamID id:{5u,6u})replaceStateValue(configured,id,0.2); // Eighth note.
        for(ParamID id:{13u,14u})replaceStateValue(configured,id,0.0); // Sixteenth note.
        configured.position=0;
        require(instance.component->setState(&configured)==kResultOk,"Cannot load sync/pan render settings");
        instance.setup(2,kSample32);
        if(sync)
        {
            String128 text{};
            require(instance.controller->getParamStringByValue(5,1.0,text)==kResultOk,"Host cannot format synced duration");
            const std::array<char16,7> twoBars{{'2',' ','B','A','R','S',0}};
            require(std::equal(twoBars.begin(),twoBars.end(),text),"Host did not expose two-bar duration label");
        }
        std::array<std::array<float,256>,2> input{},output{};
        float* ins[2]{input[0].data(),input[1].data()};float* outs[2]{output[0].data(),output[1].data()};
        AudioBusBuffers in{},out{};in.numChannels=out.numChannels=2;in.channelBuffers32=ins;out.channelBuffers32=outs;
        ProcessContext context{};context.sampleRate=48000;context.tempo=bpm;context.state=ProcessContext::kPlaying|ProcessContext::kTempoValid;
        ProcessData data{};data.processMode=kRealtime;data.symbolicSampleSize=kSample32;data.numSamples=256;
        data.numInputs=data.numOutputs=1;data.inputs=&in;data.outputs=&out;data.processContext=&context;
        std::array<std::vector<float>,2> result;
        for(int block=0;block<160;++block)
        {
            for(int i=0;i<256;++i)input[0][i]=input[1][i]=0.2f*std::sin(static_cast<float>(block*256+i)*0.053f);
            Events events;if(block==0)events.note(0);data.inputEvents=&events;context.projectTimeSamples=block*256;
            require(instance.audio->process(data)==kResultOk,"Sync/pan callback failed");
            for(int c=0;c<2;++c)for(const auto value:output[c]){require(std::isfinite(value),"Sync/pan render was non-finite");result[c].push_back(value);}
        }
        return result;
    };
    const auto at80=render(80,true),at120=render(120,true),slow=render(30,true);
    require(at80!=at120&&slow!=at80,"Tempo changes did not reach synced grain processing");
    double leftEnergy=0,rightEnergy=0;
    for(std::size_t i=0;i<at80[0].size();++i){leftEnergy+=at80[0][i]*at80[0][i];rightEnergy+=at80[1][i]*at80[1][i];}
    require(leftEnergy<1e-10&&rightEnergy>0.001,"PAN hard right did not reach per-grain output");
    const auto free80=render(80,false),free120=render(120,false);
    require(free80==free120,"Unsynced processing changed when only host tempo changed");
    std::cout<<"Host tempo sync at 30/80/120 BPM, note labels, and hard-right grain pan passed\n";
}

void tripletChoicesHost(Module& module)
{
    Instance instance(module);
    const auto set=[&](ParamID id,double normalized)
    {
        require(instance.controller->setParamNormalized(id,normalized)==kResultOk,"Cannot configure host triplet switch");
    };
    const auto parse=[&](ParamID id,const char* label,double& normalized)
    {
        String128 text{};for(std::size_t i=0;label[i]&&i<127;++i)text[i]=static_cast<char16>(label[i]);
        return instance.controller->getParamValueByString(id,text,normalized);
    };
    const auto formatted=[&](ParamID id,double normalized)
    {
        String128 text{};require(instance.controller->getParamStringByValue(id,normalized,text)==kResultOk,
                                "Cannot format musical duration for host");
        std::string result;for(const auto c:text){if(!c)break;result.push_back(static_cast<char>(c));}return result;
    };
    set(29,1);set(33,1);set(34,0);
    double value=0;
    require(parse(4,"1/8T",value)==kResultOk&&std::abs(value-0.1)<1e-12,
            "BUFFER triplet switch did not enable triplet host input");
    require(formatted(4,0.1)=="1/8T"&&formatted(5,0.1)=="1/8",
            "BUFFER triplet switch changed GRAIN's independent choices");
    require(parse(5,"1/8T",value)!=kResultOk,"GRAIN accepted a triplet while its own T switch was disabled");
    set(33,0);set(34,1);
    require(parse(4,"1/8T",value)!=kResultOk&&parse(5,"1/8T",value)==kResultOk,
            "BUFFER/GRAIN triplet switch pools were not independent");
    require(formatted(4,0.1)=="1/8"&&formatted(5,0.1)=="1/8T",
            "GRAIN triplet host labels did not use the selected choice pool");
    require(std::abs(instance.controller->normalizedParamToPlain(4,0.1)-250.0)<1e-5
            &&std::abs(instance.controller->normalizedParamToPlain(5,0.1)-500.0/3.0)<1e-5,
            "Triplet and straight choices did not convert independently to milliseconds");
    require(parse(13,"1/8T",value)!=kResultOk&&formatted(13,0.1)=="1/8",
            "GRAIN's T switch unexpectedly changed the event attack choices");
    set(29,0);
    require(instance.controller->getParamNormalized(34)==1.0,"Disabling SYNC discarded a stored T setting");
    set(29,1);
    require(formatted(5,0.1)=="1/8T","Re-enabling SYNC lost the selected GRAIN triplet pool");
    std::cout<<"Independent BUFFER/GRAIN triplet parsing, labels, durations and retained settings passed\n";
}

void eqFrequencyChoicesHost(Module& module)
{
    Instance instance(module);
    const auto parse=[&](ParamID id,const char* label,double& normalized)
    {
        String128 text{};for(std::size_t i=0;label[i]&&i<127;++i)text[i]=static_cast<char16>(label[i]);
        return instance.controller->getParamValueByString(id,text,normalized);
    };
    const auto formatted=[&](ParamID id,double normalized)
    {
        String128 text{};require(instance.controller->getParamStringByValue(id,normalized,text)==kResultOk,
                                "Cannot format EQ cutoff for host");
        std::string result;for(const auto c:text){if(!c)break;result.push_back(static_cast<char>(c));}return result;
    };
    require(instance.controller->setParamNormalized(29,1)==kResultOk,"Cannot enable SYNC for frequency-unit check");
    for(const ParamID id:{35u,36u})
    {
        ParameterInfo info{};require(instance.controller->getParameterInfo(id,info)==kResultOk,"EQ parameter metadata missing");
        require(info.units[0]=='H'&&info.units[1]=='z'&&info.units[2]==0,"EQ host unit is not Hz");
        require(std::abs(instance.controller->normalizedParamToPlain(id,0)-20)<1e-10
                &&std::abs(instance.controller->normalizedParamToPlain(id,1)-20000)<1e-8,
                "EQ cutoff range does not cover20Hz through20kHz");
        require(std::abs(instance.normalized(id,200)-1.0/3.0)<1e-10
                &&std::abs(instance.normalized(id,2000)-2.0/3.0)<1e-10,
                "EQ slider does not distribute frequency logarithmically");
        for(const double hz:{20.0,125.0,2000.0,20000.0})
        {
            const auto normalized=instance.normalized(id,hz);
            const auto label=formatted(id,normalized);
            require(label.find("Hz")!=std::string::npos,"SYNC changed an EQ cutoff into musical units");
            double restored=0;
            require(parse(id,label.c_str(),restored)==kResultOk
                    &&std::abs(instance.controller->normalizedParamToPlain(id,restored)-hz)<1e-6,
                    "EQ frequency text did not round-trip through the host");
        }
        double value=0;
        require(parse(id,"2 kHz",value)==kResultOk
                &&std::abs(instance.controller->normalizedParamToPlain(id,value)-2000)<1e-6,
                "EQ parser did not accept kHz input");
        require(parse(id,"250 Hz",value)==kResultOk
                &&std::abs(instance.controller->normalizedParamToPlain(id,value)-250)<1e-6,
                "EQ parser did not accept Hz input");
    }
    std::cout<<"Logarithmic EQ frequency range, Hz/kHz text, and SYNC independence passed\n";
}

void wetEqAndDuckingHost(Module& module)
{
    using Audio=std::array<std::vector<float>,2>;
    constexpr int rate=48000,blockFrames=256,blocks=384;
    constexpr double pi=3.14159265358979323846;
    const auto render=[&](double toneHz,double lowHz,double highHz,double duckPercent,
                          bool frozen,bool dryOnly,bool automateHigh,double leftAmplitude,double rightAmplitude)
    {
        Instance instance(module);MemoryStream configured;
        require(instance.component->getState(&configured)==kResultOk,"Cannot configure wet EQ/duck render");
        const auto set=[&](ParamID id,double plain){replaceStateValue(configured,id,instance.normalized(id,plain));};
        set(0,frozen?2:0);set(1,100);set(4,300);set(5,80);set(6,80);set(7,50);set(8,50);
        set(9,0);set(10,0);set(13,40);set(14,40);set(15,6000);set(16,6000);
        set(17,duckPercent);set(18,0);set(19,dryOnly?0:-60);set(20,dryOnly?-60:0);
        set(24,1);set(25,0);set(26,0);set(30,1);set(31,0);set(32,0);set(35,lowHz);set(36,highHz);
        configured.position=0;
        require(instance.component->setState(&configured)==kResultOk,"Cannot load wet EQ/duck settings");
        instance.setup(2,kSample32);
        std::array<std::array<float,blockFrames>,2> input{},output{};
        float* ins[2]{input[0].data(),input[1].data()};float* outs[2]{output[0].data(),output[1].data()};
        AudioBusBuffers in{},out{};in.numChannels=out.numChannels=2;in.channelBuffers32=ins;out.channelBuffers32=outs;
        ProcessContext context{};context.sampleRate=rate;context.state=ProcessContext::kPlaying;
        ProcessData data{};data.processMode=kRealtime;data.symbolicSampleSize=kSample32;data.numSamples=blockFrames;
        data.numInputs=data.numOutputs=1;data.inputs=&in;data.outputs=&out;data.processContext=&context;
        Audio audio;for(auto& channel:audio)channel.reserve(blockFrames*blocks);
        for(int block=0;block<blocks;++block)
        {
            for(int i=0;i<blockFrames;++i)
            {
                const int n=block*blockFrames+i;
                const auto signal=frozen&&n>=rate?0.0:toneHz==0?1.0:std::sin(2*pi*toneHz*n/rate);
                input[0][i]=static_cast<float>(leftAmplitude*signal);input[1][i]=static_cast<float>(rightAmplitude*signal);
            }
            Events events;if(frozen&&block==96)events.note(128);data.inputEvents=&events;
            Changes automation;
            if(automateHigh&&block==128)
            {
                automation.add(36,0,instance.normalized(36,20000));
                automation.add(36,173,instance.normalized(36,500));
            }
            data.inputParameterChanges=automateHigh&&block==128?&automation:nullptr;
            context.projectTimeSamples=block*blockFrames;
            require(instance.audio->process(data)==kResultOk,"Wet EQ/duck callback failed");
            for(int c=0;c<2;++c)for(int i=0;i<blockFrames;++i)
            {
                require(std::isfinite(output[c][i]),"Wet EQ/duck produced non-finite host audio");
                if(dryOnly)require(output[c][i]==input[c][i],"Wet EQ or sidechain duck changed the dry audio path");
                audio[c].push_back(output[c][i]);
            }
        }
        return audio;
    };
    const auto energy=[](const Audio& audio,int channel,int start,int end)
    {
        double result=0;for(int i=start;i<end;++i)result+=static_cast<double>(audio[channel][i])*audio[channel][i];return result;
    };
    const auto neutral=render(0,20,20000,0,false,false,false,0.14,0.09);
    for(int n=rate/2;n<rate;++n)
        require(std::abs(neutral[0][n]-0.14f)<1e-5f&&std::abs(neutral[1][n]-0.09f)<1e-5f,
                "Default wet EQ endpoints did not preserve a constant stereo source");
    const auto bass=render(120,20,20000,0,false,false,false,0.14,0.09);
    const auto highPassed=render(120,2000,20000,0,false,false,false,0.14,0.09);
    const auto treble=render(6000,20,20000,0,false,false,false,0.14,0.09);
    const auto lowPassed=render(6000,20,500,0,false,false,false,0.14,0.09);
    for(int c=0;c<2;++c)
    {
        const auto bassEnergy=energy(bass,c,rate,rate*2),trebleEnergy=energy(treble,c,rate,rate*2);
        require(bassEnergy>0.001&&trebleEnergy>0.001,"Wet EQ test did not exercise audible granular output");
        require(energy(highPassed,c,rate,rate*2)<bassEnergy*0.15,"Host low-cut parameter did not remove wet bass");
        require(energy(lowPassed,c,rate,rate*2)<trebleEnergy*0.15,"Host high-cut parameter did not remove wet treble");
    }
    for(int n=rate;n<rate*2;++n)
        require(std::abs(lowPassed[1][n]-lowPassed[0][n]*(0.09f/0.14f))<1e-5f,
                "Wet EQ mixed the stereo channels instead of filtering them independently");
    const auto automated=render(6000,20,20000,0,false,false,true,0.14,0.09);
    for(int n=0;n<128*blockFrames;++n)
        require(automated[0][n]==treble[0][n]&&automated[1][n]==treble[1][n],
                "EQ cutoff automation affected an earlier processing block");
    require(energy(automated,0,rate,rate*2)<energy(treble,0,rate,rate*2)*0.15,
            "In-block EQ cutoff automation did not reach the wet audio processor");

    // The quiet left source cannot trigger strong ducking alone: the louder
    // right input must drive the same gain on both wet channels while DRY is muted.
    const auto noDuck=render(220,20,20000,0,true,false,false,0.01,0.5);
    const auto ducked=render(220,20,20000,100,true,false,false,0.01,0.5);
    for(int c=0;c<2;++c)
    {
        const auto early=energy(noDuck,c,40800,47500),late=energy(noDuck,c,67200,86400);
        require(early>1e-5&&late>1e-5,"Frozen-tail duck test did not contain audible captured audio");
        const auto earlyRatio=energy(ducked,c,40800,47500)/early;
        const auto lateRatio=energy(ducked,c,67200,86400)/late;
        require(earlyRatio<0.15,"Raw stereo input did not sidechain-duck the wet output with DRY muted");
        require(lateRatio>0.7&&lateRatio>earlyRatio+0.5,"Wet duck did not recover after the dry input stopped");
    }
    render(220,1500,2500,100,true,true,false,0.24,0.18);
    std::cout<<"Neutral wet EQ, stereo cuts, in-block cutoff automation, linked wet duck/recovery and exact dry preservation passed\n";
}

void outputLimiterHarmonicsHost(Module& module)
{
    Instance instance(module);MemoryStream configured;
    require(instance.component->getState(&configured)==kResultOk,"Cannot configure limiter harmonic render");
    const auto set=[&](ParamID id,double plain){replaceStateValue(configured,id,instance.normalized(id,plain));};
    set(0,0);set(4,200);set(5,6000);set(6,6000);set(7,50);set(8,50);
    set(13,40);set(14,40);set(17,0);set(18,0);set(19,0);set(20,0);
    set(24,1);set(25,0);set(26,0);set(29,0);set(30,8);set(31,0);set(32,0);
    set(35,20);set(36,20000);configured.position=0;
    require(instance.component->setState(&configured)==kResultOk,"Cannot load limiter harmonic settings");
    instance.setup(2,kSample32);
    require(instance.audio->getLatencySamples()==0,"Output peak protection introduced additional latency");
    constexpr int rate=48000,blockFrames=256,blocks=600;
    constexpr double frequency=437.0,pi=3.14159265358979323846;
    std::array<std::array<float,blockFrames>,2> input{},output{};
    float* ins[2]{input[0].data(),input[1].data()};float* outs[2]{output[0].data(),output[1].data()};
    AudioBusBuffers in{},out{};in.numChannels=out.numChannels=2;in.channelBuffers32=ins;out.channelBuffers32=outs;
    ProcessContext context{};context.sampleRate=rate;context.state=ProcessContext::kPlaying;
    ProcessData data{};data.processMode=kRealtime;data.symbolicSampleSize=kSample32;data.numSamples=blockFrames;
    data.numInputs=data.numOutputs=1;data.inputs=&in;data.outputs=&out;data.processContext=&context;
    std::vector<float> left;left.reserve(blockFrames*blocks);
    float peak=0,maximumStereoError=0;
    for(int block=0;block<blocks;++block)
    {
        for(int i=0;i<blockFrames;++i)
        {
            input[0][i]=static_cast<float>(0.4*std::sin(2*pi*frequency*(block*blockFrames+i)/rate));
            input[1][i]=-0.5f*input[0][i];
        }
        context.projectTimeSamples=block*blockFrames;
        require(instance.audio->process(data)==kResultOk,"Limiter harmonic callback failed");
        for(int i=0;i<blockFrames;++i)
        {
            require(std::isfinite(output[0][i])&&std::isfinite(output[1][i]),"Peak protection produced non-finite audio");
            peak=std::max({peak,std::abs(output[0][i]),std::abs(output[1][i])});
            maximumStereoError=std::max(maximumStereoError,std::abs(output[1][i]+0.5f*output[0][i]));
            left.push_back(output[0][i]);
        }
    }
    // With live BUFFER fully limiting every grain, the readheads coincide.
    // Their sum remains a single sine; extra steady harmonics reveal output
    // waveshaping rather than expected changes in granular timing or source.
    const auto magnitude=[&](int harmonic)
    {
        double real=0,imaginary=0;
        constexpr int start=rate*3/2,count=rate;
        for(int i=0;i<count;++i)
        {
            const auto phase=2*pi*frequency*harmonic*i/rate;
            real+=left[start+i]*std::cos(phase);imaginary-=left[start+i]*std::sin(phase);
        }
        return 2.0*std::sqrt(real*real+imaginary*imaginary)/count;
    };
    const auto fundamental=magnitude(1);
    double harmonicPower=0;for(int harmonic=2;harmonic<=9;++harmonic){const auto value=magnitude(harmonic);harmonicPower+=value*value;}
    const auto thd=std::sqrt(harmonicPower)/std::max(1e-12,fundamental);
    require(peak<=0.98001f,"Linked output protection exceeded its peak ceiling");
    require(fundamental>0.2,"Limiter harmonic regression rendered silence or excessive attenuation");
    require(maximumStereoError<2e-6f,"Peak protection independently reshaped the stereo channels");
    require(thd<0.02,"Overloaded additive MASS still generated new saw-like steady harmonics");
    std::cout<<"Linked output limiter ceiling/stereo/latency and steady overload harmonic regression passed (THD "<<thd<<")\n";
}

void fixtureOffsets(Module& module) {
    const auto silent=renderFixture(module,-1),note=renderFixture(module,128),repeated=renderFixture(module,128);
    for(int i=0;i<128;++i)require(std::abs(note[i]-silent[i])<1e-7f,"MIDI note affected audio before its sample offset");
    double difference=0;for(std::size_t i=128;i<note.size();++i)difference+=std::abs(note[i]-silent[i]);
    require(difference>0.01,"MIDI input did not trigger actual granular audio");
    require(note==repeated,"Same host MIDI/timeline did not repeat precisely");
    const auto automated=renderFixture(module,128,192);
    // The wet automation ramp starts at block 32; earlier blocks must be bit-identical.
    for(int i=0;i<8192;++i)require(automated[i]==note[i],"Automation leaked into an earlier processing block");
    double later=0;for(std::size_t i=8448;i<note.size();++i)later+=std::abs(automated[i]-note[i]);
    require(later>0.001,"Wet automation did not reach the DSP");
    const auto bypassed=renderFixture(module,128,-1,192);
    for(int i=0;i<8384;++i)require(bypassed[i]==note[i],"Discrete bypass automation ignored its in-block sample offset");
    double bypassDifference=0;for(std::size_t i=8600;i<note.size();++i)bypassDifference+=std::abs(bypassed[i]-note[i]);
    require(bypassDifference>0.001,"In-block bypass automation did not reach the DSP");
    const auto sway=renderFixture(module,128,-1,-1,1);
    const auto swayRepeated=renderFixture(module,128,-1,-1,1);
    require(sway==swayRepeated,"SWAY host render did not repeat deterministically");
    double swayDifference=0;for(std::size_t i=512;i<note.size();++i)swayDifference+=std::abs(sway[i]-note[i]);
    require(swayDifference>0.001,"SWAY mode did not affect granular audio through host parameters");
    const auto motionAutomated=renderFixture(module,128,-1,-1,0,192);
    for(int i=0;i<8384;++i)require(motionAutomated[i]==note[i],"Motion mode automation applied before its sample offset");
    double motionDifference=0;for(std::size_t i=8600;i<note.size();++i)motionDifference+=std::abs(motionAutomated[i]-note[i]);
    require(motionDifference>0.001,"Automated motion mode did not change an active event");
    std::cout<<"Fixture MIDI offsets, seeded repeats, and host automation passed\n";
}
}

int wmain(int argc,wchar_t** argv) {
    try{
        require(argc==3,"Usage: GrateModuleSmoke <production-vst3-binary> <fixture-binary>");
        {Module production(argv[1]);formatSmoke<float>(production,1,kSample32);formatSmoke<float>(production,2,kSample32);formatSmoke<double>(production,1,kSample64);formatSmoke<double>(production,2,kSample64);}
        {Module fixture(argv[2]);stateCompatibility(fixture);fixtureOffsets(fixture);transportRestartRepeatability(fixture);syncAndPanHost(fixture);tripletChoicesHost(fixture);eqFrequencyChoicesHost(fixture);wetEqAndDuckingHost(fixture);outputLimiterHarmonicsHost(fixture);}
        std::cout<<"Grate Windows module smoke passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
