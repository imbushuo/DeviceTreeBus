/*++

Module Name:

    public.h

Abstract:

    This module contains the common declarations shared by driver
    and user applications.

Environment:

    user and kernel

--*/

//
// Define an Interface Guid so that apps can find the device and talk to it.
//

DEFINE_GUID (GUID_DEVINTERFACE_DeviceTreeBus,
    0xa7f0bca4,0xe8ba,0x4aa9,0x85,0xb5,0x15,0x75,0x07,0xcf,0x41,0xad);
// {a7f0bca4-e8ba-4aa9-85b5-157507cf41ad}
