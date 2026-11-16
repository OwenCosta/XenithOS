# Puts the XenithOS Desktop ISO on a USB stick from Windows, and names the stick XENITHOS so you
# can tell it from your others in File Explorer.
#
#   powershell -ExecutionPolicy Bypass -File desktop\write-usb.ps1 [-Iso FILE] [-Disk NUMBER] [-Raw]
#
# It erases the whole stick. It lists the USB sticks and asks which one, then asks you to type
# ERASE. Windows asks for administrator rights (needed to write to a drive).
#
# How the stick is made: one FAT32 partition named XENITHOS with the ISO's files copied onto it,
# so Windows can read it (no "you need to format this disk" messages). It starts on UEFI PCs
# (Secure Boot included) from EFI\boot on that partition, and on old BIOS PCs from GRUB's boot
# code, which this writes in front of the partition (the ISO carries it, in
# boot\grub\i386-pc\xenithos-usb-*.img). Every copied file is checked against the ISO's own list
# of SHA-256 sums. If the stick can't be made that way, it's written the classic way instead.
#   -Raw   writes the ISO byte for byte (the classic way; Windows then can't read the stick and
#          shows it without a name).
#
# A PC's own boot menu (F12, F11, Esc...) lists sticks by the name their maker gave them
# ("PNY USB 2.0 FD"), which no writer can change.
param(
    [string] $Iso = "",
    [int] $Disk = -1,
    [switch] $Raw,
    [switch] $Yes,
    [string] $Log = ""
)
$ErrorActionPreference = 'Stop'
$Label = 'XENITHOS'

function Say($m) {
    Write-Host $m
    if ($Log) { "$(Get-Date -Format HH:mm:ss) $m" | Out-File -FilePath $Log -Append -Encoding utf8 }
}
function Fail($m) { Say "ERROR $m"; exit 1 }

if (-not $Iso) {
    $found = Get-ChildItem -Path (Join-Path $PSScriptRoot 'out') -Filter 'xenithos-desktop-*.iso' -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $found) { Fail "No ISO found in desktop\out. Pass -Iso FILE." }
    $Iso = $found.FullName
}
$Iso = (Resolve-Path -LiteralPath $Iso).Path

# ---- which stick
$sticks = @(Get-Disk | Where-Object { $_.BusType -eq 'USB' -and -not $_.IsBoot -and -not $_.IsSystem -and $_.Size -gt 0 })
if ($sticks.Count -eq 0) { Fail "No USB stick found. Plug one in and run this again." }
if ($Disk -lt 0) {
    Say "USB sticks:"
    foreach ($s in $sticks) {
        $names = (Get-Partition -DiskNumber $s.Number -ErrorAction SilentlyContinue | Get-Volume -ErrorAction SilentlyContinue |
            ForEach-Object { if ($_.DriveLetter) { "$($_.DriveLetter): $($_.FileSystemLabel)" } }) -join ', '
        Say ("  {0}   {1}   {2:N1} GB   {3}" -f $s.Number, $s.FriendlyName, ($s.Size / 1GB), $names)
    }
    $Disk = [int](Read-Host "Number of the stick to erase")
}
$target = $sticks | Where-Object { $_.Number -eq $Disk }
if (-not $target) { Fail "Disk $Disk isn't a USB stick." }
$isoSize = (Get-Item -LiteralPath $Iso).Length
if ($target.Size -lt $isoSize + 64MB) { Fail "That stick is too small for this ISO." }
if (-not $Yes) {
    Say ""
    Say ("Everything on disk {0} ({1}, {2:N1} GB) will be deleted." -f $target.Number, $target.FriendlyName, ($target.Size / 1GB))
    if ((Read-Host "Type ERASE to go on") -cne 'ERASE') { Say "Nothing was changed."; exit 0 }
}

