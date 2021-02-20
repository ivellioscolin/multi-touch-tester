// MIT License

// Copyright (c) 2021- Colin Xu <colin.xu@gmail.com>

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_win32.h"
#include "imgui/backends/imgui_impl_dx11.h"

#include <tchar.h>
#include <stdio.h>
#include <time.h>
#include <unordered_map>

#include <windows.h>
#include <windowsx.h>
#include <VersionHelpers.h>
//#include <shellscalingapi.h>
#include <d3d11.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

#include <initguid.h>
#include <devguid.h>
#include <Devpkey.h>
#include <hidusage.h>
#include <hidclass.h>
#include <SetupAPI.h>

struct TOUCH_INFO
{
    PBYTE pDesc;
    DWORD dwVendor;
    DWORD dwProduct;
    DWORD dwVersion;
    USHORT usUsagePage;
    USHORT usUsage;
    BOOLEAN bUsing;
};

struct FINGER_INFO
{
    DWORD dwContactID;
    HANDLE hDevice;
    BOOLEAN bContactArea;
    BOOLEAN bTimeStamp;
    LONG x;
    LONG y;
    DWORD dwWidth;
    DWORD dwHeight;
    DWORD dwTimeStamp;
    ImU32 color;
    BOOLEAN dirty;
};

static ID3D11Device *g_pd3dDevice = NULL;
static ID3D11DeviceContext *g_pd3dDeviceContext = NULL;
static IDXGISwapChain *g_pSwapChain = NULL;
static ID3D11RenderTargetView *g_mainRenderTargetView = NULL;
static std::unordered_map<HANDLE, TOUCH_INFO> g_devices;
static std::unordered_map<DWORD, FINGER_INFO> g_fingers;
static CRITICAL_SECTION g_cs;

bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                      _In_opt_ HINSTANCE hPrevInstance,
                      _In_ LPWSTR lpCmdLine,
                      _In_ int nCmdShow)
{
    srand((UINT)time(NULL));
    ::InitializeCriticalSection(&g_cs);

    {
        UINT ret = 0;
        DWORD err = 0;
        UINT nDevices;
        if (0 == ::GetRawInputDeviceList(NULL, &nDevices, sizeof(RAWINPUTDEVICELIST)))
        {
            PRAWINPUTDEVICELIST pRawInputDeviceList = (PRAWINPUTDEVICELIST)malloc(sizeof(RAWINPUTDEVICELIST) * nDevices);
            if (pRawInputDeviceList)
            {
                if (-1 != ::GetRawInputDeviceList(pRawInputDeviceList, &nDevices, sizeof(RAWINPUTDEVICELIST)))
                {
                    for (UINT dev = 0; dev < nDevices; dev++)
                    {
                        if (pRawInputDeviceList[dev].dwType == RIM_TYPEMOUSE)
                        {}
                        else if (pRawInputDeviceList[dev].dwType == RIM_TYPEKEYBOARD)
                        {}
                        else if (pRawInputDeviceList[dev].dwType == RIM_TYPEHID)
                        {
                            UINT ret = 0;
                            UINT size = 0;
                            RID_DEVICE_INFO info = {0};
                            info.cbSize = sizeof(RID_DEVICE_INFO);
                            size = sizeof(RID_DEVICE_INFO);

                            ret = ::GetRawInputDeviceInfo(pRawInputDeviceList[dev].hDevice, RIDI_DEVICEINFO, &info, &size);
                            if (0 == ret || 0xFFFFFFFF == ret)
                            {
                                continue;
                            }

                            if (HID_USAGE_PAGE_DIGITIZER != info.hid.usUsagePage)
                            {
                                continue;
                            }
                            g_devices[pRawInputDeviceList[dev].hDevice] = {0};
                            g_devices[pRawInputDeviceList[dev].hDevice].dwVendor = info.hid.dwVendorId;
                            g_devices[pRawInputDeviceList[dev].hDevice].dwProduct = info.hid.dwProductId;
                            g_devices[pRawInputDeviceList[dev].hDevice].dwVersion = info.hid.dwVersionNumber;
                            g_devices[pRawInputDeviceList[dev].hDevice].usUsagePage = info.hid.usUsagePage;
                            g_devices[pRawInputDeviceList[dev].hDevice].usUsage = info.hid.usUsage;

                            // if (0 == ::GetRawInputDeviceInfo(pRawInputDeviceList[dev].hDevice, RIDI_PREPARSEDDATA, NULL, &size))
                            // {
                            //     pBuffer = (PHIDP_PREPARSED_DATA)malloc(size);
                            //     if (::GetRawInputDeviceInfo(pRawInputDeviceList[dev].hDevice, RIDI_PREPARSEDDATA, (PHIDP_PREPARSED_DATA)pBuffer, &size))
                            //     {
                            //     }
                            //     free(pBuffer);
                            // }

                            if (0 == ::GetRawInputDeviceInfo(pRawInputDeviceList[dev].hDevice, RIDI_DEVICENAME, NULL, &size))
                            {
                                PVOID pHidName = malloc(sizeof(WCHAR) * size);

                                ret = ::GetRawInputDeviceInfo(pRawInputDeviceList[dev].hDevice, RIDI_DEVICENAME, pHidName, &size);
                                if (ret && ret != 0xFFFFFFFF)
                                {
                                    HDEVINFO hHidDev = ::SetupDiGetClassDevs(&GUID_DEVCLASS_HIDCLASS, NULL, 0, DIGCF_PRESENT);
                                    if (INVALID_HANDLE_VALUE != hHidDev)
                                    {
                                        SP_DEVINFO_DATA hidDevInfoData = { 0 };
                                        hidDevInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
                                        for (DWORD devIdx = 0;
                                             ::SetupDiEnumDeviceInfo(hHidDev, devIdx, &hidDevInfoData);
                                             devIdx++)
                                        {
                                            DWORD bufSize = 0;
                                            DEVPROPTYPE PropType;
                                            if (0 == ::SetupDiGetDeviceProperty(hHidDev, &hidDevInfoData, &DEVPKEY_Device_InstanceId, &PropType, NULL, 0, &bufSize, 0))
                                            {
                                                PBYTE pInstanceBuf = (PBYTE)malloc(bufSize);
                                                if (::SetupDiGetDeviceProperty(hHidDev, &hidDevInfoData, &DEVPKEY_Device_InstanceId, &PropType, pInstanceBuf, bufSize, NULL, 0))
                                                {
                                                    HDEVINFO hHidInterface = ::SetupDiGetClassDevs(&GUID_DEVINTERFACE_HID, (PCWSTR)pInstanceBuf, NULL, DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);
                                                    if (INVALID_HANDLE_VALUE != hHidInterface)
                                                    {
                                                        SP_DEVINFO_DATA hidInterfaceDevData = {0};
                                                        hidInterfaceDevData.cbSize = sizeof(hidInterfaceDevData);
                                                        if (::SetupDiOpenDeviceInfo(hHidInterface, (PCWSTR)pInstanceBuf, NULL, DIOD_INHERIT_CLASSDRVS, &hidInterfaceDevData))
                                                        {
                                                            SP_DEVICE_INTERFACE_DATA hidInterfaceData = { 0 };
                                                            hidInterfaceData.cbSize = sizeof(hidInterfaceData);
                                                            for (DWORD interfaceIdx = 0;
                                                                 ::SetupDiEnumDeviceInterfaces(hHidInterface, &hidInterfaceDevData, &GUID_DEVINTERFACE_HID, interfaceIdx, &hidInterfaceData);
                                                                 interfaceIdx++)
                                                            {
                                                                if (0 == ::SetupDiGetDeviceInterfaceDetail(hHidInterface, &hidInterfaceData, NULL, 0, &bufSize, NULL))
                                                                {
                                                                    PSP_DEVICE_INTERFACE_DETAIL_DATA pDeviceInterfaceDetailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA)malloc(bufSize);
                                                                    pDeviceInterfaceDetailData->cbSize = sizeof(PSP_DEVICE_INTERFACE_DETAIL_DATA);
                                                                    if (::SetupDiGetDeviceInterfaceDetail(hHidInterface, &hidInterfaceData, pDeviceInterfaceDetailData, bufSize, NULL, NULL))
                                                                    {
                                                                        if (0 == ::_wcsicmp(pDeviceInterfaceDetailData->DevicePath, (PCWSTR)pHidName))
                                                                        {
                                                                            if (0 == ::SetupDiGetDeviceProperty(hHidDev, &hidDevInfoData, &DEVPKEY_Device_DeviceDesc, &PropType, NULL, 0, &bufSize, 0))
                                                                            {
                                                                                PBYTE pDescBuf = (PBYTE)malloc(bufSize);
                                                                                if (::SetupDiGetDeviceProperty(hHidDev, &hidDevInfoData, &DEVPKEY_Device_DeviceDesc, &PropType, pDescBuf, bufSize, NULL, 0))
                                                                                {
                                                                                    int mbSize = ::WideCharToMultiByte(CP_UTF8, 0, (LPCWCH)pDescBuf, (int)wcsnlen_s((LPCWCH)pDescBuf, bufSize), NULL, 0, NULL, NULL);
                                                                                    g_devices[pRawInputDeviceList[dev].hDevice].pDesc = (PBYTE)malloc(mbSize + 1);
                                                                                    if (g_devices[pRawInputDeviceList[dev].hDevice].pDesc)
                                                                                    {
                                                                                        ::WideCharToMultiByte(CP_UTF8, 0, (LPCWCH)pDescBuf, (int)wcsnlen_s((LPCWCH)pDescBuf, bufSize), (LPSTR)g_devices[pRawInputDeviceList[dev].hDevice].pDesc, mbSize, NULL, NULL);
                                                                                        g_devices[pRawInputDeviceList[dev].hDevice].pDesc[mbSize] = '\0';
                                                                                    }
                                                                                }
                                                                                free(pDescBuf);
                                                                            }
                                                                        }
                                                                    }
                                                                    free(pDeviceInterfaceDetailData);
                                                                }
                                                            }
                                                        }
                                                        ::SetupDiDestroyDeviceInfoList(hHidInterface);
                                                    }
                                                }
                                                free(pInstanceBuf);
                                            }
                                        }
                                        ::SetupDiDestroyDeviceInfoList(hHidDev);
                                    }
                                }
                                free(pHidName);
                            }
                        }
                    }
                }
                free(pRawInputDeviceList);
            }
        }
    }

    // Rely on imgui to set correct DPI awareness instead of declare in manifest
    ImGui_ImplWin32_EnableDpiAwareness();
    WNDCLASSEX wc = {sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, _T("ImGui Example"), NULL};
    ::RegisterClassEx(&wc);
    HWND hwnd = ::CreateWindow(wc.lpszClassName, _T("Multi-Touch Tester"), WS_OVERLAPPEDWINDOW, 100, 100, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, wc.hInstance, NULL);

    ::RegisterTouchWindow(hwnd, TWF_FINETOUCH | TWF_WANTPALM);

    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        ::UnregisterClass(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    (void)io;
    io.IniFilename = NULL;

    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    {
        ImFontConfig fontCfg = ImFontConfig();
        float dpiRatio = 1.0f;

        // Not working until we embed the manifest file
        // if (IsWindows8Point1OrGreater())
        // {
        //     HMONITOR hMonitor = ::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONULL);

        //     if (hMonitor)
        //     {
        //         UINT dpiX, dpiY;
        //         if (S_OK == ::GetDpiForMonitor(hMonitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))
        //         {
        //             dpiRatio = (float)dpiX / (float)USER_DEFAULT_SCREEN_DPI;
        //         }
        //     }
        // }
        // else
        {
            HDC screen = GetDC(0);
            float dpiX, dpiY;
            dpiX = (float)(GetDeviceCaps(screen, LOGPIXELSX));
            dpiY = (float)(GetDeviceCaps(screen, LOGPIXELSY));
            ReleaseDC(0, screen);
            dpiRatio = dpiX / (float)USER_DEFAULT_SCREEN_DPI;
        }

        fontCfg.SizePixels = 13.0f * dpiRatio;
        io.Fonts->AddFontDefault(&fontCfg);
    }

    // Clear RT as black to make visualized contact vivid
    ImVec4 clear_color = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);

    MSG msg;
    ZeroMemory(&msg, sizeof(msg));
    while (msg.message != WM_QUIT)
    {
        // Poll and handle messages (inputs, window resize, etc.)
        // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
        // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application.
        // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application.
        // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
        if (::PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            continue;
        }

        EnterCriticalSection(&g_cs);
        std::unordered_map<DWORD, FINGER_INFO> fingerStatus = g_fingers;
        LeaveCriticalSection(&g_cs);

        // Start the Dear ImGui frame
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        {
            const ImGuiViewport *viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->WorkPos);
            ImGui::SetNextWindowSize(viewport->WorkSize);

            for (std::unordered_map<HANDLE, TOUCH_INFO>::iterator dev = g_devices.begin();
                 dev != g_devices.end();
                 dev++)
            {
                dev->second.bUsing = FALSE;
            }

            if (ImGui::Begin("Draw Window", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground))
            {
                for (std::unordered_map<DWORD, FINGER_INFO>::iterator finger = fingerStatus.begin();
                     finger != fingerStatus.end();
                     finger++)
                {
                    if (finger->second.bContactArea)
                    {
                        ImGui::GetForegroundDrawList()->AddCircleFilled(
                            ImVec2((float)finger->second.x, (float)finger->second.y),
                            50.0f + (float)sqrt(pow(finger->second.dwWidth,2) + pow(finger->second.dwHeight, 2)),
                            finger->second.color,
                            0);
                    }
                    else
                    {
                        ImGui::GetForegroundDrawList()->AddCircle(ImVec2((float)finger->second.x, (float)finger->second.y), 50.0f, finger->second.color, 0, 10);
                    }
                    if (g_devices.find(finger->second.hDevice) != g_devices.end())
                    {
                        g_devices[finger->second.hDevice].bUsing = TRUE;
                    }
                }
            }
            ImGui::End();
        }

        {
            const float PAD = 10.0f;
            ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
            const ImGuiViewport *viewport = ImGui::GetMainViewport();
            ImVec2 work_pos = viewport->WorkPos;
            ImVec2 work_size = viewport->WorkSize;
            ImVec2 window_pos, window_pos_pivot;
            window_pos.x = (work_pos.x + PAD);
            window_pos.y = (work_pos.y + PAD);
            window_pos_pivot.x = 0.0f;
            window_pos_pivot.y = 0.0f;
            ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
            window_flags |= ImGuiWindowFlags_NoMove;

            ImGui::SetNextWindowBgAlpha(0.35f);
            if (ImGui::Begin("Status Panel", NULL, window_flags))
            {
                ImGui::Text("Available touch device: %d", g_devices.size());
                if (ImGui::BeginTable("table_all_touch", g_devices.empty() ? 1 : 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
                {
                    if (!g_devices.empty())
                    {
                        ImGui::TableSetupColumn("Name");
                        ImGui::TableSetupColumn("Usage");
                        ImGui::TableSetupColumn("VID");
                        ImGui::TableSetupColumn("PID");
                        ImGui::TableHeadersRow();

                        for (std::unordered_map<HANDLE, TOUCH_INFO>::iterator dev = g_devices.begin();
                             dev != g_devices.end();
                             dev++)
                        {
                            char buf[MAX_PATH];

                            ImGui::TableNextRow();
                            if (dev->second.bUsing)
                            {
                                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ImVec4(0.26f, 0.59f, 0.98f, 0.80f)));
                            }

                            ImGui::TableSetColumnIndex(0);
                            if (dev->second.pDesc)
                            {
                                ImGui::Text((const char*)dev->second.pDesc);
                            }
                            else
                            {
                                ImGui::Text("");
                            }

                            ImGui::TableSetColumnIndex(1);
                            switch (dev->second.usUsage)
                            {
                                case 0x01:
                                    ImGui::TextUnformatted("Digitizer");
                                    break;
                                case 0x02:
                                    ImGui::TextUnformatted("Pen");
                                    break;
                                case 0x03:
                                    ImGui::TextUnformatted("Light Pen");
                                    break;
                                case 0x04:
                                    ImGui::TextUnformatted("Touch Screen");
                                    break;
                                case 0x05:
                                    ImGui::TextUnformatted("Touch Pad");
                                    break;
                                case 0x06:
                                    ImGui::TextUnformatted("Whiteboard");
                                    break;
                                case 0x07:
                                    ImGui::TextUnformatted("Coordinate Measuring Machine");
                                    break;
                                case 0x08:
                                    ImGui::TextUnformatted("3D Digitizer");
                                    break;
                                case 0x09:
                                    ImGui::TextUnformatted("Stereo Plotter");
                                    break;
                                case 0x0A:
                                    ImGui::TextUnformatted("Articulated Arm");
                                    break;
                                case 0x0B:
                                    ImGui::TextUnformatted("Armature");
                                    break;
                                case 0x0C:
                                    ImGui::TextUnformatted("Multiple Point Digitizer");
                                    break;
                                case 0x0D:
                                    ImGui::TextUnformatted("Free Space Wand");
                                    break;
                                case 0x0E:
                                    ImGui::TextUnformatted("Device Configuration");
                                    break;
                                case 0x0F:
                                    ImGui::TextUnformatted("Capacitive Heat Map Digitizer");
                                    break;
                                default:
                                    sprintf_s(buf, sizeof(buf), "%08X", dev->second.usUsage);
                                    ImGui::TextUnformatted(buf);
                                    break;
                            }

                            ImGui::TableSetColumnIndex(2);
                            sprintf_s(buf, sizeof(buf), "%04X", dev->second.dwVendor);
                            ImGui::TextUnformatted(buf);

                            ImGui::TableSetColumnIndex(3);
                            sprintf_s(buf, sizeof(buf), "%04X", dev->second.dwProduct);
                            ImGui::TextUnformatted(buf);
                        }
                    }
                    else
                    {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted("N/A");
                    }

                    ImGui::EndTable();
                }

                ImGui::Separator();

                if (!g_devices.empty())
                {
                    ImGui::Text("Touch Info.:");
                    if (ImGui::BeginTable("table_touch_info", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
                    {
                        char buf[MAX_PATH];

                        ImGui::TableSetupColumn("Contact ID");
                        ImGui::TableSetupColumn(" ");
                        ImGui::TableSetupColumn("X");
                        ImGui::TableSetupColumn("Y");
                        ImGui::TableSetupColumn("Width");
                        ImGui::TableSetupColumn("Height");
                        ImGui::TableHeadersRow();

                        for (std::unordered_map<DWORD, FINGER_INFO>::iterator finger = fingerStatus.begin();
                             finger != fingerStatus.end();
                             finger++)
                        {
                            ImGui::TableNextRow();

                            ImGui::TableSetColumnIndex(0);
                            sprintf_s(buf, sizeof(buf), "%d", finger->second.dwContactID);
                            ImGui::TextUnformatted(buf);

                            ImGui::TableSetColumnIndex(1);
                            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, finger->second.color);

                            ImGui::TableSetColumnIndex(2);
                            sprintf_s(buf, sizeof(buf), "%d", finger->second.x);
                            ImGui::TextUnformatted(buf);

                            ImGui::TableSetColumnIndex(3);
                            sprintf_s(buf, sizeof(buf), "%d", finger->second.y);
                            ImGui::TextUnformatted(buf);

                            ImGui::TableSetColumnIndex(4);
                            sprintf_s(buf, sizeof(buf), "%d", finger->second.dwWidth);
                            ImGui::TextUnformatted(buf);

                            ImGui::TableSetColumnIndex(5);
                            sprintf_s(buf, sizeof(buf), "%d", finger->second.dwHeight);
                            ImGui::TextUnformatted(buf);
                        }
                        ImGui::EndTable();
                    }
                    ImGui::Separator();
                }
            }
            ImGui::End();
        }

        ImGui::Render();
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, (float *)&clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClass(wc.lpszClassName, wc.hInstance);

    DeleteCriticalSection(&g_cs);

    for (std::unordered_map<HANDLE, TOUCH_INFO>::iterator dev = g_devices.begin();
         dev != g_devices.end();
         dev++)
    {
        if (dev->second.pDesc)
        {
            free(dev->second.pDesc);
        }
    }
    g_devices.clear();
    g_fingers.clear();

    return 0;
}

bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    //createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_0,
    };
    if (D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext) != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain)
    {
        g_pSwapChain->Release();
        g_pSwapChain = NULL;
    }
    if (g_pd3dDeviceContext)
    {
        g_pd3dDeviceContext->Release();
        g_pd3dDeviceContext = NULL;
    }
    if (g_pd3dDevice)
    {
        g_pd3dDevice->Release();
        g_pd3dDevice = NULL;
    }
}

void CreateRenderTarget()
{
    ID3D11Texture2D *pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView)
    {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = NULL;
    }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_TOUCH:
        {
            UINT cInputs = LOWORD(wParam);
            PTOUCHINPUT pInputs = new TOUCHINPUT[cInputs];
            if (NULL != pInputs)
            {
                if (::GetTouchInputInfo((HTOUCHINPUT)lParam,
                                    cInputs,
                                    pInputs,
                                    sizeof(TOUCHINPUT)))
                {
                    ::EnterCriticalSection(&g_cs);
                    for (UINT input = 0; input < cInputs; input++)
                    {
                        if (pInputs[input].dwFlags & (TOUCHEVENTF_DOWN | TOUCHEVENTF_MOVE))
                        {
                            POINT pos = {pInputs[input].x / 100, pInputs[input].y / 100};

                            // Set up a new contact
                            if (g_fingers.find(pInputs[input].dwID) == g_fingers.end())
                            {
                                RID_DEVICE_INFO info = {0};
                                info.cbSize = sizeof(RID_DEVICE_INFO);
                                UINT size = sizeof(RID_DEVICE_INFO);

                                g_fingers[pInputs[input].dwID] = {0};
                                g_fingers[pInputs[input].dwID].dwContactID = pInputs[input].dwID;
                                g_fingers[pInputs[input].dwID].hDevice = pInputs[input].hSource;
                                g_fingers[pInputs[input].dwID].color = IM_COL32(rand() % 200 + 55, rand() % 200 + 55, rand() % 200 + 55, 200);
                            }

                            ::ScreenToClient(hWnd, &pos);
                            g_fingers[pInputs[input].dwID].x = pos.x;
                            g_fingers[pInputs[input].dwID].y = pos.y;

                            if (pInputs[input].dwMask & TOUCHINPUTMASKF_CONTACTAREA)
                            {
                                g_fingers[pInputs[input].dwID].bContactArea = TRUE;
                                g_fingers[pInputs[input].dwID].dwWidth = pInputs[input].cxContact / 100;
                                g_fingers[pInputs[input].dwID].dwHeight = pInputs[input].cyContact / 100;
                            }
                            else
                            {
                                g_fingers[pInputs[input].dwID].bContactArea = FALSE;
                            }

                            if (pInputs[input].dwMask & TOUCHINPUTMASKF_TIMEFROMSYSTEM)
                            {
                                g_fingers[pInputs[input].dwID].bTimeStamp = TRUE;
                                g_fingers[pInputs[input].dwID].dwTimeStamp = pInputs[input].dwTime;
                            }
                            else
                            {
                                g_fingers[pInputs[input].dwID].bTimeStamp = FALSE;
                            }

                            g_fingers[pInputs[input].dwID].dirty = TRUE;
                        }
                        else if (pInputs[input].dwFlags & TOUCHEVENTF_UP)
                        {
                            if (g_fingers.find(pInputs[input].dwID) != g_fingers.end())
                            {
                                g_fingers.erase(g_fingers.find(pInputs[input].dwID));
                            }
                        }
                    }
                    // for (std::unordered_map<DWORD, FINGER_INFO>::iterator finger = g_fingers.begin();
                    //      finger != g_fingers.end();)
                    // {
                    //     if (!finger->second.dirty)
                    //         finger = g_fingers.erase(finger);
                    //     else
                    //         ++finger;
                    // }
                    ::LeaveCriticalSection(&g_cs);
                    ::CloseTouchInputHandle((HTOUCHINPUT)lParam);
                }
                delete[] pInputs;
            }
        }
        return 0;
    case WM_SIZE:
        if (g_pd3dDevice != NULL && wParam != SIZE_MINIMIZED)
        {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProc(hWnd, msg, wParam, lParam);
}
