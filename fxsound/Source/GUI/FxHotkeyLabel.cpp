/*
FxSound
Copyright (C) 2025  FxSound LLC

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
#include <cmath>

#include "FxHotkeyLabel.h"
#include "FxController.h"
#include "FxTheme.h"

namespace
{
int getWrappedTextHeight(const Font& font, const String& text, int width)
{
	AttributedString attributed_text;
	attributed_text.setWordWrap(AttributedString::WordWrap::byWord);
	attributed_text.append(text, font, Colours::white);

	TextLayout layout;
	layout.createLayout(attributed_text, static_cast<float>(juce::jmax(1, width)));
	return static_cast<int>(std::ceil(layout.getHeight()));
}
}

FxHotkeyLabel::FxHotkeyLabel(const String& name, const String& command) : name_(name), hotkey_editor_(command)
{
	label_.setJustificationType(Justification::topLeft);

	addAndMakeVisible(label_);
	addAndMakeVisible(hotkey_editor_);
	lookAndFeelChanged();
}

int FxHotkeyEditor::getPreferredEditorWidth() const
{
	auto& theme = dynamic_cast<FxTheme&>(LookAndFeel::getDefaultLookAndFeel());
	return juce::jmax(HOTKEY_EDITOR_MIN_WIDTH, theme.getSmallFont().getStringWidth(getText()) + HOTKEY_EDITOR_PADDING);
}

int FxHotkeyLabel::getPreferredLabelWidth() const
{
	auto& theme = dynamic_cast<FxTheme&>(LookAndFeel::getDefaultLookAndFeel());
	return juce::jmax(HOTKEY_LABEL_MIN_WIDTH, theme.getSmallFont().getStringWidth(TRANS(name_)) + HOTKEY_LABEL_PADDING);
}

int FxHotkeyLabel::getPreferredEditorWidth() const
{
	return hotkey_editor_.getPreferredEditorWidth();
}

int FxHotkeyLabel::getPreferredWidth() const
{
	return getPreferredLabelWidth() + HOTKEY_DEFAULT_GAP + getPreferredEditorWidth();
}

int FxHotkeyLabel::getPreferredHeight(int labelWidth) const
{
	auto& theme = dynamic_cast<FxTheme&>(LookAndFeel::getDefaultLookAndFeel());
	return juce::jmax(hotkey_editor_.getHeight(), getWrappedTextHeight(theme.getSmallFont(), TRANS(name_), juce::jmax(1, labelWidth)));
}

void FxHotkeyLabel::refreshText()
{
	label_.setText(TRANS(name_), NotificationType::dontSendNotification);
}

void FxHotkeyLabel::setLayoutMetrics(int labelWidth, int editorWidth, int gap)
{
	label_width_ = juce::jmax(HOTKEY_LABEL_MIN_WIDTH, labelWidth);
	editor_width_ = juce::jmax(hotkey_editor_.getPreferredEditorWidth(), editorWidth);
	control_gap_ = juce::jmax(0, gap);
}

void FxHotkeyLabel::lookAndFeelChanged()
{
	auto& theme = dynamic_cast<FxTheme&>(getLookAndFeel());
	label_.setFont(theme.getSmallFont());
	refreshText();
}

void FxHotkeyLabel::resized()
{
	auto bounds = getLocalBounds();
	auto editor_bounds = hotkey_editor_.getBounds();

	label_.setBounds(bounds.getX(), bounds.getY(), juce::jmin(label_width_, bounds.getWidth()), bounds.getHeight());
	editor_bounds.setSize(juce::jmin(editor_width_, juce::jmax(1, bounds.getWidth() - label_.getWidth() - control_gap_)), editor_bounds.getHeight());
	editor_bounds.setX(label_.getRight() + control_gap_);
	editor_bounds.setY(bounds.getY() + juce::jmax(0, (bounds.getHeight() - editor_bounds.getHeight()) / 2));
	hotkey_editor_.setBounds(editor_bounds);
}

void FxHotkeyEditor::lookAndFeelChanged()
{
	auto& theme = dynamic_cast<FxTheme&>(getLookAndFeel());
	setFont(theme.getSmallFont());
	setKeyText();
	setTooltip(TRANS("Press Ctrl + Alt/Shift + 0-9/A-Z to change the hotkey"));

	repaint();
}

FxHotkeyEditor::FxHotkeyEditor(const String& command)
{
	command_ = command;

	FxController::getInstance().getHotkey(command, mod_, vk_);

	setWantsKeyboardFocus(true);
	setMouseClickGrabsKeyboardFocus(true);

	auto& theme = dynamic_cast<FxTheme&>(getLookAndFeel());
	setFont(theme.getSmallFont());
	setJustificationType(Justification::centred);
	setKeyText();

	String tooltip = TRANS("Press Ctrl + Alt/Shift + 0-9/A-Z to change the hotkey");
	setTooltip(tooltip);

	setBounds(0, 0, getPreferredEditorWidth(), HOTKEY_EDITOR_HEIGHT);
}

bool FxHotkeyEditor::keyPressed(const KeyPress& key)
{
	int mod = 0;
	int vk = 0;

	auto modifiers = key.getModifiers();
	if (modifiers.isCtrlDown())
	{
		mod = MOD_CONTROL;
	}
	if (modifiers.isAltDown())
	{
		mod |= MOD_ALT;
	}
	else if (modifiers.isShiftDown())
	{
		mod |= MOD_SHIFT;
	}

	if ((mod & MOD_CONTROL) == 0 || (mod & (MOD_ALT | MOD_SHIFT)) == 0)
	{
		return false;
	}

	auto keyCode = key.getKeyCode();
	if ((keyCode >= 0x30 && keyCode <= 0x39) || (keyCode >= 'A' && keyCode <= 'Z'))
	{
		vk = static_cast<int>(keyCode);
	}

	int saved_mod;
	int saved_vk;
	auto& controller = FxController::getInstance();
	if (controller.getHotkey(FxController::HK_CMD_ON_OFF, saved_mod, saved_vk))
	{
		if (mod == saved_mod && vk == saved_vk)
		{
			return false;
		}
	}
	if (controller.getHotkey(FxController::HK_CMD_OPEN_CLOSE, saved_mod, saved_vk))
	{
		if (mod == saved_mod && vk == saved_vk)
		{
			return false;
		}
	}
	if (controller.getHotkey(FxController::HK_CMD_NEXT_PRESET, saved_mod, saved_vk))
	{
		if (mod == saved_mod && vk == saved_vk)
		{
			return false;
		}
	}
	if (controller.getHotkey(FxController::HK_CMD_PREVIOUS_PRESET, saved_mod, saved_vk))
	{
		if (mod == saved_mod && vk == saved_vk)
		{
			return false;
		}
	}
	if (controller.getHotkey(FxController::HK_CMD_NEXT_OUTPUT, saved_mod, saved_vk))
	{
		if (mod == saved_mod && vk == saved_vk)
		{
			return false;
		}
	}

	if (vk != 0)
	{
		mod_ = mod;
		vk_ = vk;

		setKeyText();
		FxController::getInstance().setHotkey(command_, mod_, vk_);

		return true;
	}
	else
	{
		return false;
	}
}

void FxHotkeyEditor::focusGained(FocusChangeType cause)
{
	repaint();
}

void FxHotkeyEditor::focusLost(FocusChangeType cause)
{
	repaint();
}

void FxHotkeyEditor::paint(juce::Graphics& g)
{
	Label::paint(g);

	juce::Rectangle<int> bounds = getLocalBounds();

	float borderThickness;
	Colour textColour;
	if (hasKeyboardFocus(false))
	{
		borderThickness = 2;
	}
	else
	{
		borderThickness = 1;
	}

	auto& theme = dynamic_cast<FxTheme&>(getLookAndFeel());
	if (isEnabled())
	{
		if (FxController::getInstance().isValidHotkey(mod_, vk_))
		{
			setColour(Label::textColourId, theme.findColour(TextEditor::highlightedTextColourId));
		}
		else
		{
			setColour(Label::textColourId, theme.findColour(TextEditor::textColourId));
		}
	}
	else
	{
		borderThickness = 1;
		setColour(Label::textColourId, theme.findColour(TextEditor::textColourId));
	}

	g.setColour(getLookAndFeel().findColour(TextEditor::textColourId));
	g.drawRoundedRectangle(bounds.reduced(borderThickness, borderThickness).toFloat(), 5.0f, borderThickness);
}

void FxHotkeyEditor::setKeyText()
{
	key_text_ = "";

	if (mod_ == 0)
	{
		key_text_ = TRANS("Not configured");
		setText(key_text_, NotificationType::dontSendNotification);
	}
	else
	{
		if (mod_ & MOD_CONTROL)
		{
			key_text_ = L" " + TRANS("Ctrl") + L" + ";
		}
		if (mod_ & MOD_ALT)
		{
			key_text_ += L" " + TRANS("Alt") + L" + ";
		}
		if (mod_ & MOD_SHIFT)
		{
			key_text_ += L" " + TRANS("Shift") + L" + ";
		}
	}

	if (mod_ != 0)
	{
		if ((vk_ >= 0x30 && vk_ <= 0x39) || (vk_ >= 'A' && vk_ <= 'Z'))
		{
			key_text_ += static_cast<WCHAR>(vk_);
		}

		setText(key_text_, NotificationType::dontSendNotification);
	}
}
