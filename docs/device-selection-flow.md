# Device Selection Flow

This note documents the extracted device-selection logic shared by:

- [`FxController.cpp`](/D:/Downloads/FxSound/fxsound-app/fxsound/Source/GUI/FxController.cpp)
- [`OutputDeviceSelection.h`](/D:/Downloads/FxSound/fxsound-app/fxsound/Source/GUI/OutputDeviceSelection.h)
- [`FxSoundCoreTests.cpp`](/D:/Downloads/FxSound/fxsound-app/tests/FxSoundCoreTests/FxSoundCoreTests.cpp)

## Entry Points

The controller has five main device-related entry points:

- `initOutputs(...)`
  Startup path.
- `setOutput(...)`
  Manual output selection from the UI.
- `updateOutputs(...)`
  Processing-on synchronization path.
- `syncOutputWithSystemDefault(...)`
  Processing-off synchronization path.
- `handleSoundDeviceChange()`
  Callback-driven device-change path.

## Pure Helper Mapping

The controller delegates state resolution to pure helpers in `OutputDeviceSelection.h`.

| Controller entry point | Pure helper(s) |
| --- | --- |
| `initOutputs(...)` | `buildVisibleOutputDevices`, `makeOutputResolutionContext`, `buildInitDecision` |
| `setOutput(...)` | `buildManualSelectionDecision`, `buildAutoPresetDecision` |
| `updateOutputs(...)` | `scanProcessingOutputs`, `buildVisibleOutputDevices`, `makeOutputResolutionContext`, `buildSyncDecision`, `buildAutoPresetDecision` |
| `syncOutputWithSystemDefault(...)` | `buildVisibleOutputDevices`, `makeOutputResolutionContext`, `buildIdleSyncDecision`, `buildAutoPresetDecision` |
| `handleSoundDeviceChange()` | `shouldIgnoreDeviceChange`, then `updateOutputs(...)` or `syncOutputWithSystemDefault(...)` |

## Main Flow

```mermaid
flowchart TD
    A["Event"] --> B{"Controller entry point"}
    B -->|"Startup"| C["initOutputs(...)"]
    B -->|"Manual output change"| D["setOutput(...)"]
    B -->|"Device callback"| E["handleSoundDeviceChange()"]
    B -->|"Processing on"| F["updateOutputs(...)"]
    B -->|"Processing off"| G["syncOutputWithSystemDefault(...)"]

    C --> H["buildVisibleOutputDevices(...)"]
    F --> H
    G --> H

    C --> I["makeOutputResolutionContext(...)"]
    F --> I
    G --> I

    I --> J["buildInitDecision(...)"]
    I --> K["buildSyncDecision(...)"]
    I --> L["buildIdleSyncDecision(...)"]

    D --> M["buildManualSelectionDecision(...)"]
    E --> N["shouldIgnoreDeviceChange(...)"]

    N -->|"Ignore"| F
    N -->|"Refresh"| O["restartProcessingForDeviceChange()"]

    O --> P{"Timer running?"}
    P -->|"Yes"| F
    P -->|"No"| G

    J --> Q["applySelectedOutput(...)"]
    K --> Q
    L --> Q
    M --> Q

    K --> R["applyRoutingActions(...)"]
    M --> R

    Q --> S["FxModel::setSelectedOutput(...)"]
    Q --> T["saveSelectedOutputToSettings(...)"]

    R --> U["setAsPlaybackDevice(...)"]
    R --> V["restartProcessingForDeviceChange()"]
    R --> W["beginAudioProcessingGracePeriod()"]

    K --> X{"Mute?"}
    L --> Y{"Notify error?"}
    M --> Z{"Found output?"}

    X -->|"Yes"| AA["notifyPlaybackUnavailable(...)"]
    X -->|"No"| AB["tryApplyAutoPresetForCurrentOutput(...)"]

    Y -->|"Yes"| AA
    Y -->|"No"| AB

    Z -->|"No"| AC["mute + powerOff + push 'Output Disconnected'"]
    Z -->|"Yes"| AD["push 'Output:' message"]
```

## Device-Change Sequence

