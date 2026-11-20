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
        for(int i=0;i<2;++i){PClassInfo c{};require(factory->getClassInfo(i,&c)==kResultOk,"Class metadata failed");require(std::string(c.name).find("INHA Grain")==0,"Wrong product identity");}
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
        require(component->queryInterface(IAudioProcessor_iid,reinterpret_cast<void**>(&audio))==kResultOk&&audio,"Audio processor interface missing");
        require(component->queryInterface(IEditController_iid,reinterpret_cast<void**>(&controller))==kResultOk&&controller,"Controller interface missing");
        require(controller->getParameterCount()==29,"Parameter inventory changed unexpectedly");
        require(audio->canProcessSampleSize(kSample32)==kResultTrue&&audio->canProcessSampleSize(kSample64)==kResultTrue,"Both sample formats required");
        IProcessContextRequirements* contextRequirements=nullptr;
        require(component->queryInterface(IProcessContextRequirements_iid,reinterpret_cast<void**>(&contextRequirements))==kResultOk&&contextRequirements,"VST3 process context requirements interface missing");
        require((contextRequirements->getProcessContextRequirements()&IProcessContextRequirements::kNeedTransportState)!=0,"Transport state requirement missing");
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
    ParameterInfo motionInfo{};
    require(instance.controller->getParameterInfo(24,motionInfo)==kResultOk&&motionInfo.id==24
            &&motionInfo.stepCount==1&&(motionInfo.flags&ParameterInfo::kIsList)!=0
            &&(motionInfo.flags&ParameterInfo::kCanAutomate)!=0,
            "Motion mode must be an appended, automatable two-item host list");
    String128 motionText{};
    require(instance.controller->getParamStringByValue(24,1.0,motionText)==kResultOk,
            "Could not format SWAY mode for host");
    const std::array<char16,5> expectedSway{{'S','W','A','Y',0}};
    require(std::equal(expectedSway.begin(),expectedSway.end(),motionText),"Host motion list does not label SWAY correctly");
    MemoryStream saved;
    require(instance.component->getState(&saved)==kResultOk,"Could not save initial v2 state");
    require(saved.bytes.size()==8+29*sizeof(double)&&stateWord(saved,0)==0x47524149u&&stateWord(saved,4)==2,
            "Expected v2 state with 29 parameter values");
    const auto setPlain=[&](ParamID id,double plain){replaceStateValue(saved,id,instance.normalized(id,plain));};
    // UND and SWAY controls are independent banks, retained even while inactive.
    setPlain(9,-35);setPlain(10,45);setPlain(11,75);setPlain(12,180);
    setPlain(24,1);setPlain(25,17);setPlain(26,63);setPlain(27,250);setPlain(28,3500);
    saved.position=0;
    require(instance.component->setState(&saved)==kResultOk,"Could not restore SWAY v2 state");
    MemoryStream restored;
    require(instance.component->getState(&restored)==kResultOk&&saved.bytes==restored.bytes,
            "SWAY state round trip lost mode or inactive UND/SWAY values");
    require(instance.controller->getParamNormalized(24)==1.0,"SWAY mode was not restored");
    saved.position=0;
    require(instance.controller->setComponentState(&saved)==kResultOk,"Controller rejected SWAY state");
    for(ParamID id:{9u,10u,11u,12u,24u,25u,26u,27u,28u})
        require(instance.controller->getParamNormalized(id)==stateValue(saved,id),"Controller lost an independent motion parameter");

    MemoryStream legacy;
    legacy.bytes=saved.bytes;
    legacy.bytes.resize(8+24*sizeof(double));
    replaceStateWord(legacy,4,1);
    require(instance.component->setState(&legacy)==kResultOk,"Existing v1 projects are not compatible");
    MemoryStream migrated;
    require(instance.component->getState(&migrated)==kResultOk&&stateWord(migrated,4)==2,
            "Legacy project did not migrate to v2 state");
    for(ParamID id=0;id<24;++id)
        require(stateValue(migrated,id)==stateValue(legacy,id),"Migration changed a legacy parameter value");
    require(stateValue(migrated,24)==0.0,"Legacy project did not default to UNDULATION");
    for(const auto expected:std::array<std::pair<ParamID,double>,4>{{{25,10},{26,30},{27,500},{28,1500}}})
        require(std::abs(stateValue(migrated,expected.first)-instance.normalized(expected.first,expected.second))<1e-12,
                "Legacy project did not initialize a SWAY parameter to its declared default");

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
    invalid.bytes=saved.bytes;invalid.bytes.pop_back();rejected(invalid,"Truncated v2 parameter payload accepted");
    invalid.bytes=legacy.bytes;invalid.bytes.pop_back();rejected(invalid,"Truncated legacy parameter payload accepted");
    invalid.bytes.assign(4,0);rejected(invalid,"Truncated state header accepted");
    require(instance.component->setState(nullptr)!=kResultOk,"Null state stream accepted");
    std::cout<<"v1 compatibility, independent SWAY v2 state, and invalid-state rejection passed\n";
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
    set(0,2);set(1,100);set(4,20);set(5,10);set(6,10);set(7,0);set(8,0);
    set(9,0);set(10,0);set(13,1);set(14,1);set(15,200);set(16,200);
    set(17,0);set(18,0);set(19,0);set(20,0);
    set(24,initialMotionMode);set(25,60);set(26,60);set(27,25);set(28,25);
    std::array<std::array<float,256>,2> input{},output{};
    float* ins[2]{input[0].data(),input[1].data()};float* outs[2]{output[0].data(),output[1].data()};
    AudioBusBuffers in{},out{};in.numChannels=out.numChannels=2;in.channelBuffers32=ins;out.channelBuffers32=outs;
    ProcessContext context{};context.sampleRate=48000;context.state=ProcessContext::kPlaying;
    ProcessData data{};data.processMode=kRealtime;data.symbolicSampleSize=kSample32;data.numSamples=256;data.numInputs=data.numOutputs=1;data.inputs=&in;data.outputs=&out;data.processContext=&context;
    std::vector<float> result;result.reserve(4096);
    for(int block=0;block<16;++block){
        for(int i=0;i<256;++i)input[0][i]=input[1][i]=0.15f*std::sin(static_cast<float>(block*256+i)*0.027f);
        Events events;if(block==0&&noteOffset>=0)events.note(noteOffset);
        Changes automation;if(block==4&&wetAutomationOffset>=0){automation.add(20,0,instance.normalized(20,0));automation.add(20,wetAutomationOffset,instance.normalized(20,-60));}
        if(block==4&&bypassAutomationOffset>=0)automation.add(23,bypassAutomationOffset,1.0);
        if(block==4&&motionAutomationOffset>=0)automation.add(24,motionAutomationOffset,1.0);
        const bool automate=block==4&&(wetAutomationOffset>=0||bypassAutomationOffset>=0||motionAutomationOffset>=0);
        data.inputEvents=&events;data.inputParameterChanges=block==0?&controls:(automate?&automation:nullptr);context.projectTimeSamples=block*256;
        require(instance.audio->process(data)==kResultOk,"Fixture render callback failed");
        for(float value:output[0]){require(std::isfinite(value),"Fixture produced non-finite audio");result.push_back(value);}
    }
    return result;
}

