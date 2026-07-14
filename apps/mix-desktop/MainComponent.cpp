#include "mix-desktop/MainComponent.h"

#include <algorithm>
#include <nlohmann/json.hpp>

namespace mastering::desktop {
namespace {

bool isAudioFile(const juce::File& file)
{
    return file.hasFileExtension("wav;aif;aiff");
}

juce::Array<juce::File> toAudioFiles(const juce::StringArray& paths)
{
    juce::Array<juce::File> files;
    for (const auto& path : paths) {
        juce::File file(path);
        if (isAudioFile(file))
            files.add(file);
    }
    return files;
}

} // namespace

MainComponent::MainComponent()
{
    createProject();
    addAndMakeVisible(webView_);
    webView_.setCommandHandler([this](const juce::var& command) {
        handleCommand(command);
    });

    juce::Component::SafePointer<MainComponent> safeThis(this);
    bridge_.setAnalysisHandler([safeThis](const juce::var& report) {
        juce::MessageManager::callAsync([safeThis, report] {
            if (safeThis != nullptr)
                safeThis->handleBridgeAnalysis(report);
        });
    });
    bridge_.start();
    setAudioChannels(0, 2);
    startTimerHz(15);
}

MainComponent::~MainComponent()
{
    stopTimer();
    bridge_.stop();
    shutdownAudio();
}

void MainComponent::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    engine_.prepareToPlay(samplesPerBlockExpected, sampleRate);
}

void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    engine_.getNextAudioBlock(bufferToFill);
}

void MainComponent::releaseResources()
{
    engine_.releaseResources();
}

void MainComponent::resized()
{
    webView_.setBounds(getLocalBounds());
}

bool MainComponent::isInterestedInFileDrag(const juce::StringArray& files)
{
    return std::ranges::any_of(files, [](const auto& path) {
        return isAudioFile(juce::File(path));
    });
}

void MainComponent::filesDropped(const juce::StringArray& files, int, int)
{
    importStems(toAudioFiles(files));
}

void MainComponent::timerCallback()
{
    pushState();
}

void MainComponent::handleCommand(const juce::var& command)
{
    const auto* object = command.getDynamicObject();
    if (object == nullptr)
        return;

    const auto type = object->getProperty("type").toString();
    if (type == "ui-ready") {
        pushState();
    } else if (type == "create-project") {
        createProject();
    } else if (type == "open-project") {
        chooseProjectToOpen();
    } else if (type == "save-project") {
        saveProject(projectFile_ == juce::File {});
    } else if (type == "import-stems") {
        chooseStems();
    } else if (type == "import-reference") {
        chooseReference();
    } else if (type == "toggle-playback") {
        engine_.togglePlayback();
    } else if (type == "generate-mix-plan") {
        generateMixPlan();
    } else if (type == "apply-mix-plan") {
        applyMixPlan();
    } else if (type == "export-master") {
        chooseMasterDestination();
    } else if (type == "set-role") {
        if (auto* track = findTrack(object->getProperty("trackId").toString())) {
            if (const auto role = project::roleFromString(
                object->getProperty("role").toString().toStdString())) {
                track->role = *role;
                engine_.updateTrack(*track);
            }
        }
    } else if (type == "set-track-gain") {
        if (auto* track = findTrack(object->getProperty("trackId").toString())) {
            track->gainDb = juce::jlimit(
                -18.0,
                12.0,
                static_cast<double>(object->getProperty("value")));
            engine_.updateTrack(*track);
        }
    } else if (type == "toggle-track") {
        if (auto* track = findTrack(object->getProperty("trackId").toString())) {
            const auto field = object->getProperty("field").toString();
            if (field == "muted")
                track->muted = !track->muted;
            if (field == "soloed")
                track->soloed = !track->soloed;
            engine_.updateTrack(*track);
        }
    }
    pushState();
}

