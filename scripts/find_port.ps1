# Find FTDI USB Serial Port (Nexys A7-100T uses FT2232H, VID_0403 PID_6010)
$device = Get-WMIObject Win32_PnPEntity |
    Where-Object { $_.Name -like '*USB Serial Port*' -and $_.DeviceID -like '*VID_0403*' } |
    Select-Object -First 1
if ($device) {
    $device.Name -replace '.*\((COM\d+)\).*', '$1'
}
