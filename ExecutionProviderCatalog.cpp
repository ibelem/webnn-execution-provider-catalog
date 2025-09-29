// ExecutionProviderCatalog.cpp : Uses Microsoft.Windows.AI.MachineLearning API
//

#include <iostream>
#include "Microsoft.Windows.AI.MachineLearning.h"
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <roapi.h>
#include <winstring.h>
#include <vector>
#include <appmodel.h>

using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Microsoft::Windows::AI::MachineLearning;

// Type aliases for cleaner async operation types
using EnsureReadyAsyncOp = __FIAsyncOperationWithProgress_2_Microsoft__CWindows__CAI__CMachineLearning__CExecutionProviderReadyResult_double;
using EnsureReadyCompletedHandler = __FIAsyncOperationWithProgressCompletedHandler_2_Microsoft__CWindows__CAI__CMachineLearning__CExecutionProviderReadyResult_double;

// Needed for RoGetActivationFactory and RAII wrapper classes
// In Chromium, use base/win/core_winrt_util.h instead.
#pragma comment(lib, "runtimeobject.lib")

// Helper function to create and add Windows App Runtime dynamic dependency
HRESULT CreateWindowsAppRuntimeDependency()
{
    // Load kernelbase.dll and get function pointers
    HMODULE kernelbaseModule = LoadLibraryW(L"kernelbase.dll");
    if (!kernelbaseModule)
    {
        std::wcout << L"Failed to load kernelbase.dll. Error: " << GetLastError() << std::endl;
        return E_FAIL;
    }

    // Get function pointers
    typedef HRESULT(WINAPI* TryCreatePackageDependencyFunc)(
        PSID, PCWSTR, PACKAGE_VERSION, PackageDependencyProcessorArchitectures,
        PackageDependencyLifetimeKind, PCWSTR, CreatePackageDependencyOptions, PWSTR*);
    
    typedef HRESULT(WINAPI* AddPackageDependencyFunc)(
        PCWSTR, INT32, AddPackageDependencyOptions, PACKAGEDEPENDENCY_CONTEXT*, PWSTR*);

    auto tryCreateFunc = reinterpret_cast<TryCreatePackageDependencyFunc>(
        GetProcAddress(kernelbaseModule, "TryCreatePackageDependency"));
    
    auto addFunc = reinterpret_cast<AddPackageDependencyFunc>(
        GetProcAddress(kernelbaseModule, "AddPackageDependency"));

    if (!tryCreateFunc || !addFunc)
    {
        std::wcout << L"Failed to get package dependency function addresses from kernelbase.dll" << std::endl;
        FreeLibrary(kernelbaseModule);
        return E_FAIL;
    }

    const PCWSTR packageFamilyName = L"Microsoft.WindowsAppRuntime.1.8_8wekyb3d8bbwe";
    
    PACKAGE_VERSION minVersion = {};
    minVersion.Major = 0;
    minVersion.Minor = 0;
    minVersion.Build = 0;
    minVersion.Revision = 0;
    
    std::wcout << L"Creating dynamic dependency on " << packageFamilyName << std::endl;
    
    // Try to create the package dependency
    PWSTR packageDependencyId = nullptr;
    HRESULT hr = tryCreateFunc(
        nullptr, // user (nullptr = current user)
        packageFamilyName,
        minVersion,
        PackageDependencyProcessorArchitectures_None, 
        PackageDependencyLifetimeKind_Process, // Lifetime tied to this process - automatic cleanup
        nullptr, // lifetimeArtifact (nullptr for process lifetime)
        CreatePackageDependencyOptions_None,
        &packageDependencyId
    );
    
    if (FAILED(hr))
    {
        std::wcout << L"Failed to create package dependency. HRESULT: 0x" << std::hex << hr << std::endl;
        FreeLibrary(kernelbaseModule);
        return hr;
    }
    
    std::wcout << L"Successfully created package dependency with ID: " << packageDependencyId << std::endl;
    
    // Add the package dependency to the current process
    PACKAGEDEPENDENCY_CONTEXT dependencyContext = nullptr;
    PWSTR packageFullName = nullptr;
    hr = addFunc(
        packageDependencyId,
        PACKAGE_DEPENDENCY_RANK_DEFAULT, // Default rank
        AddPackageDependencyOptions_None,
        &dependencyContext,
        &packageFullName
    );
    
    if (FAILED(hr))
    {
        std::wcout << L"Failed to add package dependency. HRESULT: 0x" << std::hex << hr << std::endl;
        // Clean up the dependency ID on failure
        typedef HRESULT(WINAPI* DeletePackageDependencyFunc)(PCWSTR);
        auto deleteFunc = reinterpret_cast<DeletePackageDependencyFunc>(
            GetProcAddress(kernelbaseModule, "DeletePackageDependency"));
        if (deleteFunc)
        {
            deleteFunc(packageDependencyId);
        }
        HeapFree(GetProcessHeap(), 0, packageDependencyId);
        FreeLibrary(kernelbaseModule);
        return hr;
    }
    
    if (packageFullName)
    {
        std::wcout << L"Successfully added package dependency. Package: " << packageFullName << std::endl;
        HeapFree(GetProcessHeap(), 0, packageFullName);
    }
    else
    {
        std::wcout << L"Successfully added package dependency (package name not returned)" << std::endl;
    }
    
    // Clean up allocated strings (dependency will be automatically cleaned up on process exit)
    HeapFree(GetProcessHeap(), 0, packageDependencyId);
    FreeLibrary(kernelbaseModule);
    
    std::wcout << L"Package dependency will be automatically cleaned up when process exits" << std::endl;
    return S_OK;
}

