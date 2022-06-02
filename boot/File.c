//
// Created by Aaron Gill-Braun on 2022-05-29.
//

#include <File.h>

#include <Guid/FileInfo.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Library/BaseMemoryLib.h>

EFI_LOADED_IMAGE_PROTOCOL *LoadedImage;
EFI_HANDLE BootDeviceHandle;
EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *BootDeviceFileSystem;
EFI_FILE *BootVolumeRoot;


EFI_STATUS EFIAPI ConvertToWinPath(IN CONST CHAR16 *Path, IN OUT CHAR16 **WinPath) {
  *WinPath = AllocatePool(StrSize(Path) + 1);
  if (*WinPath == NULL) {
    PRINT_ERROR("Failed to allocate memory for path");
    return EFI_OUT_OF_RESOURCES;
  }

  StrCpyS(*WinPath, StrSize(Path) + 1, Path);

  CHAR16 *Ptr = *WinPath;
  while (*Ptr) {
    if (*Ptr == L'/') {
      *Ptr = L'\\';
    }
    Ptr++;
  }
  return EFI_SUCCESS;
}

//

EFI_STATUS EFIAPI InitializeFileProtocols() {
  EFI_STATUS Status;
  EFI_GUID LoadedImageGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;

  Status = gBS->HandleProtocol(gImageHandle, &LoadedImageGuid, (void **) &LoadedImage);
  if (EFI_ERROR(Status)) {
    PRINT_ERROR("Failed to get EFI_LOADED_IMAGE_PROTOCOL for image handle");
    return Status;
  }

  BootDeviceHandle = LoadedImage->DeviceHandle;
  Status = gBS->HandleProtocol(BootDeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (void **) &BootDeviceFileSystem);
  if (EFI_ERROR(Status)) {
    PRINT_ERROR("Failed to get EFI_SIMPLE_FILE_SYSTEM_PROTOCOL for boot device");
    return Status;
  }

  Status = BootDeviceFileSystem->OpenVolume(BootDeviceFileSystem, &BootVolumeRoot);
  if (EFI_ERROR(Status)) {
    PRINT_ERROR("Failed to open boot device volume");
    return Status;
  }

  return EFI_SUCCESS;
}

EFI_STATUS EFIAPI OpenFile(IN CONST CHAR16 *Path, OUT EFI_FILE **File) {
  ASSERT(BootVolumeRoot != NULL);
  EFI_STATUS Status;

  CHAR16 *WinPath;
  Status = ConvertToWinPath(Path, &WinPath);
  if (EFI_ERROR(Status)) {
    PRINT_ERROR("Failed to convert path");
    return Status;
  }

  Status = BootVolumeRoot->Open(BootVolumeRoot, File, WinPath, EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR(Status)) {
    PRINT_ERROR("Failed to open file %s", Path);
    FreePool(WinPath);
    return Status;
  }

  FreePool(WinPath);
  return EFI_SUCCESS;
}

EFI_STATUS EFIAPI ReadFile(IN EFI_FILE *File, OUT UINTN *BufferSize, OUT VOID **Buffer) {
  EFI_STATUS Status;
  EFI_GUID FileInfoGuid = EFI_FILE_INFO_ID;
  UINTN FileInfoSize = sizeof(EFI_FILE_INFO);

RETRY: NULL;

  EFI_FILE_INFO *FileInfo = AllocatePool(FileInfoSize);
  if (FileInfo == NULL) {
    PRINT_ERROR("Failed to allocate memory for file info");
    return EFI_OUT_OF_RESOURCES;
  }

  Status = File->GetInfo(File, &FileInfoGuid, &FileInfoSize, FileInfo);
  if (EFI_ERROR(Status)) {
    if (Status == EFI_BUFFER_TOO_SMALL) {
      FreePool(FileInfo);
      goto RETRY;
    }

    FreePool(FileInfo);
    PRINT_ERROR("Failed to get file info");
    return Status;
  }

  UINTN FileSize = FileInfo->FileSize;
  UINT8 *Data = AllocateZeroPool(FileSize);
  if (Data == NULL) {
    FreePool(FileInfo);
    PRINT_ERROR("Failed to allocate memory for file");
    return EFI_OUT_OF_RESOURCES;
  }

  SetMem(Data, FileSize, 0);
  Status = File->SetPosition(File, 0);
  if (EFI_ERROR(Status)) {
    FreePool(FileInfo);
    FreePool(Data);
    PRINT_ERROR("Failed to set file position");
    return Status;
  }

  Status = File->Read(File, &FileSize, Data);
  if (EFI_ERROR(Status)) {
    FreePool(FileInfo);
    FreePool(Data);
    PRINT_ERROR("Failed to read file");
    return Status;
  }

  *Buffer = Data;
  *BufferSize = FileSize;
  FreePool(FileInfo);
  return EFI_SUCCESS;
}
