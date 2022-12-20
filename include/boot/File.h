//
// Created by Aaron Gill-Braun on 2022-05-29.
//

#ifndef BOOT_FILE_H
#define BOOT_FILE_H

#include <Common.h>
#include <Guid/FileInfo.h>

EFI_STATUS EFIAPI InitializeFileProtocols();

EFI_STATUS EFIAPI OpenFile(IN CONST CHAR16 *Path, OUT EFI_FILE **File);
EFI_STATUS EFIAPI GetFileInfo(IN EFI_FILE *File, OUT EFI_FILE_INFO **FileInfo);
EFI_STATUS EFIAPI ReadFile(IN EFI_FILE *File, OUT UINTN *BufferSize, OUT VOID **Buffer);
EFI_STATUS EFIAPI CloseFile(IN EFI_FILE *File);

/**
 * Looks for a file with the specified name in the given directory.
 *
 * @param Parent The directory to search in. If NULL, the root directory is used.
 * @param Name The name of the file to search for.
 * @param Recurse If true, the search will be a depth-first recursive search.
 * @param File [out] The file that was found.
 * @return EFI_SUCCESS if the file was found, otherwise an error code.
 */
EFI_STATUS EFIAPI LocateFileByName(
  IN EFI_FILE *Parent,
  IN CONST CHAR16 *Name,
  IN BOOLEAN Recurse,
  OUT EFI_FILE **File
);

#endif
