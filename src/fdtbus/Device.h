#include "public.h"

EXTERN_C_START

//
// The device context performs the same job as
// a WDM device extension in the driver frameworks
//
typedef struct _FDO_DEVICE_CONTEXT
{
    WDFWAITLOCK ChildLock;

    VOID* pDeviceTreeBlob;
    size_t DeviceTreeBlobSize;

    int CurrentDepth;
    int CurrentOffset;
} FDO_DEVICE_CONTEXT, *PFDO_DEVICE_CONTEXT;

typedef struct _PDO_DEVICE_CONTEXT
{
    VOID* pDeviceTreeBlob;
    size_t DeviceTreeBlobSize;

    int CurrentDepth;
    int CurrentOffset;
} PDO_DEVICE_CONTEXT, *PPDO_DEVICE_CONTEXT;

//
// This macro will generate an inline function called DeviceGetContext
// which will be used to get a pointer to the device context memory
// in a type safe manner.
//
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(FDO_DEVICE_CONTEXT, FdoGetContext)
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(PDO_DEVICE_CONTEXT, PdoGetContext)

//
// Function to initialize the device and its callbacks
//
NTSTATUS
DeviceTreeBusCreateDevice(
    _Inout_ PWDFDEVICE_INIT DeviceInit
    );

NTSTATUS
EnumerateDevices(
    _In_ WDFDEVICE Device,
    _In_ PFDO_DEVICE_CONTEXT Context
);

NTSTATUS
CreatePdoAndInsert(
    _In_ WDFDEVICE Device,
    _In_ PFDO_DEVICE_CONTEXT Context,
    _In_ const char* NodeName,
    _In_ int Offset,
    _In_ int Depth,
    _In_ BOOLEAN IsBusNode
);

#define DT_BUSENUM_DEVICENODE_COMPATIBLE_IDS L"DEVICETREE\\DeviceNode"
#define DT_BUSENUM_DEVICENODE_COMPATIBLE_IDS_LENGTH sizeof(DT_BUSENUM_DEVICENODE_COMPATIBLE_IDS)

#define DT_BUSENUM_BUSNODE_COMPATIBLE_IDS L"DEVICETREE\\BusNode"
#define DT_BUSENUM_BUSNODE_COMPATIBLE_IDS_LENGTH sizeof(DT_BUSENUM_BUSNODE_COMPATIBLE_IDS)

#define DT_BUSENUM_LOCATION_ID L"Device Tree Bus 0"

#define DT_COMPATIBLE_PROP "compatible"

#define MAX_ID_LEN 255

EXTERN_C_END
