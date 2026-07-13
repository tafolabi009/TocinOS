/*
 * TocinBoot — minimal freestanding UEFI definitions (x86-64).
 *
 * Self-contained subset of the UEFI 2.x specification: only the types,
 * protocols, and boot-services slots TocinBoot uses are given real
 * prototypes; everything else is a VOID* placeholder in the correct slot so
 * structure offsets match the spec. Compiled as a native PE with
 * x86_64-w64-mingw32-gcc, where the Microsoft calling convention (EFIAPI) is
 * the default; EFIAPI is spelled out anyway so the file also compiles with a
 * SysV-targeting GCC/Clang.
 */

#ifndef TOCINBOOT_EFI_H
#define TOCINBOOT_EFI_H

typedef unsigned char      UINT8;
typedef unsigned short     UINT16;
typedef unsigned int       UINT32;
typedef unsigned long long UINT64;
typedef long long          INT64;
typedef UINT64             UINTN; /* x86-64 only */
typedef INT64              INTN;
typedef UINT8              BOOLEAN;
typedef UINT16             CHAR16;
typedef void               VOID;

typedef UINTN  EFI_STATUS;
typedef VOID  *EFI_HANDLE;
typedef VOID  *EFI_EVENT;
typedef UINT64 EFI_PHYSICAL_ADDRESS;
typedef UINT64 EFI_VIRTUAL_ADDRESS;

#if defined(__GNUC__) && !defined(_MSC_VER)
#define EFIAPI __attribute__((ms_abi))
#else
#define EFIAPI
#endif

/* ---- status codes ------------------------------------------------------- */

#define EFI_SUCCESS 0ULL
#define EFI_ERR(n) (0x8000000000000000ULL | (n))
#define EFI_LOAD_ERROR        EFI_ERR(1)
#define EFI_INVALID_PARAMETER EFI_ERR(2)
#define EFI_UNSUPPORTED       EFI_ERR(3)
#define EFI_BAD_BUFFER_SIZE   EFI_ERR(4)
#define EFI_BUFFER_TOO_SMALL  EFI_ERR(5)
#define EFI_NOT_FOUND         EFI_ERR(14)
#define EFI_ERROR(st) (((INT64)(st)) < 0)

/* ---- GUID ---------------------------------------------------------------- */

typedef struct {
    UINT32 Data1;
    UINT16 Data2;
    UINT16 Data3;
    UINT8  Data4[8];
} EFI_GUID;

/* ---- table header -------------------------------------------------------- */

typedef struct {
    UINT64 Signature;
    UINT32 Revision;
    UINT32 HeaderSize;
    UINT32 CRC32;
    UINT32 Reserved;
} EFI_TABLE_HEADER;

/* ---- memory -------------------------------------------------------------- */

typedef enum {
    AllocateAnyPages = 0,
    AllocateMaxAddress = 1,
    AllocateAddress = 2
} EFI_ALLOCATE_TYPE;

typedef enum {
    EfiReservedMemoryType = 0,
    EfiLoaderCode = 1,
    EfiLoaderData = 2,
    EfiBootServicesCode = 3,
    EfiBootServicesData = 4,
    EfiRuntimeServicesCode = 5,
    EfiRuntimeServicesData = 6,
    EfiConventionalMemory = 7,
    EfiUnusableMemory = 8,
    EfiACPIReclaimMemory = 9,
    EfiACPIMemoryNVS = 10,
    EfiMemoryMappedIO = 11,
    EfiMemoryMappedIOPortSpace = 12,
    EfiPalCode = 13,
    EfiPersistentMemory = 14
} EFI_MEMORY_TYPE;

/* Natural x86-64 layout puts PhysicalStart at offset 8, per spec.
 * Always iterate a returned map with the DescriptorSize stride. */
typedef struct {
    UINT32               Type;
    EFI_PHYSICAL_ADDRESS PhysicalStart;
    EFI_VIRTUAL_ADDRESS  VirtualStart;
    UINT64               NumberOfPages;
    UINT64               Attribute;
} EFI_MEMORY_DESCRIPTOR;

#define EFI_PAGE_SIZE 4096ULL

/* ---- simple text output --------------------------------------------------- */

typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    VOID *Reset;
    EFI_STATUS(EFIAPI *OutputString)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,
                                     const CHAR16 *String);
    VOID *TestString;
    VOID *QueryMode;
    VOID *SetMode;
    VOID *SetAttribute;
    VOID *ClearScreen;
    VOID *SetCursorPosition;
    VOID *EnableCursor;
    VOID *Mode;
};

