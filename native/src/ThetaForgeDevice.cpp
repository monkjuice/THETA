#include "ThetaForgeDevice.h"
namespace theta {
namespace { constexpr const char* ids[] {"oscAPosition","oscBPosition","oscBLevel","oscBTune","subLevel","noiseLevel","unison","detune","cutoff","resonance","attack","decay","sustain","release"}; constexpr const char* names[] {"A Position","B Position","B Level","B Tune","Sub","Noise","Unison","Detune","Cutoff","Resonance","Attack","Decay","Sustain","Release"}; }
ThetaForgeDevice::ThetaForgeDevice(te::PluginCreationInfo info) : Plugin(info)
{
    auto* undo = getUndoManager(); float defaults[] { .55f,.18f,.25f,7,.12f,0,2,.18f,7800,.12f,.01f,.24f,.75f,.35f };
    juce::CachedValue<float>* values[] {&oscAPosition,&oscBPosition,&oscBLevel,&oscBTune,&subLevel,&noiseLevel,&unison,&detune,&cutoff,&resonance,&attack,&decay,&sustain,&release};
    for (int i=0;i<14;++i) values[i]->referTo(state, ids[i], undo, defaults[i]);
    for (int i=0;i<14;++i) { const auto range = i==3 ? juce::NormalisableRange<float>{-24,24,1} : i==6 ? juce::NormalisableRange<float>{1,8,1} : i==8 ? juce::NormalisableRange<float>{30,18000,0,.25f} : (i>=10 && i!=12 ? juce::NormalisableRange<float>{.001f,8,0,.35f} : juce::NormalisableRange<float>{0,1}); parameters[i]=addParam(ids[i],names[i],range); parameters[i]->attachToCurrentValue(*values[i]); }
}
ThetaForgeDevice::~ThetaForgeDevice() { notifyListenersOfDeletion(); for (auto* p:getAutomatableParameters()) p->detachFromCurrentValue(); }
void ThetaForgeDevice::initialise(const te::PluginInitialisationInfo& info) { core.initialise(info.sampleRate); }
void ThetaForgeDevice::reset() { core.reset(); }
forge::Patch ThetaForgeDevice::patch() { return {oscAPosition,oscBPosition,oscBLevel,oscBTune,subLevel,noiseLevel,unison,detune,cutoff,resonance,attack,decay,sustain,release}; }
void ThetaForgeDevice::applyToBuffer(const te::PluginRenderContext& context)
{ if (!context.destBuffer) return; SCOPED_REALTIME_CHECK auto& b=*context.destBuffer; if (context.bufferForMidiMessages) for (const auto& event:*context.bufferForMidiMessages) { if(event.isNoteOn()) core.noteOn(event.getNoteNumber(),event.getFloatVelocity()); else if(event.isNoteOff()) core.noteOff(event.getNoteNumber()); else if(event.isAllNotesOff()) core.allNotesOff(); } const auto p=patch(); for(int i=context.bufferStartSample;i<context.bufferStartSample+context.bufferNumSamples;++i){float l,r;core.renderSample(p,l,r); if(b.getNumChannels()) b.setSample(0,i,b.getSample(0,i)+l); if(b.getNumChannels()>1)b.setSample(1,i,b.getSample(1,i)+r);} }
void ThetaForgeDevice::restorePluginStateFromValueTree(const juce::ValueTree& source) { te::copyPropertiesToCachedValues(source,oscAPosition,oscBPosition,oscBLevel,oscBTune,subLevel,noiseLevel,unison,detune,cutoff,resonance,attack,decay,sustain,release); for(auto* p:getAutomatableParameters())p->updateFromAttachedValue(); }
}
