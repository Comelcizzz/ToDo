#include "mix-desktop/MainComponent.h"
#include "mastering/research/ResearchExample.h"

#include <algorithm>

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
        saveProject(projectFile_.getFullPathName().isEmpty());
    } else if (type == "import-stems") {
        chooseStems();
    } else if (type == "import-reference") {
        chooseReference();
    } else if (type == "export-research-example") {
        chooseResearchDestination();
    } else if (type == "toggle-playback") {
        engine_.togglePlayback();
    } else if (type == "toggle-ab") {
        engine_.setMonitorSource(
            engine_.monitorSource() == StemEngine::MonitorSource::mix
                ? StemEngine::MonitorSource::reference
                : StemEngine::MonitorSource::mix);
    } else if (type == "set-monitor") {
        const auto source = object->getProperty("source").toString();
        engine_.setMonitorSource(
            source == "reference"
                ? StemEngine::MonitorSource::reference
                : StemEngine::MonitorSource::mix);
    } else if (type == "generate-mix-plan") {
        generateMixPlan();
    } else if (type == "select-variant") {
        selectVariant(object->getProperty("variant").toString());
    } else if (type == "apply-mix-plan") {
        applyMixPlan();
    } else if (type == "export-master") {
        exportBitDepth_ = juce::jlimit(
            24,
            32,
            static_cast<int>(object->getProperty("bitsPerSample")));
        if (exportBitDepth_ != 24 && exportBitDepth_ != 32)
            exportBitDepth_ = 24;
        chooseMasterDestination(exportBitDepth_);
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
    } else if (type == "set-track-pan") {
        if (auto* track = findTrack(object->getProperty("trackId").toString())) {
            track->pan = juce::jlimit(
                -1.0,
                1.0,
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
            if (field == "polarityInverted")
                track->polarityInverted = !track->polarityInverted;
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
    if (!role || !metricsValue.isObject())
        return;

    analysis::AudioMetrics metrics;
    const auto* metricsObject = metricsValue.getDynamicObject();
    metrics.samplePeakDbfs = static_cast<double>(metricsObject->getProperty("samplePeakDbfs"));
    metrics.estimatedTruePeakDbtp =
        static_cast<double>(metricsObject->getProperty("estimatedTruePeakDbtp"));
    metrics.rmsDbfs = static_cast<double>(metricsObject->getProperty("rmsDbfs"));
    metrics.integratedLufs = static_cast<double>(metricsObject->getProperty("integratedLufs"));
    metrics.crestFactorDb = static_cast<double>(metricsObject->getProperty("crestFactorDb"));
    metrics.stereoCorrelation =
        static_cast<double>(metricsObject->getProperty("stereoCorrelation"));
    metrics.transientDensityHz =
        static_cast<double>(metricsObject->getProperty("transientDensityHz"));
    metrics.durationSeconds = static_cast<double>(metricsObject->getProperty("durationSeconds"));
    metrics.sampleRate = static_cast<int>(metricsObject->getProperty("sampleRate"));
    metrics.channels = static_cast<std::size_t>(
        static_cast<int>(metricsObject->getProperty("channels")));
    if (const auto* spectrum = metricsObject->getProperty("spectrum").getDynamicObject()) {
        metrics.spectrum.subDb = static_cast<double>(spectrum->getProperty("subDb"));
        metrics.spectrum.bassDb = static_cast<double>(spectrum->getProperty("bassDb"));
        metrics.spectrum.lowMidDb = static_cast<double>(spectrum->getProperty("lowMidDb"));
        metrics.spectrum.midDb = static_cast<double>(spectrum->getProperty("midDb"));
        metrics.spectrum.presenceDb = static_cast<double>(spectrum->getProperty("presenceDb"));
        metrics.spectrum.airDb = static_cast<double>(spectrum->getProperty("airDb"));
    }

    const auto match = std::ranges::find_if(project_.tracks, [role](const auto& track) {
        return track.role == *role;
    });
    if (match != project_.tracks.end()) {
        match->metrics = metrics;
        engine_.updateTrack(*match);
    }
    pushState();
}

void MainComponent::createProject()
{
    engine_.stop();
    engine_.clearReference();
    project_ = {};
    project_.id = project::makeProjectId();
    project_.name = "Untitled Mix";
    projectFile_ = juce::File();
    currentPlan_ = {};
    planVariants_.clear();
    selectedVariant_ = assistant::MixVariant::balanced;
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
            juce::String error;
            if (!safeThis->engine_.loadReference(chooser.getResult(), error)) {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    "Reference failed",
                    error);
                return;
            }
            safeThis->project_.referencePath =
                chooser.getResult().getFullPathName().toStdString();
            juce::AudioFormatManager manager;
            manager.registerBasicFormats();
            if (auto reader = std::unique_ptr<juce::AudioFormatReader>(
                    manager.createReaderFor(chooser.getResult()))) {
                const auto frameCount = static_cast<int>(std::min<double>(
                    reader->lengthInSamples,
                    reader->sampleRate * 15.0 * 60.0));
                const auto channels = juce::jlimit(1, 2, static_cast<int>(reader->numChannels));
                juce::AudioBuffer<float> buffer(channels, frameCount);
                reader->read(&buffer, 0, frameCount, 0, true, channels > 1);
                std::vector<std::vector<float>> samples(
                    static_cast<std::size_t>(channels),
                    std::vector<float>(static_cast<std::size_t>(frameCount)));
                for (int channel = 0; channel < channels; ++channel) {
                    std::copy_n(
                        buffer.getReadPointer(channel),
                        frameCount,
                        samples[static_cast<std::size_t>(channel)].begin());
                }
                safeThis->referenceMetrics_ =
                    analysis::AudioAnalyzer {}.analyze(samples, reader->sampleRate);
            }
            safeThis->pushState();
        });
}

