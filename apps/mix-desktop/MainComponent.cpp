#include "mix-desktop/MainComponent.h"
#include "mastering/analysis/AnalysisFingerprint.h"
#include "mastering/analysis/StreamingAnalyzer.h"
#include "mastering/assistant/ActionBudget.h"
#include "mastering/assistant/MetalcoreAnalysis.h"
#include "mastering/assistant/MetalcoreProfile.h"
#include "mastering/assistant/RenderIdentity.h"
#include "mastering/assistant/SectionAutomation.h"
#include "mastering/benchmark/CalibrationWorksheet.h"
#include "mastering/benchmark/ExperimentRunner.h"
#include "mastering/benchmark/ImportValidator.h"
#include "mastering/benchmark/ListeningPackageV2.h"
#include "mastering/benchmark/ObjectiveReport.h"
#include "mastering/benchmark/RenderRetention.h"
#include "mastering/benchmark/UserEditCapture.h"
#include "mastering/ipc/BridgeProtocol.h"
#include "mastering/ipc/MixNodeProtocol.h"
#include "mastering/product/ProductVersion.h"
#include "mastering/research/ResearchExample.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
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
    bridge_.setMixNodeHandler([safeThis](const ipc::MixNodeEnvelope& env, const juce::String& raw) {
        juce::MessageManager::callAsync([safeThis, env, raw] {
            if (safeThis != nullptr)
                safeThis->handleMixNodeMessage(env, raw);
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
    } else if (type == "set-compare-mode") {
        const auto mode = object->getProperty("mode").toString();
        if (mode == "raw")
            engine_.setCompareMode(StemEngine::CompareMode::raw);
        else if (mode == "auto")
            engine_.setCompareMode(StemEngine::CompareMode::autoProcessed);
        else if (mode == "reference" || mode == "ref")
            engine_.setCompareMode(StemEngine::CompareMode::reference);
        else
            engine_.setCompareMode(StemEngine::CompareMode::current);
    } else if (type == "generate-mix-plan") {
        generateMixPlan();
    } else if (type == "generate-metalcore-mix-pass") {
        generateMetalcoreMixPass();
    } else if (type == "ensure-hierarchy") {
        assistant::MetalcoreMixPass::ensureHierarchy(project_);
    } else if (type == "set-bpm") {
        project_.bpm = juce::jlimit(40.0, 300.0, static_cast<double>(object->getProperty("bpm")));
    } else if (type == "add-section") {
        addSection(command);
    } else if (type == "remove-section") {
        removeSection(object->getProperty("sectionId").toString());
    } else if (type == "mixpass-preview") {
        previewMixPassAction(object->getProperty("actionId").toString());
    } else if (type == "mixpass-apply") {
        applyMixPassActionCmd(object->getProperty("actionId").toString());
    } else if (type == "mixpass-reject") {
        rejectMixPassAction(object->getProperty("actionId").toString());
    } else if (type == "mixpass-edit") {
        editMixPassAction(
            object->getProperty("actionId").toString(),
            static_cast<double>(object->getProperty("proposedValue")));
    } else if (type == "mixpass-cancel-preview") {
        cancelMixPassPreview(object->getProperty("actionId").toString());
    } else if (type == "mixpass-undo") {
        undoMixPass();
    } else if (type == "mixpass-redo") {
        redoMixPass();
    } else if (type == "select-variant") {
        selectVariant(object->getProperty("variant").toString());
    } else if (type == "apply-mix-plan") {
        applyMixPlan();
    } else if (type == "reject-mix-plan") {
        assistant::MixAdvisor::rejectPlan(currentPlan_);
        project_.actions.clear();
        for (const auto& adjustment : currentPlan_.trackAdjustments) {
            project_.actions.push_back({
                adjustment.actionId,
                adjustment.trackId,
                adjustment.targetGainDb,
                assistant::actionStateToString(adjustment.state)
            });
        }
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
                assistant::MetalcoreMixPass::ensureHierarchy(project_);
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
    } else if (type == "mix-node-preview"
        || type == "mix-node-commit"
        || type == "mix-node-cancel-preview"
        || type == "mix-node-undo"
        || type == "mix-node-request-state") {
        sendMixNodeAction(type, command);
    } else if (type == "select-mix-node") {
        selectedMixNodeId_ = object->getProperty("instanceId").toString();
    } else if (type == "open-local-data-folder") {
        openLocalDataFolder();
    } else if (type == "clear-benchmark-cache") {
        clearBenchmarkCache();
    } else if (type == "clear-benchmark-renders") {
        clearBenchmarkRenders();
    } else if (type == "run-profile-experiment") {
        runProfileExperiment();
    } else if (type == "cancel-experiment") {
        experimentCancelled_ = true;
        lastExperimentSummary_ = "cancelled";
    } else if (type == "validate-benchmark-import") {
        validateBenchmarkImportWizard();
    } else if (type == "set-metalcore-profile") {
        activeProfileId_ = object->getProperty("profileId").toString();
        if (activeProfileId_.isEmpty())
            activeProfileId_ = "modern-metalcore-balanced";
        project_.metalcoreProfileId = activeProfileId_.toStdString();
    }
    pushState();
}

void MainComponent::handleBridgeAnalysis(const juce::var& report)
{
    const auto* object = report.getDynamicObject();
    if (object == nullptr)
        return;

    const auto payload = juce::JSON::toString(report, false);
    const auto validation = ipc::validateTrackAnalysisPayload(payload.toStdString());
    if (!validation.ok)
        return;

    if (object->getProperty("type").toString() != "track-analysis")
        return;

    const auto roleName = object->getProperty("role").toString().toStdString();
    const auto role = project::roleFromString(roleName);
    const auto metricsValue = object->getProperty("metrics");
    if (!role || !metricsValue.isObject())
        return;

    analysis::AudioMetrics metrics;
    const auto* metricsObject = metricsValue.getDynamicObject();
    metrics.samplePeakDbfs = static_cast<double>(metricsObject->getProperty("samplePeakDbfs"));
    metrics.rmsDbfs = static_cast<double>(metricsObject->getProperty("rmsDbfs"));
    metrics.estimatedLoudnessDb = static_cast<double>(
        metricsObject->hasProperty("estimatedLoudnessDb")
            ? metricsObject->getProperty("estimatedLoudnessDb")
            : metricsObject->getProperty("rmsDbfs"));
    metrics.estimatedLoudnessIsValid = true;
    metrics.truePeakIsEstimate = static_cast<bool>(metricsObject->getProperty("truePeakIsEstimate"));
    metrics.integratedLufsIsValid =
        static_cast<bool>(metricsObject->getProperty("integratedLufsIsValid"));
    if (metrics.truePeakIsEstimate)
        metrics.estimatedTruePeakDbtp =
            static_cast<double>(metricsObject->getProperty("estimatedTruePeakDbtp"));
    if (metrics.integratedLufsIsValid)
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
    mixPassUndoStack_.clear();
    mixPassRedoStack_.clear();
    analysisStatus_ = "idle";
    engine_.loadProject(project_);
    bridge_.setSuiteSession(juce::String(project_.id), juce::String(project::makeProjectId()));
    pushState();
}

void MainComponent::handleMixNodeMessage(const ipc::MixNodeEnvelope& env, const juce::String&)
{
    MixNodeConnectionInfo info;
    info.connected = true;
    info.lastHeartbeatMs = env.timestampMs;
    info.identity.projectId = env.projectId;
    info.identity.sessionId = env.sessionId;
    info.identity.instanceId = env.instanceId;

    if (!env.payloadJson.empty()) {
        ipc::MixNodeProtocolError err;
        if (auto host = ipc::parseHostState(env.payloadJson, &err)) {
            info.identity = host->identity;
            info.lastKnown = host->committed;
            info.stateRevision = host->stateRevision;
            info.status = "committed";
        }
    }

    // Source of truth: host plugin committed DSP. Suite stores orchestration only.
    // Reject cross-project registrations silently for isolation.
    if (!bridge_.suiteProjectId().isEmpty()
        && !env.projectId.empty()
        && env.projectId != bridge_.suiteProjectId().toStdString()
        && env.type == ipc::MixNodeMessageType::RegisterInstance) {
        // Still track under its own project key for multi-project awareness.
        info.status = "other-project";
    }

    if (env.type == ipc::MixNodeMessageType::Disconnect)
        info.connected = false;

    if (!info.identity.instanceId.empty())
        bridge_.upsertMixNode(info);
}

void MainComponent::sendMixNodeAction(const juce::String& messageType, const juce::var& command)
{
    const auto* object = command.getDynamicObject();
    if (object == nullptr)
        return;

    const auto instanceId = object->getProperty("instanceId").toString().toStdString();
    ipc::MixNodeEnvelope env;
    env.mixNodeProtocolVersion = ipc::kMixNodeProtocolVersion;
    env.projectId = bridge_.suiteProjectId().toStdString();
    env.sessionId = bridge_.suiteSessionId().toStdString();
    env.instanceId = instanceId;
    env.messageId = project::makeProjectId();
    env.timestampMs = juce::Time::currentTimeMillis();
    env.requiresAck = true;

    if (messageType == "mix-node-cancel-preview") {
        env.type = ipc::MixNodeMessageType::CancelPreview;
        env.payloadJson = "{}";
    } else if (messageType == "mix-node-request-state") {
        env.type = ipc::MixNodeMessageType::RequestState;
        env.payloadJson = "{}";
    } else if (messageType == "mix-node-undo") {
        env.type = ipc::MixNodeMessageType::Undo;
        env.payloadJson = "{}";
    } else {
        ipc::MixNodeAction action;
        action.actionId = object->getProperty("actionId").toString().isNotEmpty()
            ? object->getProperty("actionId").toString().toStdString()
            : project::makeProjectId();
        action.actionVersion = 1;
        action.projectId = env.projectId;
        action.sessionId = env.sessionId;
        action.targetInstanceId = instanceId;
        action.processorId = object->getProperty("processorId").toString().toStdString();
        action.parameterId = object->getProperty("parameterId").toString().toStdString();
        action.previousValue = static_cast<double>(object->getProperty("previousValue"));
        action.proposedValue = static_cast<double>(object->getProperty("proposedValue"));
        action.allowedMin = -120.0;
        action.allowedMax = 24.0;
        if (object->hasProperty("allowedMin"))
            action.allowedMin = static_cast<double>(object->getProperty("allowedMin"));
        if (object->hasProperty("allowedMax"))
            action.allowedMax = static_cast<double>(object->getProperty("allowedMax"));
        action.preview = messageType == "mix-node-preview";
        action.origin = "suite";
        action.explanation = object->getProperty("explanation").toString().toStdString();
        action.createdAtMs = env.timestampMs;
        action.state = action.preview ? ipc::ActionLifecycle::previewing : ipc::ActionLifecycle::pending;
        env.type = action.preview ? ipc::MixNodeMessageType::PreviewAction
                                  : ipc::MixNodeMessageType::CommitAction;
        env.payloadJson = ipc::serializeAction(action);
    }

    bridge_.sendToAll(juce::String(ipc::serializeEnvelope(env)));
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
                    error,
                    safeThis->engine_.compareMode())) {
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
    // Failed/empty decode is a no-op so project model and engine stay in sync.
    auto imported = engine_.importFiles(files);
    if (imported.empty())
        return;
    project_.tracks = std::move(imported);
    project_.sampleRate = project_.tracks.front().metrics.sampleRate;
    if (project_.name == "Untitled Mix")
        project_.name = files.getFirst().getParentDirectory().getFileName().toStdString();
    assistant::MetalcoreMixPass::ensureHierarchy(project_);
    currentPlan_ = {};
    planVariants_.clear();
    project_.mixPassActions.clear();
    mixPassUndoStack_.clear();
    mixPassRedoStack_.clear();
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
    project::DeserializeError error;
    const auto restored = project::deserialize(file.loadFileAsString().toStdString(), error);
    if (!restored) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "Project could not be opened",
            error.message.empty()
                ? juce::String("The file is invalid or uses an unsupported schema version.")
                : juce::String(error.message));
        return;
    }
    project_ = *restored;
    projectFile_ = file;
    currentPlan_ = {};
    planVariants_.clear();
    bridge_.setSuiteSession(juce::String(project_.id), juce::String(project::makeProjectId()));
    engine_.loadProject(project_);
    if (!project_.referencePath.empty()) {
        juce::String referenceError;
        if (engine_.loadReference(juce::File(project_.referencePath), referenceError)) {
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
    assistant::MixAdvisor::applyPlanToProject(project_, currentPlan_);
    engine_.applyPlan(currentPlan_);
    project_.actions.clear();
    for (const auto& adjustment : currentPlan_.trackAdjustments) {
        project_.actions.push_back({
            adjustment.actionId,
            adjustment.trackId,
            adjustment.targetGainDb,
            assistant::actionStateToString(adjustment.state)
        });
    }
    project_.selectedVariant = assistant::mixVariantToString(selectedVariant_);
    pushState();
}

void MainComponent::generateMetalcoreMixPass()
{
    analysisStatus_ = "analyzing 0%";
    pushState();
    assistant::MetalcoreMixPass::ensureHierarchy(project_);

    assistant::MetalcoreMixPass::AnalysisMap analysis;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    const int trackCount = static_cast<int>(project_.tracks.size());
    int trackIndex = 0;
    for (const auto& track : project_.tracks) {
        assistant::TrackAnalysisExtras extras;
        extras.trackId = track.id;
        extras.role = track.role;
        extras.metrics = track.metrics;

        if (!track.audioPath.empty()) {
            if (auto reader = std::unique_ptr<juce::AudioFormatReader>(
                    formats.createReaderFor(juce::File(track.audioPath)))) {
                const auto sr = reader->sampleRate;
                const auto totalFrames = reader->lengthInSamples;
                const auto originalSeconds = sr > 0.0 ? double(totalFrames) / sr : 0.0;
                const auto framesCap = static_cast<juce::int64>(
                    sr * analysis::kMaxAnalysisSeconds);
                const auto framesToRead = std::min(totalFrames, framesCap);
                const bool willTruncate = totalFrames > framesCap;

                if (willTruncate) {
                    analysisStatus_ =
                        "warning: stem exceeds 30 min — analysis will truncate to "
                        + juce::String(analysis::kMaxAnalysisSeconds / 60.0, 0)
                        + " min (not full-track)";
                    pushState();
                }

                analysis::StreamingAnalyzer streaming;
                analysis::AnalysisFingerprint fp;
                fp.assetId = track.id;
                fp.fileSize = static_cast<std::uint64_t>(juce::File(track.audioPath).getSize());
                fp.mtimeHash = static_cast<std::uint64_t>(
                    juce::File(track.audioPath).getLastModificationTime().toMilliseconds());
                fp.sampleRate = sr;
                fp.channelCount = static_cast<int>(reader->numChannels);
                fp.sampleCount = static_cast<std::int64_t>(framesToRead);
                fp.role = project::roleToString(track.role);
                fp.settingsHash = analysis::hashSettingsBlob(
                    "{\"algorithm\":3,\"schema\":5,\"windows\":\"presets\"}");
                // Fast fingerprint from file head/mid/tail via short reads.
                {
                    std::vector<float> probe;
                    const int ch = std::max(1, static_cast<int>(reader->numChannels));
                    const int probeFrames = 2048;
                    juce::AudioBuffer<float> tmp(ch, probeFrames);
                    auto appendAt = [&](juce::int64 start) {
                        tmp.clear();
                        const auto n = static_cast<int>(std::min<juce::int64>(
                            probeFrames, std::max<juce::int64>(0, totalFrames - start)));
                        if (n <= 0)
                            return;
                        reader->read(&tmp, 0, n, start, true, true);
                        for (int i = 0; i < n; ++i) {
                            for (int c = 0; c < ch; ++c)
                                probe.push_back(tmp.getSample(c, i));
                        }
                    };
                    appendAt(0);
                    appendAt(std::max<juce::int64>(0, totalFrames / 2 - probeFrames / 2));
                    appendAt(std::max<juce::int64>(0, totalFrames - probeFrames));
                    fp.contentFingerprint = analysis::fastContentFingerprint(
                        probe.data(), probe.size(), 1);
                }
                streaming.setFingerprint(std::move(fp));
                streaming.setOriginalDurationSeconds(originalSeconds);
                {
                    std::vector<analysis::AnalysisSectionMarker> markers;
                    markers.reserve(project_.sections.size());
                    for (const auto& section : project_.sections) {
                        analysis::AnalysisSectionMarker marker;
                        marker.id = section.id;
                        marker.startSeconds = section.startSeconds;
                        marker.endSeconds = section.endSeconds;
                        markers.push_back(std::move(marker));
                    }
                    streaming.setSectionMarkers(std::move(markers));
                }

                juce::int64 position = 0;
                constexpr int kChunk = 4096;
                juce::AudioBuffer<float> chunkBuffer(
                    juce::jlimit(1, 2, static_cast<int>(reader->numChannels)),
                    kChunk);

                auto result = streaming.analyze(
                    [&](float* monoOut, int maxChunkFrames, int& framesRead) -> bool {
                        if (position >= framesToRead) {
                            framesRead = 0;
                            return false;
                        }
                        const auto toRead = static_cast<int>(std::min<juce::int64>(
                            maxChunkFrames,
                            std::min<juce::int64>(kChunk, framesToRead - position)));
                        chunkBuffer.setSize(
                            chunkBuffer.getNumChannels(),
                            toRead,
                            false,
                            false,
                            true);
                        chunkBuffer.clear();
                        reader->read(&chunkBuffer, 0, toRead, position, true, true);
                        const auto ch = chunkBuffer.getNumChannels();
                        for (int i = 0; i < toRead; ++i) {
                            float sum = 0.0f;
                            for (int c = 0; c < ch; ++c)
                                sum += chunkBuffer.getSample(c, i);
                            monoOut[i] = sum / static_cast<float>(std::max(1, ch));
                        }
                        framesRead = toRead;
                        position += toRead;

                        const double trackBase = trackCount > 0
                            ? (100.0 * double(trackIndex) / double(trackCount))
                            : 0.0;
                        const double trackSpan = trackCount > 0 ? (100.0 / double(trackCount)) : 100.0;
                        const int pct = static_cast<int>(std::clamp(
                            trackBase + trackSpan * streaming.progress(),
                            0.0,
                            99.0));
                        const auto status = "analyzing " + juce::String(pct) + "%";
                        if (status != analysisStatus_) {
                            analysisStatus_ = status;
                            // Avoid flooding the UI: push only on integer percent changes.
                            pushState();
                        }
                        return true;
                    },
                    sr);

                extras.metrics = result.metrics.durationSeconds > 0.0 ? result.metrics : track.metrics;
                extras.analysisCacheKey = result.cacheKey;
                extras.analysisTruncated = result.truncated || willTruncate;
                extras.analyzedDurationSeconds = result.analyzedDurationSeconds > 0.0
                    ? result.analyzedDurationSeconds
                    : (sr > 0.0 ? double(framesToRead) / sr : 0.0);
                extras.originalDurationSeconds = originalSeconds;
                extras.evidencePenalty = result.evidencePenalty;
                if (extras.analysisTruncated) {
                    project_.analysisTruncated = true;
                    project_.analyzedDurationSeconds = std::max(
                        project_.analyzedDurationSeconds,
                        extras.analyzedDurationSeconds);
                    project_.originalDurationSeconds = std::max(
                        project_.originalDurationSeconds,
                        extras.originalDurationSeconds);
                    project_.analysisDurationWarning =
                        "Analysis truncated at 30 minutes — not full-track evidence. "
                        "Actions carry an evidence penalty.";
                    analysisStatus_ = "warning: truncated to 30 min (not full-track)";
                    pushState();
                }
                extras.vocalActivityRatio = result.vocal.activityRatio;
                for (const auto& mask : result.masks) {
                    assistant::TrackAnalysisExtras::ActivityMaskInfo info;
                    info.kind = mask.kind;
                    info.startSeconds = mask.startSeconds;
                    info.endSeconds = mask.endSeconds;
                    info.confidence = mask.confidence;
                    if (mask.kind == "vocal" && mask.confidence > 0.35)
                        extras.vocalMaskActive = true;
                    extras.activityMasks.push_back(std::move(info));
                }
                for (const auto& summary : result.sectionSummaries) {
                    assistant::TrackAnalysisExtras::SectionSummaryInfo info;
                    info.sectionId = summary.sectionId;
                    info.medianFundamentalHz = summary.medianFundamentalHz;
                    info.kickEventCount = summary.kickEventCount;
                    info.snareEventCount = summary.snareEventCount;
                    info.meanRmsDb = summary.meanRmsDb;
                    extras.sectionSummaries.push_back(std::move(info));
                }

                // Map streaming profiles into Mix Pass extras.
                if (track.role == project::TrackRole::kick) {
                    extras.low.dominantLowHz = result.kickLow.dominantLowHz;
                    extras.low.bodyHz = result.kickLow.bodyHz;
                    extras.low.clickHz = result.kickLow.clickHz;
                    extras.low.sustainSeconds = result.kickLow.sustainSeconds;
                    extras.low.lowDecaySeconds = result.kickLow.lowDecaySeconds;
                    extras.low.stabilityScore = result.kickLow.stabilityScore;
                    extras.low.eventCount = result.kickLow.eventCount > 0
                        ? result.kickLow.eventCount
                        : static_cast<int>(result.kickEvents.size());
                    extras.low.evidence = result.kickLow.evidence.empty()
                        ? "streaming-kick"
                        : result.kickLow.evidence;
                    for (const auto& peak : result.kickLow.fundamentalCandidates) {
                        assistant::SpectralPeak p;
                        p.frequencyHz = peak.frequencyHz;
                        p.magnitudeDb = peak.magnitudeDb;
                        p.prominenceDb = peak.prominenceDb;
                        p.bandwidthHz = peak.bandwidthHz;
                        extras.low.fundamentalCandidates.push_back(p);
                    }
                    if (extras.low.dominantLowHz <= 0.0 && !result.kickEvents.empty()) {
                        extras.low.dominantLowHz = result.kickEvents.front().fundamentalHz;
                        extras.low.bodyHz = result.kickEvents.front().bodyHz;
                    }
                } else if (track.role == project::TrackRole::bass) {
                    extras.low.dominantLowHz = result.bassLow.dominantLowHz > 0.0
                        ? result.bassLow.dominantLowHz
                        : result.bass.stableFundamentalHz;
                    extras.low.bodyHz = result.bassLow.bodyHz;
                    extras.low.stabilityScore = result.bassLow.stabilityScore;
                    extras.low.eventCount = result.bassLow.eventCount;
                    extras.low.evidence = result.bassLow.evidence.empty()
                        ? result.bass.evidence
                        : result.bassLow.evidence;
                    for (const auto& peak : result.bassLow.fundamentalCandidates) {
                        assistant::SpectralPeak p;
                        p.frequencyHz = peak.frequencyHz;
                        p.magnitudeDb = peak.magnitudeDb;
                        p.prominenceDb = peak.prominenceDb;
                        extras.low.fundamentalCandidates.push_back(p);
                    }
                } else if (track.role == project::TrackRole::rhythmGuitarLeft
                    || track.role == project::TrackRole::rhythmGuitarRight
                    || track.role == project::TrackRole::rhythmGuitar
                    || track.role == project::TrackRole::leadGuitar
                    || track.role == project::TrackRole::cleanGuitar) {
                    extras.guitar.longTermRmsDb = result.guitar.longTermRmsDb;
                    extras.guitar.lowMidBuildDb = result.guitar.lowMidBuildDb;
                    extras.guitar.presenceDb = result.guitar.presenceDb;
                    extras.guitar.harshPeakHz = result.guitar.harshPeakHz;
                    extras.guitar.harshPeakDb = result.guitar.harshPeakDb;
                    extras.guitar.harshQ = result.guitar.harshQ;
                    extras.guitar.fizzEnergyDb = result.guitar.fizzEnergyDb;
                    extras.guitar.articulationDb = result.guitar.articulationDb;
                    extras.guitar.spectralTiltDbPerOct = result.guitar.spectralTiltDbPerOct;
                    extras.guitar.transientDensityHz = result.guitar.transientDensityHz;
                    extras.guitar.mudLikely = result.guitar.mudLikely;
                    extras.guitar.harshLikely = result.guitar.harshLikely;
                    extras.guitar.fizzLikely = result.guitar.fizzLikely;
                    extras.guitar.evidence = result.guitar.evidence.empty()
                        ? "streaming-guitar"
                        : result.guitar.evidence;
                } else if (track.role == project::TrackRole::cleanVocal
                    || track.role == project::TrackRole::screamVocal
                    || track.role == project::TrackRole::backingVocal) {
                    extras.vocal.rideTargetDb = result.vocal.rideTargetDb;
                    extras.vocal.activityRatio = result.vocal.activityRatio;
                    extras.vocal.presenceCentroidHz = result.vocal.presenceCentroidHz;
                    extras.vocal.sibilanceHz = result.vocal.sibilanceHz;
                    extras.vocal.sibilanceEnergyDb = result.vocal.sibilanceEnergyDb;
                    extras.vocal.resonanceHz = result.vocal.resonanceHz;
                    extras.vocal.resonanceDb = result.vocal.resonanceDb;
                    extras.vocal.crestDb = result.vocal.crestDb;
                    extras.vocal.needsRide = result.vocal.needsRide;
                    extras.vocal.needsDeEss = result.vocal.needsDeEss;
                    extras.vocal.needsResonance = result.vocal.needsResonance;
                    extras.vocal.needsPeakComp = result.vocal.needsPeakComp;
                    extras.vocal.evidence = result.vocal.evidence.empty()
                        ? "streaming-vocal"
                        : result.vocal.evidence;
                    extras.vocalActivityRatio = result.vocal.activityRatio;
                    extras.vocalMaskActive = extras.vocalMaskActive
                        || result.vocal.activityRatio > 0.08;
                } else if (track.role == project::TrackRole::snare) {
                    extras.snare.crackHz = result.snare.crackHz;
                    extras.snare.bodyHz = result.snare.bodyHz;
                    extras.snare.ringHz = result.snare.ringHz;
                    extras.snare.eventRateHz = result.snare.eventRateHz;
                    extras.snare.crackEnergyDb = result.snare.crackEnergyDb;
                    extras.snare.evidence = result.snare.evidence.empty()
                        ? "streaming-snare"
                        : result.snare.evidence;
                }
            }
        }
        analysis.emplace(track.id, std::move(extras));
        ++trackIndex;
    }

    std::vector<assistant::ReferenceProfile> references;
    if (referenceMetrics_) {
        references.push_back(assistant::MetalcoreAnalysis::buildReferenceProfile(
            *referenceMetrics_,
            "overall"));
        references.push_back(assistant::MetalcoreAnalysis::buildReferenceProfile(
            *referenceMetrics_,
            "vocal-balance"));
        references.push_back(assistant::MetalcoreAnalysis::buildReferenceProfile(
            *referenceMetrics_,
            "low-end"));
    }

    assistant::MetalcoreMixPass::Options options;
    options.bpm = project_.bpm;
    options.allowSyntheticFrequencyFallback = false;
    {
        assistant::MetalcoreProfile profile = assistant::defaultBalancedProfile();
        if (activeProfileId_ == "modern-metalcore-aggressive")
            profile = assistant::defaultAggressiveProfile();
        else if (activeProfileId_ == "custom")
            profile = assistant::customProfileFrom(assistant::defaultBalancedProfile());
        if (!project_.metalcoreProfileJson.empty()) {
            if (auto parsed = assistant::deserializeProfile(project_.metalcoreProfileJson))
                profile = *parsed;
        }
        std::vector<std::string> clamped;
        assistant::clampProfileToHardCaps(profile, clamped);
        options.profile = profile;
        project_.metalcoreProfileId = profile.profileId;
        project_.metalcoreProfileRevision = profile.revision;
        project_.engineVersion = product::currentProductVersion().full();
        activeProfileId_ = juce::String(profile.profileId);
    }
    project_.mixPassActions = mixPass_.generateActions(project_, analysis, references, options);

    // Persist ActionBudget summary + analysis cache metadata (schema v5).
    {
        nlohmann::json budgetJson;
        budgetJson["minEvidence"] = assistant::ActionBudget {}.minEvidence;
        budgetJson["maxActionsPerTrack"] = assistant::ActionBudget {}.maxActionsPerTrack;
        budgetJson["actionCount"] = project_.mixPassActions.size();
        int rejected = 0;
        for (const auto& a : project_.mixPassActions) {
            if (a.state == "rejected")
                ++rejected;
        }
        budgetJson["rejectedCount"] = rejected;
        project_.actionBudgetJson = budgetJson.dump();
        project_.analysisCacheVersion = 1;
        if (!project_.tracks.empty() && !analysis.empty()) {
            const auto it = analysis.find(project_.tracks.front().id);
            if (it != analysis.end())
                project_.analysisCacheVersion = static_cast<int>(
                    std::hash<std::string> {}(it->second.analysisCacheKey) & 0x7fffffff);
        }

        assistant::RenderIdentityInputs identityInputs;
        identityInputs.schemaVersion = "5";
        identityInputs.actionGraphJson = assistant::mixPassActionsToJson(project_.mixPassActions);
        identityInputs.sectionAutomationJson = project_.sectionAutomationJson;
        for (const auto& track : project_.tracks)
            identityInputs.sourceAssetPaths.push_back(track.audioPath);
        if (!project_.referencePath.empty())
            identityInputs.referencePaths.push_back(project_.referencePath);
        const auto identity = assistant::RenderIdentityBuilder::build(identityInputs);
        project_.renderIdentityJson = identity.identityJson;
    }

    // AUTO compare policy: risk-tiered auto-apply (never universal 0.45).
    // Flow: generate → evidence → risk → resolve → budget → auto-eligibility → apply.
    for (auto& action : project_.mixPassActions) {
        if (action.state == "rejected" || action.state == "cancelled")
            continue;
        if (action.state != "pending" && action.state != "edited")
            continue;
        if (!action.autoApplyEligibility)
            continue;
        if (action.processorId == "none")
            continue;
        if (assistant::MetalcoreMixPass::applyAction(project_, action)) {
            if (!action.decisionTrace.empty())
                action.decisionTrace += " | ";
            action.decisionTrace += "auto-accepted:" + action.autoApplyReason;
        }
    }

    if (project_.virtualDrumBusEnabled == false) {
        const auto* drumStem = assistant::MetalcoreMixPass::findTrack(
            project_,
            project::TrackRole::drumBus);
        if (drumStem == nullptr) {
            for (const auto& bus : project_.buses) {
                if (bus.role == project::TrackRole::drumBus && bus.parallelEnabled) {
                    project_.virtualDrumBusEnabled = true;
                    break;
                }
            }
        }
    }

    const auto automation = assistant::SectionAutomation::fromActions(project_.mixPassActions);
    project_.sectionAutomationJson = "{}"; // lanes live in actions; engine rebuilds fromActions
    engine_.setSectionAutomation(automation);
    engine_.loadProject(project_);
    for (const auto& track : project_.tracks)
        engine_.updateTrack(track);

    // Seed compare LUFS estimates for RAW/AUTO/CURRENT match gain.
    {
        double avg = 0.0;
        int n = 0;
        for (const auto& track : project_.tracks) {
            if (track.metrics.integratedLufsIsValid) {
                avg += track.metrics.integratedLufs;
                ++n;
            }
        }
        if (n > 0) {
            avg /= static_cast<double>(n);
            engine_.setModeIntegratedLufs(StemEngine::CompareMode::current, avg);
            engine_.setModeIntegratedLufs(StemEngine::CompareMode::autoProcessed, avg);
            engine_.setModeIntegratedLufs(StemEngine::CompareMode::raw, avg - 1.5);
        }
    }

    mixPassUndoStack_.clear();
    mixPassRedoStack_.clear();
    analysisStatus_ = "ready";
    engine_.setCompareMode(StemEngine::CompareMode::autoProcessed);
}

void MainComponent::previewMixPassAction(const juce::String& actionId)
{
    if (auto* action = findMixPassAction(actionId)) {
        if (assistant::MetalcoreMixPass::previewAction(*action)) {
            // Preview applies proposed DSP temporarily for audition.
            engine_.applyMixPassAction(*action);
        }
    }
}

void MainComponent::applyMixPassActionCmd(const juce::String& actionId)
{
    if (auto* action = findMixPassAction(actionId)) {
        if (assistant::MetalcoreMixPass::applyAction(project_, *action)) {
            engine_.applyMixPassAction(*action);
            // Re-sync engine tracks after pair-linked updates.
            for (const auto& track : project_.tracks)
                engine_.updateTrack(track);
            mixPassUndoStack_.push_back(action->actionId);
            mixPassRedoStack_.clear();
            engine_.setCompareMode(StemEngine::CompareMode::current);
        }
    }
}

void MainComponent::rejectMixPassAction(const juce::String& actionId)
{
    if (auto* action = findMixPassAction(actionId)) {
        const auto before = *action;
        assistant::MetalcoreMixPass::rejectAction(*action);
        userEditLog_.append(benchmark::captureMixPassEdit(
            before,
            *action,
            "reject",
            product::currentProductVersion().full(),
            activeProfileId_.toStdString(),
            project_.metalcoreProfileRevision.empty() ? "1" : project_.metalcoreProfileRevision));
    }
}

void MainComponent::editMixPassAction(const juce::String& actionId, double proposedValue)
{
    if (auto* action = findMixPassAction(actionId)) {
        const auto before = *action;
        assistant::MetalcoreMixPass::editAction(*action, proposedValue);
        userEditLog_.append(benchmark::captureMixPassEdit(
            before,
            *action,
            "edit",
            product::currentProductVersion().full(),
            activeProfileId_.toStdString(),
            project_.metalcoreProfileRevision.empty() ? "1" : project_.metalcoreProfileRevision));
    }
}

void MainComponent::cancelMixPassPreview(const juce::String& actionId)
{
    if (auto* action = findMixPassAction(actionId)) {
        if (assistant::MetalcoreMixPass::cancelPreview(*action)) {
            // Restore committed project state into engine.
            for (const auto& track : project_.tracks)
                engine_.updateTrack(track);
        }
    }
}

void MainComponent::undoMixPass()
{
    if (mixPassUndoStack_.empty())
        return;
    const auto actionId = mixPassUndoStack_.back();
    mixPassUndoStack_.pop_back();
    if (auto* action = findMixPassAction(juce::String(actionId))) {
        if (assistant::MetalcoreMixPass::undoAction(project_, *action)) {
            for (const auto& track : project_.tracks)
                engine_.updateTrack(track);
            mixPassRedoStack_.push_back(actionId);
        }
    }
}

void MainComponent::redoMixPass()
{
    if (mixPassRedoStack_.empty())
        return;
    const auto actionId = mixPassRedoStack_.back();
    mixPassRedoStack_.pop_back();
    applyMixPassActionCmd(juce::String(actionId));
}

void MainComponent::addSection(const juce::var& command)
{
    const auto* object = command.getDynamicObject();
    if (object == nullptr)
        return;
    project::SectionMarker section;
    section.id = project::makeProjectId();
    section.name = object->getProperty("name").toString().toStdString();
    section.startSeconds = static_cast<double>(object->getProperty("startSeconds"));
    section.endSeconds = static_cast<double>(object->getProperty("endSeconds"));
    if (const auto kind = project::sectionKindFromString(
            object->getProperty("kind").toString().toStdString()))
        section.kind = *kind;
    if (section.name.empty())
        section.name = project::sectionKindToString(section.kind);
    project_.sections.push_back(std::move(section));
}

void MainComponent::removeSection(const juce::String& sectionId)
{
    const auto id = sectionId.toStdString();
    std::erase_if(project_.sections, [&](const auto& section) { return section.id == id; });
}

juce::File MainComponent::localDataRoot() const
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("MasteringAudioSuite")
        .getChildFile("benchmarks")
        .getChildFile("personal");
}

