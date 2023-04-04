# Device Tree Bus Driver

This is a statically-enumerated Windows Bus Driver that takes a Linux/Open Firmware Flattened Device Tree input and emits devices nodes. It might be useful for certain scenarios where writing a complete ACPI DSDT table takes too much effort and time.

So far it enumerates all device nodes, but doing nothing else.

## Testing

Using Visual Studio 2022 and Windows 11 WDK to build.

	devcon install DeviceTreeBus.inf Root\DeviceTreeBus

## TODO

- Make code cleaner. There are many copy and paste now.
- Create ACPI device binding simular to Hyper-V Virtual Machine Bus device, and load Device Tree payload from DSDT (load Device Tree externally via Filesystem/Registry may create a security hazard)
- Hide certain devices/nodes that are not applicable in PnP context, such as Interrupt Controller, Architectural Timer and PSCI information (they can be trivially translated to static ACPI tables)
- Provide resource information (MMIO addresses, interrupt line, etc.) to PnP subsystem
- Handle known ACPI IOCTLs for device connections with known standard binding (GPIO, I2C, etc.)
- Create an interface for querying raw OF DeviceTree properties (similar/compatible to `ACPI\PRP0001`)

## License 

GPLv2 / BSD
