#include "driver.h"
#include "device.tmh"
#include "include/TestDeviceTreeApple.h"

#define NTSTRSAFE_LIB
#include <ntstrsafe.h>
#include <initguid.h>

#include <libfdt.h>
#include <devguid.h>

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, DeviceTreeBusCreateDevice)
#endif

NTSTATUS
DeviceTreeBusCreateDevice(
    _Inout_ PWDFDEVICE_INIT DeviceInit
)
{
    WDF_OBJECT_ATTRIBUTES attributes;
    PFDO_DEVICE_CONTEXT deviceContext;
    WDFDEVICE device;
    NTSTATUS status;
    PNP_BUS_INFORMATION busInfo;
    DT_BUS_INTERFACE_STANDARD Interface;
    int err = 0;

    PAGED_CODE();

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, FDO_DEVICE_CONTEXT);
    status = WdfDeviceCreate(&DeviceInit, &attributes, &device);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    deviceContext = FdoGetContext(device);

    // Try to probe the device interface of parent device, and retrieve depth and offset information back
    status = WdfFdoQueryForInterface(
        device,
        &GUID_INTERNAL_INTERFACE_QUERY_DT,
        (PINTERFACE)&Interface,
        sizeof(Interface),
        1,
        NULL
    );

    // Initialize the context.
    if (NT_SUCCESS(status)) {
        if (sizeof(g_testDeviceTree) != Interface.DeviceTreeBlobSize)
        {
            TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! Child device has a mismatched DT");
            status = STATUS_BAD_DATA;
            return status;
        }

        deviceContext->CurrentDepth = Interface.CurrentDepth;
        deviceContext->CurrentOffset = Interface.CurrentOffset;
        deviceContext->pDeviceTreeBlob = (VOID*)g_testDeviceTree;
        deviceContext->DeviceTreeBlobSize = sizeof(g_testDeviceTree);

        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! WdfFdoQueryForInterface succeeded, this is a child device node. Depth %d, Offset %d", Interface.CurrentDepth, Interface.CurrentOffset);
    }
    else {
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! WdfFdoQueryForInterface failed, this is a root device node");
        deviceContext->CurrentDepth = 0;
        deviceContext->pDeviceTreeBlob = (VOID*)g_testDeviceTree;
        deviceContext->DeviceTreeBlobSize = sizeof(g_testDeviceTree);
        deviceContext->CurrentOffset = 0;
        status = STATUS_SUCCESS;
    }

    // Make sure the device tree has sanity.
    err = fdt_check_header(deviceContext->pDeviceTreeBlob);
    if (err != 0) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! fdt_check_header reported invalid DT header: %d", err);
        return STATUS_BAD_DATA;
    }

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = device;
    status = WdfWaitLockCreate(&attributes, &deviceContext->ChildLock);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    // Initialize the I/O Package and any Queues
    status = DeviceTreeBusQueueInitialize(device);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    // Create interface
    status = WdfDeviceCreateDeviceInterface(
        device,
        &GUID_DEVINTERFACE_DeviceTreeBus,
        NULL // ReferenceString
    );
    if (!NT_SUCCESS(status)) {
        return status;
    }

    // Set optional bus info
    busInfo.BusTypeGuid = GUID_DEVCLASS_SYSTEM;
    busInfo.LegacyBusType = PNPBus;
    busInfo.BusNumber = 0;
    WdfDeviceSetBusInformationForChildren(device, &busInfo);

    // Perform static enumeration of bus device
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Start to enumearte devices");
    if (deviceContext->CurrentDepth > 0)
    {
        status = CreateAccessNodeForChildBus(device, deviceContext, &Interface);
        if (!NT_SUCCESS(status)) return status;
    }

    status = EnumerateDevices(device, deviceContext);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Finished enumerating devices");

    return status;
}