```mermaid
sequenceDiagram
    participant OS as "Windows / MMDevice"
    participant C as "FxController"
    participant O as "OutputDeviceSelection"
    participant A as "IAudioPassthru"
    participant M as "FxModel"

    OS->>C: onSoundDeviceChange(change_kind, device_id)
    C->>C: PostMessage(WMAPP_SOUND_DEVICE_CHANGE)
    C->>C: handleSoundDeviceChange()
    C->>A: getSoundDevices(false)
    C->>O: shouldIgnoreDeviceChange(...)

    alt "Unrelated change while selected output stays active and targeted"
        O-->>C: true
        C->>C: updateOutputs(sound_devices)
        C->>O: buildSyncDecision(...)
        O-->>C: SyncDecision
        C->>C: applySelectedOutput(...)
        Note over C,A: No processing restart
    else "Selected device changed, went inactive, or reconnected"
        O-->>C: false
        C->>C: beginAudioProcessingGracePeriod()
        C->>A: restartProcessingForDeviceChange()
        C->>A: getSoundDevices(false)

        alt "Processing on"
            C->>C: updateOutputs(sound_devices)
            C->>O: buildSyncDecision(...)
            O-->>C: SyncDecision
            C->>C: applySelectedOutput(...)
            C->>C: applyRoutingActions(...)
        else "Processing off"
            C->>C: syncOutputWithSystemDefault(sound_devices)
            C->>O: buildIdleSyncDecision(...)
            O-->>C: IdleSyncDecision
            C->>C: applySelectedOutput(...)
        end

        alt "Resolved output is inactive"
            C->>A: mute(true)
            C->>M: notifyOutputError()
        else "Resolved output is active"
            C->>C: tryApplyAutoPresetForCurrentOutput(...)
        end
    end
```

## Scenario Flows

The sections below show the concrete method-call flow for the main scenarios that
the extracted helpers are meant to cover.

### 1. Startup Restores the Last Selected Output

```mermaid
sequenceDiagram
    participant App as "FxController::init(...)"
    participant C as "FxController"
    participant O as "OutputDeviceSelection"
    participant A as "IAudioPassthru"
    participant M as "FxModel"

    App->>A: init()
    App->>A: getSoundDevices(false)
    App->>C: loadSelectedOutputFromSettings()
    App->>C: initOutputs(sound_devices)
    C->>O: buildVisibleOutputDevices(...)
    C->>O: makeOutputResolutionContext(...)
    C->>O: buildInitDecision(sound_devices, output_devices, context)
    O-->>C: InitDecision
    C->>C: rebuildOutputDeviceList(...)
    C->>C: applySelectedOutput(resolved_output)

    alt "Resolved output is active"
        C->>A: setAsPlaybackDevice(...)
        C->>M: setSelectedOutput(...)
    else "Resolved output is inactive"
        C->>M: setSelectedOutput(...)
        Note over C,A: Selected output stays visible but muted/inactive
    end
```

### 2. User Manually Selects an Active Output

```mermaid
sequenceDiagram
    participant UI as "Main window / tray"
    participant C as "FxController"
    participant O as "OutputDeviceSelection"
    participant A as "IAudioPassthru"
    participant M as "FxModel"

    UI->>C: setOutput(output_device_id, notify)
    C->>A: getSoundDevices(false)
    C->>O: buildManualSelectionDecision(...)
    O-->>C: ManualSelectionDecision

    alt "Output id resolves to a stereo device"
        C->>C: applySelectedOutput(selected_output, notify, true)
        C->>C: applyRoutingActions(selected_output, routing_actions)
        C->>C: tryApplyAutoPresetForCurrentOutput(true)
        C->>M: pushMessage("Output: ...")
    else "Output id is missing or invalid"
        C->>A: mute(true)
        C->>C: powerOn(false)
        C->>M: notifyOutputError()
    end
```

### 3. Unrelated Device Change Is Ignored While the Selected Output Stays Active

```mermaid
sequenceDiagram
    participant OS as "Windows callback"
    participant C as "FxController"
    participant O as "OutputDeviceSelection"
    participant A as "IAudioPassthru"

    OS->>C: onSoundDeviceChange(change_kind, device_id)
    C->>C: handleSoundDeviceChange()
    C->>A: getSoundDevices(false)
    C->>O: shouldIgnoreDeviceChange(...)
    O-->>C: true
    C->>C: updateOutputs(sound_devices)
    C->>O: scanProcessingOutputs(...)
    C->>O: buildVisibleOutputDevices(...)
    C->>O: makeOutputResolutionContext(...)
    C->>O: buildSyncDecision(...)
    O-->>C: SyncDecision
    C->>C: applySelectedOutput(...)
    Note over C,A: No restart or retarget occurs
```