void MainComponent::handleBridgeAnalysis(const juce::var& report)
{
    const auto* object = report.getDynamicObject();
    if (object == nullptr || object->getProperty("type").toString() != "track-analysis")
        return;

    const auto roleName = object->getProperty("role").toString().toStdString();
    const auto role = project::roleFromString(roleName);
    const auto metricsValue = object->getProperty("metrics");
    if (!role || metricsValue.isVoid())
        return;

    const auto metricsJson = juce::JSON::toString(metricsValue, false).toStdString();
    project::ProjectDocument temporary;
    temporary.id = "bridge";
    project::TrackRecord reportTrack;
    reportTrack.id = "report";
    reportTrack.role = *role;
    temporary.tracks.push_back(reportTrack);
    auto serialized = nlohmann::json::parse(project::serialize(temporary));
    serialized["tracks"][0]["metrics"] = nlohmann::json::parse(metricsJson);
    const auto restored = project::deserialize(serialized.dump());
    if (!restored || restored->tracks.empty())
        return;

    const auto match = std::ranges::find_if(project_.tracks, [role](const auto& track) {
        return track.role == *role;
    });
    if (match != project_.tracks.end()) {
        match->metrics = restored->tracks.front().metrics;
        engine_.updateTrack(*match);
    }
    pushState();
}

void MainComponent::createProject()
{
    engine_.stop();
    project_ = {};
    project_.id = project::makeProjectId();
    project_.name = "Untitled Mix";
    projectFile_ = {};
    currentPlan_ = {};
    referenceMetrics_.reset();
    engine_.loadProject(project_);
    pushState();
}

void MainComponent::chooseProjectToOpen()
{
    fileChooser_ = std::make_unique<juce::FileChooser>(
        "Open Mastering Audio project",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*.masuite");
    juce::Component::SafePointer<MainComponent> safeThis(this);
    fileChooser_->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safeThis](const juce::FileChooser& chooser) {
            if (safeThis != nullptr && chooser.getResult().existsAsFile())
                safeThis->openProject(chooser.getResult());
        });
}

void MainComponent::chooseStems()
{
    fileChooser_ = std::make_unique<juce::FileChooser>(
        "Import FL Studio split mixer tracks",
        juce::File::getSpecialLocation(juce::File::userMusicDirectory),
        "*.wav;*.aif;*.aiff");
    juce::Component::SafePointer<MainComponent> safeThis(this);
    fileChooser_->launchAsync(
        juce::FileBrowserComponent::openMode
            | juce::FileBrowserComponent::canSelectFiles
            | juce::FileBrowserComponent::canSelectMultipleItems,
        [safeThis](const juce::FileChooser& chooser) {
            if (safeThis != nullptr)
                safeThis->importStems(chooser.getResults());
        });
}

void MainComponent::chooseReference()
{
    fileChooser_ = std::make_unique<juce::FileChooser>(
        "Choose a mastered reference",
        juce::File::getSpecialLocation(juce::File::userMusicDirectory),
        "*.wav;*.aif;*.aiff");
    juce::Component::SafePointer<MainComponent> safeThis(this);
    fileChooser_->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safeThis](const juce::FileChooser& chooser) {
            if (safeThis == nullptr || !chooser.getResult().existsAsFile())
                return;
            safeThis->project_.referencePath =
                chooser.getResult().getFullPathName().toStdString();
            safeThis->referenceMetrics_ = safeThis->analyzeReference(chooser.getResult());
            safeThis->pushState();
        });
}

void MainComponent::chooseMasterDestination()
{
    fileChooser_ = std::make_unique<juce::FileChooser>(
        "Export mastered WAV",
        juce::File::getSpecialLocation(juce::File::userMusicDirectory)
            .getChildFile(juce::String(project_.name) + " - MASTER.wav"),
        "*.wav");
    juce::Component::SafePointer<MainComponent> safeThis(this);
    fileChooser_->launchAsync(
        juce::FileBrowserComponent::saveMode
            | juce::FileBrowserComponent::canSelectFiles
            | juce::FileBrowserComponent::warnAboutOverwriting,
        [safeThis](const juce::FileChooser& chooser) {
            if (safeThis == nullptr || chooser.getResult() == juce::File {})
                return;
            juce::String error;
            if (!safeThis->engine_.renderMaster(
                    safeThis->project_,
                    chooser.getResult().withFileExtension("wav"),
                    24,
                    error)) {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    "Export failed",
                    error);
            }
        });
}

void MainComponent::importStems(const juce::Array<juce::File>& files)
{
    if (files.isEmpty())
        return;
    project_.tracks = engine_.importFiles(files);
    if (!project_.tracks.empty()) {
        project_.sampleRate = project_.tracks.front().metrics.sampleRate;
        if (project_.name == "Untitled Mix")
            project_.name = files.getFirst().getParentDirectory().getFileName().toStdString();
    }
    currentPlan_ = {};
    pushState();
}