NTSTATUS
EnumerateDevices(
    _In_ WDFDEVICE Device,
    _In_ PFDO_DEVICE_CONTEXT Context
)
{
    int offset = Context->CurrentOffset, depth = Context->CurrentDepth;
    NTSTATUS status;

    PAGED_CODE();

    // We have strong guarantee that this will only be run by entry point, so no lock here.
    offset = fdt_next_node(Context->pDeviceTreeBlob, offset, &depth);
    while (offset >= 0 && depth >= 0) {
        int tmpOffset = 0, tmpDepth = 0;
        BOOLEAN busNode = FALSE;

        // For depth = 0 (Root node), automatically advance to the next node if this device is root node, otherwise
        // bail out
        if (Context->CurrentDepth == 0 && depth == 0) {
            goto next_node;
        }
        else if (Context->CurrentDepth != 0 && depth == 0) {
            break;
        }
        // For depth > 0 (child node), enumerate anything with current depth + 1, run until see node of current depth
        if (Context->CurrentDepth != 0 && depth <= Context->CurrentDepth) {
            break;
        }
        // Guardrail: only enumerate one level device
        if (depth > Context->CurrentDepth + 1)
        {
            goto next_node;
        }

        // Check if this is a end device node, or it's a extended bus node
        tmpOffset = fdt_next_node(Context->pDeviceTreeBlob, offset, &tmpDepth);
        if (tmpOffset >= 0) tmpDepth = fdt_node_depth(Context->pDeviceTreeBlob, tmpOffset);
        busNode = (tmpOffset >= 0 && tmpDepth > depth) ? TRUE : FALSE;

        // Read name property and enumerate the device
        const char* nodename = fdt_get_name(Context->pDeviceTreeBlob, offset, NULL);
        if (nodename == NULL) {
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_DEVICE, "%!FUNC! Skipped enumerating node %d with no name", offset);
            goto next_node;
        }

        // Enumerate static device
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Enumerate device for %s @ %d (Depth = %d), BusNode %d", nodename, offset, depth, busNode);
        status = CreatePdoAndInsert(Device, Context, nodename, offset, depth, busNode);
        if (!NT_SUCCESS(status)) {
            TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! CreatePdoAndInsert failed on node %d with %!STATUS!", offset, status);
            return status;
        }

    next_node:
        offset = fdt_next_node(Context->pDeviceTreeBlob, offset, &depth);
    }

    return STATUS_SUCCESS;
}