### 4. The Selected Output Becomes Inactive

```mermaid
sequenceDiagram
    participant OS as "Windows callback"
    participant C as "FxController"
    participant O as "OutputDeviceSelection"
    participant A as "IAudioPassthru"
    participant M as "FxModel"

    OS->>C: onSoundDeviceChange(change_kind, selected_device_id)
    C->>C: handleSoundDeviceChange()
    C->>A: getSoundDevices(false)
    C->>O: shouldIgnoreDeviceChange(...)
    O-->>C: false
    C->>C: beginAudioProcessingGracePeriod()
    C->>A: restartProcessingForDeviceChange()
    C->>A: getSoundDevices(false)
    C->>C: updateOutputs(sound_devices)
    C->>O: buildVisibleOutputDevices(..., include_selected_inactive=true)
    C->>O: buildSyncDecision(...)
    O-->>C: SyncDecision{should_mute=true}
    C->>C: applySelectedOutput(inactive_selected_output)
    C->>A: mute(true)
    C->>M: notifyOutputError()
    Note over C,M: The inactive selected output stays visible and greyed out
```

### 5. An Inactive Selected Output Reconnects

```mermaid
sequenceDiagram
    participant OS as "Windows callback"
    participant C as "FxController"
    participant O as "OutputDeviceSelection"
    participant A as "IAudioPassthru"
    participant M as "FxModel"

    OS->>C: onSoundDeviceChange(change_kind, reconnected_device_id)
    C->>C: handleSoundDeviceChange()
    C->>A: getSoundDevices(false)
    C->>O: shouldIgnoreDeviceChange(...)
    O-->>C: false
    C->>A: restartProcessingForDeviceChange()
    C->>A: getSoundDevices(false)
    C->>C: updateOutputs(sound_devices)
    C->>O: buildVisibleOutputDevices(...)
    C->>O: makeOutputResolutionContext(...)
    C->>O: buildSyncDecision(...)
    O-->>C: SyncDecision
    C->>C: applySelectedOutput(reconnected_output)
    C->>C: applyRoutingActions(reconnected_output, routing_actions)
    C->>C: tryApplyAutoPresetForCurrentOutput(true)
    C->>M: setSelectedOutput(reconnected_output, ...)
    Note over C,O: Matching uses device_id, then container_id, then legacy name fallback
```

### 6. Idle Sync While Processing Is Off

```mermaid
sequenceDiagram
    participant Timer as "FxController::timerCallback()"
    participant C as "FxController"
    participant O as "OutputDeviceSelection"
    participant A as "IAudioPassthru"
    participant M as "FxModel"

    Timer->>A: getSoundDevices(false)
    Timer->>C: syncOutputWithSystemDefault(sound_devices)
    C->>O: buildVisibleOutputDevices(...)
    C->>O: makeOutputResolutionContext(...)
    C->>O: buildIdleSyncDecision(...)
    O-->>C: IdleSyncDecision
    C->>C: applySelectedOutput(resolved_output)

    alt "Resolved output is inactive"
        C->>M: notifyOutputError()
        Note over C,A: No backend restart while processing is off
    else "Resolved output is active"
        C->>C: tryApplyAutoPresetForCurrentOutput(true)
    end
```

## Scenario Summary

- Selected active output stays selected across unrelated device reconnects.
- Selected inactive output stays visible and muted.
- Reconnected selected output is matched by `device_id`, then `container_id`, then legacy name fallback.
- Manual selection of an active output can recover from a previously inactive selection.
- Mono outputs are excluded from priority building and manual selection.

## Test Coverage

`FxSoundCoreTests.cpp` covers both:

- pure helper decisions
- fake `IAudioPassthru` runtime scenarios

Important scenarios include:

- inactive selected output retention
- unselected inactive output removal
- reconnect with changed endpoint ID
- same-name different-container devices
- manual recovery after inactive selection
- idle sync without audio backend side effects
