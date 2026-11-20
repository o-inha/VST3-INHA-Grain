#include "AutomationTimeline.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
void require(bool condition,const char* message)
{
    if(!condition)throw std::runtime_error(message);
}
void close(double actual,double expected,const char* message)
{
    if(!std::isfinite(actual)||std::abs(actual-expected)>1e-12)
    {
        std::cerr<<message<<": expected "<<expected<<", got "<<actual<<'\n';
        throw std::runtime_error(message);
    }
}

void interpolationAndBoundaries()
{
    InhaAutomation::Timeline<3,12> timeline;
    timeline.reset({0.1,0.8,0.25},100);
    require(!timeline.hasPoints(0)&&!timeline.hasPoints(3),"Empty/invalid lane reported points");
    close(timeline.valueAt(1,50,false),0.8,"Unchanged lane lost its initial value");
    close(timeline.valueAt(3,50,false),0,"Invalid lane did not return zero");
    require(timeline.addPoint(0,80,0.9)&&timeline.addPoint(0,20,0.5)
            &&timeline.addPoint(0,50,0.7)&&timeline.addPoint(2,50,0.75),"Could not collect points");
    require(timeline.hasPoints(0)&&!timeline.hasPoints(1)&&timeline.hasPoints(2),"Lane point mask is incorrect");
    close(timeline.valueAt(0,10,false),0.3,"Initial-to-first interpolation failed");
    close(timeline.valueAt(0,20,false),0.5,"Exact first offset failed");
    close(timeline.valueAt(0,35,false),0.6,"Middle interpolation failed");
    close(timeline.valueAt(0,65,false),0.8,"Unsorted point interpolation failed");
    close(timeline.valueAt(0,99,false),0.9,"Last value did not hold to block end");
    close(timeline.valueAt(0,10,true),0.1,"Discrete point applied early");
    close(timeline.valueAt(0,35,true),0.5,"Discrete point did not hold");
    close(timeline.valueAt(0,50,true),0.7,"Discrete exact point failed");
    close(timeline.valueAt(1,50,false),0.8,"Cross-lane point contamination");
    close(timeline.valueAt(2,25,false),0.5,"Second lane interpolation failed");
    std::array<int,14> boundaries{};
    const auto count=timeline.boundaries(boundaries);
    require(count==5,"Boundary count did not deduplicate cross-lane offsets");
    require(std::equal(boundaries.begin(),boundaries.begin()+count,
                       std::array<int,5>{0,20,50,80,100}.begin()),"Boundary order is incorrect");

    // Collection after a lookup must invalidate the prepared lane index.
    require(timeline.addPoint(1,60,0.2),"Could not add after first lookup");
    close(timeline.valueAt(1,30,false),0.5,"Prepared search index was not refreshed after collection");
    timeline.reset({0.3,0.4,0.5},100);
    require(!timeline.hasPoints(0),"Reset retained old lane flags");
    close(timeline.valueAt(0,50,false),0.3,"Reset retained preceding block points");
}

void duplicatesAndSanitization()
{
    InhaAutomation::Timeline<2,16> timeline;
    timeline.reset({0.1,0.8},100);
    require(timeline.addPoint(0,20,0.2)&&timeline.addPoint(0,20,0.7)
            &&timeline.addPoint(0,40,0.9),"Could not collect duplicate offsets");
    close(timeline.valueAt(0,20,false),0.7,"Duplicate exact offset did not use last-added value");
    close(timeline.valueAt(0,10,false),0.4,"Ramp used superseded upcoming duplicate value");
    close(timeline.valueAt(0,30,false),0.8,"Ramp used superseded previous duplicate value");
    close(timeline.valueAt(0,20,true),0.7,"Discrete duplicate did not use last-added value");
    require(timeline.addPoint(0,20,0.5),"Could not add duplicate after preparation");
    close(timeline.valueAt(0,20,false),0.5,"Late duplicate insertion did not supersede earlier point");

    timeline.reset({std::numeric_limits<double>::quiet_NaN(),2.0},100);
    close(timeline.valueAt(0,10,false),0,"Nonfinite initial value was not cleaned");
    close(timeline.valueAt(1,10,false),1,"Initial value was not clamped");
    require(timeline.addPoint(0,-5,0.4)&&timeline.addPoint(0,500,0.8)
            &&timeline.addPoint(0,50,std::numeric_limits<double>::infinity())
            &&timeline.addPoint(1,25,-0.5)&&timeline.addPoint(1,75,4.0),"Could not collect sanitized points");
    close(timeline.valueAt(0,-99,false),0.4,"Negative point/query offsets were not clamped");
    close(timeline.valueAt(0,999,false),0.8,"Oversized point/query offsets were not clamped");
    close(timeline.valueAt(0,50,false),0,"Nonfinite point value was not cleaned");
    close(timeline.valueAt(1,25,false),0,"Negative point value was not clamped");
    close(timeline.valueAt(1,75,false),1,"Oversized point value was not clamped");
}