void MainComponent::chooseMasterDestination(int bitsPerSample)
{
    fileChooser_ = std::make_unique<juce::FileChooser>(
        "Export mastered WAV",
        juce::File::getSpecialLocation(juce::File::userMusicDirectory)
            .getChildFile(
                juce::String(project_.name)
                + (bitsPerSample == 32 ? " - MASTER-32f.wav" : " - MASTER-24.wav")),
        "*.wav");
    juce::Component::SafePointer<MainComponent> safeThis(this);
    fileChooser_->launchAsync(
        juce::FileBrowserComponent::saveMode
            | juce::FileBrowserComponent::canSelectFiles
            | juce::FileBrowserComponent::warnAboutOverwriting,
        [safeThis, bitsPerSample](const juce::FileChooser& chooser) {
            if (safeThis == nullptr || chooser.getResult().getFullPathName().isEmpty())
                return;
            juce::String error;
            if (!safeThis->engine_.renderMaster(
                    safeThis->project_,
                    chooser.getResult().withFileExtension("wav"),
                    bitsPerSample,
                    error)) {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    "Export failed",
                    error);
            }
        });
}

void MainComponent::chooseResearchDestination()
{
    if (project_.tracks.empty()) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::InfoIcon,
            "No research example",
            "Import stems and approve the mix settings before exporting a training example.");
        return;
    }

    fileChooser_ = std::make_unique<juce::FileChooser>(
        "Export anonymous ML research example",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
            .getChildFile(juce::String(project_.name) + ".research.json"),
        "*.json");
    juce::Component::SafePointer<MainComponent> safeThis(this);
    fileChooser_->launchAsync(
        juce::FileBrowserComponent::saveMode
            | juce::FileBrowserComponent::canSelectFiles
            | juce::FileBrowserComponent::warnAboutOverwriting,
        [safeThis](const juce::FileChooser& chooser) {
            if (safeThis == nullptr || chooser.getResult().getFullPathName().isEmpty())
                return;

            const research::ExportOptions options {
                assistant::mixVariantToString(safeThis->selectedVariant_),
                true
            };
            const auto source = research::serializeExample(safeThis->project_, options);
            const auto destination = chooser.getResult().withFileExtension("json");
            if (!destination.replaceWithText(juce::String(source))) {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    "Research export failed",
                    "The metadata file could not be written.");
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
    planVariants_.clear();
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
                if (safeThis == nullptr || chooser.getResult().getFullPathName().isEmpty())
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
    currentPlan_ = {};
    planVariants_.clear();
    engine_.loadProject(project_);
    if (!project_.referencePath.empty()) {
        juce::String error;
        if (engine_.loadReference(juce::File(project_.referencePath), error)) {
            juce::AudioFormatManager manager;
            manager.registerBasicFormats();
            if (auto reader = std::unique_ptr<juce::AudioFormatReader>(
                    manager.createReaderFor(juce::File(project_.referencePath)))) {
                const auto frameCount = static_cast<int>(std::min<double>(
                    reader->lengthInSamples,
                    reader->sampleRate * 15.0 * 60.0));
                const auto channels = juce::jlimit(1, 2, static_cast<int>(reader->numChannels));
                juce::AudioBuffer<float> buffer(channels, frameCount);
                reader->read(&buffer, 0, frameCount, 0, true, channels > 1);
                std::vector<std::vector<float>> samples(
                    static_cast<std::size_t>(channels),
                    std::vector<float>(static_cast<std::size_t>(frameCount)));
                for (int channel = 0; channel < channels; ++channel) {
                    std::copy_n(
                        buffer.getReadPointer(channel),
                        frameCount,
                        samples[static_cast<std::size_t>(channel)].begin());
                }
                referenceMetrics_ =
                    analysis::AudioAnalyzer {}.analyze(samples, reader->sampleRate);
            }
        } else {
            referenceMetrics_.reset();
        }
    } else {
        engine_.clearReference();
        referenceMetrics_.reset();
    }
    pushState();
}

void MainComponent::generateMixPlan()
{
    planVariants_ = advisor_.createVariants(project_, referenceMetrics_);
    selectVariant(assistant::mixVariantToString(selectedVariant_));
}

void MainComponent::selectVariant(const juce::String& variant)
{
    if (const auto parsed = assistant::mixVariantFromString(variant.toStdString()))
        selectedVariant_ = *parsed;
    const auto match = std::ranges::find_if(planVariants_, [this](const auto& plan) {
        return plan.variant == selectedVariant_;
    });
    if (match != planVariants_.end())
        currentPlan_ = *match;
    else if (!planVariants_.empty())
        currentPlan_ = planVariants_.front();
}

void MainComponent::applyMixPlan()
{
    if (currentPlan_.trackAdjustments.empty() && planVariants_.empty())
        generateMixPlan();
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
    object->setProperty("projectId", juce::String(project_.id));
    object->setProperty("projectName", juce::String(project_.name));
    object->setProperty("playing", engine_.isPlaying());
    object->setProperty("positionSeconds", engine_.positionSeconds());
    object->setProperty("durationSeconds", engine_.durationSeconds());
    object->setProperty(
        "monitorSource",
        engine_.monitorSource() == StemEngine::MonitorSource::reference ? "reference" : "mix");
    object->setProperty("hasReference", engine_.hasReference());
    object->setProperty("referenceGainDb", engine_.referenceGainDb());
    object->setProperty("selectedVariant", juce::String(assistant::mixVariantToString(selectedVariant_)));
    object->setProperty("exportBitDepth", exportBitDepth_);
    const auto planState = juce::JSON::parse(juce::String(assistant::toJson(currentPlan_)));
    if (const auto* planObject = planState.getDynamicObject()) {
        object->setProperty("suggestions", planObject->getProperty("suggestions"));
        object->setProperty("variantLabel", planObject->getProperty("variant"));
    }
    juce::Array<juce::var> variants;
    for (const auto& plan : planVariants_)
        variants.add(juce::String(assistant::mixVariantToString(plan.variant)));
    object->setProperty("variants", variants);
    webView_.pushState(state);
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
