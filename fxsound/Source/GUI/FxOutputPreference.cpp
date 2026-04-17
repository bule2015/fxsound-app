/*
FxSound
Copyright (C) 2026  FxSound LLC

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
#include "FxOutputPreference.h"
#include "OutputDeviceSelection.h"
#include "OutputPresetSelectionPolicy.h"
#include "OutputPriorityReorderPolicy.h"

namespace
{
FxSound::OutputDeviceSelection::PresetApplyIdentity makePresetApplyIdentity(const DeviceConfig& device_config)
{
    return {
        std::wstring(device_config.device_id.toWideCharPointer()),
        std::wstring(device_config.device_name.toWideCharPointer()),
        std::wstring(device_config.container_id.toWideCharPointer())
    };
}

bool matchesDeviceConfigEntry(const DeviceConfig& lhs, const DeviceConfig& rhs)
{
    if (!lhs.device_id.isEmpty() && !rhs.device_id.isEmpty() && lhs.device_id == rhs.device_id)
    {
        return true;
    }

    if (!lhs.container_id.isEmpty() &&
        !rhs.container_id.isEmpty() &&
        lhs.container_id == rhs.container_id &&
        lhs.device_name == rhs.device_name)
    {
        return true;
    }

    return lhs.container_id.isEmpty() &&
        rhs.container_id.isEmpty() &&
        lhs.device_name == rhs.device_name;
}

void setArrowButtonImages(DrawableButton& button, Drawable* normal_image, Drawable* selected_image, bool is_row_selected)
{
	auto* active_image = is_row_selected ? selected_image : normal_image;
	button.setImages(active_image, selected_image, active_image);
}

StringArray buildPresetOptionLabels(const FxModel& model)
{
    StringArray preset_option_labels;
    preset_option_labels.ensureStorageAllocated(model.getPresetCount() + 1);
    preset_option_labels.add(TRANS("No preset"));

    for (auto i = 0; i < model.getPresetCount(); ++i)
    {
        preset_option_labels.add(model.getPreset(i).name);
    }

    return preset_option_labels;
}

std::wstring getPresetNameAtIndex(const StringArray& preset_option_labels, int preset_index)
{
    return std::wstring(preset_option_labels[preset_index + 1].toWideCharPointer());
}

int getOutputPreferencePreferredWidth(const Font& font, const juce::Array<DeviceConfig>& device_configs)
{
    constexpr int kButtonWidth = 18;
    constexpr int kMargin = 5;
    constexpr int kPresetListWidth = 150;
    constexpr int kMinimumDeviceNameWidth = 240;
    constexpr int kOuterPadding = 8;

    auto max_device_name_width = kMinimumDeviceNameWidth;
    for (auto index = 0; index < device_configs.size(); ++index)
    {
        auto label_text = String::formatted("%d. ", index + 1) + device_configs[index].device_name;
        max_device_name_width = juce::jmax(max_device_name_width, font.getStringWidth(label_text) + 20);
    }

    return (kMargin * 4) + (kButtonWidth * 2) + kPresetListWidth + max_device_name_width + kOuterPadding;
}

}

FxOutputDeviceRow::FxOutputDeviceRow(FxOutputPreferenceListModel& model) : up_button_("up", DrawableButton::ImageFitted), down_button_("down", DrawableButton::ImageFitted), output_preference_list_model_(model)
{
    setInterceptsMouseClicks(true, true);
    setMouseCursor(MouseCursor::DraggingHandCursor);

    up_image_ = Drawable::createFromImageData(FXIMAGE(ArrowUp), FXIMAGESIZE(ArrowUp));
    down_image_ = Drawable::createFromImageData(FXIMAGE(ArrowDown), FXIMAGESIZE(ArrowDown));
    up_selected_image_ = FxTheme::createSelectedArrowDrawable(true);
    down_selected_image_ = FxTheme::createSelectedArrowDrawable(false);

    up_button_.setMouseCursor(MouseCursor::PointingHandCursor);
    up_button_.setSize(BUTTON_WIDTH, BUTTON_WIDTH);
    up_button_.setImages(up_image_.get(), up_selected_image_.get(), up_image_.get());
    up_button_.setWantsKeyboardFocus(true);
    up_button_.onClick = [this]() {
        output_preference_list_model_.moveRowUp(row_index_);
        };

    down_button_.setMouseCursor(MouseCursor::PointingHandCursor);
    down_button_.setSize(BUTTON_WIDTH, BUTTON_WIDTH);
    down_button_.setImages(down_image_.get(), down_selected_image_.get(), down_image_.get());
    down_button_.setWantsKeyboardFocus(true);
    down_button_.onClick = [this]() {
        output_preference_list_model_.moveRowDown(row_index_);
        };

    preset_list_.setColour(ComboBox::ColourIds::backgroundColourId, Colour(FXCOLOR(WidgetBackground)).withAlpha(1.0f));
    preset_list_.setColour(ComboBox::ColourIds::outlineColourId, Colour(FXCOLOR(RowOutline)).withAlpha(0.5f));
    preset_list_.setColour(ComboBox::ColourIds::focusedOutlineColourId, Colour(FXCOLOR(SelectedRowOutline)).withAlpha(1.0f));
    preset_list_.setWantsKeyboardFocus(true);
    preset_list_.setJustificationType(Justification::centredLeft);
    preset_list_.onChange = [this]() {
        auto selected_id = preset_list_.getSelectedId();
        if (selected_id > 0)
        {
            auto preset = String(FxSound::OutputPresetSelectionPolicy::getPresetNameForSelectedId(
                selected_id,
                preset_option_labels_.size() - 1,
                [this](int preset_index)
                {
                    return getPresetNameAtIndex(preset_option_labels_, preset_index);
                }).c_str());
            device_config_.preset = preset;
            output_preference_list_model_.updateDeviceConfig(device_config_);

            auto selected_output = FxModel::getModel().getSelectedOutput();
            auto selected_output_matches = FxSound::OutputDeviceSelection::shouldApplyPresetToSelectedOutput(
                makePresetApplyIdentity(device_config_),
                selected_output,
                std::wstring(FxController::getInstance().getOutputName().toWideCharPointer()));

            if (selected_output_matches && preset.isNotEmpty())
            {
                FxController::getInstance().setPreset(preset);
            }

            syncSelectedPreset();
        }
    };

    auto& theme = dynamic_cast<FxTheme&>(LookAndFeel::getDefaultLookAndFeel());
    device_name_.setInterceptsMouseClicks(false, false);
    device_name_.setFont(theme.getNormalFont());
    device_name_.setJustificationType(Justification::centredLeft);
    device_name_.setMinimumHorizontalScale(0.85f);

    addAndMakeVisible(up_button_);
    addAndMakeVisible(down_button_);
    addAndMakeVisible(device_name_);
    addAndMakeVisible(preset_list_);

    row_index_ = -1;
    is_row_selected_ = false;

    refreshText();
}

void FxOutputDeviceRow::lookAndFeelChanged()
{
    refreshText();
    updateSelectionVisuals();
}

void FxOutputDeviceRow::mouseDown(const MouseEvent&)
{
    resetDragState();
    drag_hotspot_ = getMouseXYRelative();

    if (auto* list_box = findParentListBox())
    {
        list_box->selectRow(row_index_);
        list_box->grabKeyboardFocus();
    }
}

void FxOutputDeviceRow::mouseDrag(const MouseEvent& e)
{
    if (row_index_ < 0)
    {
        return;
    }

    if (!is_dragging_row_ &&
        !FxSound::OutputPriorityReorderPolicy::shouldStartDrag(e.getDistanceFromDragStart()))
    {
        return;
    }

    auto* output_preference = findParentOutputPreference();
    auto* list_box = findParentListBox();
    if (output_preference == nullptr || list_box == nullptr)
    {
        return;
    }

    if (!is_dragging_row_)
    {
        beginRowDrag(*output_preference, e);
    }

    updateRowDrag(*output_preference, *list_box, e);
}

void FxOutputDeviceRow::mouseUp(const MouseEvent&)
{
    if (auto* output_preference = findParentOutputPreference())
    {
        endRowDrag(*output_preference);
    }
    else
    {
        resetDragState();
    }
}

void FxOutputDeviceRow::beginRowDrag(FxOutputPreference& output_preference, const MouseEvent& e)
{
    is_dragging_row_ = true;
    drag_snapshot_ = createComponentSnapshot(getLocalBounds());
    output_preference.showDragGhost(drag_snapshot_, getCursorPositionInOutputPreference(output_preference, e), drag_hotspot_);
}

void FxOutputDeviceRow::updateRowDrag(FxOutputPreference& output_preference, ListBox& list_box, const MouseEvent& e)
{
    drag_target_row_index_ = FxSound::OutputPriorityReorderPolicy::resolveDropRow(
        getDropRowIndex(e),
        row_index_,
        output_preference_list_model_.getNumRows());

    list_box.selectRow(drag_target_row_index_);
    output_preference.moveDragGhost(getCursorPositionInOutputPreference(output_preference, e), drag_hotspot_);
}

void FxOutputDeviceRow::endRowDrag(FxOutputPreference& output_preference)
{
    if (is_dragging_row_)
    {
        output_preference_list_model_.moveRow(row_index_, drag_target_row_index_);
    }

    output_preference.hideDragGhost();
    resetDragState();
}

void FxOutputDeviceRow::refreshText()
{
    auto& theme = dynamic_cast<FxTheme&>(LookAndFeel::getDefaultLookAndFeel());
    device_name_.setFont(theme.getNormalFont());
    preset_list_.setTextWhenNothingSelected(TRANS("Select preset"));
    refreshPresetItemsIfNeeded();
}

void FxOutputDeviceRow::refreshPresetItemsIfNeeded()
{
    auto& model = FxModel::getModel();
    auto next_preset_option_labels = buildPresetOptionLabels(model);

    if (next_preset_option_labels == preset_option_labels_)
    {
        return;
    }

    preset_option_labels_ = next_preset_option_labels;
    preset_list_.clear(NotificationType::dontSendNotification);
    for (auto i = 0; i < preset_option_labels_.size(); ++i)
    {
        preset_list_.addItem(preset_option_labels_[i], i + 1);
    }

    syncSelectedPreset();
}

void FxOutputDeviceRow::updateSelectionVisuals()
{
    setArrowButtonImages(up_button_, up_image_.get(), up_selected_image_.get(), is_row_selected_);
    setArrowButtonImages(down_button_, down_image_.get(), down_selected_image_.get(), is_row_selected_);
    preset_list_.setColour(ComboBox::ColourIds::outlineColourId,
        Colour(is_row_selected_ ? FXCOLOR(SelectedRowOutline) : FXCOLOR(RowOutline)).withAlpha(is_row_selected_ ? 1.0f : 0.5f));
}

void FxOutputDeviceRow::update(int index, bool is_row_selected, const DeviceConfig& device_config)
{
    if (index < 0)
        return;

    row_index_ = index;
    device_config_ = device_config;

    up_button_.setVisible(row_index_ > 0);
    down_button_.setVisible(row_index_ < output_preference_list_model_.getNumRows() - 1);

    auto bounds = getLocalBounds().reduced(2);

    auto y = (getHeight() - BUTTON_WIDTH) / 2;
    int x;

    if (row_index_ < (output_preference_list_model_.getNumRows() - 1))
    {
        x = MARGIN;
    }
    else
    {
        x = MARGIN + BUTTON_WIDTH / 2;
    }
    up_button_.setBounds(x, y, BUTTON_WIDTH, BUTTON_WIDTH);

    if (row_index_ != 0)
    {
        x = up_button_.getRight();
    }
    else
    {
        x = MARGIN + BUTTON_WIDTH / 2;
    }
    down_button_.setBounds(x, y, BUTTON_WIDTH, BUTTON_WIDTH);

    preset_list_.setBounds((bounds.getWidth() - PRESET_LIST_WIDTH - MARGIN), bounds.getY(), PRESET_LIST_WIDTH, bounds.getHeight());
    
    x = MARGIN * 2 + BUTTON_WIDTH * 2;
    int width = bounds.getWidth() - x - PRESET_LIST_WIDTH - MARGIN * 2;
    device_name_.setBounds(x, bounds.getY(), width, bounds.getHeight());

    device_name_.setText(String::formatted("%d. ", row_index_ + 1) +  device_config.device_name, NotificationType::dontSendNotification);
    device_name_.setEnabled(FxController::getInstance().isOutputDeviceConnected(device_config));

    if (is_row_selected_ != is_row_selected)
    {
        is_row_selected_ = is_row_selected;
        updateSelectionVisuals();
        repaint();
    }

    refreshPresetItemsIfNeeded();
    syncSelectedPreset();
    resetDragState();
}

ListBox* FxOutputDeviceRow::findParentListBox() const
{
    return findParentComponentOfClass<ListBox>();
}

FxOutputPreference* FxOutputDeviceRow::findParentOutputPreference() const
{
    return findParentComponentOfClass<FxOutputPreference>();
}

int FxOutputDeviceRow::getDropRowIndex(const MouseEvent& e) const
{
    auto* list_box = findParentListBox();
    if (list_box == nullptr)
    {
        return row_index_;
    }

    auto position = e.getEventRelativeTo(list_box).position;
    return list_box->getRowContainingPosition(roundToInt(position.x), roundToInt(position.y));
}

Point<int> FxOutputDeviceRow::getCursorPositionInOutputPreference(FxOutputPreference& output_preference, const MouseEvent& e) const
{
    return e.getEventRelativeTo(&output_preference).position.toInt();
}

void FxOutputDeviceRow::resetDragState()
{
    is_dragging_row_ = false;
    drag_target_row_index_ = row_index_;
    drag_hotspot_ = {};
    drag_snapshot_ = {};
}

void FxOutputDeviceRow::paint(Graphics& g)
{
    if (is_row_selected_)
    {
        g.setColour(Colour(FXCOLOR(SelectedRowOutline)).withAlpha(1.0f));
        g.drawLine(device_name_.getX(), device_name_.getBottom() - 0.5f, device_name_.getRight(), device_name_.getBottom() - 1.0f, 1.0f);
    }
    else
    {
        g.setColour(Colour(FXCOLOR(RowOutline)).withAlpha(1.0f));
        g.drawLine(device_name_.getX(), device_name_.getBottom() - 0.5f, device_name_.getRight(), device_name_.getBottom() - 0.5f, 0.5f);
    }
}

FxOutputPreferenceListModel::FxOutputPreferenceListModel()
{
    FxModel::getModel().addListener(this);

    device_configs_ = FxController::getInstance().getDeviceConfigs();
}

FxOutputPreferenceListModel::~FxOutputPreferenceListModel()
{
    FxModel::getModel().removeListener(this);
}

int FxOutputPreferenceListModel::getNumRows()
{
    return device_configs_.size();
}

void FxOutputPreferenceListModel::paintListBoxItem(int, juce::Graphics&, int, int, bool)
{
    // Rows are fully rendered by FxOutputDeviceRow.
}

Component* FxOutputPreferenceListModel::refreshComponentForRow(int rowNumber, bool isRowSelected, Component* existingComponent)
{
    auto* row = dynamic_cast<FxOutputDeviceRow*>(existingComponent);

    if (row == nullptr)
        row = new FxOutputDeviceRow(*this);

    row->update(rowNumber, isRowSelected, device_configs_[rowNumber]);

    return row;
}

bool FxOutputPreferenceListModel::moveRow(int from_index, int to_index)
{
    if (from_index < 0 ||
        to_index < 0 ||
        from_index >= device_configs_.size() ||
        to_index >= device_configs_.size() ||
        from_index == to_index)
    {
        return false;
    }

    device_configs_.move(from_index, to_index);
    persist();

    if (onRowMoved)
        onRowMoved(to_index);

    return true;
}

void FxOutputPreferenceListModel::moveRowUp(int index)
{
    moveRow(index, index - 1);
}

void FxOutputPreferenceListModel::moveRowDown(int index)
{
    moveRow(index, index + 1);
}

void FxOutputPreferenceListModel::modelChanged(FxModel::Event model_event)
{
    if (model_event == FxModel::Event::OutputListUpdated)
    {
        device_configs_ = FxController::getInstance().getDeviceConfigs();
        if (onModelChanged)
            onModelChanged();
    }
}

void FxOutputPreferenceListModel::updateDeviceConfig(const DeviceConfig& device_config)
{
    for (auto i = 0; i < device_configs_.size(); i++)
    {
        if (matchesDeviceConfigEntry(device_configs_[i], device_config))
        {
            device_configs_.getReference(i).preset = device_config.preset;
            break;
        }
    }

    persist();
}

int FxOutputPreferenceListModel::getPreferredWidth(const Font& font) const
{
    return getOutputPreferencePreferredWidth(font, device_configs_);
}

void FxOutputPreferenceListModel::persist()
{
    FxController::getInstance().saveDeviceConfigs(device_configs_);
}

void FxOutputDeviceRow::syncSelectedPreset()
{
    auto selected_id = FxSound::OutputPresetSelectionPolicy::getSelectedIdForPresetName(
        std::wstring_view(device_config_.preset.toWideCharPointer()),
        preset_option_labels_.size() - 1,
        [this](int preset_index)
        {
            return getPresetNameAtIndex(preset_option_labels_, preset_index);
        });

    if (preset_list_.getSelectedId() != selected_id)
    {
        preset_list_.setSelectedId(selected_id, NotificationType::dontSendNotification);
    }
}

FxOutputPreference::FxOutputPreference()
{
    output_preference_model_.onModelChanged = [this]() {
        refreshListBox();
    };

    output_preference_model_.onRowMoved = [this](int row_index) {
        refreshListBox(row_index);
    };

    output_preference_list_.setModel(&output_preference_model_);
    output_preference_list_.addKeyListener(this);
    output_preference_list_.setWantsKeyboardFocus(true);

    addAndMakeVisible(output_preference_list_);
    addAndMakeVisible(drag_ghost_);
    drag_ghost_.setInterceptsMouseClicks(false, false);
    drag_ghost_.setAlpha(0.65f);
    drag_ghost_.setVisible(false);

    output_preference_list_.setRowHeight(ROW_HEIGHT);
    output_preference_list_.setMultipleSelectionEnabled(false);

    refreshText();
    refreshListBox();
}

void FxOutputPreference::update()
{
    refreshListBox();
}

int FxOutputPreference::getPreferredWidth() const
{
    if (auto* theme = dynamic_cast<FxTheme*>(&LookAndFeel::getDefaultLookAndFeel()))
    {
        return output_preference_model_.getPreferredWidth(theme->getNormalFont());
    }

    return output_preference_model_.getPreferredWidth(Font());
}

bool FxOutputPreference::keyPressed(const KeyPress& key, Component*)
{
    auto selected_row = output_preference_list_.getSelectedRow();
    if (selected_row < 0)
    {
        return false;
    }

    if (key == KeyPress(KeyPress::upKey, ModifierKeys::shiftModifier, 0))
    {
        output_preference_model_.moveRowUp(selected_row);
        output_preference_list_.grabKeyboardFocus();
        return true;
    }

    if (key == KeyPress(KeyPress::downKey, ModifierKeys::shiftModifier, 0))
    {
        output_preference_model_.moveRowDown(selected_row);
        output_preference_list_.grabKeyboardFocus();
        return true;
    }

    return false;
}

void FxOutputPreference::resized()
{
    output_preference_list_.setBounds(getLocalBounds().reduced(5, 10));
}

void FxOutputPreference::lookAndFeelChanged()
{
    refreshText();
    refreshListBox();
}

void FxOutputPreference::paint(Graphics& g)
{
    g.setFillType(FillType(Colour(FXCOLOR(WidgetBackground)).withAlpha(1.0f)));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 8.0f);
}

void FxOutputPreference::refreshText()
{
    output_preference_list_.setTooltip(TRANS("Drag rows or use Shift+Up and Shift+Down to change the device priority"));
}

void FxOutputPreference::showDragGhost(const Image& snapshot, Point<int> cursor_position, Point<int> hotspot)
{
    if (snapshot.isNull())
    {
        return;
    }

    drag_ghost_.setImage(snapshot);
    drag_ghost_.setSize(snapshot.getWidth(), snapshot.getHeight());
    drag_ghost_.setVisible(true);
    drag_ghost_.toFront(false);
    moveDragGhost(cursor_position, hotspot);
}

void FxOutputPreference::moveDragGhost(Point<int> cursor_position, Point<int> hotspot)
{
    if (!drag_ghost_.isVisible())
    {
        return;
    }

    drag_ghost_.setTopLeftPosition(cursor_position - hotspot);
}

void FxOutputPreference::hideDragGhost()
{
    drag_ghost_.setVisible(false);
    drag_ghost_.setImage(Image());
}

void FxOutputPreference::refreshListBox()
{
    output_preference_list_.updateContent();
    output_preference_list_.repaint();
}

void FxOutputPreference::refreshListBox(int selected_row)
{
    refreshListBox();
    output_preference_list_.selectRow(selected_row);
}
