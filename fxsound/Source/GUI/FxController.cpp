/*
FxSound
Copyright (C) 2025  FxSound LLC

Contributors:
	www.theremino.com (2025)

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "FxController.h"
#include "FxMainWindow.h"
#include "FxSettingsDialog.h"
#include "FxSystemTrayView.h"
#include "FxMessage.h"
#include "AudioSignalPolicy.h"
#include "OutputDeviceSelection.h"
#include "PresetAutoSavePolicy.h"
#include "StartupOptionPolicy.h"
#include "FxEffects.h"
#include "../Utils/SysInfo/SysInfo.h"

namespace FxSound
{
	namespace StartupOptionPolicy
	{
		bool shouldEnableOutputLatencyLogging(const juce::ArgumentList& arguments)
		{
			for (int index = 0; index < arguments.size(); ++index)
			{
				if (arguments[index].text == "--measure-output-latency")
				{
					return true;
				}
			}

			return false;
		}
	}
}

namespace
{
constexpr auto kSelectedOutputIdSetting = "selected_output_device_id";
constexpr auto kSelectedOutputNameSetting = "selected_output_device_name";
constexpr auto kSelectedOutputContainerIdSetting = "selected_output_container_id";
constexpr auto kSelectedOutputDescriptionSetting = "selected_output_device_description";
constexpr auto kSelectedOutputChannelsSetting = "selected_output_device_channels";

int findPresetIndexByName(const FxModel& model, const String& preset_name)
{
    if (preset_name.isEmpty())
    {
        return 0;
    }

    for (int i = 0; i < model.getPresetCount(); ++i)
    {
        if (model.getPreset(i).name == preset_name)
        {
            return i;
        }
    }

    return -1;
}

// Loads the persisted UI priority order into the pure helper representation.
std::vector<FxSound::OutputDeviceSelection::PriorityEntry> loadOutputPriorities(FxSound::Settings& settings)
{
	std::vector<FxSound::OutputDeviceSelection::PriorityEntry> priorities;
	auto device_configs = DeviceConfig::loadDeviceConfigs(settings, "device_configs");
	priorities.reserve(static_cast<size_t>(device_configs.size()));

	for (const auto& device_config : device_configs)
	{
		priorities.push_back({
			device_config.device_id.toWideCharPointer(),
			device_config.device_name.toWideCharPointer(),
			device_config.container_id.toWideCharPointer()
			});
	}

	return priorities;
}

// Builds the shared resolution context passed into startup, live sync, and idle sync decisions.
FxSound::OutputDeviceSelection::OutputResolutionContext buildOutputResolutionContext(
	FxSound::Settings& settings,
	const SoundDevice& selected_output,
	const String& output_name)
{
	return FxSound::OutputDeviceSelection::makeOutputResolutionContext(
		selected_output,
		output_name.toWideCharPointer(),
		loadOutputPriorities(settings));
}

// Centralizes the side effects for an unavailable playback path so the different
// controller flows report and mute consistently.
void notifyPlaybackUnavailable(IAudioPassthru& audio_passthru,
	bool& playback_device_available,
	FxModel& model,
	bool mute_audio = true)
{
	playback_device_available = false;
	if (mute_audio)
	{
		audio_passthru.mute(true);
	}

	model.notifyOutputError();
}

// Matches a saved device configuration to a live output while tolerating endpoint
// id churn across reconnects.
bool matchesConfiguredOutput(const DeviceConfig& device_config, const SoundDevice& sound_device)
{
	if (!device_config.device_id.isEmpty() &&
		device_config.device_id == sound_device.pwszID.c_str())
	{
		return true;
	}

	if (!device_config.container_id.isEmpty() &&
		!sound_device.containerId.empty() &&
		device_config.container_id == sound_device.containerId.c_str())
	{
		return true;
	}

	return device_config.container_id.isEmpty() &&
		device_config.device_name == sound_device.deviceFriendlyName.c_str();
}
}

class FxDeviceErrorMessage : public FxWindow
{
public:
	FxDeviceErrorMessage()
	{
		setContent(&message_content_);
		centreWithSize(getWidth(), getHeight());
		addToDesktop(0);
		setAlwaysOnTop(true);
	}
	~FxDeviceErrorMessage() = default;

	void closeButtonPressed() override
	{
		exitModalState(0);
		removeFromDesktop();
	}

private:
	class MessageComponent : public Component
	{
	public:
		MessageComponent()
		{
			auto& theme = dynamic_cast<FxTheme&>(LookAndFeel::getDefaultLookAndFeel());

			error_message_.setFont(theme.getNormalFont());
			contact_message_.setFont(theme.getNormalFont());

			error_message_.setText(TRANS("Oops! There's an issue with your playback device settings.\r\nBefore we can get started, please go through the "), NotificationType::dontSendNotification);
			error_message_.setJustificationType(Justification::topLeft);
			addAndMakeVisible(error_message_);

			error_link_.setButtonText(TRANS("troubleshooting steps here."));
			error_link_.setURL(URL(L"https://www.fxsound.com/learning-center/installation-troubleshooting"));
			error_link_.setJustificationType(Justification::topLeft);
			addAndMakeVisible(error_link_);

			contact_message_.setText(TRANS(" if you're still having problems."), NotificationType::dontSendNotification);
			contact_message_.setJustificationType(Justification::topLeft);
			addAndMakeVisible(contact_message_);

			contact_link_.setButtonText(TRANS("Contact us"));
			contact_link_.setURL(URL(L"https://www.fxsound.com/support"));
			contact_link_.setJustificationType(Justification::topLeft);
			addAndMakeVisible(contact_link_);

			setSize(WIDTH, HEIGHT);
		}
		~MessageComponent() = default;

	private:
		static constexpr int WIDTH = 400;
		static constexpr int HEIGHT = 142;
		static constexpr int MESSAGE_HEIGHT = (24 + 2) * 2;
		static constexpr int HYPERLINK_HEIGHT = 24;

		void resized() override
		{
			auto bounds = getLocalBounds();
			bounds.setTop(10);
			bounds.reduce(20, 0);

			RectanglePlacement placement(RectanglePlacement::xLeft
										| RectanglePlacement::yTop
										| RectanglePlacement::doNotResize);

			auto component_area = juce::Rectangle<int>(0, 0, bounds.getWidth(), MESSAGE_HEIGHT);
			error_message_.setBounds(placement.appliedTo(component_area, bounds));
			auto x = error_message_.getX() + error_message_.getBorderSize().getLeft();

			bounds.setTop(error_message_.getBottom() + 2);
			component_area = juce::Rectangle<int>(0, 0, bounds.getWidth(), HYPERLINK_HEIGHT);
			error_link_.setBounds(placement.appliedTo(component_area, bounds).withX(x));

			bounds.setTop(error_link_.getBottom() + 20);
			component_area = juce::Rectangle<int>(0, 0, contact_link_.getTextWidth(), HYPERLINK_HEIGHT);
			contact_link_.setBounds(placement.appliedTo(component_area, bounds).withX(x));

			bounds = juce::Rectangle<int>(contact_link_.getRight(), contact_link_.getY(), error_message_.getWidth() - contact_link_.getWidth(), MESSAGE_HEIGHT / 2);
			contact_message_.setBounds(bounds);
		}

		Label error_message_;
		Label contact_message_;
		FxHyperlink error_link_;
		FxHyperlink contact_link_;

		JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MessageComponent)
	};

	MessageComponent message_content_;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FxDeviceErrorMessage)
};

FxController::FxController() : message_window_(L"FxSoundHotkeys", (WNDPROC) eventCallback)
{
	dfx_enabled_ = true;
	authenticated_ = true;
	minimize_tip_ = true;
	device_change_message_pending_ = false;
	shutting_down_ = false;
	pending_device_change_kind_ = AudioDeviceChangeKind::Unknown;
	pending_device_change_id_ = String();

	hotkeys_registered_ = false;
	output_changed_ = false;
    playback_device_available_ = true;

	device_count_ = 0;
	output_device_name_ = L"";
	
	audio_signal_counters_ = {};
	audio_process_on_ = false;

	audio_process_start_time_ = -1LL;
	audio_process_grace_deadline_ms_ = 0;
	output_latency_logging_enabled_ = false;
	preset_dirty_ = false;
	auto_save_counter_ = 0;
	tray_icon_health_check_counter_ = 0;
	tray_icon_recovery_pending_ = false;
	audio_recovery_state_ = {};
    main_window_ = nullptr;
	settings_dialog_ = nullptr;
    audio_passthru_ = nullptr;

	file_logger_.reset(FileLogger::createDefaultAppLogger(L"FxSound", L"fxsound.log", L"FxSound logs"));
    logMessage("v" + JUCEApplication::getInstance()->getApplicationVersion());
	logMessage(SystemStats::getOperatingSystemName());

	SYSTEM_INFO sys_info;
	GetNativeSystemInfo(&sys_info);
	if (sys_info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL)
	{
		logMessage(String("x86"));
	}
	else if (sys_info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64)
	{
		logMessage(String("x64"));
	}
	else if (sys_info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64)
	{
		logMessage(String("ARM64"));
	}

	auto view = settings_.getInt("view");
	if (view <= 0 || view > 2)
	{
		view_ = ViewType::Pro;
	}
	else
	{
		view_ = static_cast<ViewType>(view);
	}
	auto hotkeys_support = settings_.getBool("hotkeys") && SysInfo::canSupportHotkeys();
	FxModel::getModel().setHotkeySupport(hotkeys_support);
	if (hotkeys_support)
	{
		registerHotkeys();
	}
	FxModel::getModel().setMenuClicked(settings_.getBool("menu_clicked"));

	always_on_top_ = settings_.getBool("always_on_top");
    hide_help_tooltips_ = settings_.getBool("hide_help_tooltips");
	hide_notifications_ = settings_.getBool("hide_notifications");
	auto_updates_ = settings_.getBool("automatic_updates", true);
	max_user_presets_ = settings_.getInt("max_user_presets");
	if (max_user_presets_ < 10 || max_user_presets_ > 120)
	{
		settings_.setInt("max_user_presets", 120);
		max_user_presets_ = 120;		
	}
	
	SetWindowLongPtr(message_window_.getHandle(), GWLP_USERDATA, (LONG_PTR)this);

	session_id_ = 0;
	ProcessIdToSessionId(GetCurrentProcessId(), &session_id_);
	WTSRegisterSessionNotification(message_window_.getHandle(), NOTIFY_FOR_THIS_SESSION);
}

FxController::~FxController()
{
	shutting_down_ = true;
	SetWindowLongPtr(message_window_.getHandle(), GWLP_USERDATA, 0);
	if (audio_passthru_ != nullptr)
	{
		audio_passthru_->registerCallback(nullptr);
	}
	WTSUnRegisterSessionNotification(message_window_.getHandle());
	unregisterHotkeys();
}


void FxController::config(const String& commandline)
{
    auto arg_list = ArgumentList(File::getSpecialLocation(File::SpecialLocationType::invokedExecutableFile).getFileName(), commandline);

    auto preset = arg_list.getValueForOption("--preset").unquoted();
    auto view = arg_list.getValueForOption("--view");
    auto output_device = arg_list.getValueForOption("--output").unquoted();
	auto language = arg_list.getValueForOption("--language");
	auto numbands = arg_list.getValueForOption("--num_bands");
	auto balance = arg_list.getValueForOption("--balance");
	auto filterq = arg_list.getValueForOption("--filter_q");
	auto mastergain = arg_list.getValueForOption("--master_gain");
	auto normalization = arg_list.getValueForOption("--normalization");
	output_latency_logging_enabled_ = FxSound::StartupOptionPolicy::shouldEnableOutputLatencyLogging(arg_list);
    
    if (preset.isNotEmpty())
    {
        settings_.setString("preset", preset);
    }

    if (view.isNotEmpty())
    {
        auto value = view.getIntValue();
        if (value == ViewType::Lite || value == ViewType::Pro)
        {
            settings_.setInt("view", value);
            view_ = static_cast<ViewType>(value);
        }
    }

    if (language.isEmpty())
    {
        language = settings_.getString("language");
        if (language.isEmpty())
        {
            language = SystemStats::getDisplayLanguage();
        }        
    }

    setLanguage(language);

	// ------------------------------------------------------------------------------------------
	//  NumBands / Balance / FilterQ / MasterGain / Normalization / Volume Leveling - SET ON START
	// ------------------------------------------------------------------------------------------
	int nb = 10;
	if (numbands == "")
	{
		nb = settings_.getInt("num_bands");
	}		
	else
	{
		nb = numbands.getIntValue();
	}	
	if (nb < 5 || nb > 31) nb = DEFAULT_NUM_EQ_BANDS;
	setNumEqBands(nb);

	float nm = 0;
	if (normalization == "")
	{
		nm = settings_.getDouble("normalization");
	}
	else
	{
		nm = normalization.getFloatValue();
	}
	if (nm < -20 || nm > 0) nm = DEFAULT_NORMALIZATION;
	setNormalization(nm);

	float vl = settings_.getDouble("volume_leveling");
	if (vl < 0 || vl > 4) vl = DEFAULT_VOLUME_LEVELING;
	setVolumeLeveling(vl);

	float auto_eq_range = settings_.getDouble("auto_eq_range");
	if (auto_eq_range < 1 || auto_eq_range > 12) auto_eq_range = DEFAULT_AUTO_EQ_RANGE;
	setAutoEqRange(auto_eq_range);

	setAutoEqEnabled(settings_.getBool("auto_eq_enabled", DEFAULT_AUTO_EQ_ENABLED));

	float bl = 0;
	if (balance == "")
	{
		bl = settings_.getDouble("balance");
	}
	else
	{
		bl = balance.getFloatValue();
	}
	if (bl < -20 || bl > +20) bl = DEFAULT_BALANCE;
	setBalance(bl);	

	float fq = 0;
	if (filterq == "")
	{
		fq = settings_.getDouble("filter_q");
	}
	else
	{
		fq = filterq.getFloatValue();
	}
	if (fq < 1 || fq > 3) fq = DEFAULT_FILTER_Q;
	setFilterQ(fq);

	float mg = 0;
	if (mastergain == "")
	{
		mg = settings_.getDouble("master_gain");
	}
	else
	{
		mg = mastergain.getFloatValue();
	}		
	if (mg < -20 || mg > +20) mg = DEFAULT_MASTER_GAIN;
	setMasterGain(mg);

	if (output_latency_logging_enabled_)
	{
		logMessage("Output latency logging enabled");
	}
}

void FxController::init(FxMainWindow* main_window, FxSystemTrayView* system_tray_view, IAudioPassthru* audio_passthru)
{
	if (!isTimerRunning())
	{
		main_window_ = main_window;
		audio_passthru_ = audio_passthru;
		system_tray_view_ = system_tray_view;
        
        if (audio_passthru_->init(output_latency_logging_enabled_) != 0)
        {
            String message(TRANS("Error in system audio configuration. Unable to run FxSound"));
            AlertWindow::showMessageBox(AlertWindow::AlertIconType::WarningIcon, JUCEApplication::getInstance()->getApplicationName(), message, TRANS("OK"));

            JUCEApplication::getInstance()->systemRequestedQuit();
            return;
		}

		audio_passthru_->setDspProcessingModule(&dfx_dsp_);
		audio_passthru_->setDspProcessingEnabled(true);
		auto selected_output = loadSelectedOutputFromSettings();
		if (!selected_output.pwszID.empty() || !selected_output.deviceFriendlyName.empty())
		{
			FxModel::getModel().setSelectedOutput(selected_output, false);
			if (!selected_output.deviceFriendlyName.empty())
			{
				setOutputName(selected_output.deviceFriendlyName.c_str());
			}
		}
		initOutputs(audio_passthru_->getSoundDevices(false));
		if (!dfx_enabled_)
		{
			main_window_->removeFromDesktop();
			FxDeviceErrorMessage error_message;
			error_message.runModalLoop();
			JUCEApplication::getInstance()->systemRequestedQuit();
			return;
		}
	
		audio_passthru_->registerCallback(this);

		auto path = File(File::addTrailingSeparator(File::getSpecialLocation(File::SpecialLocationType::userApplicationDataDirectory).getFullPathName()) + L"FxSound\\Presets");
		if (!path.exists())
		{
			path.createDirectory();
		}

		auto app_version = JUCEApplication::getInstance()->getApplicationVersion();
		auto prev_version = settings_.getString("version");
		auto version_changed = prev_version != app_version;
		auto show_open_source_message = !prev_version.startsWith("1.1.2") && app_version.startsWith("1.1.2");
        if (version_changed)
        {
			RegDeleteTree(HKEY_CURRENT_USER, L"Software\\DFX");
            settings_.setString("version", app_version);
            view_ = ViewType::Pro;
            settings_.setBool("run_minimized", false);
        }

		setPowerState(dfx_enabled_ && settings_.getBool("power"));

		initPresets();
		
		auto preset_name = settings_.getString("preset");
		auto selected_preset = findPresetIndexByName(FxModel::getModel(), preset_name);
		if (!setPreset(selected_preset) && selected_preset != 0)
		{
			setPreset(0);
		}

        if (version_changed)
        {
            FxModel::getModel().pushMessage(" ", { TRANS("Click here to see what's new on this version!"), "https://www.fxsound.com/changelog" });			
			if (show_open_source_message)
			{
				FxMessage::showMessage(TRANS("FxSound is now open-source"), { TRANS("GitHub"), "https://github.com/fxsound2/fxsound-app" });
			}
        }
		showView();

		auto theme_mode = settings_.getInt("theme_mode", 0);
		if (theme_mode < 0 || theme_mode >= static_cast<int>(FxThemeMode::NumModes))
		{
			theme_mode = 0;
		}
		if (FxTheme::getThemeMode() != static_cast<FxThemeMode>(theme_mode))
		{
			FxTheme::setThemeMode(static_cast<FxThemeMode>(theme_mode));
			main_window_->sendLookAndFeelChange();

			auto* theme = dynamic_cast<FxTheme*>(&LookAndFeel::getDefaultLookAndFeel());
			if (theme != nullptr)
			{
				theme->loadFont(language_);
			}
		}

        survey_tip_ = !settings_.getBool("survey_displayed");
        
        if (!settings_.getBool("run_minimized"))
        {
            showMainWindow();
        }
        else
        {
            hideMainWindow();
        }

		auto power = FxModel::getModel().getPowerState();
		main_window_->setIcon(power, false);
		system_tray_view_->setStatus(power, false);
	}
}

void FxController::releaseRuntimeObjects()
{
	ScopedLock auto_lock(lock_);

	shutting_down_ = true;
	device_change_message_pending_ = false;
	pending_device_change_kind_ = AudioDeviceChangeKind::Unknown;
	pending_device_change_id_.clear();

	stopTimer();
	SetWindowLongPtr(message_window_.getHandle(), GWLP_USERDATA, 0);

	if (audio_passthru_ != nullptr)
	{
		audio_passthru_->registerCallback(nullptr);
	}

	audio_passthru_ = nullptr;
	system_tray_view_ = nullptr;
	main_window_ = nullptr;
}

void FxController::initPresets()
{
	Array<FxModel::Preset> presets;
	auto working_dir = File::getCurrentWorkingDirectory();
	FileSearchPath preset_search_path(File::addTrailingSeparator(working_dir.getFullPathName()) + L"Factsoft");
	auto app_preset_paths = preset_search_path.findChildFiles(File::findFiles, false, "*.fac");
	for (auto path : app_preset_paths)
	{
		auto preset_info = dfx_dsp_.getPresetInfo(path.getFullPathName().toWideCharPointer());
		if (!preset_info.name.empty())
		{
			presets.add({ preset_info.name.c_str(), path.getFullPathName(), FxModel::PresetType::AppPreset });
		}
	}

	auto data_dir = File::addTrailingSeparator(File::getSpecialLocation(File::SpecialLocationType::userApplicationDataDirectory).getFullPathName());
	FileSearchPath user_preset_search_path(data_dir + L"FxSound\\Presets");
	auto user_preset_paths = user_preset_search_path.findChildFiles(File::findFiles, false, "*.fac");
	for (auto path : user_preset_paths)
	{
		auto preset_info = dfx_dsp_.getPresetInfo(path.getFullPathName().toWideCharPointer());
		if (!preset_info.name.empty())
		{
			presets.add({ preset_info.name.c_str(), path.getFullPathName(), FxModel::PresetType::UserPreset });
		}
	}

	cleanupOrphanedAutoSavedPresets(presets);

	for (auto& preset : presets)
	{
		if (getAutoSavePresetFile(preset.name).existsAsFile())
		{
			preset.modified = true;
		}
	}

	FxModel::getModel().initPresets(presets);
}

void FxController::showView()
{
	if (view_ == ViewType::Pro)
	{
		main_window_->showProView();
	}
	else
	{
		main_window_->showLiteView();
	}
}

void FxController::switchView()
{
	if (view_ == ViewType::Pro)
	{
		view_ = ViewType::Lite;
		main_window_->showLiteView();
		settings_.setInt("view", static_cast<int>(view_));
	}
	else
	{
		view_ = ViewType::Pro;
		main_window_->showProView();
		settings_.setInt("view", static_cast<int>(view_));
	}
}

ViewType FxController::getCurrentView()
{
	return view_;
}

void FxController::hideMainWindow()
{
	if (main_window_->isOnDesktop())
	{
		main_window_->removeFromDesktop();
		main_window_->setVisible(false);
        settings_.setBool("run_minimized", true);
	}
	
	if (minimize_tip_)
	{
        Thread::sleep(2000);
		FxModel::getModel().pushMessage(TRANS("FxSound in system tray\r\nClick FxSound icon to reopen"));
		minimize_tip_ = false;
	}
}

void FxController::showMainWindow()
{
	if (main_window_ != nullptr)
	{
		settings_.setBool("run_minimized", false);
		main_window_->show();

		auto power = FxModel::getModel().getPowerState();
		main_window_->setIcon(power, audio_process_on_);

        if (survey_tip_)
        {
            uint32_t survey_timer = settings_.getInt("survey_timer");
            if (survey_timer == 0)
            {
                survey_timer = std::time(nullptr) + (7 * (24 * 60 * 60));
                settings_.setInt("survey_timer", survey_timer);
            }
            else
            {
                uint32_t current_time = std::time(nullptr);
                if (current_time > survey_timer)
                {
                    survey_tip_ = false;
                    settings_.setBool("survey_displayed", true);
                    String message = TRANS("Thanks for using FxSound! Would you be\r\ninterested in helping us by taking a quick 4 minute\r\nsurvey so we can make FxSound better?");
                    if (authenticated_)
                    {
                        FxModel::getModel().pushMessage(message, { TRANS("Take the survey."), "https://forms.gle/ATx1ayXDWRaMdiR59" });
                    }                    
                }
            }
        }
	}
}

bool FxController::isMainWindowVisible()
{
    if (main_window_ != nullptr)
    {
        return main_window_->isOnDesktop() && main_window_->isVisible();
    }
    
    return false;
}

void FxController::setMenuClicked(bool clicked)
{
	settings_.setBool("menu_clicked", clicked);
	FxModel::getModel().setMenuClicked(clicked);
}

FxWindow* FxController::getMainWindow()
{
	return main_window_;
}

Point<int> FxController::getSystemTrayWindowPosition(int width, int height)
{
	return system_tray_view_->getSystemTrayWindowPosition(width, height);
}

File FxController::getAutoSaveDir() const
{
	auto data_dir = File::addTrailingSeparator(File::getSpecialLocation(File::SpecialLocationType::userApplicationDataDirectory).getFullPathName());
	return File(data_dir + L"FxSound\\AutoSave");
}

File FxController::getAutoSavePresetFile(const String& preset_name) const
{
	return getAutoSaveDir().getChildFile(preset_name + L".fac");
}

void FxController::cleanupOrphanedAutoSavedPresets(const Array<FxModel::Preset>& presets)
{
	auto auto_save_dir = getAutoSaveDir();
	if (!auto_save_dir.isDirectory())
	{
		return;
	}

	std::vector<std::wstring> preset_names;
	preset_names.reserve(static_cast<size_t>(presets.size()));
	for (const auto& preset : presets)
	{
		preset_names.push_back(preset.name.toWideCharPointer());
	}

	auto auto_save_paths = auto_save_dir.findChildFiles(File::findFiles, false, "*.fac");
	for (const auto& auto_save_path : auto_save_paths)
	{
		auto auto_save_name = auto_save_path.getFileNameWithoutExtension();
		if (!FxSound::PresetAutoSavePolicy::shouldKeepAutoSavedPreset(
			auto_save_name.toWideCharPointer(),
			preset_names))
		{
			auto_save_path.deleteFile();
		}
	}
}

bool FxController::savePresetToDirectory(const String& preset_name, const File& directory)
{
	if (!directory.isDirectory())
	{
		auto create_result = directory.createDirectory();
		if (create_result.failed())
		{
			return false;
		}
	}

	return dfx_dsp_.savePreset(preset_name.toWideCharPointer(), directory.getFullPathName().toWideCharPointer()) == 0;
}

void FxController::resetAutoSaveState()
{
	preset_dirty_ = false;
	auto_save_counter_ = 0;
}

void FxController::autoSavePreset(int preset_index)
{
	ScopedLock auto_lock(save_lock_);

	auto& model = FxModel::getModel();
	auto preset = model.getPreset(preset_index);
	if (preset.name.isEmpty())
	{
		return;
	}

	if (!savePresetToDirectory(preset.name, getAutoSaveDir()))
	{
		logMessage("Failed to autosave preset " + preset.name + "\n");
		auto_save_counter_ = 0;
		return;
	}

	resetAutoSaveState();
}

void FxController::deleteAutoSavedPreset(const String& preset_name)
{
	getAutoSavePresetFile(preset_name).deleteFile();
}

void FxController::autoSaveModifiedPreset()
{
	auto& model = FxModel::getModel();
	if (model.isPresetModified())
	{
		autoSavePreset(model.getSelectedPreset());
	}
}

bool FxController::exit()
{
	if (FxModel::getModel().getPowerState())
	{
		audio_passthru_->restoreDefaultPlaybackDevice();
	}
	
	JUCEApplication::getInstance()->systemRequestedQuit();

	return true;
}

void FxController::setPowerState(bool power_state)
{
	FxModel::getModel().setPowerState(power_state);
	powerOn(power_state);
	settings_.setBool("power", power_state);

	system_tray_view_->setStatus(power_state, audio_process_on_);
	main_window_->setIcon(power_state, audio_process_on_);
}

bool FxController::setPreset(const String& preset_name, bool notify)
{
	auto selected_preset = findPresetIndexByName(FxModel::getModel(), preset_name);
	return setPreset(selected_preset, notify);
}

bool FxController::setPreset(int selected_preset, bool notify)
{
	auto& model = FxModel::getModel();

	if (selected_preset < 0 || selected_preset >= model.getPresetCount())
	{
		return false;
	}

    auto preset = model.getPreset(selected_preset);
	auto switch_decision = FxSound::PresetAutoSavePolicy::buildPresetSwitchDecision(
		model.isPresetModified(),
		model.getSelectedPreset(),
		selected_preset,
		getAutoSavePresetFile(preset.name).existsAsFile());

	if (switch_decision.should_auto_save_current)
	{
		autoSavePreset(model.getSelectedPreset());
	}

	if (preset.path.isNotEmpty())
	{
		auto preset_path = preset.path;
		if (switch_decision.should_load_auto_saved_preset)
		{
			auto auto_save_path = getAutoSavePresetFile(preset.name);
			preset_path = auto_save_path.getFullPathName();
		}

		if (dfx_dsp_.loadPreset(preset_path.toWideCharPointer()) != 0)
		{
			model.pushMessage(FormatString(TRANS("Unable to load preset %s."), preset.name));
			return false;
		}

		settings_.setString("preset", preset.name);
		model.selectPreset(selected_preset, true);
		model.setPresetModified(selected_preset, switch_decision.should_mark_loaded_preset_modified);
		resetAutoSaveState();

        for (auto e=0; e<FxEffects::EffectType::NumEffects; e++)
        {
            auto value = dfx_dsp_.getEffectValue(static_cast<DfxDsp::Effect>(e));
            dfx_dsp_.setEffectValue(static_cast<DfxDsp::Effect>(e), value*10);
        }

        auto num_bands = getNumEqBands();
        for (auto b=0; b<num_bands; b++)
        {
            dfx_dsp_.setEqBandFrequency(b, dfx_dsp_.getEqBandFrequency(b));
            dfx_dsp_.setEqBandBoostCut(b, dfx_dsp_.getEqBandBoostCut(b));
        }
	}

	if (notify && FxModel::getModel().getPowerState())
	{ 
		model.pushMessage(TRANS("Preset: ") + model.getPreset(selected_preset).name);
	}
	
	return true;
}

void FxController::setOutput(const String output_device_id, bool notify)
{
	std::vector<SoundDevice> sound_devices = audio_passthru_->getSoundDevices();
	auto previous_selected_output = FxModel::getModel().getSelectedOutput();
	auto decision = FxSound::OutputDeviceSelection::buildManualSelectionDecision(
		sound_devices,
		output_device_id.toWideCharPointer(),
		previous_selected_output,
		isTimerRunning(),
		FxModel::getModel().getPowerState(),
		audio_passthru_->isPlaybackDeviceAvailable());

	if (!decision.found_output)
	{
		audio_passthru_->mute(true);
		powerOn(false);

		FxModel::getModel().pushMessage(TRANS("Output Disconnected"));
	}
	else
	{
		auto sound_device = decision.selected_output;
		applySelectedOutput(sound_device, notify);

		if (applyRoutingActions(sound_device, decision.routing_actions))
		{
			output_changed_ = true;
		}

		String message = TRANS("Output: ") + sound_device.deviceFriendlyName.c_str();

		auto applied_preset_name = tryApplyAutoPresetForCurrentOutput(true);
		if (applied_preset_name.isNotEmpty())
		{
			message += "\n" + TRANS("Preset: ") + applied_preset_name;
		}

		FxModel::getModel().pushMessage(message);

		if (decision.should_sync_processing_state)
		{
			playback_device_available_ = audio_passthru_->isPlaybackDeviceAvailable();
			powerOn(true);
			audio_passthru_->mute(!playback_device_available_);
			FxModel::getModel().notifyOutputError();
		}
	}

	system_tray_view_->setStatus(FxModel::getModel().getPowerState(), isAudioProcessing());
}

void FxController::setOutput(int output, bool notify)
{
	auto output_devices = FxModel::getModel().getOutputDevices();

	if (output < output_devices.size())
	{
		setOutput(output_devices[output].pwszID.c_str(), notify);
	}
}

bool FxController::isPlaybackDeviceAvailable()
{
    return playback_device_available_;
}

void FxController::savePreset(const String& preset_name)
{
	ScopedLock lock(save_lock_);

	auto& model = FxModel::getModel();

	auto preset_index = model.getSelectedPreset();
	auto preset = model.getPreset(preset_index);

	if (preset_name.isEmpty())
	{
		auto path = File(File::addTrailingSeparator(File::getSpecialLocation(File::SpecialLocationType::userApplicationDataDirectory).getFullPathName()) + L"FxSound\\Presets");
		if (!savePresetToDirectory(preset.name, path))
		{
			model.pushMessage(FormatString(TRANS("Unable to save preset %s."), preset.name));
			return;
		}

		deleteAutoSavedPreset(preset.name);
		resetAutoSaveState();

		model.pushMessage(FormatString(TRANS(L"Changes to preset %s are saved."), preset.name));
	}
	else
	{
		auto path = File(File::addTrailingSeparator(File::getSpecialLocation(File::SpecialLocationType::userApplicationDataDirectory).getFullPathName()) + L"FxSound\\Presets");
		if (!savePresetToDirectory(preset_name, path))
		{
			model.pushMessage(FormatString(TRANS("Unable to save preset %s."), preset_name));
			return;
		}

		deleteAutoSavedPreset(preset.name);
		getAutoSavePresetFile(preset_name).deleteFile();
		model.setPresetModified(preset_index, false);
		resetAutoSaveState();

		initPresets();
		setPreset(preset_name);

		model.pushMessage(FormatString(TRANS("New preset %s is saved."), preset_name));

		if (model.getUserPresetCount() == FxController::getInstance().getMaxUserPresets())
		{
			Thread::sleep(2000);
			model.pushMessage(TRANS("Reached the limit on new presets."));
		}
	}

	model.setPresetModified(false);
}

void FxController::renamePreset(const String& new_name)
{
	ScopedLock lock(save_lock_);

	auto& model = FxModel::getModel();

	auto preset_index = model.getSelectedPreset();
	auto preset = model.getPreset(preset_index);

	if (preset.name == new_name) return;
	if (preset.type == FxModel::PresetType::UserPreset)
	{
		auto path = File(File::addTrailingSeparator(File::getSpecialLocation(File::SpecialLocationType::userApplicationDataDirectory).getFullPathName()) + L"FxSound\\Presets");
		if (!savePresetToDirectory(new_name, path))
		{
			model.pushMessage(FormatString(TRANS("Unable to rename preset %s."), preset.name));
			return;
		}
		getAutoSavePresetFile(new_name).deleteFile();

		File new_preset_file(path.getChildFile(new_name + L".fac"));
		File old_preset_file(preset.path);
		if (!old_preset_file.deleteFile())
		{
			if (new_preset_file.existsAsFile())
			{
				new_preset_file.deleteFile();
			}

			model.pushMessage(FormatString(TRANS("Unable to rename preset %s."), preset.name));
			return;
		}

		deleteAutoSavedPreset(preset.name);
		model.setPresetModified(preset_index, false);
		resetAutoSaveState();

		initPresets();
		setPreset(new_name);

		model.setPresetModified(false);
	}
}

void FxController::deletePreset()
{
	auto& model = FxModel::getModel();

	auto preset_index = model.getSelectedPreset();
	auto preset = model.getPreset(preset_index);

	if (preset.type == FxModel::PresetType::UserPreset)
	{
		deleteAutoSavedPreset(preset.name);

		wchar_t path[MAX_PATH] = {};
		
		wcscpy_s(path, preset.path.toWideCharPointer());
		if (preset.path.length() + 1 < MAX_PATH)
		{
			path[preset.path.length() + 1] = L'\0';
		}

		SHFILEOPSTRUCT file_op = { NULL, FO_DELETE, path, L"",
								   FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT, FALSE, 0, L"" };
		SHFileOperation(&file_op);

		finalizePresetMutation();

		FxModel::getModel().pushMessage(FormatString(TRANS("Preset %s is deleted."), preset.name));
	}
}

void FxController::undoPreset()
{
	auto& model = FxModel::getModel();

	auto preset_index = model.getSelectedPreset();
	if (!model.isPresetModified(preset_index))
	{
		return;
	}

	deleteAutoSavedPreset(model.getPreset(preset_index).name);
	setPreset(preset_index);
}

void FxController::resetPresets()
{
	auto& model = FxModel::getModel();

	setNumEqBands(DEFAULT_NUM_EQ_BANDS);
	setNormalization(DEFAULT_NORMALIZATION);
	setVolumeLeveling(DEFAULT_VOLUME_LEVELING);
	setAutoEqEnabled(DEFAULT_AUTO_EQ_ENABLED);
	setAutoEqRange(DEFAULT_AUTO_EQ_RANGE);
	setBalance(DEFAULT_BALANCE);
	setFilterQ(DEFAULT_FILTER_Q);
	setMasterGain(DEFAULT_MASTER_GAIN);

	auto count = model.getPresetCount();
	for (auto i=0; i<count; i++)
	{
		auto preset = model.getPreset(i);
		if (preset.modified)
		{
			deleteAutoSavedPreset(preset.name);
		}

		if (preset.type == FxModel::PresetType::UserPreset)
		{
			wchar_t path[MAX_PATH] = {};

			wcscpy_s(path, preset.path.toWideCharPointer());
			if (preset.path.length() + 1 < MAX_PATH)
			{
				path[preset.path.length() + 1] = L'\0';
			}

			SHFILEOPSTRUCT file_op = { NULL, FO_DELETE, path, L"",
									   FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT, FALSE, 0, L"" };
			SHFileOperation(&file_op);
		}
	}
	
	finalizePresetMutation();

	FxModel::getModel().pushMessage(TRANS("Presets are restored to factory defaults"));
}

bool FxController::exportPresets(const Array< FxModel::Preset>& presets)
{
    auto path_name = File::addTrailingSeparator(File::getSpecialLocation(File::SpecialLocationType::userDocumentsDirectory).getFullPathName()) + L"FxSound\\Presets\\Export\\";

    File path(path_name);
    if (!path.exists())
    {
        path.createDirectory();
    }

    bool exported = false;

    for (auto preset : presets)
    {
        bool skip = false;

        auto preset_file = File(path_name + preset.name + ".fac");
        if (preset_file.exists())
        {
            if (!FxConfirmationMessage::showMessage(String::formatted(TRANS("Preset file %s already exists in the export path, do you want to overwrite the preset file?"), preset.name.toWideCharPointer())))
            {
                skip = true;
            }
        }

        if (!skip)
        {
            dfx_dsp_.exportPreset(preset.path.toWideCharPointer(), preset.name.toWideCharPointer(), path.getFullPathName().toWideCharPointer());
            exported = true;
        }
    }
    
    return exported;
}

bool FxController::importPresets(const Array<File>& preset_files, StringArray& imported_presets, StringArray& skipped_presets)
{
    auto path_name = File::addTrailingSeparator(File::getSpecialLocation(File::SpecialLocationType::userApplicationDataDirectory).getFullPathName()) + L"FxSound\\Presets";
    File path(path_name);
    if (!path.exists())
    {
        path.createDirectory();
    }

    auto& model = FxModel::getModel();

    for (auto preset_file : preset_files)
    {
        auto preset_info = dfx_dsp_.getPresetInfo(preset_file.getFullPathName().toWideCharPointer());
        if (model.isPresetNameValid(preset_info.name.c_str()))
        {
            File import_file(path_name + "\\" + preset_info.name.c_str() + ".fac");
            if (preset_file.copyFileTo(import_file))
            {
                imported_presets.add(preset_info.name.c_str());
            }
        }
        else
        {
            skipped_presets.add(preset_info.name.c_str());
        }
    }

    if (!imported_presets.isEmpty())
    {
        initPresets();

        auto preset_name = settings_.getString("preset");
        auto selected_preset = findPresetIndexByName(FxModel::getModel(), preset_name);
        setPreset(selected_preset);

        return true;
    }

    return false;
}

void FxController::initOutputs(const std::vector<SoundDevice>& sound_devices)
{
	auto device_configs = DeviceConfig::loadDeviceConfigs(settings_, "device_configs");
	if (device_configs.size() == 0)
	{
        // First time running the app, initialize device configs with sound devices
		DeviceConfig::initDeviceConfigs(settings_, sound_devices);
	}
	else
	{
        // Update device configs with when relaunching the app
		DeviceConfig::updateDeviceConfigs(settings_, sound_devices);
	}

	dfx_enabled_ = FxSound::OutputDeviceSelection::scanProcessingOutputs(sound_devices).dfx_enabled;

	rebuildOutputDeviceList(sound_devices, true);
	auto output_resolution = buildOutputResolutionContext(
		settings_,
		FxModel::getModel().getSelectedOutput(),
		getOutputName());
	auto init_decision = FxSound::OutputDeviceSelection::buildInitDecision(
		sound_devices,
		active_output_devices_,
		output_resolution);

	FxModel::getModel().initOutputs(active_output_devices_);
	if (init_decision.has_resolved_output)
	{
		auto& default_output = init_decision.resolved_output;
		applySelectedOutput(default_output);

		if (init_decision.should_apply_output)
		{
			setOutput(default_output.pwszID.c_str());
		}
		else if (init_decision.should_mute)
		{
			notifyPlaybackUnavailable(*audio_passthru_, playback_device_available_, FxModel::getModel());
		}
	}
}

void FxController::rebuildOutputDeviceList(const std::vector<SoundDevice>& sound_devices, bool include_selected_inactive)
{
	active_output_devices_.clear();
	active_output_devices_ = FxSound::OutputDeviceSelection::buildVisibleOutputDevices(
		sound_devices,
		FxModel::getModel().getSelectedOutput(),
		loadOutputPriorities(settings_),
		include_selected_inactive);
}

void FxController::updateOutputs(const std::vector<SoundDevice>& sound_devices)
{
	DeviceConfig::updateDeviceConfigs(settings_, sound_devices);
	auto processing_snapshot = FxSound::OutputDeviceSelection::scanProcessingOutputs(sound_devices);
	dfx_enabled_ = processing_snapshot.dfx_enabled;

	rebuildOutputDeviceList(sound_devices);
	FxModel::getModel().initOutputs(active_output_devices_);
	auto output_resolution = buildOutputResolutionContext(
		settings_,
		FxModel::getModel().getSelectedOutput(),
		getOutputName());
	auto sync_decision = FxSound::OutputDeviceSelection::buildSyncDecision(
		active_output_devices_,
		output_resolution,
		isTimerRunning());

	if (sync_decision.has_resolved_output)
	{
		auto& model = FxModel::getModel();
		auto synced_output = sync_decision.resolved_output;

		applySelectedOutput(synced_output, false, sync_decision.output_changed);

		if (applyRoutingActions(synced_output, sync_decision.routing_actions))
		{
			output_changed_ = true;
		}

		if (sync_decision.should_mute)
		{
			notifyPlaybackUnavailable(*audio_passthru_, playback_device_available_, model);
		}
		else
		{
			tryApplyAutoPresetForCurrentOutput(sync_decision.output_changed || sync_decision.name_changed);
		}
	}
}

// Handled when FxSound processing is on
void FxController::selectProcessingOutput(const std::vector<SoundDevice>& sound_devices)
{
	auto available = audio_passthru_->isPlaybackDeviceAvailable();
	if (available != playback_device_available_)
	{
		playback_device_available_ = available;
		FxModel::getModel().notifyOutputError();
	}

	updateOutputs(sound_devices);
	device_count_ = (uint32_t)sound_devices.size();

	if (!dfx_enabled_)
	{
		stopTimer();
		main_window_->removeFromDesktop();
		FxDeviceErrorMessage error_message;
		error_message.runModalLoop();
		JUCEApplication::getInstance()->systemRequestedQuit();
		return;
	}
}

// Handled when FxSound processing is off
void FxController::syncOutputWithSystemDefault(const std::vector<SoundDevice>& sound_devices)
{
	rebuildOutputDeviceList(sound_devices);

	// Update the configuration as per the change in active devices and update the output device list in UI
	if (sound_devices.size() != device_count_)
	{
		DeviceConfig::updateDeviceConfigs(settings_, sound_devices);

		FxModel::getModel().initOutputs(active_output_devices_);

		device_count_ = (uint32_t)sound_devices.size();
	}

	auto& model = FxModel::getModel();
	auto output_resolution = buildOutputResolutionContext(
		settings_,
		model.getSelectedOutput(),
		getOutputName());
	auto idle_sync_decision = FxSound::OutputDeviceSelection::buildIdleSyncDecision(
		active_output_devices_,
		output_resolution);

	if (idle_sync_decision.has_resolved_output)
	{
		auto& synced_output = idle_sync_decision.resolved_output;
		applySelectedOutput(synced_output);

		if (idle_sync_decision.should_notify_error)
		{
			notifyPlaybackUnavailable(*audio_passthru_, playback_device_available_, model, false);
			return;
		}

		tryApplyAutoPresetForCurrentOutput(true);
	}
}

void FxController::applySelectedOutput(const SoundDevice& sound_device, bool notify, bool output_changed)
{
	setOutputName(sound_device.deviceFriendlyName.c_str());
	FxModel::getModel().setSelectedOutput(sound_device, notify || output_changed);
	saveSelectedOutputToSettings(sound_device);
}

bool FxController::applyRoutingActions(const SoundDevice& sound_device,
	const FxSound::OutputDeviceSelection::OutputRoutingActions& actions)
{
	auto routing_changed = false;

	if (actions.should_retarget_playback)
	{
		audio_passthru_->setAsPlaybackDevice(sound_device);
		routing_changed = true;
	}

	if (actions.should_restart_processing)
	{
		audio_passthru_->restartProcessingForDeviceChange();
		routing_changed = true;
	}

	if (actions.should_begin_grace_period)
	{
		beginAudioProcessingGracePeriod();
	}

	return routing_changed;
}

String FxController::tryApplyAutoPresetForCurrentOutput(bool trigger_change)
{
	auto& model = FxModel::getModel();
	auto device_config = DeviceConfig::getDeviceConfig(settings_, model.getSelectedOutput());
	auto auto_preset_decision = FxSound::OutputDeviceSelection::buildAutoPresetDecision(
		model.isPresetModified(),
		trigger_change,
		device_config.preset.toWideCharPointer(),
		model.getPowerState());
	if (!auto_preset_decision.should_apply)
	{
		return {};
	}

	auto selected_preset = findPresetIndexByName(model, auto_preset_decision.preset_name.c_str());
	if (!setPreset(selected_preset, false))
	{
		return {};
	}

	return String(auto_preset_decision.preset_name.c_str());
}

bool FxController::restoreConfiguredPresetForCurrentOutput()
{
	auto& model = FxModel::getModel();
	auto device_config = DeviceConfig::getDeviceConfig(settings_, model.getSelectedOutput());
	std::vector<std::wstring> preset_names;
	preset_names.reserve(static_cast<size_t>(model.getPresetCount()));
	for (int index = 0; index < model.getPresetCount(); ++index)
	{
		preset_names.push_back(model.getPreset(index).name.toWideCharPointer());
	}

	auto decision = FxSound::OutputDeviceSelection::buildConfiguredPresetRestoreDecision(
		device_config.preset.toWideCharPointer(),
		preset_names);

	if (decision.should_clear_stale_configured_preset)
	{
		auto device_configs = getDeviceConfigs();
		for (auto& config : device_configs)
		{
			if (matchesConfiguredOutput(config, model.getSelectedOutput()))
			{
				config.preset = {};
				break;
			}
		}

		saveDeviceConfigs(device_configs);
	}

	if (!decision.should_apply)
	{
		return false;
	}

	return setPreset(decision.preset_index, false);
}

void FxController::finalizePresetMutation()
{
	auto& model = FxModel::getModel();
	model.setPresetModified(false);
	initPresets();

	if (!restoreConfiguredPresetForCurrentOutput())
	{
		setPreset(0);
	}
}

void FxController::powerOn(bool on)
{
	if (on)
	{
		dfx_dsp_.powerOn(true);

		if (!isTimerRunning())
		{
			startTimer(100);
		}
	}
	else
	{
		dfx_dsp_.powerOn(false);

		if (isTimerRunning())
		{
			stopTimer();
		}

		audio_passthru_->restoreDefaultPlaybackDevice();
	}
}

float FxController::getEffectValue(FxEffects::EffectType effect)
{
	return dfx_dsp_.getEffectValue(static_cast<DfxDsp::Effect>(effect));
}

void FxController::setEffectValue(FxEffects::EffectType effect, float value)
{
	dfx_dsp_.setEffectValue(static_cast<DfxDsp::Effect>(effect), value);

	auto& model = FxModel::getModel();
	if (!model.isPresetModified())
	{
		model.setPresetModified(true);
	}

	preset_dirty_ = true;
	auto_save_counter_ = 0;
}

int FxController::getNumEqBands()
{
	return dfx_dsp_.getNumEqBands();
}

void FxController::setNumEqBands(int num_bands)
{
	dfx_dsp_.setNumBands(num_bands);
	settings_.setInt("num_bands", num_bands);
}

float FxController::getNormalization()
{
	return dfx_dsp_.getNormalization();
}

void FxController::setNormalization(float normalization_db)
{
	dfx_dsp_.setNormalization(normalization_db);
	settings_.setDouble("normalization", normalization_db);
}

float FxController::getVolumeLeveling()
{
	return dfx_dsp_.getVolumeLeveling();
}

void FxController::setVolumeLeveling(float gain_db)
{
	dfx_dsp_.setVolumeLeveling(gain_db);
	settings_.setDouble("volume_leveling", gain_db);
}

bool FxController::isAutoEqEnabled()
{
	return dfx_dsp_.getAutoEqEnabled();
}

void FxController::setAutoEqEnabled(bool enabled)
{
	dfx_dsp_.setAutoEqEnabled(enabled);
	settings_.setBool("auto_eq_enabled", enabled);
}

float FxController::getAutoEqRange()
{
	return dfx_dsp_.getAutoEqRange();
}

void FxController::setAutoEqRange(float range_db)
{
	dfx_dsp_.setAutoEqRange(range_db);
	settings_.setDouble("auto_eq_range", range_db);
}

void FxController::disableAutoEqPreservingCurrentEq()
{
	dfx_dsp_.disableAutoEqPreservingCurrentEq();
	settings_.setBool("auto_eq_enabled", false);
}

bool FxController::prepareForManualEqEdit(FxSound::AutoEqPolicy::Change change)
{
	if (!isAutoEqEnabled() || !FxSound::AutoEqPolicy::shouldDisablePreservingCurrentEq(change))
		return false;

	disableAutoEqPreservingCurrentEq();
	return true;
}

float FxController::getBalance()
{
	return dfx_dsp_.getBalance();
}

void FxController::setBalance(float balance_db)
{
	dfx_dsp_.setBalance(balance_db);
	settings_.setDouble("balance", balance_db);
}

float FxController::getMasterGain()
{
	return dfx_dsp_.getMasterGain();
}

void FxController::setMasterGain(float gain_db)
{
	dfx_dsp_.setMasterGain(gain_db);
	settings_.setDouble("master_gain", gain_db);
}

float FxController::getFilterQ()
{
	return dfx_dsp_.getFilterQ();
}

void FxController::setFilterQ(float q_multiplier)
{
	dfx_dsp_.setFilterQ(q_multiplier);
	settings_.setDouble("filter_q", q_multiplier);
}

bool FxController::isAudioProcessing()
{
    return audio_process_on_;
}

void FxController::getEqBandState(std::vector<float>& center_frequencies, std::vector<float>& band_boosts)
{
    auto num_bands = getNumEqBands();
    center_frequencies.resize(num_bands);
    band_boosts.resize(num_bands);
    dfx_dsp_.getEqBandState(center_frequencies.data(), band_boosts.data(), num_bands);
}

void FxController::beginAudioProcessingGracePeriod()
{
	audio_signal_counters_ = {};
	audio_process_grace_deadline_ms_ = Time::currentTimeMillis() + 2000;
}

bool FxController::isAudioProcessingGracePeriodActive() const
{
	return audio_process_grace_deadline_ms_ > 0 && Time::currentTimeMillis() < audio_process_grace_deadline_ms_;
}

float FxController::getEqBandFrequency(int band_num)
{
	return dfx_dsp_.getEqBandFrequency(band_num);
}

void FxController::setEqBandFrequency(int band_num, float freq)
{
    dfx_dsp_.setEqBandFrequency(band_num, freq);

    auto& model = FxModel::getModel();
    if (!model.isPresetModified())
    {
        model.setPresetModified(true);
    }

	preset_dirty_ = true;
	auto_save_counter_ = 0;
}

void FxController::getEqBandFrequencyRange(int band_num, float* min_freq, float* max_freq)
{
    dfx_dsp_.getEqBandFrequencyRange(band_num, min_freq, max_freq);
}

float FxController::getEqBandBoostCut(int band_num)
{
	return dfx_dsp_.getEqBandBoostCut(band_num);
}

void FxController::setEqBandBoostCut(int band_num, float boost)
{
	dfx_dsp_.setEqBandBoostCut(band_num, boost);

	auto& model = FxModel::getModel();
	if (!model.isPresetModified())
	{
		model.setPresetModified(true);
	}

	preset_dirty_ = true;
	auto_save_counter_ = 0;
}

LRESULT CALLBACK FxController::eventCallback(HWND hwnd, const UINT message, const WPARAM w_param, const LPARAM l_param)
{
	FxController* controller = (FxController*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

	if (controller == nullptr)
	{
		return DefWindowProc(hwnd, message, w_param, l_param);
	}

	switch (message)
	{
		case WMAPP_SOUND_DEVICE_CHANGE:
		{
			controller->device_change_message_pending_ = false;
			controller->handleSoundDeviceChange();
		}
		break;

		case WM_HOTKEY:
		{
			if (w_param == CMD_ON_OFF)
			{
				auto power_state = !FxModel::getModel().getPowerState();
				controller->setPowerState(power_state);

                String param = FxModel::getModel().getPowerState() ? TRANS(L"on") : TRANS(L"off");
                FxModel::getModel().pushMessage(controller->FormatString(TRANS("FxSound is %s."), param));
			}
			if (w_param == CMD_OPEN_CLOSE)
			{
				if (controller->main_window_->isOnDesktop())
				{
					controller->hideMainWindow();
				}
				else
				{
					controller->showMainWindow();
				}
			}
			if (w_param == CMD_NEXT_PRESET && FxModel::getModel().getPowerState())
			{
				auto preset_index = FxModel::getModel().getSelectedPreset();
				auto preset_count = FxModel::getModel().getPresetCount();
				if (preset_count > 1)
				{
					if (preset_index < preset_count - 1)
					{
						preset_index++;
					}
					else
					{
						preset_index = 0;
					}

					controller->setPreset(preset_index);
				}
			}
			if (w_param == CMD_PREVIOUS_PRESET && FxModel::getModel().getPowerState())
			{
				auto preset_index = FxModel::getModel().getSelectedPreset();
				auto preset_count = FxModel::getModel().getPresetCount();
				if (preset_count > 1)
				{
					if (preset_index != 0)
					{
						preset_index--;
					}
					else
					{
						preset_index = preset_count - 1;
					}

					controller->setPreset(preset_index);
				}
			}
			if (w_param == CMD_NEXT_OUTPUT && FxModel::getModel().getPowerState())
			{
				auto output_index = 0;
				for (auto& output_device : controller->active_output_devices_)
				{
					if (FxModel::getModel().getSelectedOutput().pwszID == output_device.pwszID)
					{
						break;
					}
					output_index++;
				}
				int count = 0;
				while (count < controller->active_output_devices_.size())
				{
					count++;
					if (output_index < controller->active_output_devices_.size() - 1)
					{
						output_index++;
					}
					else
					{
						output_index = 0;
					}

					if (controller->active_output_devices_[output_index].deviceNumChannel >= 2 &&
						controller->active_output_devices_[output_index].isActive)
					{
						controller->setOutput(output_index);
						break;
					}
				}
			}
		}
		break;

		case WM_WTSSESSION_CHANGE:
		{
			auto session_reconnect =
				w_param == WTS_CONSOLE_CONNECT ||
				w_param == WTS_SESSION_LOGON ||
				w_param == WTS_SESSION_UNLOCK ||
				w_param == WTS_SESSION_DESKTOP_READY;

			if (session_reconnect)
			{
				if (controller->system_tray_view_ != nullptr)
				{
					controller->tray_icon_recovery_pending_ = !controller->system_tray_view_->restoreIconRegistration(
						FxModel::getModel().getPowerState(),
						controller->isAudioProcessing());
				}
				else
				{
					controller->tray_icon_recovery_pending_ = false;
				}

				controller->setPowerState(FxModel::getModel().getPowerState());
			}
			else if (w_param == WTS_CONSOLE_DISCONNECT || w_param == WTS_SESSION_LOCK)
			{
				if (controller->system_tray_view_ != nullptr)
				{
					controller->system_tray_view_->clearIconRegistration();
				}

				controller->tray_icon_recovery_pending_ = true;
				controller->powerOn(false);
			}
		}
		break;

		case WMAPP_SHOW_MAIN_WINDOW:
		{
			controller->showMainWindow();
		}
		break;
	}

	return DefWindowProc(hwnd, message, w_param, l_param);
}

void FxController::timerCallback()
{
	if (output_changed_)
	{
		output_changed_ = false;
		Thread::sleep(200);
		return;
	}

	if (tray_icon_recovery_pending_ && system_tray_view_ != nullptr)
	{
		if (++tray_icon_health_check_counter_ >= 10)
		{
			tray_icon_health_check_counter_ = 0;

			if (session_id_ == WTSGetActiveConsoleSessionId())
			{
				tray_icon_recovery_pending_ = !system_tray_view_->ensureIconRegistration(
					FxModel::getModel().getPowerState(),
					audio_process_on_);
			}
		}
	}
	else
	{
		tray_icon_health_check_counter_ = 0;
	}

	const auto snapshot = createAudioPipelineSnapshot(audio_passthru_->processTimer());
	updateAudioSignalCounters(snapshot);
	maybeRecoverAudioPassthru(snapshot);
	syncAudioProcessingState(snapshot);

	static constexpr int AUTO_SAVE_INTERVAL = 600;
	if (++auto_save_counter_ >= AUTO_SAVE_INTERVAL)
	{
		if (preset_dirty_)
		{
			autoSavePreset(FxModel::getModel().getSelectedPreset());
		}

		auto_save_counter_ = 0;
	}

	auto current_time = Time::getCurrentTime();
	if (auto_updates_ && current_time.getHours() == 10 && current_time.getMinutes() == 00 && current_time.getSeconds() == 0)
	{
		checkUpdates();
	}
}

FxController::AudioPipelineSnapshot FxController::createAudioPipelineSnapshot(int process_timer_result) const
{
	AudioPipelineSnapshot snapshot;
	snapshot.process_timer_result = process_timer_result;

	if (audio_passthru_ == nullptr)
	{
		return snapshot;
	}

	snapshot.now_ms = static_cast<int64>(GetTickCount64());
	snapshot.last_capture_tick_ms = static_cast<int64>(audio_passthru_->getLastCaptureWithSamplesTickMs());
	snapshot.last_playback_tick_ms = static_cast<int64>(audio_passthru_->getLastSuccessfulPlaybackTickMs());
	snapshot.capture_age_ms = snapshot.last_capture_tick_ms > 0 ? snapshot.now_ms - snapshot.last_capture_tick_ms : -1;
	snapshot.playback_age_ms = snapshot.last_playback_tick_ms > 0 ? snapshot.now_ms - snapshot.last_playback_tick_ms : -1;
	snapshot.processing_thread_running = audio_passthru_->isProcessingThreadRunning();
	snapshot.muted = audio_passthru_->isMuted();
	snapshot.capture_input_rms_db = audio_passthru_->getLastCaptureInputRmsDb();
	snapshot.submitted_playback_rms_db = audio_passthru_->getLastSubmittedPlaybackRmsDb();
	snapshot.capture_recent = snapshot.capture_age_ms >= 0 && snapshot.capture_age_ms <= 500;
	snapshot.playback_recent = snapshot.playback_age_ms >= 0 && snapshot.playback_age_ms <= 500;
	snapshot.audio_signal_present = FxSound::AudioSignalPolicy::isCaptureSignalPresent(
		snapshot.now_ms,
		snapshot.last_capture_tick_ms,
		snapshot.capture_input_rms_db);

	auto sound_devices = audio_passthru_->getSoundDevices(false);
	auto selected_output = FxModel::getModel().getSelectedOutput();
	auto selected_output_it = std::find_if(sound_devices.begin(), sound_devices.end(),
		[&selected_output](const SoundDevice& sound_device)
		{
			return FxSound::OutputDeviceSelection::areSameOutputDevice(selected_output, sound_device);
		});
	if (selected_output_it != sound_devices.end())
	{
		snapshot.selected_output_active = selected_output_it->isActive;
	}

	return snapshot;
}

void FxController::updateAudioSignalCounters(const AudioPipelineSnapshot& snapshot)
{
	const auto signal_counters = FxSound::AudioSignalPolicy::advanceSignalCounters(
		snapshot.audio_signal_present,
		isAudioProcessingGracePeriodActive(),
		audio_signal_counters_.present,
		audio_signal_counters_.absent);
	audio_signal_counters_.present = signal_counters.signal_present_after;
	audio_signal_counters_.absent = signal_counters.signal_absent_after;
}

void FxController::syncAudioProcessingState(const AudioPipelineSnapshot& snapshot)
{
	auto power = FxModel::getModel().getPowerState();
	if (FxSound::AudioSignalPolicy::shouldEnableDsp(audio_signal_counters_.present, audio_process_on_))
	{
		audio_process_on_ = true;
		audio_passthru_->setDspProcessingEnabled(true);
		logAudioPipelineMessage("Audio DSP processing resumed after signal detection");
		system_tray_view_->setStatus(power, true);
		main_window_->setIcon(power, true);
		main_window_->startLogoAnimation();
        if (view_ == ViewType::Pro)
        {
            main_window_->showProView();
			main_window_->startVisualizer();
        }
	}
	if (FxSound::AudioSignalPolicy::shouldDisableDsp(audio_signal_counters_.absent, audio_process_on_))
	{
		logAudioPipelineSnapshot("audio_processing_stopped", snapshot);
		audio_process_on_ = false;
		audio_passthru_->setDspProcessingEnabled(false);
		logAudioPipelineMessage("Audio DSP processing paused while signal is absent");
		system_tray_view_->setStatus(power, false);
		main_window_->setIcon(power, false);
		main_window_->stopLogoAnimation();
        if (view_ == ViewType::Pro)
        {
            main_window_->showProView();
			main_window_->pauseVisualizer();
        }
	}
}

void FxController::logAudioPipelineSnapshot(const String& reason, const AudioPipelineSnapshot& snapshot)
{
	String message = "Audio pipeline snapshot reason=" + reason
		+ " process_timer_result=" + String(snapshot.process_timer_result)
		+ " power=" + String(FxModel::getModel().getPowerState() ? 1 : 0)
		+ " playback_available=" + String(playback_device_available_ ? 1 : 0)
		+ " processing_thread_running=" + String(snapshot.processing_thread_running ? 1 : 0)
		+ " muted=" + String(snapshot.muted ? 1 : 0)
		+ " selected_output_active=" + String(snapshot.selected_output_active ? 1 : 0)
		+ " capture_age_ms=" + String(snapshot.capture_age_ms)
		+ " playback_age_ms=" + String(snapshot.playback_age_ms)
		+ " capture_recent=" + String(snapshot.capture_recent ? 1 : 0)
		+ " playback_recent=" + String(snapshot.playback_recent ? 1 : 0)
		+ " capture_input_rms_db=" + String(snapshot.capture_input_rms_db, 2)
		+ " submitted_playback_rms_db=" + String(snapshot.submitted_playback_rms_db, 2)
		+ " audio_signal_present=" + String(snapshot.audio_signal_present ? 1 : 0)
		+ " signal_present_counter=" + String(audio_signal_counters_.present)
		+ " signal_absent_counter=" + String(audio_signal_counters_.absent)
		+ " grace_active=" + String(isAudioProcessingGracePeriodActive() ? 1 : 0);
	logAudioPipelineMessage(message);
}

void FxController::logAudioPipelineMessage(const String& message)
{
	auto now = Time::getCurrentTime().formatted("%Y-%m-%d %H:%M:%S");
	logMessage("[" + now + "] " + message);
}

void FxController::maybeRecoverAudioPassthru(const AudioPipelineSnapshot& snapshot)
{
	if (audio_passthru_ == nullptr || shutting_down_)
	{
		audio_recovery_state_.error_counter = 0;
		audio_recovery_state_.stall_logged = false;
		return;
	}

	auto& model = FxModel::getModel();
	if (!model.getPowerState() || isAudioProcessingGracePeriodActive())
	{
		audio_recovery_state_.error_counter = 0;
		audio_recovery_state_.stall_logged = false;
		return;
	}

	const auto stall_detected = FxSound::AudioSignalPolicy::shouldDetectPlaybackStall(
		snapshot.process_timer_result,
		playback_device_available_,
		snapshot.now_ms,
		snapshot.last_capture_tick_ms,
		snapshot.last_playback_tick_ms,
		snapshot.audio_signal_present);

	if (snapshot.process_timer_result == 0 && !stall_detected)
	{
		audio_recovery_state_.error_counter = 0;
		audio_recovery_state_.stall_logged = false;
		return;
	}

	if (stall_detected && !audio_recovery_state_.stall_logged)
	{
		logAudioPipelineMessage("Audio pipeline stall detected with live capture and no render progress");
		logAudioPipelineSnapshot("stall_detected", snapshot);
		audio_recovery_state_.stall_logged = true;
	}

	if (snapshot.now_ms < audio_recovery_state_.recovery_deadline_ms)
	{
		return;
	}

	if (!stall_detected && ++audio_recovery_state_.error_counter < 5)
	{
		return;
	}

	auto sound_devices = audio_passthru_->getSoundDevices(false);
	auto selected_output = model.getSelectedOutput();
	auto selected_output_it = std::find_if(sound_devices.begin(), sound_devices.end(),
		[&selected_output](const SoundDevice& sound_device)
		{
			return FxSound::OutputDeviceSelection::areSameOutputDevice(selected_output, sound_device);
		});

	if (selected_output_it == sound_devices.end() || !selected_output_it->isActive)
	{
		audio_recovery_state_.error_counter = 0;
		audio_recovery_state_.stall_logged = false;
		return;
	}

	logAudioPipelineMessage(stall_detected
		? "Audio passthru watchdog restarting after stalled output with live capture"
		: "Audio passthru watchdog restarting after repeated timer failures");
	logAudioPipelineSnapshot(stall_detected ? "watchdog_restart_stall" : "watchdog_restart_failure", snapshot);
	beginAudioProcessingGracePeriod();
	auto recovered = audio_passthru_->restartProcessingForDeviceChange();
	playback_device_available_ = audio_passthru_->isPlaybackDeviceAvailable();
	audio_passthru_->mute(!playback_device_available_);
	audio_recovery_state_.error_counter = 0;
	audio_recovery_state_.stall_logged = false;
	audio_recovery_state_.recovery_deadline_ms = snapshot.now_ms + 5000;

	if (recovered)
	{
		logAudioPipelineMessage("Audio passthru watchdog restart completed");
	}
	else
	{
		logAudioPipelineMessage("Audio passthru watchdog restart failed");
	}
}

void FxController::onAudioPassthruDiagnostic(const std::wstring& message)
{
	auto now = Time::getCurrentTime().formatted("%Y-%m-%d %H:%M:%S");
	logMessage("[" + now + "] AudioPassthru: " + String(message.c_str()));
}

void FxController::onSoundDeviceChange(AudioDeviceChangeKind change_kind, const std::wstring& device_id)
{
	if (shutting_down_)
		return;

	{
		ScopedLock auto_lock(lock_);
		pending_device_change_kind_ = change_kind;
		pending_device_change_id_ = String(device_id.c_str());
	}

	bool expected = false;
	if (!device_change_message_pending_.compare_exchange_strong(expected, true))
	{
		return;
	}

	if (!PostMessage(message_window_.getHandle(), WMAPP_SOUND_DEVICE_CHANGE, 0, 0))
	{
		device_change_message_pending_ = false;
	}
}

void FxController::handleSoundDeviceChange()
{
	if (shutting_down_)
		return;

	if (session_id_ != WTSGetActiveConsoleSessionId())
		return;   // another user is the active console session - do nothing

	ScopedLock auto_lock(lock_);

	if (shutting_down_ || audio_passthru_ == nullptr)
		return;

	auto pending_change_kind = pending_device_change_kind_;
	auto pending_change_id = pending_device_change_id_;
	pending_device_change_kind_ = AudioDeviceChangeKind::Unknown;
	pending_device_change_id_.clear();

	auto current_sound_devices = audio_passthru_->getSoundDevices(false);
	if (FxSound::OutputDeviceSelection::shouldIgnoreDeviceChange(
		pending_change_kind,
		pending_change_id.toWideCharPointer(),
		FxModel::getModel().getSelectedOutput(),
		current_sound_devices))
	{
		updateOutputs(current_sound_devices);
		return;
	}

	beginAudioProcessingGracePeriod();
	audio_passthru_->restartProcessingForDeviceChange();

	auto sound_devices = audio_passthru_->getSoundDevices(false);
	if (isTimerRunning())
	{
        selectProcessingOutput(sound_devices);
	}
	else
	{
        syncOutputWithSystemDefault(sound_devices);
	}

	if (FxModel::getModel().getPowerState())
	{
		powerOn(true);
		audio_passthru_->mute(false);
	}

	auto refreshed_selected_output = FxModel::getModel().getSelectedOutput();
	auto refreshed_selected_output_it = std::find_if(sound_devices.begin(), sound_devices.end(),
		[&refreshed_selected_output](const SoundDevice& sound_device)
		{
			return FxSound::OutputDeviceSelection::areSameOutputDevice(refreshed_selected_output, sound_device);
		});

	if (refreshed_selected_output_it == sound_devices.end() || !refreshed_selected_output_it->isActive)
	{
		notifyPlaybackUnavailable(*audio_passthru_, playback_device_available_, FxModel::getModel());
		system_tray_view_->setStatus(FxModel::getModel().getPowerState(), false);
	}
}

void FxController::enableHotkeys(bool enable)
{
	settings_.setBool("hotkeys", enable);
	FxModel::getModel().setHotkeySupport(enable);
	if (enable)
	{
		registerHotkeys();
	}
	else
	{
		unregisterHotkeys();
	}
}

bool FxController::getHotkey(String cmdKey, int& mod, int& vk)
{
	int cmd = settings_.getInt(cmdKey);
	mod = (cmd >> 16) & 0x7;
	vk = cmd & 0xff;
	if ((mod == (MOD_CONTROL|MOD_ALT) || mod == (MOD_CONTROL|MOD_SHIFT)) && (vk >= 0x30 && vk <= 0x39) || (vk >= 'A' && vk <= 'Z'))
	{
		return true;
	}

	return false;
}

bool FxController::setHotkey(const String& command, int new_mod, int new_vk)
{
	StringArray hotkey_cmds = { HK_CMD_ON_OFF, HK_CMD_OPEN_CLOSE, HK_CMD_NEXT_PRESET, HK_CMD_PREVIOUS_PRESET, HK_CMD_NEXT_OUTPUT };

	for (int i = 0; i < hotkey_cmds.size(); i++)
	{
		if (command == hotkey_cmds[i])
		{
			continue;
		}

		int mod, vk;
		if (getHotkey(hotkey_cmds[i], mod, vk))
		{
			if (mod == new_mod && vk == new_vk)
			{
				return false;
			}
		}
	}

	unsigned int code = (new_mod << 16) | new_vk;
	settings_.setInt(command, code);

	if (command == HK_CMD_ON_OFF)
	{
		::UnregisterHotKey(message_window_.getHandle(), CMD_ON_OFF);
		if (isValidHotkey(new_mod, new_vk))
		{
			::RegisterHotKey(message_window_.getHandle(), CMD_ON_OFF, new_mod, new_vk);
		}		
		return true;
	}

	if (command == HK_CMD_OPEN_CLOSE)
	{
		::UnregisterHotKey(message_window_.getHandle(), CMD_OPEN_CLOSE);
		if (isValidHotkey(new_mod, new_vk))
		{
			::RegisterHotKey(message_window_.getHandle(), CMD_OPEN_CLOSE, new_mod, new_vk);
		}		
		return true;
	}

	if (command == HK_CMD_NEXT_PRESET)
	{
		::UnregisterHotKey(message_window_.getHandle(), CMD_NEXT_PRESET);
		if (isValidHotkey(new_mod, new_vk))
		{
			::RegisterHotKey(message_window_.getHandle(), CMD_NEXT_PRESET, new_mod, new_vk);
		}		
		return true;
	}

	if (command == HK_CMD_PREVIOUS_PRESET)
	{
		::UnregisterHotKey(message_window_.getHandle(), CMD_PREVIOUS_PRESET);
		if (isValidHotkey(new_mod, new_vk))
		{
			::RegisterHotKey(message_window_.getHandle(), CMD_PREVIOUS_PRESET, new_mod, new_vk);
		}		
		return true;
	}

	if (command == HK_CMD_NEXT_OUTPUT)
	{
		::UnregisterHotKey(message_window_.getHandle(), CMD_NEXT_OUTPUT);
		if (isValidHotkey(new_mod, new_vk))
		{
			::RegisterHotKey(message_window_.getHandle(), CMD_NEXT_OUTPUT, new_mod, new_vk);
		}		
		return true;
	}

	return false;
}

bool FxController::isValidHotkey(int mod, int vk)
{
	if ((mod & MOD_CONTROL) == 0)
	{
		return false;
	}
		
	HKL hkl = GetKeyboardLayout(0);
	BYTE kbd_state[256] = { 0 };
	kbd_state[VK_CONTROL] = 0x80;

	if ((mod & MOD_ALT) != 0)
	{
		kbd_state[VK_MENU] = 0x80;
	}
	if ((mod & MOD_SHIFT) != 0)
	{
		kbd_state[VK_SHIFT] = 0x80;
	}
	
	WCHAR output[3] = { 0 };

	int len = ToUnicodeEx(vk, 0, kbd_state, output, 3, 0, hkl);
	if (len > 0 && output[0] >= 0x20) // The hotkey combination generates a non-control character
	{
		return false;
	}

	return true;
}

bool FxController::isHelpTooltipsHidden()
{
    return hide_help_tooltips_;
}

void FxController::setHelpTooltipsHidden(bool status)
{
    hide_help_tooltips_ = status;
    settings_.setBool("hide_help_tooltips", status);
    main_window_->repaint();
}

bool FxController::isNotificationsHidden()
{
	return hide_notifications_;
}

void FxController::setNotificationsHidden(bool status)
{
	hide_notifications_ = status;
	settings_.setBool("hide_notifications", status);
}

String FxController::getLanguage() const
{
    return language_;
}

void FxController::setLanguage(String language_code)
{
	if (language_code.isEmpty())
	{
		language_code = "en";
	}

    language_ = language_code;
    settings_.setString("language", language_);

    LocalisedStrings::setCurrentMappings(nullptr);

    if (language_.startsWithIgnoreCase("ko"))
    {
        LocalisedStrings::setCurrentMappings(new LocalisedStrings(String::createStringFromData(BinaryData::FxSound_ko_txt, BinaryData::FxSound_ko_txtSize), false));
    }
    else if (language_.startsWithIgnoreCase("vi"))
    {
        LocalisedStrings::setCurrentMappings(new LocalisedStrings(String::createStringFromData(BinaryData::FxSound_vi_txt, BinaryData::FxSound_vi_txtSize), false));
    }
    else if (language_.startsWithIgnoreCase("id"))
    {
        LocalisedStrings::setCurrentMappings(new LocalisedStrings(String::createStringFromData(BinaryData::FxSound_id_txt, BinaryData::FxSound_id_txtSize), false));
    }
    else if (language_.startsWithIgnoreCase("pt-br"))
    {
        LocalisedStrings::setCurrentMappings(new LocalisedStrings(String::createStringFromData(BinaryData::FxSound_ptbr_txt, BinaryData::FxSound_ptbr_txtSize), false));
    }
    else if (language_.startsWithIgnoreCase("pt"))
    {
        LocalisedStrings::setCurrentMappings(new LocalisedStrings(String::createStringFromData(BinaryData::FxSound_pt_txt, BinaryData::FxSound_pt_txtSize), false));
    }
    else if (language_.startsWithIgnoreCase("es"))
    {
        LocalisedStrings::setCurrentMappings(new LocalisedStrings(String::createStringFromData(BinaryData::FxSound_es_txt, BinaryData::FxSound_es_txtSize), false));
    }
    else if (language_.startsWithIgnoreCase("zh-CN"))
    {
        LocalisedStrings::setCurrentMappings(new LocalisedStrings(String::createStringFromData(BinaryData::FxSound_zhCN_txt, BinaryData::FxSound_zhCN_txtSize), false));
    }
	else if (language_.startsWithIgnoreCase("zh-TW"))
	{
		LocalisedStrings::setCurrentMappings(new LocalisedStrings(String::createStringFromData(BinaryData::FxSound_zhTW_txt, BinaryData::FxSound_zhTW_txtSize), false));
	}
    else if (language_.startsWithIgnoreCase("fr"))
    {
        LocalisedStrings::setCurrentMappings(new LocalisedStrings(String::createStringFromData(BinaryData::FxSound_fr_txt, BinaryData::FxSound_fr_txtSize), false));
    } 
    else if (language_.startsWithIgnoreCase("sv"))
    {
        LocalisedStrings::setCurrentMappings(new LocalisedStrings(String::createStringFromData(BinaryData::FxSound_sv_txt, BinaryData::FxSound_sv_txtSize), false));
    }
    else if (language_.startsWithIgnoreCase("it"))
    {
        LocalisedStrings::setCurrentMappings(new LocalisedStrings(String::createStringFromData(BinaryData::FxSound_it_txt, BinaryData::FxSound_it_txtSize), false));
    }
    else if (language_.startsWithIgnoreCase("ru"))
    {
        LocalisedStrings::setCurrentMappings(new LocalisedStrings(String::createStringFromData(BinaryData::FxSound_ru_txt, BinaryData::FxSound_ru_txtSize), false));
    }
    else if (language_.startsWithIgnoreCase("ro"))
    {
        LocalisedStrings::setCurrentMappings(new LocalisedStrings(String::createStringFromData(BinaryData::FxSound_ro_txt, BinaryData::FxSound_ro_txtSize), false));
    }
    else if (language_.startsWithIgnoreCase("tr"))
    {
        LocalisedStrings::setCurrentMappings(new LocalisedStrings(String::createStringFromData(BinaryData::FxSound_tr_txt, BinaryData::FxSound_tr_txtSize), false));
    }
    else if (language_.startsWithIgnoreCase("pl"))
    {
        LocalisedStrings::setCurrentMappings(new LocalisedStrings(String::createStringFromData(BinaryData::FxSound_pl_txt, BinaryData::FxSound_pl_txtSize), false));
    }
    else if (language_.startsWithIgnoreCase("de"))
    {
        LocalisedStrings::setCurrentMappings(new LocalisedStrings(String::createStringFromData(BinaryData::FxSound_de_txt, BinaryData::FxSound_de_txtSize), false));
    }
	else if (language_.startsWithIgnoreCase("hu"))
	{
		LocalisedStrings::setCurrentMappings(new LocalisedStrings(String::createStringFromData(BinaryData::fxsound_hu_txt, BinaryData::fxsound_hu_txtSize), false));
	}
	else if (language_.startsWithIgnoreCase("th"))
	{
		LocalisedStrings::setCurrentMappings(new LocalisedStrings(String::createStringFromData(BinaryData::FxSound_th_txt, BinaryData::FxSound_th_txtSize), false));
	}
	else if (language_.startsWithIgnoreCase("nl"))
	{
		LocalisedStrings::setCurrentMappings(new LocalisedStrings(String::createStringFromData(BinaryData::FxSound_nl_txt, BinaryData::FxSound_nl_txtSize), false));
	}
	else if (language_.startsWithIgnoreCase("ja"))
	{
		LocalisedStrings::setCurrentMappings(new LocalisedStrings(String::createStringFromData(BinaryData::FxSound_ja_txt, BinaryData::FxSound_ja_txtSize), false));
	}
	else if (language_.startsWithIgnoreCase("ar"))
	{
		LocalisedStrings::setCurrentMappings(new LocalisedStrings(String::createStringFromData(BinaryData::FxSound_ar_txt, BinaryData::FxSound_ar_txtSize), false));
	}
	else if (language_.startsWithIgnoreCase("hr"))
	{
		LocalisedStrings::setCurrentMappings(new LocalisedStrings(String::createStringFromData(BinaryData::FxSound_hr_txt, BinaryData::FxSound_hr_txtSize), false));
	}
	else if (language_.startsWithIgnoreCase("ba"))
	{
		LocalisedStrings::setCurrentMappings(new LocalisedStrings(String::createStringFromData(BinaryData::FxSound_ba_txt, BinaryData::FxSound_ba_txtSize), false));
	}
	else if (language_.startsWithIgnoreCase("fa"))
	{
		LocalisedStrings::setCurrentMappings(new LocalisedStrings(String::createStringFromData(BinaryData::FxSound_ir_txt, BinaryData::FxSound_ir_txtSize), false));
	}
	else if (language_.startsWithIgnoreCase("ua"))
	{
		LocalisedStrings::setCurrentMappings(new LocalisedStrings(String::createStringFromData(BinaryData::FxSound_ua_txt, BinaryData::FxSound_ua_txtSize), false));
	}
	else if (language_.startsWithIgnoreCase("no"))
	{
		LocalisedStrings::setCurrentMappings(new LocalisedStrings(String::createStringFromData(BinaryData::FxSound_no_txt, BinaryData::FxSound_no_txtSize), false));
	}
	else if (language_.startsWithIgnoreCase("sl"))
	{
		LocalisedStrings::setCurrentMappings(new LocalisedStrings(String::createStringFromData(BinaryData::FxSound_sl_txt, BinaryData::FxSound_sl_txtSize), false));
	}

	auto* theme = dynamic_cast<FxTheme*>(&LookAndFeel::getDefaultLookAndFeel());
	if (theme != nullptr)
	{
		theme->loadFont(language_);
	}

	sendLookAndFeelChangeToWindows();
}

void FxController::registerSettingsDialog(FxSettingsDialog* settings_dialog)
{
	settings_dialog_ = settings_dialog;
}

void FxController::unregisterSettingsDialog(FxSettingsDialog* settings_dialog)
{
	if (settings_dialog_ == settings_dialog)
	{
		settings_dialog_ = nullptr;
	}
}

void FxController::sendLookAndFeelChangeToWindows()
{
	if (main_window_ != nullptr)
	{
		main_window_->sendLookAndFeelChange();
	}

	if (settings_dialog_ != nullptr)
	{
		settings_dialog_->sendLookAndFeelChange();
	}
}

String FxController::getLanguageName(String language_code) const
{
	if (language_code.startsWithIgnoreCase("en"))
	{
		return "English";
	}
	else if (language_code.startsWithIgnoreCase("ko"))
	{
		return L"\ud55c\uad6d\uc5b4";
	}
	else if (language_code.startsWithIgnoreCase("vi"))
	{
		return L"Ti\u1ebfng Vi\u1ec7t";
	}
	else if (language_code.startsWithIgnoreCase("id"))
	{
		return L"bahasa Indonesia";
	}
	else if (language_code.startsWithIgnoreCase("pt-br"))
	{
		return L"portugu\u00eas brasileiro";
	}
	else if (language_code.startsWithIgnoreCase("pt"))
	{
		return L"Portugu\u00eas";
	}
	else if (language_code.startsWithIgnoreCase("es"))
	{
		return L"Espa\u00f1ol";
	}
	else if (language_code.startsWithIgnoreCase("zh-CN"))
	{
		return L"\u7b80\u4f53\u4e2d\u6587";
	}
	else if (language_code.startsWithIgnoreCase("zh-TW"))
	{
		return L"\u7e41\u9ad4\u4e2d\u6587";
	}
	else if (language_code.startsWithIgnoreCase("sv"))
	{
		return L"svenska";
	}
	else if (language_code.startsWithIgnoreCase("fr"))
	{
		return L"fran\u00e7ais";
	}
	else if (language_code.startsWithIgnoreCase("it"))
	{
		return L"Italiano";
	}
	else if (language_code.startsWithIgnoreCase("ru"))
	{
		return L"\u0440\u0443\u0441\u0441\u043a\u0438\u0439";
	}
	else if (language_code.startsWithIgnoreCase("ro"))
	{
		return L"Rom\u00e2n\u0103";
	}
	else if (language_code.startsWithIgnoreCase("tr")) {
		return L"T\u00fcrk";
	}
	else if (language_code.startsWithIgnoreCase("pl"))
	{
		return L"Polski";
	}
	else if (language_code.startsWithIgnoreCase("de"))
	{
		return L"Deutsch";
	}
	else if (language_code.startsWithIgnoreCase("hu"))
	{
		return L"Magyar";
	}
	else if (language_code.startsWithIgnoreCase("th"))
	{
		return L"\u0e41\u0e1a\u0e1a\u0e44\u0e17\u0e22";
	}
	else if (language_code.startsWithIgnoreCase("nl"))
	{
		return L"Nederlands";
	}
	else if (language_code.startsWithIgnoreCase("ja"))
	{
		return L"\u65e5\u672c\u8a9e";
	}
	else if (language_code.startsWithIgnoreCase("ar"))
	{
		return L"\u0627\u0644\u0639\u0631\u0628\u064a\u0629";
	}
	else if (language_code.startsWithIgnoreCase("hr"))
	{
		return L"hrvatski";
	}
	else if (language_code.startsWithIgnoreCase("ba"))
	{
		return L"bosanski";
	}
	else if (language_code.startsWithIgnoreCase("fa"))
	{
		return L"\u0641\u0627\u0631\u0633\u06cc";
	}
	else if (language_code.startsWithIgnoreCase("ua"))
	{
		return L"\u0443\u043a\u0440\u0430\u0457\u043d\u0441\u044c\u043a\u0430";
	}
	else if (language_code.startsWithIgnoreCase("no"))
	{
		return L"Norsk";
	}
	else if (language_code.startsWithIgnoreCase("sl"))
	{
		return L"Sloven\u0161\u010dina";
	}

    return "English";
}

int FxController::getMaxUserPresets() const
{
	return max_user_presets_;
}

bool FxController::getAutoUpdates()
{
	return auto_updates_;
}

void FxController::setAutoUpdates(bool enable)
{
	auto_updates_ = enable;
	settings_.setBool("automatic_updates", enable);
}

void FxController::checkUpdates()
{
	if (!isAudioProcessing())
	{
		auto current_time = std::time(nullptr);
		uint32_t last_update_time = settings_.getInt("last_update_time", 0);

		if ((current_time - last_update_time) > (24 * 60 * 60))
		{
			settings_.setInt("last_update_time", static_cast<uint32_t>(current_time));

			ChildProcess child_process;
			child_process.start("updater.exe /silent");
		}
	}
}

void FxController::saveWindowPosition(int x, int y)
{
	settings_.setInt("window_x", x);
	settings_.setInt("window_y", y);
}

void FxController::getWindowPosition(int& x, int& y)
{
	x = settings_.getInt("window_x", 0);
	y = settings_.getInt("window_y", 0);
}

juce::Array<DeviceConfig> FxController::getDeviceConfigs()
{
	return DeviceConfig::loadDeviceConfigs(settings_, "device_configs");
}

void FxController::saveDeviceConfigs(const juce::Array<DeviceConfig>& device_configs)
{
	DeviceConfig::saveDeviceConfigs(settings_, "device_configs", device_configs);
}

bool FxController::isOutputDeviceConnected(const DeviceConfig& device_config)
{
	for (auto& output_device : active_output_devices_)
	{
		if (matchesConfiguredOutput(device_config, output_device))
		{
			return true;
		}
    }

	return false;
}

SoundDevice FxController::getPreferredOutput()
{
	return getPreferredOutput(active_output_devices_);
}

SoundDevice FxController::getPreferredOutput(const std::vector<SoundDevice>& output_devices)
{
	return FxSound::OutputDeviceSelection::getPreferredOutput(output_devices, loadOutputPriorities(settings_));
}

SoundDevice FxController::loadSelectedOutputFromSettings()
{
	FxSound::OutputDeviceSelection::PersistedOutputState persisted_output;
	persisted_output.device_id = settings_.getString(kSelectedOutputIdSetting).toWideCharPointer();
	persisted_output.device_name = settings_.getString(kSelectedOutputNameSetting).toWideCharPointer();
	persisted_output.container_id = settings_.getString(kSelectedOutputContainerIdSetting).toWideCharPointer();
	persisted_output.device_description = settings_.getString(kSelectedOutputDescriptionSetting).toWideCharPointer();
	persisted_output.device_num_channel = settings_.getInt(kSelectedOutputChannelsSetting, 2);
	return FxSound::OutputDeviceSelection::restorePersistedOutput(persisted_output);
}

void FxController::saveSelectedOutputToSettings(const SoundDevice& sound_device)
{
	auto persisted_output = FxSound::OutputDeviceSelection::makePersistedOutputState(sound_device);
	settings_.setString(kSelectedOutputIdSetting, String(persisted_output.device_id.c_str()));
	settings_.setString(kSelectedOutputNameSetting, String(persisted_output.device_name.c_str()));
	settings_.setString(kSelectedOutputContainerIdSetting, String(persisted_output.container_id.c_str()));
	settings_.setString(kSelectedOutputDescriptionSetting, String(persisted_output.device_description.c_str()));
	settings_.setInt(kSelectedOutputChannelsSetting, persisted_output.device_num_channel);
}

const String& FxController::getOutputName()
{
	if (output_device_name_.isEmpty())
	{
		output_device_name_ = settings_.getString("output_device_name");
	}
	
    return output_device_name_;
}

void FxController::setOutputName(const String& output_device_name)
{
	output_device_name_ = output_device_name;
	settings_.setString("output_device_name", output_device_name);
}

bool FxController::isNewOutputPrioritized()
{
	return settings_.getBool("prioritize_new_output", false);
}

void FxController::setNewOutputPrioritized(bool prioritize)
{
	settings_.setBool("prioritize_new_output", prioritize);
}

FxThemeMode FxController::getThemeMode()
{
	return FxTheme::getThemeMode();
}

void FxController::setThemeMode(FxThemeMode mode)
{
	if (mode == getThemeMode())
	{
		return;
	}

	FxTheme::setThemeMode(mode);
	settings_.setInt("theme_mode", static_cast<int>(mode));
	setLanguage(getLanguage()); // Reloads fonts and refreshes open windows for the new theme.

	auto power = FxModel::getModel().getPowerState();
	main_window_->setIcon(power, audio_process_on_);
	system_tray_view_->setStatus(power, audio_process_on_);
}

bool FxController::isAlwaysOnTop()
{
	return always_on_top_;
}

void FxController::setAlwaysOnTop(bool always_on_top)
{
	always_on_top_ = always_on_top;
	settings_.setBool("always_on_top", always_on_top);
	main_window_->setAlwaysOnTop(always_on_top);
}

bool FxController::isLaunchOnStartup()
{
	HKEY hkey;
	RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_QUERY_VALUE, &hkey);
	DWORD type;
	DWORD size = 0;
	RegQueryValueEx(hkey, L"FxSound", NULL, &type, NULL, &size);
	RegCloseKey(hkey);

	return size > 0;
}

void FxController::setLaunchOnStartup(bool launch_on_startup)
{
	wchar_t szPath[MAX_PATH];
	GetModuleFileName(NULL, szPath, MAX_PATH);
	HKEY hkey;
	RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hkey);

	if (launch_on_startup)
	{
		RegSetValueEx(hkey, L"FxSound", 0, REG_SZ, (BYTE*)szPath, sizeof(szPath));
	}
	else
	{
		RegDeleteValue(hkey, L"FxSound");
	}

	RegCloseKey(hkey);
}

void FxController::registerHotkeys()
{
	if (!hotkeys_registered_)
	{
		int mod;
		int vk;

		if (getHotkey(HK_CMD_ON_OFF, mod, vk))
		{
			if (isValidHotkey(mod, vk))
			{
				::RegisterHotKey(message_window_.getHandle(), CMD_ON_OFF, mod, vk);
			}			
		}
		
		if (getHotkey(HK_CMD_OPEN_CLOSE, mod, vk))
		{
			if (isValidHotkey(mod, vk))
			{
				::RegisterHotKey(message_window_.getHandle(), CMD_OPEN_CLOSE, mod, vk);
			}
			
		}

		if (getHotkey(HK_CMD_NEXT_PRESET, mod, vk))
		{
			if (isValidHotkey(mod, vk))
			{
				::RegisterHotKey(message_window_.getHandle(), CMD_NEXT_PRESET, mod, vk);
			}
			
		}

		if (getHotkey(HK_CMD_PREVIOUS_PRESET, mod, vk))
		{
			if (isValidHotkey(mod, vk))
			{
				::RegisterHotKey(message_window_.getHandle(), CMD_PREVIOUS_PRESET, mod, vk);
			}
			
		}

		if (getHotkey(HK_CMD_NEXT_OUTPUT, mod, vk))
		{
			if (isValidHotkey(mod, vk))
			{
				::RegisterHotKey(message_window_.getHandle(), CMD_NEXT_OUTPUT, mod, vk);
			}			
		}
		
		hotkeys_registered_ = true;
	}	
}

void FxController::unregisterHotkeys()
{
	if (hotkeys_registered_)
	{
		::UnregisterHotKey(message_window_.getHandle(), CMD_ON_OFF);
		::UnregisterHotKey(message_window_.getHandle(), CMD_OPEN_CLOSE);
		::UnregisterHotKey(message_window_.getHandle(), CMD_NEXT_PRESET);
		::UnregisterHotKey(message_window_.getHandle(), CMD_PREVIOUS_PRESET);
		::UnregisterHotKey(message_window_.getHandle(), CMD_NEXT_OUTPUT);
		hotkeys_registered_ = false;
	}
}

void FxController::getSpectrumBandValues(float* band_values, int array_size)
{
    if (band_values == nullptr || array_size <= 0)
        return;

    if (!audio_process_on_)
    {
        for (int i = 0; i < array_size; ++i)
        {
            band_values[i] = (i < NUM_SPECTRUM_BANDS) ? 0.01f : 0.0f;
        }

        return;
    }

    float values[NUM_SPECTRUM_BANDS] = { 0 };
    dfx_dsp_.getSpectrumBandValues(values, NUM_SPECTRUM_BANDS);

    auto copy_count = (array_size < NUM_SPECTRUM_BANDS) ? array_size : NUM_SPECTRUM_BANDS;
    for (int i = 0; i < copy_count; i++)
    {
		band_values[i] = values[i];
    }

    for (int i = copy_count; i < array_size; ++i)
    {
        band_values[i] = 0.0f;
    }
}

void FxController::getSpectrumBandValues(Array<float>& band_values)
{
    float values[NUM_SPECTRUM_BANDS] = { 0 };
    getSpectrumBandValues(values, NUM_SPECTRUM_BANDS);

    band_values.resize(NUM_SPECTRUM_BANDS);
    for (int i = 0; i < NUM_SPECTRUM_BANDS; ++i)
    {
        band_values.set(i, values[i]);
    }
}

String FxController::FormatString(const String& format, const String& arg)
{
    wchar_t buffer[1024];

    swprintf_s(buffer, format.toWideCharPointer(), arg.toWideCharPointer());

    return String(buffer);
}