/* ---- configuration table --------------------------------------------------- */

typedef struct {
    EFI_GUID VendorGuid;
    VOID    *VendorTable;
} EFI_CONFIGURATION_TABLE;

/* ---- boot services (slot order is normative: UEFI spec §4.4) --------------- */

typedef struct {
    EFI_TABLE_HEADER Hdr;

    VOID *RaiseTPL;
    VOID *RestoreTPL;

    EFI_STATUS(EFIAPI *AllocatePages)(EFI_ALLOCATE_TYPE Type,
                                      EFI_MEMORY_TYPE MemoryType, UINTN Pages,
                                      EFI_PHYSICAL_ADDRESS *Memory);
    EFI_STATUS(EFIAPI *FreePages)(EFI_PHYSICAL_ADDRESS Memory, UINTN Pages);
    EFI_STATUS(EFIAPI *GetMemoryMap)(UINTN *MemoryMapSize,
                                     EFI_MEMORY_DESCRIPTOR *MemoryMap,
                                     UINTN *MapKey, UINTN *DescriptorSize,
                                     UINT32 *DescriptorVersion);
    EFI_STATUS(EFIAPI *AllocatePool)(EFI_MEMORY_TYPE PoolType, UINTN Size,
                                     VOID **Buffer);
    EFI_STATUS(EFIAPI *FreePool)(VOID *Buffer);

    VOID *CreateEvent;
    VOID *SetTimer;
    VOID *WaitForEvent;
    VOID *SignalEvent;
    VOID *CloseEvent;
    VOID *CheckEvent;

    VOID *InstallProtocolInterface;
    VOID *ReinstallProtocolInterface;
    VOID *UninstallProtocolInterface;
    EFI_STATUS(EFIAPI *HandleProtocol)(EFI_HANDLE Handle, EFI_GUID *Protocol,
                                       VOID **Interface);
    VOID *Reserved;
    VOID *RegisterProtocolNotify;
    VOID *LocateHandle;
    VOID *LocateDevicePath;
    VOID *InstallConfigurationTable;

    VOID *LoadImage;
    VOID *StartImage;
    VOID *Exit;
    VOID *UnloadImage;
    EFI_STATUS(EFIAPI *ExitBootServices)(EFI_HANDLE ImageHandle, UINTN MapKey);

    VOID *GetNextMonotonicCount;
    EFI_STATUS(EFIAPI *Stall)(UINTN Microseconds);
    EFI_STATUS(EFIAPI *SetWatchdogTimer)(UINTN Timeout, UINT64 WatchdogCode,
                                         UINTN DataSize, CHAR16 *WatchdogData);

    VOID *ConnectController;
    VOID *DisconnectController;

    VOID *OpenProtocol;
    VOID *CloseProtocol;
    VOID *OpenProtocolInformation;

    VOID *ProtocolsPerHandle;
    VOID *LocateHandleBuffer;
    EFI_STATUS(EFIAPI *LocateProtocol)(EFI_GUID *Protocol, VOID *Registration,
                                       VOID **Interface);
    VOID *InstallMultipleProtocolInterfaces;
    VOID *UninstallMultipleProtocolInterfaces;

    VOID *CalculateCrc32;
    VOID *CopyMem;
    VOID *SetMem;
    VOID *CreateEventEx;
} EFI_BOOT_SERVICES;

/* ---- system table ----------------------------------------------------------- */

typedef struct {
    EFI_TABLE_HEADER Hdr;
    CHAR16 *FirmwareVendor;
    UINT32  FirmwareRevision;
    EFI_HANDLE ConsoleInHandle;
    VOID   *ConIn;
    EFI_HANDLE ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
    EFI_HANDLE StandardErrorHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *StdErr;
    VOID   *RuntimeServices;
    EFI_BOOT_SERVICES *BootServices;
    UINTN   NumberOfTableEntries;
    EFI_CONFIGURATION_TABLE *ConfigurationTable;
} EFI_SYSTEM_TABLE;

/* ---- loaded image ------------------------------------------------------------ */

#define EFI_LOADED_IMAGE_PROTOCOL_GUID                                         \
    { 0x5B1B31A1, 0x9562, 0x11d2,                                              \
      { 0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B } }