# ---- administrator rights: run this same script again, raised, with the answers already given
$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) {
    if (-not $Log) { $Log = Join-Path $env:TEMP "xenithos-write-usb.log" }
    Remove-Item -LiteralPath $Log -ErrorAction SilentlyContinue
    $arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"", '-Iso', "`"$Iso`"",
                   '-Disk', $Disk, '-Yes', '-Log', "`"$Log`"")
    if ($Raw) { $arguments += '-Raw' }
    Say "Windows will ask for administrator rights now."
    $p = Start-Process -FilePath powershell.exe -ArgumentList $arguments -Verb RunAs -PassThru -WindowStyle Hidden
    $shown = 0
    while (-not $p.HasExited) {
        Start-Sleep -Milliseconds 700
        if (Test-Path -LiteralPath $Log) {
            $lines = @(Get-Content -LiteralPath $Log)
            for (; $shown -lt $lines.Count; $shown++) { Write-Host $lines[$shown] }
        }
    }
    if (Test-Path -LiteralPath $Log) {
        $lines = @(Get-Content -LiteralPath $Log)
        for (; $shown -lt $lines.Count; $shown++) { Write-Host $lines[$shown] }
    }
    exit $p.ExitCode
}

# ---- raised from here on
Add-Type -Namespace XenithOS -Name Native -MemberDefinition @'
[DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
public static extern Microsoft.Win32.SafeHandles.SafeFileHandle CreateFile(string name, uint access, uint share,
    IntPtr security, uint creation, uint flags, IntPtr template);
'@
function Open-Disk([int] $number) {
    $h = [XenithOS.Native]::CreateFile("\\.\PhysicalDrive$number", [uint32]3221225472, 3, [IntPtr]::Zero, 3, 0, [IntPtr]::Zero)
    if ($h.IsInvalid) { throw "couldn't open disk $number (error $([Runtime.InteropServices.Marshal]::GetLastWin32Error()))" }
    New-Object System.IO.FileStream($h, [System.IO.FileAccess]::ReadWrite, 1048576)
}
# The SHA-256 of a file, read from the stick itself. Read the usual way, Windows answers from its
# own copy in memory of what it has just written, and a stick that stored it wrong would pass.
function Get-StickHash([string] $path) {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    $buf = New-Object byte[] 1048576
    $fs = New-Object System.IO.FileStream($path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read,
        [System.IO.FileShare]::Read, 1048576, [System.IO.FileOptions]0x20000000)
    try {
        while (($n = $fs.Read($buf, 0, $buf.Length)) -gt 0) { $sha.TransformBlock($buf, 0, $n, $null, 0) | Out-Null }
        $sha.TransformFinalBlock($buf, 0, 0) | Out-Null
    } finally {
        $fs.Close()
    }
    ($sha.Hash | ForEach-Object { $_.ToString('x2') }) -join ''
}
function Clear-Stick {
    $d = Get-Disk -Number $Disk
    if ($d.PartitionStyle -eq 'RAW') { return }
    try {
        Clear-Disk -Number $Disk -RemoveData -RemoveOEM -Confirm:$false
    } catch {
        # A stick that holds an ISO written byte for byte has a layout Windows may refuse to
        # clear. Blanking its first and last megabyte, where the partition tables are, does it.
        Say "  (Windows wouldn't clear the stick its own way: blanking its partition tables)"
        $stream = Open-Disk $Disk
        try {
            $zero = New-Object byte[] 1048576
            $stream.Write($zero, 0, $zero.Length)
            $stream.Seek($d.Size - ($d.Size % 512) - $zero.Length, 'Begin') | Out-Null
            $stream.Write($zero, 0, $zero.Length)
            $stream.Flush()
        } finally {
            $stream.Close()
        }
        Update-Disk -Number $Disk
        Start-Sleep -Seconds 2
    }
}

# The classic way: the ISO byte for byte, then read back and compared.
function Write-Raw {
    Say "Writing the ISO byte for byte"
    Clear-Stick
    $stream = Open-Disk $Disk
    try {
        $src = [System.IO.File]::OpenRead($Iso)
        $buf = New-Object byte[] 4194304
        $done = 0L; $next = 10
        while (($n = $src.Read($buf, 0, $buf.Length)) -gt 0) {
            if ($n % 512) { $pad = 512 - $n % 512; [Array]::Clear($buf, $n, $pad); $n += $pad }
            $stream.Write($buf, 0, $n); $done += $n
            $pct = [int][Math]::Floor(100 * $done / $isoSize)
            if ($pct -ge $next -and $pct -lt 100) { Say "  $pct%"; $next = $pct - ($pct % 10) + 10 }
        }
        $stream.Flush(); $src.Close()
        Say "Checking it"
        $stream.Seek(0, 'Begin') | Out-Null
        $sha = [System.Security.Cryptography.SHA256]::Create()
        $left = $isoSize
        while ($left -gt 0) {
            $got = $stream.Read($buf, 0, [int][Math]::Min($buf.Length, $left))
            if ($got -le 0) { throw "couldn't read the stick back" }
            $sha.TransformBlock($buf, 0, $got, $null, 0) | Out-Null; $left -= $got
        }
        $sha.TransformFinalBlock($buf, 0, 0) | Out-Null
        $onStick = ($sha.Hash | ForEach-Object { $_.ToString('x2') }) -join ''
        if ($onStick -ne (Get-FileHash -Algorithm SHA256 -LiteralPath $Iso).Hash.ToLower()) { throw "the stick doesn't read back the same as the ISO" }
    } finally {
        $stream.Close()
    }
    Update-Disk -Number $Disk
}

# The named way: a FAT32 partition called XENITHOS with the ISO's files on it. Sets $script:Letter.
function Write-Named {
    Clear-Stick
    $d = Get-Disk -Number $Disk
    # A cleared hard drive is left blank. A cleared USB stick comes back set up already, in
    # whichever style it had, and "set it up" would then be refused.
    if ($d.PartitionStyle -eq 'RAW') { Initialize-Disk -Number $Disk -PartitionStyle MBR }
    elseif ($d.PartitionStyle -ne 'MBR') { Set-Disk -Number $Disk -PartitionStyle MBR }
    # Windows formats FAT32 up to 32 GB only, so bigger sticks get a 31 GB partition (plenty).
    $size = [Math]::Min($d.Size - 8MB, 31GB)
    $size = $size - ($size % 1MB)
    # Marked as the one to start from. No drive letter until it's formatted, so Windows doesn't
    # offer to format it in the meantime.
    $part = New-Partition -DiskNumber $Disk -Size $size -MbrType FAT32 -IsActive
    Format-Volume -Partition $part -FileSystem FAT32 -NewFileSystemLabel $Label -Confirm:$false | Out-Null
    Add-PartitionAccessPath -DiskNumber $Disk -PartitionNumber $part.PartitionNumber -AssignDriveLetter | Out-Null
    $letter = $null
    for ($i = 0; $i -lt 30 -and -not $letter; $i++) {
        Start-Sleep -Milliseconds 500
        $got = (Get-Partition -DiskNumber $Disk -PartitionNumber $part.PartitionNumber).DriveLetter
        if ($got -and $got -ne [char]0) { $letter = $got }
    }
    if (-not $letter) { throw "Windows didn't give the stick a drive letter" }
    $script:Letter = $letter
    $root = "${letter}:\"
    Say "Made ${letter}: $Label"

    Say "Copying XenithOS Desktop onto it (a few minutes)"
    $image = Mount-DiskImage -ImagePath $Iso -PassThru
    try {
        $isoLetter = $null
        for ($i = 0; $i -lt 30 -and -not $isoLetter; $i++) {
            $got = ($image | Get-Volume).DriveLetter
            if ($got -and $got -ne [char]0) { $isoLetter = $got } else { Start-Sleep -Milliseconds 500 }
        }
        if (-not $isoLetter) { throw "Windows couldn't open the ISO" }
        $from = "${isoLetter}:\"
        $files = @(Get-ChildItem -LiteralPath $from -Recurse -File -Force)
        if ($files | Where-Object { $_.Length -ge 4GB }) { throw "a file in this ISO is 4 GB or more, too big for FAT32" }
        $total = ($files | Measure-Object -Property Length -Sum).Sum
        $copy = Start-Process -FilePath robocopy.exe -PassThru -WindowStyle Hidden `
            -ArgumentList @($from, $root, '/E', '/R:2', '/W:2', '/NFL', '/NDL', '/NJH', '/NJS', '/NP')
        $null = $copy.Handle        # without this, PowerShell forgets the exit code
        $next = 10
        while (-not $copy.HasExited) {
            Start-Sleep -Seconds 2
            $v = Get-Volume -DriveLetter $letter -ErrorAction SilentlyContinue
            if (-not $v) { continue }
            $pct = [int][Math]::Floor(100 * ($v.Size - $v.SizeRemaining) / $total)
            if ($pct -ge $next -and $pct -lt 100) { Say "  $pct%"; $next = $pct - ($pct % 10) + 10 }
        }
        if ($copy.ExitCode -ge 8) { throw "copying failed (robocopy said $($copy.ExitCode))" }

        Say "Checking every file on the stick (a minute or two)"
        $bad = 0; $checked = 0; $direct = $true
        foreach ($line in Get-Content -LiteralPath (Join-Path $from 'sha256sum.txt')) {
            if ($line -notmatch '^([0-9a-f]{64})\s+\.?/?(.+)$') { continue }
            $want = $Matches[1]; $rel = $Matches[2] -replace '/', '\'
            # The boot code for CDs is changed when the ISO is made, after its sum was listed
            # (and a stick doesn't use it).
            if ($rel -eq 'boot\grub\grub_eltorito' -or $rel -eq 'isolinux\isolinux.bin') { continue }
            $file = Join-Path $root $rel
            if (-not (Test-Path -LiteralPath $file)) { Say "  missing: $rel"; $bad++; continue }
            $got = $null
            if ($direct) {
                try { $got = Get-StickHash $file } catch { $direct = $false }
            }
            # A stick that can't be read that way is read the usual way.
            if (-not $got) { $got = (Get-FileHash -Algorithm SHA256 -LiteralPath $file).Hash.ToLower() }
            if ($got -ne $want) { Say "  wrong: $rel"; $bad++ }
            $checked++
        }
        if ($bad -gt 0) { throw "$bad files didn't copy right" }
        if ($checked -eq 0) { throw "the ISO's list of sums is empty" }
        Say "  $checked files are right"

        # Old BIOS PCs: GRUB's boot code in front of the partition. The first 440 bytes of the
        # stick's first sector (the partition table after them stays), then GRUB's core from
        # the second sector on, in the gap before the partition.
        $bootImg = Join-Path $from 'boot\grub\i386-pc\xenithos-usb-boot.img'
        $coreImg = Join-Path $from 'boot\grub\i386-pc\xenithos-usb-core.img'
        if ((Test-Path -LiteralPath $bootImg) -and (Test-Path -LiteralPath $coreImg)) {
            $boot = [System.IO.File]::ReadAllBytes($bootImg)
            $core = [System.IO.File]::ReadAllBytes($coreImg)
            $start = (Get-Partition -DiskNumber $Disk -PartitionNumber $part.PartitionNumber).Offset
            if ($core.Length + 512 -gt $start) { throw "no room for GRUB in front of the partition" }
            $padded = New-Object byte[] ([int]([Math]::Ceiling($core.Length / 512) * 512))
            [Array]::Copy($core, $padded, $core.Length)
            $stream = Open-Disk $Disk
            try {
                $mbr = New-Object byte[] 512
                $stream.Read($mbr, 0, 512) | Out-Null
                [Array]::Copy($boot, 0, $mbr, 0, 440)
                $stream.Seek(0, 'Begin') | Out-Null
                $stream.Write($mbr, 0, 512)
                $stream.Write($padded, 0, $padded.Length)
                $stream.Flush()
            } finally {
                $stream.Close()
            }
            Say "Added the boot code for old BIOS PCs"
        } else {
            Say "This ISO has no boot code for old BIOS PCs: the stick starts on UEFI PCs only"
        }
    } finally {
        Dismount-DiskImage -ImagePath $Iso | Out-Null
    }
    # Everything Windows still holds for the stick goes onto it now, so it can be pulled out.
    try { Write-VolumeCache -DriveLetter $letter -ErrorAction Stop } catch { Say "  (couldn't flush the stick: eject it from Windows before pulling it out)" }
}

try {
    $d = Get-Disk -Number $Disk
    if ($d.BusType -ne 'USB' -or $d.IsBoot -or $d.IsSystem) { throw "disk $Disk isn't a USB stick" }
    Say "Erasing disk $Disk ($($d.FriendlyName))"
    if (-not $Raw) {
        try {
            Write-Named | Out-Null
            Say "DONE. The stick is $($script:Letter): $Label. Start the PC from it (its boot menu key is usually F12, F11 or Esc)."
            exit 0
        } catch {
            Say "Couldn't make the named stick ($($_.Exception.Message)). Writing it the classic way instead."
        }
    }
    Write-Raw | Out-Null
    Say "DONE. The stick holds the ISO, byte for byte. Windows shows it without a name; it starts a PC all the same."
    exit 0
} catch {
    Say "ERROR $($_.Exception.Message)"
    exit 1
}
