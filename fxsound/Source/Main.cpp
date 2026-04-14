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

#include <JuceHeader.h>
#include <comdef.h>
#include "GUI/FxSystemTrayView.h"
#include "GUI/FxController.h"
#include "GUI/FxTheme.h"
#include "GUI/FxMainWindow.h"
#include "GUI/DefaultPlaybackRestorePolicy.h"
#include "AudioPassthru.h"
#include <dbghelp.h>

#pragma comment(lib, "dbghelp.lib")

// Do not use high-performance GPU on laptops with dual graphics adapters
extern "C" {
    __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000000;

    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 0;
}

namespace
{
constexpr auto kFxSoundMessageWindowName = L"FxSoundHotkeys";

DWORD getCurrentSessionId()
{
    DWORD session_id = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &session_id);
    return session_id;
}

String buildSessionInstanceMutexName()
{
    return "Local\\FxSound_" + String(getCurrentSessionId());
}

bool signalExistingSessionInstance()
{
    for (int attempt = 0; attempt < 40; ++attempt)
    {
        if (auto existing_window = ::FindWindowW(nullptr, kFxSoundMessageWindowName))
        {
            return ::PostMessage(existing_window, FxController::WMAPP_SHOW_MAIN_WINDOW, 0, 0) != FALSE;
        }

        Thread::sleep(50);
    }

    return false;
}
}

//==============================================================================
class FxSoundApplication : public JUCEApplication
{
public:
    //==============================================================================
    FxSoundApplication() { }

    const String getApplicationName() override       { return ProjectInfo::projectName; }
    const String getApplicationVersion() override    { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override       { return true; }

    //==============================================================================
    void initialise (const String& commandline) override
    {
        // This method is where you should put your application's initialisation code..
        try
        {
            if (!acquireSessionInstanceLock())
            {
                signalExistingSessionInstance();
                quit();
                return;
            }

            SetUnhandledExceptionFilter(unhandledExceptionFilter);

            HRESULT hRes = CoInitializeEx(0, COINIT_MULTITHREADED);
            if (SUCCEEDED(hRes))
            {
                com_initialized_ = true;
                CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_DEFAULT,
                    RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE, NULL);
            }
            
            LookAndFeel::setDefaultLookAndFeel(&theme_);

            setWorkingDirectory();

            FxController::getInstance().config(commandline);

            audio_passthru_ = std::make_unique<AudioPassthru>();
            main_window_ = std::make_unique<FxMainWindow>();
            system_tray_view_.reset(new FxSystemTrayView());

            FxController::getInstance().init(main_window_.get(), system_tray_view_.get(), audio_passthru_.get());
        }
        catch (const std::exception& e)
        {
            auto& controller = FxController::getInstance();
            controller.logMessage(String::formatted("std::exception: %s\n", e.what()));

            CaptureAndLogCallStack();

            quit();
        }
        catch (...)
        {
            auto& controller = FxController::getInstance();
            controller.logMessage("Unknown exception\n");

            CaptureAndLogCallStack();

            quit();
        }
    }

    void suspended() override
    {
        FxController::getInstance().stopTimer();
    }

    void resumed() override
    {
        FxController::getInstance().startTimer(100);
    }

    void shutdown() override
    {
        restoreDefaultPlaybackDeviceIfNeeded();

        if (main_window_.get() != nullptr)
        {
            // Add your application's shutdown code here..
            FxController::getInstance().autoSaveModifiedPreset();
            FxController::getInstance().releaseRuntimeObjects();

            audio_passthru_.reset();

            system_tray_view_.reset();

            main_window_.reset(); // (deletes our window)
        }

        LookAndFeel::setDefaultLookAndFeel(nullptr);

        releaseSessionInstanceLock();
        if (com_initialized_)
        {
            CoUninitialize();
            com_initialized_ = false;
        }
    }

    //==============================================================================
    void systemRequestedQuit() override
    {
        // This is called when the app is being asked to quit: you can ignore this
        // request and let the app carry on running, or call quit() to allow the app to close.

        restoreDefaultPlaybackDeviceIfNeeded();
        quit();
    }

    void anotherInstanceStarted (const String&) override
    {
        FxController::getInstance().showMainWindow();
    }

private:
    FxTheme theme_;
    static constexpr int MAX_FRAMES = 64;
    HANDLE session_instance_mutex_ = nullptr;
    bool com_initialized_ = false;
    bool default_playback_restore_attempted_ = false;

