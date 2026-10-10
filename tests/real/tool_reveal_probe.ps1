# Sonde du plan 63 : MacDock visible aux captures de Windows le temps de l'Outil Capture.
# Copie l'écran elle-même (BitBlt, comme une capture) sur la zone du Dock, avant, pendant et après le message d'essai
# « MacDockTestToolReveal » (même chemin que ⊞⇧S, mode diagnostic). Rien n'est écrit sur le disque, le presse-papiers
# n'est pas touché. Comparaisons en C# (rapides).
Add-Type -AssemblyName System.Drawing
Add-Type -Namespace P -Name U -MemberDefinition @'
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern System.IntPtr FindWindow(string c, string t);
[DllImport("user32.dll")] public static extern bool GetWindowRect(System.IntPtr h, out RECT r);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern uint RegisterWindowMessage(string s);
[DllImport("user32.dll")] public static extern bool PostMessage(System.IntPtr h, uint m, System.IntPtr w, System.IntPtr l);
[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
public struct RECT { public int Left, Top, Right, Bottom; }
public static string Diff(byte[] a, byte[] b) {
    long sum = 0; int changed = 0, n = 0;
    for (int i = 0; i + 2 < a.Length; i += 4) {
        int d = System.Math.Abs(a[i] - b[i]) + System.Math.Abs(a[i + 1] - b[i + 1]) + System.Math.Abs(a[i + 2] - b[i + 2]);
        sum += d; n++;
        if (d > 72) changed++;
    }
    return string.Format("écart moyen {0:F2} (sur 255), {1:P2} des pixels changent de plus de 24", sum / (3.0 * n), changed / (double)n);
}
public static double Mean(byte[] a) {
    long s = 0; int n = 0;
    for (int i = 0; i + 2 < a.Length; i += 4) { s += a[i] + a[i + 1] + a[i + 2]; n++; }
    return s / (3.0 * n);
}
'@
[void][P.U]::SetProcessDPIAware()
$dock = [P.U]::FindWindow('MacDockWindow', [NullString]::Value)
if ($dock -eq [IntPtr]::Zero) { 'Dock introuvable'; exit 1 }
$r = New-Object P.U+RECT
[void][P.U]::GetWindowRect($dock, [ref]$r)
$w = $r.Right - $r.Left; $h = $r.Bottom - $r.Top
"Dock : ($($r.Left), $($r.Top)) $w x $h"

function Grab {
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($r.Left, $r.Top, 0, 0, (New-Object System.Drawing.Size $w, $h))
    $g.Dispose()
    $data = $bmp.LockBits((New-Object System.Drawing.Rectangle 0, 0, $w, $h), 'ReadOnly', 'Format32bppArgb')
    $bytes = New-Object byte[] ($data.Stride * $h)
    [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
    $bmp.UnlockBits($data); $bmp.Dispose()
    return ,$bytes
}

$msg = [P.U]::RegisterWindowMessage('MacDockTestToolReveal')
$before = Grab
Start-Sleep -Milliseconds 300
$before2 = Grab
$t0 = Get-Date
"message d'essai envoyé à $($t0.ToString('HH:mm:ss.fff'))"
[void][P.U]::PostMessage($dock, $msg, [IntPtr]::Zero, [IntPtr]::Zero)
Start-Sleep -Milliseconds 400
$during = Grab
while (((Get-Date) - $t0).TotalMilliseconds -lt 2000) { Start-Sleep -Milliseconds 50 }
$during2 = Grab
while (((Get-Date) - $t0).TotalMilliseconds -lt 4200) { Start-Sleep -Milliseconds 50 }
$after = Grab
'Luminance moyenne (0 = copie noire) : avant {0:F1}, 0,4 s {1:F1}, 2 s {2:F1}, 4,2 s {3:F1}' -f [P.U]::Mean($before), [P.U]::Mean($during), [P.U]::Mean($during2), [P.U]::Mean($after)
"Avant / avant (300 ms plus tard, bruit) : " + [P.U]::Diff($before, $before2)
"Avant / pendant (0,4 s)                 : " + [P.U]::Diff($before, $during)
"Avant / pendant (2 s)                   : " + [P.U]::Diff($before, $during2)
"Avant / après (4,2 s)                   : " + [P.U]::Diff($before, $after)
