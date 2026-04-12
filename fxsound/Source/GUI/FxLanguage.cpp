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

#include "FxLanguage.h"
#include "FxController.h"
#include "LanguageSelectorPolicy.h"
#include "FxTheme.h"

FxLanguage::FxLanguage() : next_button_("next", DrawableButton::ButtonStyle::ImageFitted), prev_button_("prev", DrawableButton::ButtonStyle::ImageFitted)
{
    languages_ = { "en", "ar", "ba", "hr", "de", "es", "fr", "hu", "id", "it", "ja", "ko", "nl", "no", "fa", "pl", "pt", "pt-br", "ro", "ru", "sl", "sv", "th", "tr", "ua", "vi", "zh-CN", "zh-TW"};

    language_.setJustificationType(Justification::centred);

    next_button_.setMouseCursor(MouseCursor::PointingHandCursor);
    prev_button_.setMouseCursor(MouseCursor::PointingHandCursor);
    
    setSize(WIDTH, HEIGHT);

    addAndMakeVisible(&prev_button_);
    addAndMakeVisible(&language_);
    addAndMakeVisible(&next_button_);

    prev_button_.onClick = [this]() {
        this->onPrevLanguage();
    };

    next_button_.onClick = [this]() {
        this->onNextLanguage();
    };

    updatePreferredWidth();
    refreshSelector();
    resized();
}

int FxLanguage::getPreferredWidth() const
{
    return preferred_width_;
}

void FxLanguage::paint(Graphics& g)
{
    g.setFillType(FillType(Colour(FXCOLOR(ControlBackground)).withAlpha(1.0f)));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 5.0f);
}

void FxLanguage::lookAndFeelChanged()
{
    refreshSelector();
}

void FxLanguage::resized()
{
    prev_button_.setBounds(10, (getHeight() - BUTTON_HEIGHT) / 2, BUTTON_WIDTH, BUTTON_HEIGHT);
    next_button_.setBounds(getWidth() - BUTTON_WIDTH - 10, (getHeight() - BUTTON_HEIGHT) / 2, BUTTON_WIDTH, BUTTON_HEIGHT);
    language_.setBounds(prev_button_.getRight(), (getHeight() - LABEL_HEIGHT) / 2, next_button_.getX() - prev_button_.getRight(), LABEL_HEIGHT);
}

void FxLanguage::onNextLanguage()
{
    if (++language_index_ >= languages_.size())
    {
        language_index_ = 0;
    }

    String language_code = languages_[language_index_];
    FxController::getInstance().setLanguage(language_code);
    refreshSelector();
}

void FxLanguage::onPrevLanguage()
{    
    if (--language_index_ < 0)
    {
        language_index_ = languages_.size() - 1;
    }

    String language_code = languages_[language_index_];
    FxController::getInstance().setLanguage(language_code);
    refreshSelector();
}

void FxLanguage::updatePreferredWidth()
{
    auto& controller = FxController::getInstance();
    auto label_width = 0;

    for (const auto& language_code : languages_)
    {
        auto label_font = FxTheme::getNormalFontForLanguage(language_code);
        label_width = juce::jmax(label_width, label_font.getStringWidth(controller.getLanguageName(language_code)));
    }

    const auto preferred_width = label_width + 48;
    preferred_width_ = juce::jmax(WIDTH, preferred_width);
}

void FxLanguage::refreshSelector()
{
    refreshButtonImages();

    auto& controller = FxController::getInstance();
    auto language_code = controller.getLanguage();

    language_index_ = FxSound::LanguageSelectorPolicy::resolveLanguageIndex(
        std::wstring_view(language_code.toWideCharPointer()),
        languages_.size(),
        [this](int language_index)
        {
            return std::wstring_view(languages_[language_index].toWideCharPointer());
        });

    if (auto* theme = dynamic_cast<FxTheme*>(&getLookAndFeel()))
    {
        language_.setFont(theme->getNormalFont());
    }

    language_.setColour(Label::ColourIds::textColourId, getLookAndFeel().findColour(TextButton::textColourOnId));
    language_.setText(controller.getLanguageName(language_code), NotificationType::dontSendNotification);
}

void FxLanguage::refreshButtonImages()
{
    auto next_normal = Drawable::createFromImageData(FXIMAGE(ArrowNext), FXIMAGESIZE(ArrowNext));
    auto next_disabled = Drawable::createFromImageData(FXIMAGE(ArrowNextBW), FXIMAGESIZE(ArrowNextBW));
    next_button_.setImages(next_normal.get(), nullptr, next_disabled.get());

    auto prev_normal = Drawable::createFromImageData(FXIMAGE(ArrowPrev), FXIMAGESIZE(ArrowPrev));
    auto prev_disabled = Drawable::createFromImageData(FXIMAGE(ArrowPrevBW), FXIMAGESIZE(ArrowPrevBW));
    prev_button_.setImages(prev_normal.get(), nullptr, prev_disabled.get());
}