    bool acquireSessionInstanceLock()
    {
        auto mutex_name = buildSessionInstanceMutexName();
        session_instance_mutex_ = ::CreateMutexW(nullptr, TRUE, mutex_name.toWideCharPointer());

        if (session_instance_mutex_ == nullptr)
        {
            return true;
        }

        if (::GetLastError() == ERROR_ALREADY_EXISTS)
        {
            ::CloseHandle(session_instance_mutex_);
            session_instance_mutex_ = nullptr;
            return false;
        }

        return true;
    }

    void releaseSessionInstanceLock()
    {
        if (session_instance_mutex_ != nullptr)
        {
            ::ReleaseMutex(session_instance_mutex_);
            ::CloseHandle(session_instance_mutex_);
            session_instance_mutex_ = nullptr;
        }
    }

    void restoreDefaultPlaybackDeviceIfNeeded()
    {
        if (default_playback_restore_attempted_)
        {
            return;
        }

        auto restore_result = FxController::getInstance().bestEffortRestoreDefaultPlaybackDevice(false);
        default_playback_restore_attempted_ = FxSound::DefaultPlaybackRestorePolicy::shouldMarkRestoreAttempted(restore_result);
    }

    static LONG WINAPI unhandledExceptionFilter(EXCEPTION_POINTERS* exception_info)
    {
        auto& controller = FxController::getInstance();
     
        auto logPath = File::getSpecialLocation(File::userApplicationDataDirectory).getChildFile("FxSound");
        if (!logPath.exists()) {
            logPath.createDirectory();
        }

        juce::File dumpFile = logPath.getChildFile("fxsound.dmp");
        HANDLE hFile = CreateFileW(dumpFile.getFullPathName().toWideCharPointer(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL, nullptr);

        if (hFile != INVALID_HANDLE_VALUE)
        {
            MINIDUMP_EXCEPTION_INFORMATION dumpInfo;
            dumpInfo.ThreadId = GetCurrentThreadId();
            dumpInfo.ExceptionPointers = exception_info;
            dumpInfo.ClientPointers = FALSE;

            MiniDumpWriteDump(GetCurrentProcess(),
                GetCurrentProcessId(),
                hFile,
                MiniDumpNormal,
                &dumpInfo,
                nullptr,
                nullptr);

            CloseHandle(hFile);
        }

        String message = String::formatted(
            "Unhandled exception\nException code: 0x%X\nException flags: 0x%X\nException address: 0x%p\n",
            exception_info->ExceptionRecord->ExceptionCode,
            exception_info->ExceptionRecord->ExceptionFlags,
            exception_info->ExceptionRecord->ExceptionAddress
        );

        controller.logMessage("\n" + message);

        // Capture stack based on crash context
        CaptureAndLogCallStack(exception_info->ContextRecord);

        return EXCEPTION_EXECUTE_HANDLER;
    }

    static void CaptureAndLogCallStack(CONTEXT* context = nullptr)
    {
        auto& controller = FxController::getInstance();
        String stacktrace_info;

        HANDLE process = GetCurrentProcess();
        HANDLE thread = GetCurrentThread();

        SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
        SymInitialize(process, NULL, TRUE);

        if (context == nullptr)
        {
            // No crash context - capture live callstack
            void* frames[MAX_FRAMES];
            USHORT capturedFrames = CaptureStackBackTrace(0, MAX_FRAMES, frames, NULL);

            char buffer[sizeof(SYMBOL_INFO) + 256] = { 0 };
            SYMBOL_INFO* symbol = (SYMBOL_INFO*)buffer;
            symbol->MaxNameLen = 255;
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);

            for (USHORT i = 0; i < capturedFrames; i++)
            {
                if (SymFromAddr(process, (DWORD64)frames[i], 0, symbol))
                {
                    stacktrace_info += String::formatted("%S at 0x%llX\n", symbol->Name, symbol->Address);

                    DWORD displacement;
                    IMAGEHLP_LINE64 line = { sizeof(IMAGEHLP_LINE64) };
                    if (SymGetLineFromAddr64(process, (DWORD64)frames[i], &displacement, &line))
                    {
                        char* file_name = std::strrchr(line.FileName, '\\');
                        file_name = file_name ? (file_name + 1) : line.FileName;
                        stacktrace_info += String::formatted("    File: %S, Line: %d\n", file_name, line.LineNumber);
                    }
                }
                else
                {
                    stacktrace_info += String::formatted("Unknown function at 0x%p\n", frames[i]);
                }
            }
        }
        else
        {
            // Crash context - walk using StackWalk64
            STACKFRAME64 stackFrame = { 0 };
#if defined(_M_IX86)
            DWORD machineType = IMAGE_FILE_MACHINE_I386;
            stackFrame.AddrPC.Offset = context->Eip;
            stackFrame.AddrPC.Mode = AddrModeFlat;
            stackFrame.AddrFrame.Offset = context->Ebp;
            stackFrame.AddrFrame.Mode = AddrModeFlat;
            stackFrame.AddrStack.Offset = context->Esp;
            stackFrame.AddrStack.Mode = AddrModeFlat;
#elif defined(_M_X64)
            DWORD machineType = IMAGE_FILE_MACHINE_AMD64;
            stackFrame.AddrPC.Offset = context->Rip;
            stackFrame.AddrPC.Mode = AddrModeFlat;
            stackFrame.AddrFrame.Offset = context->Rsp;
            stackFrame.AddrFrame.Mode = AddrModeFlat;
            stackFrame.AddrStack.Offset = context->Rsp;
            stackFrame.AddrStack.Mode = AddrModeFlat;
#elif defined(_M_ARM64)
            DWORD machineType = IMAGE_FILE_MACHINE_ARM64;
            stackFrame.AddrPC.Offset = context->Pc;
            stackFrame.AddrPC.Mode = AddrModeFlat;
            stackFrame.AddrFrame.Offset = context->Fp;
            stackFrame.AddrFrame.Mode = AddrModeFlat;
            stackFrame.AddrStack.Offset = context->Sp;
            stackFrame.AddrStack.Mode = AddrModeFlat;
#else
#error Unsupported platform
#endif

            char buffer[sizeof(SYMBOL_INFO) + 256] = { 0 };
            SYMBOL_INFO* symbol = (SYMBOL_INFO*)buffer;
            symbol->MaxNameLen = 255;
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);

            for (int frame = 0; frame < MAX_FRAMES; ++frame)
            {
                if (!StackWalk64(machineType, process, thread, &stackFrame, context, NULL,
                    SymFunctionTableAccess64, SymGetModuleBase64, NULL))
                {
                    break;
                }

                if (stackFrame.AddrPC.Offset == 0)
                    break;

                DWORD64 address = stackFrame.AddrPC.Offset;

                if (SymFromAddr(process, address, 0, symbol))
                {
                    stacktrace_info += String::formatted("%S at 0x%llX\n", symbol->Name, symbol->Address);

                    DWORD displacement;
                    IMAGEHLP_LINE64 line = { sizeof(IMAGEHLP_LINE64) };
                    if (SymGetLineFromAddr64(process, address, &displacement, &line))
                    {
                        char* file_name = std::strrchr(line.FileName, '\\');
                        file_name = file_name ? (file_name + 1) : line.FileName;
                        stacktrace_info += String::formatted("    File: %S, Line: %d\n", file_name, line.LineNumber);
                    }
                }
                else
                {
                    stacktrace_info += String::formatted("Unknown function at 0x%llX\n", address);
                }
            }
        }

        SymCleanup(process);

        controller.logMessage(stacktrace_info);
    }

    void setWorkingDirectory()
    {
        wchar_t module_file[MAX_PATH];
        DWORD length = GetModuleFileName(NULL, module_file, MAX_PATH);
        if (length > 0)
        {
            std::wstring path(module_file);
            size_t pos = path.find_last_of(L"\\/");
            if (pos != std::string::npos)
            {
                SetCurrentDirectory(path.substr(0, pos).c_str());
            }
        }
    };

    std::unique_ptr<FxMainWindow> main_window_;
    
    std::unique_ptr<FxSystemTrayView> system_tray_view_;
    std::unique_ptr<AudioPassthru> audio_passthru_;
};

//==============================================================================
// This macro generates the main() routine that launches the app.
START_JUCE_APPLICATION (FxSoundApplication)