NTSTATUS
CreateAccessNodeForChildBus(
    _In_ WDFDEVICE Device,
    _In_ PFDO_DEVICE_CONTEXT Context,
    _In_ PDT_BUS_INTERFACE_STANDARD pInterface
)
{
    NTSTATUS                    status = STATUS_SUCCESS;
    PWDFDEVICE_INIT             pDeviceInit = NULL;
    WDFDEVICE                   hChild = NULL;
    WDF_OBJECT_ATTRIBUTES       pdoAttributes;
    WDF_DEVICE_PNP_CAPABILITIES pnpCaps;
    WDF_DEVICE_POWER_CAPABILITIES powerCaps;

    ANSI_STRING nodeNameAnsi;
    ANSI_STRING dtCompatibleNameAnsi;
    UNICODE_STRING dtCompatibleNameUnicode;
    DECLARE_CONST_UNICODE_STRING(devNodeCompatId, DT_BUSENUM_DEVICENODE_COMPATIBLE_IDS);
    DECLARE_CONST_UNICODE_STRING(deviceLocation, DT_BUSENUM_LOCATION_ID);
    DECLARE_UNICODE_STRING_SIZE(buffer, MAX_ID_LEN);
    DECLARE_UNICODE_STRING_SIZE(deviceId, 255);
    DECLARE_UNICODE_STRING_SIZE(hardwareId, 255);

    // Requires compatible property to successfully enumerate (for hardware ID), otherwise silently drop it
    const struct fdt_property* compatProperty = fdt_get_property(Context->pDeviceTreeBlob, pInterface->CurrentOffset, DT_COMPATIBLE_PROP, NULL);
    if (compatProperty == NULL || compatProperty->len < 1) {
        status = STATUS_SUCCESS;
        goto cleanup;
    }

    // Post process dtCompatibleName and generate hardware ID
    RtlInitAnsiString(&dtCompatibleNameAnsi, compatProperty->data);
    status = RtlAnsiStringToUnicodeString(&dtCompatibleNameUnicode, &dtCompatibleNameAnsi, TRUE);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! RtlAnsiStringToUnicodeString with %!STATUS!", status);
        goto cleanup;
    }
    status = RtlUnicodeStringPrintf(&hardwareId, L"OFDT\\%wZ", &dtCompatibleNameUnicode);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! RtlUnicodeStringPrintf with %!STATUS!", status);
        goto cleanup;
    }
    for (auto i = 0; i < dtCompatibleNameUnicode.Length; i++) {
        if (dtCompatibleNameUnicode.Buffer[i] == L',' || dtCompatibleNameUnicode.Buffer[i] == L'-' || dtCompatibleNameUnicode.Buffer[i] == L'.') {
            dtCompatibleNameUnicode.Buffer[i] = L'_';
        }
    }

    // Build a device ID on the fly
    status = RtlUnicodeStringPrintf(&deviceId, L"OFDT\\DT_NODE_%d_Access", pInterface->CurrentOffset);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! RtlUnicodeStringPrintf with %!STATUS!", status);
        goto cleanup;
    }

    // Post process dtCompatibleName and generate hardware ID
    status = RtlUnicodeStringPrintf(&hardwareId, L"OFDT\\%wZ", &dtCompatibleNameUnicode);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! RtlUnicodeStringPrintf with %!STATUS!", status);
        goto cleanup;
    }

    pDeviceInit = WdfPdoInitAllocate(Device);
    if (pDeviceInit == NULL) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto cleanup;
    }

    // XXX: Should this be changed?
    WdfDeviceInitSetDeviceType(pDeviceInit, FILE_DEVICE_BUS_EXTENDER);

    status = WdfPdoInitAssignDeviceID(pDeviceInit, &deviceId);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! WdfPdoInitAssignDeviceID with %!STATUS!", status);
        goto cleanup;
    }

    status = WdfPdoInitAddHardwareID(pDeviceInit, &hardwareId);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! WdfPdoInitAddHardwareID with %!STATUS!", status);
        goto cleanup;
    }

    status = WdfPdoInitAddCompatibleID(pDeviceInit, &devNodeCompatId);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! WdfPdoInitAddCompatibleID with %!STATUS!", status);
        goto cleanup;
    }

    status = RtlUnicodeStringPrintf(&buffer, L"%02d", pInterface->CurrentOffset + 1);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! RtlUnicodeStringPrintf with %!STATUS!", status);
        goto cleanup;
    }

    status = WdfPdoInitAssignInstanceID(pDeviceInit, &buffer);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! WdfPdoInitAssignInstanceID with %!STATUS!", status);
        goto cleanup;
    }

    const char* nodename = fdt_get_name(Context->pDeviceTreeBlob, pInterface->CurrentOffset, NULL);
    RtlInitAnsiString(&nodeNameAnsi, nodename);
    status = RtlAnsiStringToUnicodeString(&buffer, &nodeNameAnsi, FALSE);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! RtlAnsiStringToUnicodeString with %!STATUS!", status);
        goto cleanup;
    }

    status = WdfPdoInitAddDeviceText(pDeviceInit, &buffer, &deviceLocation, 0x409);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! WdfPdoInitAddDeviceText with %!STATUS!", status);
        goto cleanup;
    }

    WdfPdoInitSetDefaultLocale(pDeviceInit, 0x409);
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&pdoAttributes, PDO_DEVICE_CONTEXT);
    status = WdfDeviceCreate(&pDeviceInit, &pdoAttributes, &hChild);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! WdfDeviceCreate with %!STATUS!", status);
        goto cleanup;
    }

    PPDO_DEVICE_CONTEXT pdoData = PdoGetContext(hChild);
    pdoData->CurrentDepth = pInterface->CurrentDepth;
    pdoData->CurrentOffset = pInterface->CurrentOffset;
    pdoData->DeviceTreeBlobSize = Context->DeviceTreeBlobSize;
    pdoData->pDeviceTreeBlob = Context->pDeviceTreeBlob;

    // Platform device, can't be removed
    WDF_DEVICE_PNP_CAPABILITIES_INIT(&pnpCaps);
    pnpCaps.Removable = WdfFalse;
    pnpCaps.EjectSupported = WdfFalse;
    pnpCaps.SurpriseRemovalOK = WdfFalse;
    pnpCaps.Address = pInterface->CurrentOffset + 1;
    pnpCaps.UINumber = pInterface->CurrentOffset + 1;
    WdfDeviceSetPnpCapabilities(hChild, &pnpCaps);

    WDF_DEVICE_POWER_CAPABILITIES_INIT(&powerCaps);
    powerCaps.DeviceD1 = WdfTrue;
    powerCaps.WakeFromD1 = WdfTrue;
    powerCaps.DeviceWake = PowerDeviceD1;
    powerCaps.DeviceState[PowerSystemWorking] = PowerDeviceD0;
    powerCaps.DeviceState[PowerSystemSleeping1] = PowerDeviceD1;
    powerCaps.DeviceState[PowerSystemSleeping2] = PowerDeviceD3;
    powerCaps.DeviceState[PowerSystemSleeping3] = PowerDeviceD3;
    powerCaps.DeviceState[PowerSystemHibernate] = PowerDeviceD3;
    powerCaps.DeviceState[PowerSystemShutdown] = PowerDeviceD3;
    WdfDeviceSetPowerCapabilities(hChild, &powerCaps);

    status = WdfFdoAddStaticChild(Device, hChild);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! WdfFdoAddStaticChild with %!STATUS!", status);
        goto cleanup;
    }

