using System.Runtime.InteropServices;
using System.Text.Json;
namespace GbfHotkeys;

/// <summary>One session per focused WebView. Construct and call on its UI thread.
/// executeScript uses CoreWebView2.ExecuteScriptAsync; source returns its current Source.
/// Call Cancel on navigation, focus loss, settings changes, and before closing the view.
/// The viewport handle is a dedicated window whose client fills the game viewport.</summary>
public sealed class HotkeysSession : IDisposable
{
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate uint Abi();
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate nint Create(nuint owner, nuint viewport);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate nint Command(nint session, [MarshalAs(UnmanagedType.LPUTF8Str)] string json);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate nint Script();
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate void Destroy(nint session);
    private nint library, session;
    private readonly Command command;
    private readonly Destroy destroy;
    private readonly string probe;
    private readonly int thread = Environment.CurrentManagedThreadId;
    private readonly Func<string, Task<string>> executeScript;
    private readonly Func<string> source;
    private readonly Action reload;
    private int epoch;
    private bool running;
    public event Action<string>? Status;

    public HotkeysSession(string absoluteDllPath, nint owner, nint viewport,
        Func<string, Task<string>> executeScript, Func<string> source, Action reload)
    {
        if (!Path.IsPathFullyQualified(absoluteDllPath)) throw new ArgumentException("An absolute DLL path is required");
        if (SynchronizationContext.Current is null) throw new InvalidOperationException("Construct on the WebView UI thread");
        this.executeScript = executeScript; this.source = source; this.reload = reload;
        library = NativeLibrary.Load(absoluteDllPath);
        try {
            T Export<T>(string name) where T : Delegate => Marshal.GetDelegateForFunctionPointer<T>(NativeLibrary.GetExport(library,name));
            if (Export<Abi>("gbf_abi_version")() != 1) throw new NotSupportedException("Unsupported SDK ABI");
            command = Export<Command>("gbf_command"); destroy = Export<Destroy>("gbf_destroy");
            probe = Marshal.PtrToStringUTF8(Export<Script>("gbf_probe_script")())!;
            session = Export<Create>("gbf_create")((nuint)owner,(nuint)viewport);
            if (session == 0) throw new ArgumentException("Invalid owner or viewport HWND");
        } catch { NativeLibrary.Free(library); library = 0; throw; }
    }
    private void CheckThread() {
        if (Environment.CurrentManagedThreadId != thread) throw new InvalidOperationException("Use the WebView UI thread");
    }
    private JsonElement Exchange(object request) {
        CheckThread(); ObjectDisposedException.ThrowIf(session == 0, this);
        using var doc = JsonDocument.Parse(Marshal.PtrToStringUTF8(command(session,JsonSerializer.Serialize(request)))!);
        return doc.RootElement.Clone();
    }
    public void Cancel() { CheckThread(); ++epoch; running=false; if(session!=0)Exchange(new {op="cancel"}); }
    /// <summary>Call once per physical gesture with the configured action mask. No autorepeat.</summary>
    public async Task EnqueueAsync(uint mask) {
        var result=Exchange(new {op="start",mask});
        if(result.GetProperty("op").GetString()!="started" || running) return;
        running=true; int current=epoch; string expected=source();
        try {
            result=Exchange(new {op="poll"});
            while(current==epoch && result.GetProperty("op").GetString()=="probe") {
                await Task.Delay(30);
                if(current!=epoch || session==0)return;
                if(source()!=expected){Cancel();return;}
                string js=$"{probe}({JsonSerializer.Serialize(expected)},{result.GetProperty("mask")},{result.GetProperty("phase")},{JsonSerializer.Serialize(result.GetProperty("locked").GetString())})";
                var json=await executeScript(js);
                if(current!=epoch || session==0)return;
                using var parsed=JsonDocument.Parse(json);
                result=Exchange(new {op="step",ticket=result.GetProperty("ticket").GetDouble(),reply=parsed.RootElement});
            }
            if(current!=epoch)return;
            if(result.GetProperty("op").GetString()=="reload")reload();
            else Status?.Invoke(result.ToString());
        } catch { if(current==epoch)Cancel(); throw; }
        finally {if(current==epoch)running=false;}
    }
    public void Dispose() {
        CheckThread();Cancel();
        if(session!=0){destroy(session);session=0;}
        if(library!=0){NativeLibrary.Free(library);library=0;}
    }
}