void MainComponent::openLocalDataFolder()
{
    const auto root = localDataRoot();
    root.createDirectory();
    root.revealToUser();
}

void MainComponent::clearBenchmarkCache()
{
    const auto cache = localDataRoot().getChildFile("sessions");
    if (!cache.isDirectory()) {
        lastExperimentSummary_ = "no-cache";
        return;
    }
    for (const auto& entry : juce::RangedDirectoryIterator(cache, true, "*", juce::File::findDirectories)) {
        if (entry.getFile().getFileName() == "cache")
            entry.getFile().deleteRecursively();
    }
    lastExperimentSummary_ = "cache-cleared";
}

void MainComponent::clearBenchmarkRenders()
{
    const auto sessions = localDataRoot().getChildFile("sessions");
    if (!sessions.isDirectory()) {
        lastExperimentSummary_ = "no-renders";
        return;
    }
    for (const auto& entry : juce::RangedDirectoryIterator(sessions, true, "*", juce::File::findDirectories)) {
        const auto name = entry.getFile().getFileName();
        if (name == "renders" || name == "listening")
            entry.getFile().deleteRecursively();
    }
    lastExperimentSummary_ = "renders-cleared";
}

void MainComponent::validateBenchmarkImportWizard()
{
    const auto root = localDataRoot();
    root.createDirectory();
    auto manifest = benchmark::makeReadinessFixtureManifest(root.getFullPathName().toStdString());
    const auto report = benchmark::validateImport(manifest, root.getFullPathName().toStdString());
    nlohmann::json j;
    j["overall"] = severityToString(report.overall);
    j["issueCount"] = report.issues.size();
    j["localOnly"] = true;
    j["path"] = root.getFullPathName().toStdString();
    j["note"] = "Import wizard validation; ambiguous alignment is never auto-fixed";
    lastImportValidation_ = juce::String(j.dump());
    analysisStatus_ = "import-validated";
}

