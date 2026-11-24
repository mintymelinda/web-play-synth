#include "Dsp/Reverb.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
void require(bool ok, const char* message)
{
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
juce::Reverb::Parameters toJuce(const webplaysynth::dsp::StereoReverb::Parameters& p)
{
    juce::Reverb::Parameters r;
    r.roomSize=p.roomSize; r.damping=p.damping; r.wetLevel=p.wetLevel;
    r.dryLevel=p.dryLevel; r.width=p.width; r.freezeMode=p.freezeMode;
    return r;
}
void compare(double rate, bool stereo)
{
    constexpr int total=32771;
    constexpr float tolerance=2.0e-6f;
    webplaysynth::dsp::StereoReverb candidate;
    webplaysynth::dsp::StereoReverb::Parameters p;
    p.roomSize=.55f; p.damping=.4f; p.wetLevel=.72f; p.dryLevel=.14f; p.width=.83f;
    candidate.prepare(rate,p);
    juce::Reverb reference;
    reference.setParameters(toJuce(p)); reference.setSampleRate(rate); reference.reset();

    std::vector<float> left(total),right(total),refL(total),refR(total);
    for(int i=0;i<total;++i) {
        const auto t=static_cast<double>(i)/rate;
        left[static_cast<size_t>(i)]=static_cast<float>(.31*std::sin(2.0*juce::MathConstants<double>::pi*173.0*t)+(i%997==0?.7:0.0));
        right[static_cast<size_t>(i)]=static_cast<float>(.23*std::cos(2.0*juce::MathConstants<double>::pi*811.0*t)+(i%1291==0?-.4:0.0));
    }
    refL=left; refR=right;
    constexpr std::array<int,7> blocks{1,7,64,127,256,511,1024};
    size_t block=0; int offset=0; int nextChange=0;
    constexpr std::array<int,3> changes{4093,12017,23011};
    double sum=0.0,squares=0.0; float maxError=0.0f;
    while(offset<total) {
        if(nextChange<3 && offset>=changes[static_cast<size_t>(nextChange)]) {
            p.roomSize=nextChange==0?.91f:.22f;
            p.damping=nextChange==1?.83f:.12f;
            p.wetLevel=nextChange==2?.35f:.91f;
            p.dryLevel=.5f*(1.0f-p.wetLevel);
            p.width=nextChange==1?0.0f:1.0f;
            candidate.setParameters(p); reference.setParameters(toJuce(p)); ++nextChange;
        }
        const auto n=std::min(blocks[block++%blocks.size()],total-offset);
        if(stereo) {
            candidate.processStereo(left.data()+offset,right.data()+offset,n);
            reference.processStereo(refL.data()+offset,refR.data()+offset,n);
        } else {
            candidate.processMono(left.data()+offset,n);
            reference.processMono(refL.data()+offset,n);
        }
        for(int i=0;i<n;++i) {
            const auto k=static_cast<size_t>(offset+i);
            const auto el=left[k]-refL[k];
            const auto er=stereo?right[k]-refR[k]:0.0f;
            sum+=el+er; squares+=static_cast<double>(el)*el+static_cast<double>(er)*er;
            maxError=std::max(maxError,std::max(std::abs(el),std::abs(er)));
            require(std::isfinite(left[k])&&(!stereo||std::isfinite(right[k])),"reverb output must remain finite");
        }
        offset+=n;
    }
    const auto samples=static_cast<double>(total)*(stereo?2.0:1.0);
    std::cout<<"Reverb parity: rate="<<rate<<", stereo="<<stereo<<", mean="<<sum/samples
             <<", rms="<<std::sqrt(squares/samples)<<", max="<<maxError<<'\n';
    require(maxError<=tolerance,"portable reverb must match JUCE 8.0.6 within 2e-6");
}
void resetClearsTail()
{
    webplaysynth::dsp::StereoReverb candidate;
    webplaysynth::dsp::StereoReverb::Parameters p; p.wetLevel=1.0f; p.dryLevel=0.0f;
    candidate.prepare(48000.0,p);
    std::array<float,12000> l{},r{}; l[0]=r[0]=1.0f;
    candidate.processStereo(l.data(),r.data(),static_cast<int>(l.size()));
    candidate.reset(); l.fill(0.0f); r.fill(0.0f);
    candidate.processStereo(l.data(),r.data(),static_cast<int>(l.size()));
    for(size_t i=0;i<l.size();++i) require(l[i]==0.0f&&r[i]==0.0f,"reset clears stored reverb energy");
}
}
int main()
{
    resetClearsTail();
    for(const auto rate:{44100.0,48000.0,96000.0}) { compare(rate,false); compare(rate,true); }
    std::cout<<"PASS: portable reverb matches JUCE across rates, layouts, automation, and reset\n";
    return 0;
}
