//
// Created by Aaron Gill-Braun on 2022-05-29.
//

#include <File.h>

#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DevicePathLib.h>

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

EFI_STATUS EFIAPI ListFilesInDirectory(EFI_FILE *Dir) {
  EFI_STATUS Status;
  EFI_FILE_INFO *FileInfo;
  UINTN FileInfoSize = 1024;

  FileInfo = AllocatePool(FileInfoSize);
  if (FileInfo == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Dir->SetPosition(Dir, 0);
  PRINT_INFO("Listing directory:");
  while (TRUE) {
    FileInfoSize = 1024;
    Status = Dir->Read(Dir, &FileInfoSize, (void *) FileInfo);
    if (FileInfoSize == 0) {
      break;
    } else if (EFI_ERROR(Status)) {
      FreePool(FileInfo);
      return Status;
    }

    Print(L"  %s\n", FileInfo->FileName);
  }

  FreePool(FileInfo);
  return 0;
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

  EFI_DEVICE_PATH *DevicePath = DevicePathFromHandle(BootDeviceHandle);
  if (DevicePath == NULL || DevicePath->Type == 0) {
    PRINT_ERROR("Failed to get EFI_SIMPLE_FILE_SYSTEM_PROTOCOL for boot device");
    return EFI_PROTOCOL_ERROR;
  }
  ConvertDevicePathToText(DevicePath, TRUE, TRUE);

  CHAR16 *PathStr = ConvertDevicePathToText(DevicePath, TRUE, TRUE);
  PRINT_INFO("Boot device: %s", PathStr);
  FreePool(PathStr);

  ListFilesInDirectory(BootVolumeRoot);
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

EFI_STATUS EFIAPI GetFileInfo(IN EFI_FILE *File, OUT EFI_FILE_INFO **OutFileInfo) {
  ASSERT(File != NULL);
  EFI_STATUS Status;
  EFI_GUID FileInfoGuid = EFI_FILE_INFO_ID;
  UINTN FileInfoSize = sizeof(EFI_FILE_INFO);
  EFI_FILE_INFO *_FileInfo = NULL;

RETRY:;
  EFI_FILE_INFO *FileInfo = AllocatePool(FileInfoSize);
  if (_FileInfo == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Status = File->GetInfo(File, &FileInfoGuid, &FileInfoSize, _FileInfo);
  if (EFI_ERROR(Status)) {
    if (Status == EFI_BUFFER_TOO_SMALL) {
      FreePool(_FileInfo);
      goto RETRY;
    }

    FreePool(_FileInfo);
    PRINT_ERROR("Failed to get file info");
    return Status;
  }

  *OutFileInfo = FileInfo;
  return EFI_SUCCESS;
}

EFI_STATUS EFIAPI ReadFile(IN EFI_FILE *File, OUT UINTN *BufferSize, OUT VOID **Buffer) {
  EFI_STATUS Status;
  EFI_GUID FileInfoGuid = EFI_FILE_INFO_ID;
  UINTN FileInfoSize = sizeof(EFI_FILE_INFO);

RETRY:;
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

EFI_STATUS EFIAPI CloseFile(IN EFI_FILE *File) {
  ASSERT(File != NULL);
  EFI_STATUS Status;

  Status = File->Close(File);
  if (EFI_ERROR(Status)) {
    PRINT_ERROR("Failed to close file");
    return Status;
  }

  return EFI_SUCCESS;
}

//

EFI_STATUS EFIAPI LocateFileByName(
  IN EFI_FILE *Parent,
  IN CONST CHAR16 *Name,
  IN BOOLEAN Recurse,
  OUT EFI_FILE **File
) {
  ASSERT(BootVolumeRoot != NULL);
  EFI_STATUS Status;
  EFI_FILE_INFO *FileInfo;
  UINTN FileInfoSize = 1024;
  UINTN NameLen = StrLen(Name);

  if (Parent == NULL) {
    Parent = BootVolumeRoot;
  }

  FileInfo = AllocatePool(FileInfoSize);
  if (FileInfo == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  EFI_FILE_HANDLE Dir = Parent;
  Dir->SetPosition(Dir, 0);
  while (TRUE) {
    FileInfoSize = 1024;
    Status = Dir->Read(Dir, &FileInfoSize, (void *) FileInfo);
    if (FileInfoSize == 0) {
      // we've reached the end with no matches
      FreePool(FileInfo);
      return EFI_NOT_FOUND;
    } else if (EFI_ERROR(Status)) {
      FreePool(FileInfo);
      return Status;
    }

    if (StrnCmp(FileInfo->FileName, L".", 1) == 0 || StrnCmp(FileInfo->FileName, L"..", 2) == 0) {
      // skip the hardlinks to self and parent
      continue;
    }

    if (StrnCmp(FileInfo->FileName, Name, NameLen) == 0) {
      // we found the file
      EFI_FILE_HANDLE Result;
      Status = Dir->Open(Dir, &Result, FileInfo->FileName, EFI_FILE_MODE_READ, 0);
      if (EFI_ERROR(Status)) {
        FreePool(FileInfo);
        return Status;
      }

      *File = Result;
      return EFI_SUCCESS;
    } else if (FileInfo->Attribute & EFI_FILE_DIRECTORY && Recurse) {
      // look for the file one directory down
      EFI_FILE_HANDLE NextDir;
      Status = Dir->Open(Dir, &NextDir, FileInfo->FileName, EFI_FILE_MODE_READ, 0);
      if (EFI_ERROR(Status)) {
        FreePool(FileInfo);
        return Status;
      }

      Status = LocateFileByName(NextDir, Name, TRUE, File);
      NextDir->Close(NextDir);
      if (Status == EFI_SUCCESS) {
        FreePool(FileInfo);
        return EFI_SUCCESS;
      } else if (Status != EFI_NOT_FOUND) {
        FreePool(FileInfo);
        return Status;
      }

      // keep looking
    }
  }
}