void MainComponent::runProfileExperiment()
{
    experimentCancelled_ = false;
    assistant::MetalcoreMixPass::AnalysisMap analysis;
    benchmark::ExperimentDefinition def;
    def.experimentId = "suite-profile-ab";
    def.profileIdA = "modern-metalcore-balanced";
    def.profileIdB = "modern-metalcore-aggressive";
    def.randomSeed = 42;
    def.outputFolder = localDataRoot().getChildFile("experiments").getFullPathName().toStdString();
    def.cancelled = experimentCancelled_;
    const auto cmp = benchmark::ExperimentRunner::compareProfiles(project_, analysis, def);
    if (experimentCancelled_) {
        lastExperimentSummary_ = "cancelled";
        return;
    }
    nlohmann::json summary;
    summary["experimentId"] = def.experimentId;
    summary["engine"] = product::currentProductVersion().full();
    summary["actionsA"] = cmp.runA.actions.size();
    summary["actionsB"] = cmp.runB.actions.size();
    summary["added"] = cmp.actionDiff.added;
    summary["removed"] = cmp.actionDiff.removed;
    summary["changed"] = cmp.actionDiff.changed;
    summary["deterministicRerun"] = cmp.runA.deterministicRerunMatch;
    summary["note"] = "Metric/Action change is not automatically an improvement";
    summary["localOnly"] = true;
    lastExperimentSummary_ = juce::String(summary.dump());

    const auto outDir = juce::File(juce::String(def.outputFolder));
    outDir.createDirectory();
    outDir.getChildFile("last-experiment.json").replaceWithText(lastExperimentSummary_);
    const auto sheet = benchmark::emptyWorksheetFor(
        def.experimentId,
        product::currentProductVersion().full(),
        def.profileIdA,
        "1");
    outDir.getChildFile("calibration-worksheet.json")
        .replaceWithText(juce::String(benchmark::serializeCalibrationWorksheet(sheet)));
    outDir.getChildFile("render-retention.json")
        .replaceWithText(juce::String(
            benchmark::serializeRenderRetentionPolicy(benchmark::defaultRenderRetentionPolicy())));
    if (!userEditLog_.events().empty() || true) {
        outDir.getChildFile("user-edit-events.json")
            .replaceWithText(juce::String(userEditLog_.toJson()));
    }
}

