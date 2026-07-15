#include "analyzer-plugin/PluginProcessor.h"
#include "analyzer-plugin/PluginEditor.h"
#include "mastering/analysis/PassThroughPolicy.h"
#include "mastering/ipc/BridgeProtocol.h"

#include <array>
#include <cmath>
#include <ranges>

namespace mastering::plugin {
namespace {

const std::array roleValues {
    project::TrackRole::custom,
    project::TrackRole::drums,
    project::TrackRole::kick,
    project::TrackRole::snare,
    project::TrackRole::toms,
    project::TrackRole::cymbals,
    project::TrackRole::bass,
    project::TrackRole::rhythmGuitar,
    project::TrackRole::leadGuitar,
    project::TrackRole::cleanVocal,
    project::TrackRole::screamVocal,
    project::TrackRole::backingVocal,
    project::TrackRole::synth,
    project::TrackRole::orchestra,
    project::TrackRole::effects
};

juce::StringArray roleNames()
{
    juce::StringArray names;
    for (const auto role : roleValues)
        names.add(project::roleToString(role));
    return names;
}

} // namespace

AnalyzerProcessor::AnalyzerProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state_(*this, nullptr, "AnalyzerState", createParameters())
{
    state_.state.setProperty(
        "instanceId",
        juce::String(project::makeProjectId()),
        nullptr);
    state_.state.setProperty("projectId", "", nullptr);
    bridge_.start();
}

AnalyzerProcessor::~AnalyzerProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout AnalyzerProcessor::createParameters()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID {"role", 1},
        "Signal role",
        roleNames(),
        0));
    return layout;
}

void AnalyzerProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    meter_.prepare(sampleRate, samplesPerBlock);
    floatMeterScratch_.setSize(2, std::max(1, samplesPerBlock), false, false, true);
}

void AnalyzerProcessor::releaseResources()
{
}

bool AnalyzerProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto input = layouts.getMainInputChannelSet();
    return (input == juce::AudioChannelSet::mono() || input == juce::AudioChannelSet::stereo())
        && layouts.getMainOutputChannelSet() == input;
}

void AnalyzerProcessor::processBlock(
    juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    // Finite audio is bit-transparent; only NaN/Inf are zeroed (see PassThroughPolicy).
    analysis::applyAnalyzerSanitizeOnly(
        buffer.getArrayOfWritePointers(),
        buffer.getNumChannels(),
        buffer.getNumSamples());
    meter_.process(
        buffer.getArrayOfReadPointers(),
        buffer.getNumChannels(),
        buffer.getNumSamples());
}

void AnalyzerProcessor::processBlock(
    juce::AudioBuffer<double>& buffer,
    juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const auto channels = buffer.getNumChannels();
    const auto samples = buffer.getNumSamples();
    analysis::applyAnalyzerSanitizeOnly(
        buffer.getArrayOfWritePointers(),
        channels,
        samples);
    // Never allocate on the audio thread. If scratch is too small, keep full audio
    // pass-through (already sanitized in-place) and drop analysis for this callback.
    if (samples > floatMeterScratch_.getNumSamples()
        || channels > floatMeterScratch_.getNumChannels()) {
        meter_.noteDroppedAnalysisFrames(static_cast<std::uint64_t>(
            std::max(1, samples) * std::max(1, channels)));
        return;
    }
    const auto usableChannels = channels;
    const auto usableSamples = samples;
    for (int channel = 0; channel < usableChannels; ++channel) {
        const auto* source = buffer.getReadPointer(channel);
        auto* destination = floatMeterScratch_.getWritePointer(channel);
        for (int sample = 0; sample < usableSamples; ++sample)
            destination[sample] = static_cast<float>(source[sample]);
    }
    meter_.process(
        floatMeterScratch_.getArrayOfReadPointers(),
        usableChannels,
        usableSamples);
}

juce::AudioProcessorEditor* AnalyzerProcessor::createEditor()
{
    return new AnalyzerEditor(*this);
}

bool AnalyzerProcessor::hasEditor() const { return true; }
const juce::String AnalyzerProcessor::getName() const { return JucePlugin_Name; }
double AnalyzerProcessor::getTailLengthSeconds() const { return 0.0; }
bool AnalyzerProcessor::acceptsMidi() const { return false; }
bool AnalyzerProcessor::producesMidi() const { return false; }
bool AnalyzerProcessor::isMidiEffect() const { return false; }
int AnalyzerProcessor::getNumPrograms() { return 1; }
int AnalyzerProcessor::getCurrentProgram() { return 0; }
void AnalyzerProcessor::setCurrentProgram(int) {}
const juce::String AnalyzerProcessor::getProgramName(int) { return {}; }
void AnalyzerProcessor::changeProgramName(int, const juce::String&) {}

void AnalyzerProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    if (const auto xml = state_.copyState().createXml())
        copyXmlToBinary(*xml, destination);
}

void AnalyzerProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (const auto xml = getXmlFromBinary(data, sizeInBytes)) {
        const auto restored = juce::ValueTree::fromXml(*xml);
        if (restored.isValid())
            state_.replaceState(restored);
    }
}

analysis::AudioMetrics AnalyzerProcessor::metrics() const noexcept
{
    return meter_.snapshot();
}

project::TrackRole AnalyzerProcessor::role() const noexcept
{
    const auto* parameter = state_.getRawParameterValue("role");
    const auto index = parameter != nullptr
        ? juce::jlimit(0, static_cast<int>(roleValues.size() - 1), static_cast<int>(parameter->load()))
        : 0;
    return roleValues[static_cast<std::size_t>(index)];
}

void AnalyzerProcessor::setRole(project::TrackRole role)
{
    const auto iterator = std::ranges::find(roleValues, role);
    if (iterator == roleValues.end())
        return;
    const auto index = static_cast<int>(std::distance(roleValues.begin(), iterator));
    if (auto* parameter = state_.getParameter("role")) {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(
            parameter->convertTo0to1(static_cast<float>(index)));
        parameter->endChangeGesture();
    }
}

bool AnalyzerProcessor::bridgeConnected() const noexcept
{
    return bridge_.connected();
}

juce::String AnalyzerProcessor::projectId() const
{
    return state_.state.getProperty("projectId").toString();
}

void AnalyzerProcessor::publishAnalysis(bool writeSidecarWhenOffline)
{
    auto report = juce::DynamicObject::Ptr(new juce::DynamicObject());
    report->setProperty("schemaVersion", mastering::ipc::kCurrentSchemaVersion);
    report->setProperty("type", "track-analysis");
    report->setProperty("instanceId", state_.state.getProperty("instanceId"));
    report->setProperty("projectId", state_.state.getProperty("projectId"));
    report->setProperty("role", juce::String(project::roleToString(role())));
    report->setProperty(
        "metrics",
        juce::JSON::parse(juce::String(analysis::toJson(metrics()))));
    const auto json = juce::JSON::toString(juce::var(report.get()), false);

    if (bridge_.sendJson(json) || !writeSidecarWhenOffline)
        return;

    const auto reportDirectory = juce::File::getSpecialLocation(
        juce::File::userDocumentsDirectory)
        .getChildFile("Mastering Audio Reports");
    reportDirectory.createDirectory();
    reportDirectory
        .getChildFile(state_.state.getProperty("instanceId").toString() + ".analysis.json")
        .replaceWithText(json);
}

} // namespace mastering::plugin

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new mastering::plugin::AnalyzerProcessor();
}
