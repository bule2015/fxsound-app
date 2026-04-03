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

#pragma once

#include <JuceHeader.h>
#include <array>

//==============================================================================
/*
*/
#if JUCE_MAJOR_VERSION >=8
class FxVisualizer : public Component
#else
class FxVisualizer : public AnimatedAppComponent
#endif
{
public:
    static constexpr int SPECTRUM_BANDS = 10;
    static constexpr int HISTORY_LENGTH = 6;

    FxVisualizer();

    void start();
    void pause();

    void reset();
#if JUCE_MAJOR_VERSION >=8
    void update();
#else
	void update() override;
#endif

    void calcGradient();

private:
    static constexpr int WIDTH = 960;
    static constexpr int HEIGHT = 120;
    static constexpr int NUM_BARS = 10;
    static constexpr int TOTAL_BARS = SPECTRUM_BANDS * NUM_BARS;

    void paint(Graphics& g) override;
    void enablementChanged() override;
	void lookAndFeelChanged() override;
    void rebuildBarLayout();

    std::array<float, SPECTRUM_BANDS> band_values_{};
    std::array<float, SPECTRUM_BANDS * HISTORY_LENGTH> band_history_{};
    std::array<float, TOTAL_BARS> bar_x_positions_{};
    int band_history_head_ = 0;
    ColourGradient gradient_;

#if JUCE_MAJOR_VERSION >=8
    std::unique_ptr<juce::VBlankAttachment> vblank_listener_;
#endif
};