void boundedStorageAndThinning()
{
    using Timeline=InhaAutomation::Timeline<2,4>;
    Timeline timeline;
    timeline.reset({0.0,0.0},20);
    require(!timeline.addPoint(2,0,0.5),"Invalid lane consumed storage");
    for(int i=0;i<4;++i)require(timeline.addPoint(i%2,i*5,0.1*i),"Available capacity was rejected");
    require(!timeline.addPoint(0,19,1.0),"Full fixed storage accepted overflow");
    close(timeline.valueAt(0,20,false),0.2,"Overflow changed retained values");
    std::array<int,2> endpoints{};
    require(timeline.boundaries(endpoints)==2&&endpoints[0]==0&&endpoints[1]==20,
            "Minimum boundary storage did not retain block endpoints");

    require(Timeline::sourcePointIndex(0,85,10000)==0,"Thinning dropped first source point");
    require(Timeline::sourcePointIndex(84,85,10000)==9999,"Thinning dropped last source point");
    std::size_t previous=0;
    for(std::size_t i=1;i<85;++i)
    {
        const auto source=Timeline::sourcePointIndex(i,85,10000);
        require(source>previous&&source<10000,"Thinning is not strictly ordered and in range");
        previous=source;
    }
    require(Timeline::sourcePointIndex(999,85,10000)==9999,"Out-of-range retained index was not clamped");
    require(Timeline::sourcePointIndex(0,0,10000)==0
            &&Timeline::sourcePointIndex(1,1,10000)==0
            &&Timeline::sourcePointIndex(3,85,0)==0
            &&Timeline::sourcePointIndex(3,85,1)==0,"Empty/single-point thinning is unsafe");
    for(std::size_t i=0;i<8;++i)
        require(Timeline::sourcePointIndex(i,85,8)==i,"Thinning expanded a short source lane");

    timeline.reset({0.25,0.5},-100);
    require(timeline.addPoint(0,55,0.75),"Zero-length block could not collect point");
    close(timeline.valueAt(0,10,false),0.75,"Negative block length was not reduced to zero");
    std::array<int,6> zero{};
    require(timeline.boundaries(zero)==1&&zero[0]==0,"Zero-length block has duplicate boundaries");
}

double referenceValue(const std::vector<std::pair<int,double>>& points,double initial,
                      int offset,bool discrete)
{
    int beforeOffset=0;
    double beforeValue=initial;
    for(const auto& point:points)
    {
        if(point.first<=offset){beforeOffset=point.first;beforeValue=point.second;continue;}
        if(discrete)return beforeValue;
        const auto fraction=static_cast<double>(offset-beforeOffset)/(point.first-beforeOffset);
        return beforeValue+(point.second-beforeValue)*fraction;
    }
    return beforeValue;
}

void fullCapacitySearch()
{
    constexpr std::size_t lanes=4,capacity=2048;
    constexpr int blockSize=4096;
    InhaAutomation::Timeline<lanes,capacity> timeline;
    const std::array<double,lanes> initial{0.1,0.3,0.5,0.7};
    timeline.reset(initial,blockSize);
    std::array<std::vector<std::pair<int,double>>,lanes> reference;
    for(std::size_t i=0;i<capacity;++i)
    {
        const auto lane=i%lanes;
        const int offset=blockSize-static_cast<int>(2*i);
        const double value=static_cast<double>((i*37)%1001)/1000.0;
        require(timeline.addPoint(lane,offset,value),"Full-capacity collection failed");
        reference[lane].emplace_back(offset,value);
    }
    require(!timeline.addPoint(0,0,1.0),"Capacity guard failed on dense automation");
    for(auto& lane:reference)std::sort(lane.begin(),lane.end());
    for(std::size_t lane=0;lane<lanes;++lane)
        for(int offset=0;offset<=blockSize;++offset)
            for(bool discrete:{false,true})
                close(timeline.valueAt(lane,offset,discrete),
                      referenceValue(reference[lane],initial[lane],offset,discrete),
                      "Binary search differs from independent linear reference");
    std::array<int,capacity+2> boundaries{};
    const auto count=timeline.boundaries(boundaries);
    require(count==capacity+1&&boundaries[0]==0&&boundaries[count-1]==blockSize,
            "Dense boundary collection lost offsets");
    require(std::is_sorted(boundaries.begin(),boundaries.begin()+count),"Dense boundaries are unsorted");
}
}

int main()
{
    try
    {
        interpolationAndBoundaries();
        duplicatesAndSanitization();
        boundedStorageAndThinning();
        fullCapacitySearch();
        std::cout<<"Automation timeline interpolation, thinning, capacity and binary search passed\n";
        return 0;
    }
    catch(const std::exception& error)
    {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
