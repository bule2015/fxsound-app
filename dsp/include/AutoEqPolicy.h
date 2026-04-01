#pragma once

namespace FxSound
{
namespace AutoEqPolicy
{
enum class Change
{
    PresetLoaded,
    FilterQChanged,
    BandCountChanged,
    BandFrequencyChanged,
    ManualBandGainChanged,
    ManualBandFrequencyChanged,
};

inline bool shouldResetAnalysisState(Change change)
{
    switch (change)
    {
    case Change::PresetLoaded:
    case Change::FilterQChanged:
    case Change::BandCountChanged:
    case Change::BandFrequencyChanged:
        return true;

    default:
        return false;
    }
}

inline bool shouldDisablePreservingCurrentEq(Change change)
{
    switch (change)
    {
    case Change::ManualBandGainChanged:
    case Change::ManualBandFrequencyChanged:
        return true;

    default:
        return false;
    }
}
}
}
