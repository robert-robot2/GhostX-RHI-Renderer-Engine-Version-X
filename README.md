# GhostXV0.0.9

> **DirectX12 RHI Renderer · WinForms Shell · Blazor WASM UI**

A Windows desktop application that fuses a raw **DirectX 12 RHI rendering engine** with a **Blazor WebAssembly UI** hosted inside a WinForms shell via WebView2. The native DX12 renderer runs alongside the Blazor frontend, enabling real-time GPU-driven graphics controlled through a modern web-based interface — all in a single application.

---

## Features

- 🎮 **DirectX 12 RHI Renderer** — low-level GPU rendering pipeline with full DX12 control
- 🌐 **Blazor WASM UI** — browser-based UI layer hosted in-process via WebView2
- 🪟 **WinForms Shell** — native Windows host with dropdown menus and window management
- 🔗 **JS Interop Bridge** — communication between the Blazor frontend and the DX12 backend
- 📦 **Self-contained** — no external servers, everything runs locally

---

## Project Structure

```
GhostXV0.0.9/
├── GhostShell/               # C# WinForms host application
│   ├── Program.cs            # Entry point, launches UI host and WinForms app
│   ├── GhostForm.cs          # Main application window
│   ├── GhostMenuBuilder.cs   # Dropdown menu construction
│   ├── GhostUIHost.cs        # WebView2 Blazor host controller
│   ├── GhostEngine.cs        # DirectX 12 RHI engine integration
│   └── wwwroot/              # Published Blazor WASM output (copied here)
├── GhostBlazorUI/            # Blazor WebAssembly frontend
│   ├── Program.cs            # Blazor WASM entry point
│   ├── wwwroot/
│   │   └── index.html        # Configured for WebView2 hosting
│   ├── Pages/                # Razor pages
│   └── Layout/               # NavMenu and layout components
└── GhostX.slnx               # Visual Studio solution file
```

---

## Prerequisites

- Windows 10/11 (x64)
- [.NET 10.0 SDK](https://dotnet.microsoft.com/download)
- [Visual Studio 2022+](https://visualstudio.microsoft.com/) with:
  - ASP.NET and web development workload
  - Desktop development with C++ workload
- [WebView2 Runtime](https://developer.microsoft.com/en-us/microsoft-edge/webview2/)
- DirectX 12 compatible GPU

---

## Setup & Installation

Follow these steps to integrate the Blazor WASM UI into the WinForms DX12 shell:

### 1. Create Blazor App Inside the Solution

Add a new **Blazor WebAssembly** project to the existing Visual Studio solution alongside the WinForms project.

### 2. Add WebView2 to the WinForms Project

Install the WebView2 NuGet package in the `GhostShell` project:

```
Microsoft.Web.WebView2
```

### 3. Add GhostUIHost.cs to the WinForms Project

Add `GhostUIHost.cs` to `GhostShell` — this class manages WebView2 initialization and points it at the published Blazor output.

### 4. Update Program.cs (WinForms)

Configure the WinForms entry point to start the UI host before launching the application:

```csharp
internal static class Program
{
    [STAThread]
    static void Main()
    {
        GhostUIHost.Start();
        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);
        Application.Run(new GhostForm());
    }
}
```

### 5. Update GhostForm.cs

Wire up the WinForms main window to display the WebView2 control and handle layout alongside the DX12 render surface.

### 5A. Update GhostMenuBuilder.cs

Configure the dropdown menu builder to add menu items that communicate with the Blazor UI layer.

### 6. Update index.html in the Blazor Project

Update `wwwroot/index.html` in the Blazor project to configure the base href and ensure compatibility with WebView2 hosting.

### 6A. Check .csproj for Both Projects

Verify both project files are correctly configured:

- **GhostBlazorUI.csproj** — ensure `Microsoft.AspNetCore.Components.WebAssembly` references and target framework are set
- **GhostShell.csproj** — ensure WebView2 package reference is present

### 7. Publish the Blazor Project

```bash
dotnet publish GhostBlazorUI -c Debug
```

### 8. Copy Published Output to WinForms wwwroot

Copy the contents of the Blazor publish output to the WinForms debug bin folder:

```
GhostBlazorUI/bin/Debug/net10.0/publish/wwwroot/
        ↓
GhostShell/bin/Debug/net10.0/wwwroot/
```

### 9. Build and Run

Build the solution in Visual Studio and run `GhostShell`. The WinForms window will launch with the DX12 renderer active and the Blazor UI hosted via WebView2.
---

## Blazor → DX12 Color Bridge

---
Once the base setup is complete, the following wires up live communication between the Blazor UI and the DirectX 12 engine, allowing the Blazor frontend to control the DX12 clear color in real time.

### 1. `index.html` — Add the JS Bridge

Inside the `<head>` tag, before `blazor.webassembly.js`:

```javascript
window.ghostBridge = {
    sendColor: function (colorStr) {
        window.chrome.webview.postMessage("SetColor:" + colorStr);
    }
};
```

### 2. `Home.razor` — Add a Toggle Button

```razor
@inject IJSRuntime JS

<button @onclick="ToggleColor">Toggle BG Color</button>

@code {
    private bool _toggled = false;

    private async Task ToggleColor()
    {
        _toggled = !_toggled;
        await JS.InvokeVoidAsync("ghostBridge.sendColor",
            _toggled ? "0.2,0.0,0.4" : "0.0,0.0,0.0");
    }
}
```

### 3. `GhostForm.cs` — Wire Up the Message Receiver

Inside `OnToggleGhostUI`, after `CoreWebView2.Navigate()`:

```csharp
_ghostUI.CoreWebView2.WebMessageReceived += (sender, args) =>
{
    var msg = args.TryGetWebMessageAsString();
    if (msg.StartsWith("SetColor:"))
    {
        var parts = msg.Replace("SetColor:", "").Split(',');
        float r = float.Parse(parts[0]);
        float g = float.Parse(parts[1]);
        float b = float.Parse(parts[2]);
        GhostEngine.GhostEngine_SetClearColor(r, g, b);
    }
};
```

> **Result:** Clicking the toggle button in the Blazor UI sends a message through WebView2 directly to `GhostEngine_SetClearColor`, changing the DX12 background color in real time.

---

## Usage

- Launch the application from Visual Studio (`Debug → Start`) or the compiled executable
- Use the **File / Settings / Help** dropdown menus in the WinForms shell to interact with the application
- The **Blazor UI** panel provides the web-based interface layer
- The **DX12 renderer** runs in the background, rendering directly to the native window surface

---

## License

This project is licensed under the **MIT License**.

```
MIT License

Copyright (c) 2026 robert-robot2

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

<div align="center">
  <sub>Built by <a href="https://github.com/robert-robot2">robert-robot2</a></sub>
</div>


<img width="1920" height="1080" alt="Screenshot 2026-09-05 003001" src="https://github.com/user-attachments/assets/b6d6613d-afe8-46ab-b263-40e625061d17" />
<img width="1920" height="1080" alt="Screenshot 2026-09-05 002729" src="https://github.com/user-attachments/assets/e4bac892-ed1b-49be-b117-32150236ef46" />
<img width="1920" height="1080" alt="Screenshot 2026-09-05 002613" src="https://github.com/user-attachments/assets/5f8b7257-055f-4bc7-8618-11dd27e09757" />
<img width="1920" height="1080" alt="Screenshot 2026-09-05 002629" src="https://github.com/user-attachments/assets/add7347d-670d-4c82-9897-f3797858d10a" />