typedef struct {
    UINT32 Revision;
    EFI_HANDLE ParentHandle;
    EFI_SYSTEM_TABLE *SystemTable;
    EFI_HANDLE DeviceHandle;
    VOID *FilePath;
    VOID *Reserved;
    UINT32 LoadOptionsSize;
    VOID *LoadOptions;
    VOID *ImageBase;
    UINT64 ImageSize;
    EFI_MEMORY_TYPE ImageCodeType;
    EFI_MEMORY_TYPE ImageDataType;
    VOID *Unload;
} EFI_LOADED_IMAGE_PROTOCOL;

/* ---- simple filesystem / file ------------------------------------------------- */

#define EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID                                   \
    { 0x964e5b22, 0x6459, 0x11d2,                                              \
      { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }

#define EFI_FILE_INFO_ID                                                       \
    { 0x09576e92, 0x6d3f, 0x11d2,                                              \
      { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }

#define EFI_FILE_MODE_READ 0x0000000000000001ULL

typedef struct EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;
struct EFI_FILE_PROTOCOL {
    UINT64 Revision;
    EFI_STATUS(EFIAPI *Open)(EFI_FILE_PROTOCOL *This,
                             EFI_FILE_PROTOCOL **NewHandle,
                             const CHAR16 *FileName, UINT64 OpenMode,
                             UINT64 Attributes);
    EFI_STATUS(EFIAPI *Close)(EFI_FILE_PROTOCOL *This);
    VOID *Delete;
    EFI_STATUS(EFIAPI *Read)(EFI_FILE_PROTOCOL *This, UINTN *BufferSize,
                             VOID *Buffer);
    VOID *Write;
    VOID *GetPosition;
    VOID *SetPosition;
    EFI_STATUS(EFIAPI *GetInfo)(EFI_FILE_PROTOCOL *This,
                                EFI_GUID *InformationType, UINTN *BufferSize,
                                VOID *Buffer);
    VOID *SetInfo;
    VOID *Flush;
};

typedef struct {
    UINT64 Revision;
    EFI_STATUS(EFIAPI *OpenVolume)(VOID *This, EFI_FILE_PROTOCOL **Root);
} EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

/* EFI_FILE_INFO: FileSize lives at offset 8; FileName is variable-length. */
typedef struct {
    UINT64 Size;
    UINT64 FileSize;
    UINT64 PhysicalSize;
    UINT8  CreateTime[16];       /* EFI_TIME */
    UINT8  LastAccessTime[16];   /* EFI_TIME */
    UINT8  ModificationTime[16]; /* EFI_TIME */
    UINT64 Attribute;
    CHAR16 FileName[1];
} EFI_FILE_INFO;

/* ---- graphics output ------------------------------------------------------------ */

#define EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID                                      \
    { 0x9042a9de, 0x23dc, 0x4a38,                                              \
      { 0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a } }

typedef enum {
    PixelRedGreenBlueReserved8BitPerColor = 0,
    PixelBlueGreenRedReserved8BitPerColor = 1,
    PixelBitMask = 2,
    PixelBltOnly = 3
} EFI_GRAPHICS_PIXEL_FORMAT;

typedef struct {
    UINT32 RedMask;
    UINT32 GreenMask;
    UINT32 BlueMask;
    UINT32 ReservedMask;
} EFI_PIXEL_BITMASK;

typedef struct {
    UINT32 Version;
    UINT32 HorizontalResolution;
    UINT32 VerticalResolution;
    EFI_GRAPHICS_PIXEL_FORMAT PixelFormat;
    EFI_PIXEL_BITMASK PixelInformation;
    UINT32 PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
    UINT32 MaxMode;
    UINT32 Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
    UINTN SizeOfInfo;
    EFI_PHYSICAL_ADDRESS FrameBufferBase;
    UINTN FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef struct EFI_GRAPHICS_OUTPUT_PROTOCOL EFI_GRAPHICS_OUTPUT_PROTOCOL;
struct EFI_GRAPHICS_OUTPUT_PROTOCOL {
    VOID *QueryMode;
    VOID *SetMode;
    VOID *Blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
};

/* ---- configuration table GUIDs ----------------------------------------------------- */

#define EFI_ACPI_20_TABLE_GUID                                                 \
    { 0x8868e871, 0xe4f1, 0x11d3,                                              \
      { 0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81 } }

#define EFI_ACPI_10_TABLE_GUID                                                 \
    { 0xeb9d2d30, 0x2d88, 0x11d3,                                              \
      { 0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d } }

#endif /* TOCINBOOT_EFI_H */