void MainComponent::saveProject(bool chooseDestination)
{
    if (chooseDestination) {
        fileChooser_ = std::make_unique<juce::FileChooser>(
            "Save Mastering Audio project",
            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                .getChildFile(juce::String(project_.name) + ".masuite"),
            "*.masuite");
        juce::Component::SafePointer<MainComponent> safeThis(this);
        fileChooser_->launchAsync(
            juce::FileBrowserComponent::saveMode
                | juce::FileBrowserComponent::canSelectFiles
                | juce::FileBrowserComponent::warnAboutOverwriting,
            [safeThis](const juce::FileChooser& chooser) {
                if (safeThis == nullptr || chooser.getResult() == juce::File {})
                    return;
                safeThis->projectFile_ = chooser.getResult().withFileExtension("masuite");
                safeThis->saveProject(false);
            });
        return;
    }
    projectFile_.replaceWithText(project::serialize(project_));
}

void MainComponent::openProject(const juce::File& file)
{
    const auto restored = project::deserialize(file.loadFileAsString().toStdString());
    if (!restored) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "Project could not be opened",
            "The file is invalid or uses an unsupported schema version.");
        return;
    }
    project_ = *restored;
    projectFile_ = file;
    referenceMetrics_ = project_.referencePath.empty()
        ? std::nullopt
        : analyzeReference(juce::File(project_.referencePath));
    currentPlan_ = {};
    engine_.loadProject(project_);
    pushState();
}

void MainComponent::generateMixPlan()
{
    currentPlan_ = advisor_.createPlan(project_, referenceMetrics_);
    pushState();
}

void MainComponent::applyMixPlan()
{
    engine_.applyPlan(currentPlan_);
    for (const auto& adjustment : currentPlan_.trackAdjustments) {
        const auto match = std::ranges::find_if(project_.tracks, [&adjustment](const auto& track) {
            return track.id == adjustment.trackId;
        });
        if (match != project_.tracks.end()) {
            match->gainDb += adjustment.gainDeltaDb;
            match->processing = adjustment.processing;
        }
    }
    project_.masterProcessing = currentPlan_.masterProcessing;
    pushState();
}

void MainComponent::pushState()
{
    auto state = juce::JSON::parse(juce::String(project::serialize(project_)));
    auto* object = state.getDynamicObject();
    if (object == nullptr)
        return;
    object->setProperty("product", "desktop");
    object->setProperty("connected", true);
    object->setProperty("projectId", project_.id);
    object->setProperty("projectName", project_.name);
    object->setProperty("playing", engine_.isPlaying());
    object->setProperty("positionSeconds", engine_.positionSeconds());
    object->setProperty("durationSeconds", engine_.durationSeconds());
    const auto planState = juce::JSON::parse(juce::String(assistant::toJson(currentPlan_)));
    if (const auto* planObject = planState.getDynamicObject())
        object->setProperty("suggestions", planObject->getProperty("suggestions"));
    webView_.pushState(state);
}

std::optional<analysis::AudioMetrics> MainComponent::analyzeReference(
    const juce::File& file) const
{
    juce::AudioFormatManager manager;
    manager.registerBasicFormats();
    auto reader = std::unique_ptr<juce::AudioFormatReader>(manager.createReaderFor(file));
    if (reader == nullptr)
        return std::nullopt;
    const auto frameCount = static_cast<int>(
        std::min<double>(reader->lengthInSamples, reader->sampleRate * 15.0 * 60.0));
    const auto channels = juce::jlimit(1, 2, static_cast<int>(reader->numChannels));
    juce::AudioBuffer<float> buffer(channels, frameCount);
    reader->read(&buffer, 0, frameCount, 0, true, channels > 1);
    std::vector<std::vector<float>> samples(
        static_cast<std::size_t>(channels),
        std::vector<float>(static_cast<std::size_t>(frameCount)));
    for (int channel = 0; channel < channels; ++channel)
        std::copy_n(buffer.getReadPointer(channel), frameCount, samples[channel].begin());
    return analysis::AudioAnalyzer {}.analyze(samples, reader->sampleRate);
}

project::TrackRecord* MainComponent::findTrack(const juce::String& id)
{
    const auto identifier = id.toStdString();
    const auto iterator = std::ranges::find_if(project_.tracks, [&identifier](const auto& track) {
        return track.id == identifier;
    });
    return iterator != project_.tracks.end() ? &*iterator : nullptr;
}

} // namespace mastering::desktop
