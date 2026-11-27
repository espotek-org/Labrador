# Run by Labrador's installer after it has put libusb0.sys in System32\drivers (#457).
#
# A bootloader device node (USB\VID_03EB&PID_2FE4) that Windows set up before then, with a driver
# package that installs the libusb0 service but brings no libusb0.sys (Atmel's atmel_usb_dfu.inf),
# was left with no driver (problem code 1 or 18), and Windows doesn't set it up again by itself.
# Every such node, one with no driver service, is removed, then the devices are rescanned: a board
# that is in the bootloader now is set up again at once, and any other is set up afresh the next
# time it enters the bootloader. Nodes that have a driver are left alone.
$ErrorActionPreference = 'Stop'

$nodes = @(Get-PnpDevice | Where-Object { $_.InstanceId -like 'USB\VID_03EB&PID_2FE4\*' })
foreach ($node in $nodes) {
    $service = (Get-PnpDeviceProperty -InstanceId $node.InstanceId -KeyName DEVPKEY_Device_Service).Data
    if ($service) {
        "keeping $($node.InstanceId) (driver service $service)"
        continue
    }
    "removing $($node.InstanceId) (no driver; present: $($node.Present))"
    pnputil /remove-device "$($node.InstanceId)"
    if ($LASTEXITCODE -ne 0) { throw "pnputil /remove-device $($node.InstanceId) exited $LASTEXITCODE" }
}
pnputil /scan-devices
if ($LASTEXITCODE -ne 0) { throw "pnputil /scan-devices exited $LASTEXITCODE" }