void MainComponent::pushState()
{
    auto state = juce::JSON::parse(juce::String(project::serialize(project_)));
    auto* object = state.getDynamicObject();
    if (object == nullptr)
        return;
    object->setProperty("product", "desktop");
    object->setProperty("connected", true);
    object->setProperty("productVersion", juce::String(product::currentProductVersion().display()));
    object->setProperty("engineVersion", juce::String(product::currentProductVersion().full()));
    object->setProperty("profileSchemaVersion", product::currentProductVersion().profileSchemaVersion);
    object->setProperty("engineRevision", product::currentProductVersion().engineRevision);
    object->setProperty("metalcoreProfileId", activeProfileId_);
    object->setProperty("localOnly", true);
    object->setProperty("localDataPath", localDataRoot().getFullPathName());
    object->setProperty("lastExperimentSummary", lastExperimentSummary_);
    object->setProperty("lastImportValidation", lastImportValidation_);
    object->setProperty("userEditEventCount", static_cast<int>(userEditLog_.events().size()));
    object->setProperty("projectId", juce::String(project_.id));
    object->setProperty("projectName", juce::String(project_.name));
    object->setProperty("playing", engine_.isPlaying());
    object->setProperty("positionSeconds", engine_.positionSeconds());
    object->setProperty("durationSeconds", engine_.durationSeconds());
    object->setProperty(
        "monitorSource",
        engine_.monitorSource() == StemEngine::MonitorSource::reference ? "reference" : "mix");
    {
        juce::String compare = "current";
        switch (engine_.compareMode()) {
        case StemEngine::CompareMode::raw: compare = "raw"; break;
        case StemEngine::CompareMode::autoProcessed: compare = "auto"; break;
        case StemEngine::CompareMode::reference: compare = "reference"; break;
        case StemEngine::CompareMode::current: compare = "current"; break;
        }
        object->setProperty("compareMode", compare);
    }
    object->setProperty("hasReference", engine_.hasReference());
    object->setProperty("referenceGainDb", engine_.referenceGainDb());
    object->setProperty("selectedVariant", juce::String(assistant::mixVariantToString(selectedVariant_)));
    object->setProperty("exportBitDepth", exportBitDepth_);
    object->setProperty("bpm", project_.bpm);
    object->setProperty("analysisStatus", analysisStatus_);
    object->setProperty("canUndoMixPass", !mixPassUndoStack_.empty());
    object->setProperty("canRedoMixPass", !mixPassRedoStack_.empty());
    const auto planState = juce::JSON::parse(juce::String(assistant::toJson(currentPlan_)));
    if (const auto* planObject = planState.getDynamicObject()) {
        object->setProperty("suggestions", planObject->getProperty("suggestions"));
        object->setProperty("variantLabel", planObject->getProperty("variant"));
    }
    juce::Array<juce::var> variants;
    for (const auto& plan : planVariants_)
        variants.add(juce::String(assistant::mixVariantToString(plan.variant)));
    object->setProperty("variants", variants);

    juce::Array<juce::var> mixNodes;
    for (const auto& node : bridge_.mixNodes()) {
        auto* n = new juce::DynamicObject();
        n->setProperty("instanceId", juce::String(node.identity.instanceId));
        n->setProperty("trackName", juce::String(node.identity.trackName));
        n->setProperty("role", juce::String(ipc::rolePresetToString(node.identity.role)));
        n->setProperty("channelPosition", juce::String(ipc::channelPositionToString(node.identity.channelPosition)));
        n->setProperty("pairId", juce::String(node.identity.pairId));
        n->setProperty("parentBusId", juce::String(node.identity.parentBusId));
        n->setProperty("connected", node.connected);
        n->setProperty("stateRevision", static_cast<int>(node.stateRevision));
        n->setProperty("latencySamples", node.latencySamples);
        n->setProperty("sampleRate", node.sampleRate);
        n->setProperty("sidechainActive", node.sidechainActive);
        n->setProperty("status", juce::String(node.status));
        n->setProperty("projectId", juce::String(node.identity.projectId));
        n->setProperty("inputGainDb", node.lastKnown.inputGainDb);
        n->setProperty("outputGainDb", node.lastKnown.outputGainDb);
        n->setProperty("eqFreq", node.lastKnown.staticEq.frequencyHz);
        n->setProperty("eqGain", node.lastKnown.staticEq.gainDb);
        n->setProperty("dynThreshold", node.lastKnown.dynamicEq.bands[0].thresholdDb);
        n->setProperty("dynMaxCut", node.lastKnown.dynamicEq.bands[0].maxCutDb);
        n->setProperty("satDrive", node.lastKnown.saturation.drive);
        n->setProperty("bypass", node.lastKnown.bypass);
        mixNodes.add(juce::var(n));
    }
    object->setProperty("mixNodes", mixNodes);
    object->setProperty("suiteSessionId", bridge_.suiteSessionId());
    object->setProperty("selectedMixNodeId", selectedMixNodeId_);
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

project::MixPassAction* MainComponent::findMixPassAction(const juce::String& id)
{
    const auto identifier = id.toStdString();
    const auto iterator = std::ranges::find_if(
        project_.mixPassActions,
        [&identifier](const auto& action) { return action.actionId == identifier; });
    return iterator != project_.mixPassActions.end() ? &*iterator : nullptr;
}

} // namespace mastering::desktop