cleanup:
    if (pDeviceInit != NULL) WdfDeviceInitFree(pDeviceInit);
    if (hChild) WdfObjectDelete(hChild);
    return status;
}

NTSTATUS
CreatePdoAndInsert(
    _In_ WDFDEVICE Device,
    _In_ PFDO_DEVICE_CONTEXT Context,
    _In_ const char* NodeName,
    _In_ int Offset,
    _In_ int Depth,
    _In_ BOOLEAN IsBusNode
)
{
    NTSTATUS                    status = STATUS_SUCCESS;
    PWDFDEVICE_INIT             pDeviceInit = NULL;
    WDFDEVICE                   hChild = NULL;
    WDF_OBJECT_ATTRIBUTES       pdoAttributes;
    WDF_DEVICE_PNP_CAPABILITIES pnpCaps;
    WDF_DEVICE_POWER_CAPABILITIES powerCaps;

    ANSI_STRING nodeNameAnsi;
    ANSI_STRING dtCompatibleNameAnsi;
    UNICODE_STRING dtCompatibleNameUnicode;
    DECLARE_CONST_UNICODE_STRING(busNodeCompatId, DT_BUSENUM_BUSNODE_COMPATIBLE_IDS);
    DECLARE_CONST_UNICODE_STRING(devNodeCompatId, DT_BUSENUM_DEVICENODE_COMPATIBLE_IDS);
    DECLARE_CONST_UNICODE_STRING(deviceLocation, DT_BUSENUM_LOCATION_ID);
    DECLARE_UNICODE_STRING_SIZE(buffer, MAX_ID_LEN);
    DECLARE_UNICODE_STRING_SIZE(deviceId, 255);
    DECLARE_UNICODE_STRING_SIZE(hardwareId, 255);

    DT_BUS_INTERFACE_STANDARD BusInterface;
    WDF_QUERY_INTERFACE_CONFIG qiConfig;

    PAGED_CODE();

    // Requires compatible property to successfully enumerate (for hardware ID), otherwise silently drop it
    const struct fdt_property* compatProperty = fdt_get_property(Context->pDeviceTreeBlob, Offset, DT_COMPATIBLE_PROP, NULL);
    if (compatProperty == NULL || compatProperty->len < 1) {
        status = STATUS_SUCCESS;
        goto cleanup;
    }

    // Build a device ID on the fly
    status = RtlUnicodeStringPrintf(&deviceId, L"OFDT\\DT_NODE_%d", Offset);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! RtlUnicodeStringPrintf with %!STATUS!", status);
        goto cleanup;
    }

    // Post process dtCompatibleName and generate hardware ID
    RtlInitAnsiString(&dtCompatibleNameAnsi, compatProperty->data);
    status = RtlAnsiStringToUnicodeString(&dtCompatibleNameUnicode, &dtCompatibleNameAnsi, TRUE);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! RtlAnsiStringToUnicodeString with %!STATUS!", status);
        goto cleanup;
    }
    for (auto i = 0; i < dtCompatibleNameUnicode.Length; i++) {
        if (dtCompatibleNameUnicode.Buffer[i] == L',' || dtCompatibleNameUnicode.Buffer[i] == L'-' || dtCompatibleNameUnicode.Buffer[i] == L'.') {
            dtCompatibleNameUnicode.Buffer[i] = L'_';
        }
    }
    status = RtlUnicodeStringPrintf(&hardwareId, L"OFDT\\%wZ", &dtCompatibleNameUnicode);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! RtlUnicodeStringPrintf with %!STATUS!", status);
        goto cleanup;
    }

    pDeviceInit = WdfPdoInitAllocate(Device);
    if (pDeviceInit == NULL) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto cleanup;
    }

    // XXX: Should this be changed?
    WdfDeviceInitSetDeviceType(pDeviceInit, FILE_DEVICE_BUS_EXTENDER);

    status = WdfPdoInitAssignDeviceID(pDeviceInit, &deviceId);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! WdfPdoInitAssignDeviceID with %!STATUS!", status);
        goto cleanup;
    }

    status = WdfPdoInitAddHardwareID(pDeviceInit, IsBusNode ? &deviceId : &hardwareId);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! WdfPdoInitAddHardwareID with %!STATUS!", status);
        goto cleanup;
    }

    status = WdfPdoInitAddCompatibleID(pDeviceInit, IsBusNode ? &busNodeCompatId : &devNodeCompatId);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! WdfPdoInitAddCompatibleID with %!STATUS!", status);
        goto cleanup;
    }

    status = RtlUnicodeStringPrintf(&buffer, L"%02d", Offset);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! RtlUnicodeStringPrintf with %!STATUS!", status);
        goto cleanup;
    }

    status = WdfPdoInitAssignInstanceID(pDeviceInit, &buffer);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! WdfPdoInitAssignInstanceID with %!STATUS!", status);
        goto cleanup;
    }

    RtlInitAnsiString(&nodeNameAnsi, NodeName);
    status = RtlAnsiStringToUnicodeString(&buffer, &nodeNameAnsi, FALSE);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! RtlAnsiStringToUnicodeString with %!STATUS!", status);
        goto cleanup;
    }

    status = WdfPdoInitAddDeviceText(pDeviceInit, &buffer, &deviceLocation, 0x409);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! WdfPdoInitAddDeviceText with %!STATUS!", status);
        goto cleanup;
    }

    WdfPdoInitSetDefaultLocale(pDeviceInit, 0x409);
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&pdoAttributes, PDO_DEVICE_CONTEXT);
    status = WdfDeviceCreate(&pDeviceInit, &pdoAttributes, &hChild);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! WdfDeviceCreate with %!STATUS!", status);
        goto cleanup;
    }

    PPDO_DEVICE_CONTEXT pdoData = PdoGetContext(hChild);
    pdoData->CurrentDepth = Depth;
    pdoData->CurrentOffset = Offset;
    pdoData->DeviceTreeBlobSize = Context->DeviceTreeBlobSize;
    pdoData->pDeviceTreeBlob = Context->pDeviceTreeBlob;

    // Platform device, can't be removed
    WDF_DEVICE_PNP_CAPABILITIES_INIT(&pnpCaps);
    pnpCaps.Removable = WdfFalse;
    pnpCaps.EjectSupported = WdfFalse;
    pnpCaps.SurpriseRemovalOK = WdfFalse;
    pnpCaps.Address = Offset;
    pnpCaps.UINumber = Offset;
    WdfDeviceSetPnpCapabilities(hChild, &pnpCaps);

    WDF_DEVICE_POWER_CAPABILITIES_INIT(&powerCaps);
    powerCaps.DeviceD1 = WdfTrue;
    powerCaps.WakeFromD1 = WdfTrue;
    powerCaps.DeviceWake = PowerDeviceD1;
    powerCaps.DeviceState[PowerSystemWorking] = PowerDeviceD0;
    powerCaps.DeviceState[PowerSystemSleeping1] = PowerDeviceD1;
    powerCaps.DeviceState[PowerSystemSleeping2] = PowerDeviceD3;
    powerCaps.DeviceState[PowerSystemSleeping3] = PowerDeviceD3;
    powerCaps.DeviceState[PowerSystemHibernate] = PowerDeviceD3;
    powerCaps.DeviceState[PowerSystemShutdown] = PowerDeviceD3;
    WdfDeviceSetPowerCapabilities(hChild, &powerCaps);

    // Add an interface for child device use
    if (IsBusNode) {
        RtlZeroMemory(&BusInterface, sizeof(BusInterface));
        BusInterface.InterfaceHeader.Size = sizeof(BusInterface);
        BusInterface.InterfaceHeader.Version = 1;
        BusInterface.InterfaceHeader.Context = (PVOID)hChild;
        BusInterface.InterfaceHeader.InterfaceReference = WdfDeviceInterfaceReferenceNoOp;
        BusInterface.InterfaceHeader.InterfaceDereference = WdfDeviceInterfaceDereferenceNoOp;
        BusInterface.CurrentDepth = Depth;
        BusInterface.CurrentOffset = Offset;
        BusInterface.DeviceTreeBlobSize = Context->DeviceTreeBlobSize;
        BusInterface.pDeviceTreeBlob = Context->pDeviceTreeBlob;

        WDF_QUERY_INTERFACE_CONFIG_INIT(&qiConfig,
            (PINTERFACE)&BusInterface,
            &GUID_INTERNAL_INTERFACE_QUERY_DT,
            NULL);

        status = WdfDeviceAddQueryInterface(hChild, &qiConfig);
        if (!NT_SUCCESS(status)) {
            TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! WdfDeviceAddQueryInterface with %!STATUS!", status);
            goto cleanup;
        }
    }
    
    status = WdfFdoAddStaticChild(Device, hChild);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! WdfFdoAddStaticChild with %!STATUS!", status);
        goto cleanup;
    }

cleanup:
    if (pDeviceInit != NULL) WdfDeviceInitFree(pDeviceInit);
    if (hChild) WdfObjectDelete(hChild);
    return status;
}
