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

#include "FxVisualizer.h"
#include "FxController.h"
#include "FxTheme.h"

FxVisualizer::FxVisualizer()
{
    rebuildBarLayout();

#if JUCE_MAJOR_VERSION >= 8
    start();
#else
    calcGradient();
    reset();
    setFramesPerSecond(10);
#endif

    setOpaque(false);
    setSize(WIDTH, HEIGHT);
}

void FxVisualizer::rebuildBarLayout()
{
    constexpr float kStartX = 27.0f;
    constexpr float kBarSpacing = 9.1f;

    float x = kStartX;
    for (int index = 0; index < TOTAL_BARS; ++index)
    {
        bar_x_positions_[index] = x;
        x += kBarSpacing;
    }
}

void FxVisualizer::start()
{    
    calcGradient();

#if JUCE_MAJOR_VERSION >= 8
    if (vblank_listener_ == nullptr)
    {
        vblank_listener_ = std::make_unique<juce::VBlankAttachment>(
            this,
            [this](double timestamp)
            {
                if (!isShowing())
                    return;

                static double last_frame_time = 0.0;
                constexpr double fps_interval = 1.0 / 30.0;

                if (timestamp - last_frame_time >= fps_interval)
                {
                    last_frame_time = timestamp;
                    if (FxController::getInstance().isAudioProcessing())
                    {
                        update();
                        repaint();
                    }
                    else
                    {
                        reset();
                        repaint();
                        vblank_listener_.reset();
                    }
                }
            });
    }
#else
    setFramesPerSecond(30);
#endif
}

void FxVisualizer::pause()
{
    calcGradient();

#if JUCE_MAJOR_VERSION >= 8
    reset();
    repaint();

    if (vblank_listener_ != nullptr)
    {
        vblank_listener_.reset();
    }
#else
    setFramesPerSecond(10);
#endif
}

void FxVisualizer::reset()
{
    band_history_head_ = 0;
    band_history_.fill(0.0f);
}

void FxVisualizer::update()
{
    if (!isEnabled())
        return;

    FxController::getInstance().getSpectrumBandValues(band_values_.data(), (int)band_values_.size());
    band_history_head_ = (band_history_head_ + 1) % HISTORY_LENGTH;

    for (int i = 0; i < FxController::NUM_SPECTRUM_BANDS; i++)
    {
        if (band_values_[i] < 0 || band_values_[i] > 1)
        {
            band_values_[i] = 0.0f;
        }

        band_history_[i * HISTORY_LENGTH + band_history_head_] = band_values_[i];
    }
}

void FxVisualizer::paint(Graphics& g)
{
    auto bounds = getLocalBounds();

    g.setFillType(FillType(Colour(FXCOLOR(ControlBackground)).withAlpha(1.0f)));
    g.fillRoundedRectangle(bounds.toFloat(), 8);

    g.setGradientFill(gradient_);

    // ------------------------------------------------------ SPECTRUM AREA - LEFT AND SIZE 
    Path barsPath;

    for (int band = 0; band < FxController::NUM_SPECTRUM_BANDS; ++band)
    {
        for (int bar = 0; bar < NUM_BARS; ++bar)
        {
            int age = (bar < NUM_BARS / 2) ? (NUM_BARS / 2 - bar) : (bar - NUM_BARS / 2);
            int slot = (band_history_head_ + HISTORY_LENGTH - age) % HISTORY_LENGTH;

            float band_value = band_history_[band * HISTORY_LENGTH + slot];
            if (band_value == 0.0f)
                band_value = 0.01f;

            float height = band_value * 100.0f;
            auto bar_index = band * NUM_BARS + bar;
            barsPath.addRectangle(bar_x_positions_[bar_index], bounds.getHeight() / 2.0f - height / 2.0f, 4.0f, height);
        }
    }

    g.fillPath(barsPath);
}

void FxVisualizer::enablementChanged()
{
    if (isEnabled())
    {
        start();
    }
    else
    {
        pause();
        reset();
    }
}

void FxVisualizer::lookAndFeelChanged()
{
    calcGradient();
    repaint();
}

void FxVisualizer::calcGradient()
{
    float alpha = 0.75;
    if (FxController::getInstance().isAudioProcessing())
    {
        alpha = 1.0;
    }

    gradient_ = ColourGradient(isEnabled() ? Colour(FXCOLOR(GraphHigh)).withAlpha(alpha) : Colour(FXCOLOR(GraphHigh)).withSaturation(0.0f).withAlpha(alpha),
        2.0f, 0.0f,
        isEnabled() ? Colour(FXCOLOR(GraphHigh)).withAlpha(alpha) : Colour(FXCOLOR(GraphHigh)).withSaturation(0.0f).withAlpha(alpha),
        2.0f, 100.0f, false);
    if (isEnabled())
    {
        gradient_.addColour(0.5f, Colour(FXCOLOR(GraphLow)).withAlpha(alpha));
    }
    else
    {
        gradient_.addColour(0.5f, Colour(FXCOLOR(GraphLow)).withSaturation(0.0f).withAlpha(alpha));
    }
}
