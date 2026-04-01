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

enum class Action
{
    None,
    ResetAnalysisState,
    DisablePreservingCurrentEq,
};

inline Action getActionForChange(Change change)
{
    switch (change)
    {
    case Change::PresetLoaded:
    case Change::FilterQChanged:
    case Change::BandCountChanged:
    case Change::BandFrequencyChanged:
        return Action::ResetAnalysisState;

    case Change::ManualBandGainChanged:
    case Change::ManualBandFrequencyChanged:
        return Action::DisablePreservingCurrentEq;
    }

    return Action::None;
}

inline bool shouldResetAnalysisState(Change change)
{
    return getActionForChange(change) == Action::ResetAnalysisState;
}

inline bool shouldDisablePreservingCurrentEq(Change change)
{
    return getActionForChange(change) == Action::DisablePreservingCurrentEq;
}
}
}
