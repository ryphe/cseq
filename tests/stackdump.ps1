param([int]$TargetPid)
$code = @'
using System;
using System.Runtime.InteropServices;
public class StackWalk {
    [DllImport("kernel32.dll")]
    public static extern IntPtr OpenThread(uint access, bool inherit, uint tid);
    [DllImport("kernel32.dll")]
    public static extern bool CloseHandle(IntPtr h);
    [DllImport("kernel32.dll")]
    public static extern uint SuspendThread(IntPtr h);
    [DllImport("kernel32.dll")]
    public static extern int ResumeThread(IntPtr h);
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool ReadProcessMemory(IntPtr h, IntPtr addr, byte[] buf, int size, out int read);
}
'@
Add-Type -TypeDefinition $code