int main()
{
    // Create dynamic dependency on Windows App Runtime
    // Note: Using PackageDependencyLifetimeKind_Process means automatic cleanup on process exit
    HRESULT hr = CreateWindowsAppRuntimeDependency();
    if (FAILED(hr))
    {
        std::wcout << L"Failed to create Windows App Runtime dependency." << std::endl;
        return -1; 
    }
    
    // Initialize Windows Runtime using RAII wrapper
    RoInitializeWrapper roInit(RO_INIT_MULTITHREADED);
    hr = roInit;
    if (FAILED(hr))
    {
        std::wcout << L"Failed to initialize Windows Runtime" << std::endl;
        return -1;
    }

    // Get the ExecutionProviderCatalog factory
    ComPtr<IExecutionProviderCatalogStatics> catalogStatics;
    hr = RoGetActivationFactory(
        HStringReference(RuntimeClass_Microsoft_Windows_AI_MachineLearning_ExecutionProviderCatalog).Get(),
        IID_PPV_ARGS(&catalogStatics));
    
    if (FAILED(hr))
    {
        std::wcout << L"Failed to get catalog factory" << std::endl;
        return -1;
    }

    // Get the default catalog
    ComPtr<IExecutionProviderCatalog> catalog;
    hr = catalogStatics->GetDefault(&catalog);
    if (FAILED(hr))
    {
        std::wcout << L"Failed to get default catalog" << std::endl;
        return -1;
    }

    std::wcout << L"Got default ExecutionProviderCatalog" << std::endl;

    // Find all providers
    UINT32 providerCount = 0;
    IExecutionProvider** providers = nullptr;
    hr = catalog->FindAllProviders(&providerCount, &providers);
    if (FAILED(hr))
    {
        std::wcout << L"Failed to find providers" << std::endl;
        return -1;
    }

    std::wcout << L"Found " << providerCount << L" providers" << std::endl;

    // Transfer ownership to ComPtr vector to avoid leaks
    std::vector<ComPtr<IExecutionProvider>> providerVector;
    providerVector.reserve(providerCount);
    for (UINT32 i = 0; i < providerCount; i++)
    {
        ComPtr<IExecutionProvider> provider;
        provider.Attach(providers[i]); // Takes ownership
        providerVector.push_back(provider);
    }
    
    // Clean up the raw array (pointers are now owned by ComPtr objects)
    CoTaskMemFree(providers);
    providers = nullptr;

    // Process each provider
    for (UINT32 i = 0; i < providerCount; i++)
    {
        const auto& provider = providerVector[i];
        std::wcout << L"Provider " << (i + 1) << L":" << std::endl;

        // Get and display provider name and library path
        HString providerName;
        hr = provider->get_Name(providerName.GetAddressOf());
        if (SUCCEEDED(hr) && providerName.IsValid())
        {
            UINT32 nameLength;
            const wchar_t* nameString = providerName.GetRawBuffer(&nameLength);
            if (nameString && nameLength > 0)
            {
                std::wcout << L"    Name: " << std::wstring(nameString, nameLength) << std::endl;
            }
        }
        else
        {
            std::wcout << L"    Name: [Failed to retrieve]" << std::endl;
        }

        // Call EnsureReadyAsync and wait for completion
        std::wcout << L"    Calling EnsureReadyAsync..." << std::endl;
        ComPtr<EnsureReadyAsyncOp> ensureOp;
        hr = provider->EnsureReadyAsync(&ensureOp);
        if (FAILED(hr))
        {
            std::wcout << L"    [ERROR] EnsureReadyAsync failed to start: 0x" << std::hex << hr << std::endl;
            continue;
        }

        std::wcout << L"    EnsureReadyAsync started successfully" << std::endl;
        
        // Create an event to wait for completion
        Event completionEvent;
        completionEvent.Attach(CreateEvent(nullptr, TRUE, FALSE, nullptr));
        if (!completionEvent.IsValid())
        {
            std::wcout << L"    [ERROR] Failed to create completion event" << std::endl;
            continue;
        }

        // Set up completion handler using a lambda
        auto completedHandler = Microsoft::WRL::Callback<EnsureReadyCompletedHandler>(
            [&completionEvent](EnsureReadyAsyncOp* asyncOp, ABI::Windows::Foundation::AsyncStatus status) -> HRESULT
            {
                // Signal the event when operation completes
                SetEvent(completionEvent.Get());
                return S_OK;
            });

        if (!completedHandler)
        {
            std::wcout << L"    [ERROR] Failed to create completion handler" << std::endl;
            continue;
        }

        // Set the completion handler
        hr = ensureOp->put_Completed(completedHandler.Get());
        if (FAILED(hr))
        {
            std::wcout << L"    [ERROR] Failed to set completion handler: 0x" << std::hex << hr << std::endl;
            continue;
        }

        std::wcout << L"    Waiting for EnsureReadyAsync to complete..." << std::endl;
        
        // Wait for the operation to complete (with timeout)
        DWORD waitResult = WaitForSingleObject(completionEvent.Get(), INFINITE);
        
        if (waitResult == WAIT_TIMEOUT)
        {
            std::wcout << L"    [WARNING] Timeout waiting for EnsureReadyAsync to complete" << std::endl;
            // Cancel the operation via IAsyncInfo
            ComPtr<IAsyncInfo> asyncInfo;
            hr = ensureOp.As(&asyncInfo);
            if (SUCCEEDED(hr))
            {
                asyncInfo->Cancel();
            }
            continue;
        }

        if (waitResult != WAIT_OBJECT_0)
        {
            std::wcout << L"    [ERROR] Wait failed with error: " << GetLastError() << std::endl;
            continue;
        }

        // Operation completed, check the status via IAsyncInfo
        ComPtr<IAsyncInfo> asyncInfo;
        hr = ensureOp.As(&asyncInfo);
        if (FAILED(hr))
        {
            std::wcout << L"    [ERROR] Failed to get IAsyncInfo interface: 0x" << std::hex << hr << std::endl;
            continue;
        }

        ABI::Windows::Foundation::AsyncStatus status;
        hr = asyncInfo->get_Status(&status);
        if (FAILED(hr))
        {
            std::wcout << L"    [ERROR] Failed to get async status: 0x" << std::hex << hr << std::endl;
            continue;
        }

        switch (status)
        {
        case ABI::Windows::Foundation::AsyncStatus::Completed:
            {
                // Get the result
                ComPtr<IExecutionProviderReadyResult> result;
                hr = ensureOp->GetResults(&result);
                if (FAILED(hr) || !result)
                {
                    std::wcout << L"    [ERROR] Failed to get operation result: 0x" << std::hex << hr << std::endl;
                    continue;
                }

                ExecutionProviderReadyResultState resultState;
                hr = result->get_Status(&resultState);
                if (FAILED(hr))
                {
                    std::wcout << L"    [ERROR] Failed to get result status: 0x" << std::hex << hr << std::endl;
                    continue;
                }

                switch (resultState)
                {
                case ExecutionProviderReadyResultState_Success:
                    std::wcout << L"    [SUCCESS] Provider ready successfully" << std::endl;
                    break;
                case ExecutionProviderReadyResultState_Failure:
                    {
                        HRESULT extendedError;
                        HString diagnosticText;
                        result->get_ExtendedError(&extendedError);
                        result->get_DiagnosticText(diagnosticText.GetAddressOf());
                        std::wcout << L"    [ERROR] Provider failed to become ready. Error: 0x" << std::hex << extendedError;
                        if (diagnosticText.IsValid())
                        {
                            UINT32 length;
                            const wchar_t* rawString = diagnosticText.GetRawBuffer(&length);
                            if (rawString && length > 0)
                            {
                                std::wcout << L" - " << std::wstring(rawString, length);
                            }
                        }
                        std::wcout << std::endl;
                    }
                    break;
                case ExecutionProviderReadyResultState_InProgress:
                    std::wcout << L"    [WARNING] Provider still in progress (unexpected)" << std::endl;
                    break;
                }
            }
            break;
        case ABI::Windows::Foundation::AsyncStatus::Error:
            {
                HRESULT errorCode;
                hr = asyncInfo->get_ErrorCode(&errorCode);
                std::wcout << L"    [ERROR] Operation failed with error: 0x" << std::hex << (SUCCEEDED(hr) ? errorCode : hr) << std::endl;
            }
            break;
        case ABI::Windows::Foundation::AsyncStatus::Canceled:
            std::wcout << L"    [WARNING] Operation was canceled" << std::endl;
            break;
        case ABI::Windows::Foundation::AsyncStatus::Started:
            std::wcout << L"    [WARNING] Operation still running (unexpected)" << std::endl;
            break;
        }

        // get_LibraryPath can only be called
        HString libraryPath;
        hr = provider->get_LibraryPath(libraryPath.GetAddressOf());
        if (SUCCEEDED(hr) && libraryPath.IsValid())
        {
            UINT32 pathLength;
            const wchar_t* pathString = libraryPath.GetRawBuffer(&pathLength);
            if (pathString && pathLength > 0)
            {
                std::wcout << L"    Library Path: " << std::wstring(pathString, pathLength) << std::endl;
            }
        }
        else
        {
            std::wcout << L"    Library Path: [Failed to retrieve]" << std::endl;
        }

    }

    // RoInitializeWrapper automatically calls RoUninitialize() in its destructor

    return 0;
}