void fixtureOffsets(Module& module) {
    const auto silent=renderFixture(module,-1),note=renderFixture(module,128),repeated=renderFixture(module,128);
    for(int i=0;i<128;++i)require(std::abs(note[i]-silent[i])<1e-7f,"MIDI note affected audio before its sample offset");
    double difference=0;for(std::size_t i=128;i<note.size();++i)difference+=std::abs(note[i]-silent[i]);
    require(difference>0.01,"MIDI input did not trigger actual granular audio");
    require(note==repeated,"Same host MIDI/timeline did not repeat precisely");
    const auto automated=renderFixture(module,128,192);
    // The wet automation ramp starts at block 4; earlier blocks must be bit-identical.
    for(int i=0;i<1024;++i)require(automated[i]==note[i],"Automation leaked into an earlier processing block");
    double later=0;for(std::size_t i=1280;i<note.size();++i)later+=std::abs(automated[i]-note[i]);
    require(later>0.001,"Wet automation did not reach the DSP");
    const auto bypassed=renderFixture(module,128,-1,192);
    for(int i=0;i<1216;++i)require(bypassed[i]==note[i],"Discrete bypass automation ignored its in-block sample offset");
    double bypassDifference=0;for(std::size_t i=1400;i<note.size();++i)bypassDifference+=std::abs(bypassed[i]-note[i]);
    require(bypassDifference>0.001,"In-block bypass automation did not reach the DSP");
    const auto sway=renderFixture(module,128,-1,-1,1);
    const auto swayRepeated=renderFixture(module,128,-1,-1,1);
    require(sway==swayRepeated,"SWAY host render did not repeat deterministically");
    double swayDifference=0;for(std::size_t i=512;i<note.size();++i)swayDifference+=std::abs(sway[i]-note[i]);
    require(swayDifference>0.001,"SWAY mode did not affect granular audio through host parameters");
    const auto motionAutomated=renderFixture(module,128,-1,-1,0,192);
    for(int i=0;i<1216;++i)require(motionAutomated[i]==note[i],"Motion mode automation applied before its sample offset");
    double motionDifference=0;for(std::size_t i=1400;i<note.size();++i)motionDifference+=std::abs(motionAutomated[i]-note[i]);
    require(motionDifference>0.001,"Automated motion mode did not change an active event");
    std::cout<<"Fixture MIDI offsets, seeded repeats, and host automation passed\n";
}
}

int wmain(int argc,wchar_t** argv) {
    try{
        require(argc==3,"Usage: GrainModuleSmoke <production-vst3-binary> <fixture-binary>");
        {Module production(argv[1]);formatSmoke<float>(production,1,kSample32);formatSmoke<float>(production,2,kSample32);formatSmoke<double>(production,1,kSample64);formatSmoke<double>(production,2,kSample64);}
        {Module fixture(argv[2]);stateCompatibility(fixture);fixtureOffsets(fixture);}
        std::cout<<"Grain Windows module smoke passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
