//
// Created by Aaron Gill-Braun on 2020-09-27.
//

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>

#include "Config.h"

#define SyntaxError(Msg) \
  ErrorPrint(L"[Loader] INI Syntax Error: " \
             Msg " (%d:%d)\n", Line, Column)

typedef enum {
  IniStartOfLine,
  IniMiddleOfLine,
  IniParseKey,
  IniParseValue,
} INI_STATE;

typedef struct _INI_BUCKET {
  INI_VARIABLE *Variable;
  struct _INI_BUCKET *Next;
} INI_BUCKET;

// The number of allocated elements in the HashMap array
static UINTN HashMapLength = 0;
// The total number of key-value pairs in the HashMap
static UINTN HashMapSize = 0;
// The HashMap array itself
static INI_BUCKET **HashMap = NULL;

// Scratch buffers for keys/values being parsed
static CHAR8 CurrentKey[INI_MAX_KEY_LEN];
static CHAR8 CurrentValue[INI_MAX_VALUE_LEN];

//

void EFIAPI FreeVariable(INI_VARIABLE *Variable) {
  FreePool(Variable->Key);
  FreePool(Variable->Value);
  FreePool(Variable);
}

//

UINTN EFIAPI GetMapIndex(CHAR8 *Key) {
  UINTN KeyLen = AsciiStrLen(Key);

  UINT32 Hash = CalculateCrc32(Key, KeyLen);
  return Hash % HashMapLength;
}

EFI_STATUS EFIAPI HashMapSet(INI_VARIABLE *Variable) {
  if (HashMap == NULL) {
    return EFI_ABORTED;
  }

  UINTN Index = GetMapIndex(Variable->Key);

  INI_BUCKET *Bucket = HashMap[Index];
  while (Bucket) {
    // if there is already one or more buckets at the given
    // index, look for an existing key to replace or the last
    // bucket that we can add on to.
    if (AsciiStrCmp(Bucket->Variable->Key, Variable->Key) == 0) {
      // replace a key

      // used the old value
      FreePool(Bucket->Variable->Value);

      // point at new value
      Bucket->Variable->Value = Variable->Value;

      // used the passed in name and struct
      FreePool(Variable->Key);
      FreePool(Variable);

      return EFI_SUCCESS;
    }

    if (Bucket->Next == NULL)
      break;
    Bucket = Bucket->Next;
  }

  // allocate new bucket for variable
  INI_BUCKET *NewBucket = AllocatePool(sizeof(INI_BUCKET));
  if (NewBucket == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  NewBucket->Variable = Variable;
  NewBucket->Next = NULL;

  if (Bucket != NULL) {
    // add to end of list
    Bucket->Next = NewBucket;
  } else {
    // add new at index
    HashMap[Index] = NewBucket;
  }

  HashMapSize++;
  return EFI_SUCCESS;
}

EFI_STATUS EFIAPI HashMapGet(CHAR8 *Key, INI_VARIABLE **Result) {
  if (HashMap == NULL) {
    return EFI_ABORTED;
  }

  UINTN Index = GetMapIndex(Key);

  INI_BUCKET *Bucket = HashMap[Index];
  if (Bucket == NULL) {
    *Result = NULL;
    return EFI_NOT_FOUND;
  }

  *Result = Bucket->Variable;
  return EFI_SUCCESS;
}

//

BOOLEAN EFIAPI OnlyWhitespaceBefore(CHAR8 Char, CHAR8 *Ptr, const CHAR8 *PtrEnd) {
  while (Ptr < PtrEnd && *Ptr != Char) {
    if (*Ptr != ' ' && *Ptr != '\t' && *Ptr != '\0') {
      return FALSE;
    }
    Ptr++;
  }
  return TRUE;
}

//

EFI_STATUS EFIAPI ConfigParse(void *Buffer, UINTN BufferSize) {
  if (HashMap != NULL) {
    return RETURN_ABORTED;
  }

  EFI_STATUS Status;

  HashMap = AllocateZeroPool(INI_MAP_LEN * sizeof(INI_BUCKET *));
  if (HashMap == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  HashMapLength = INI_MAP_LEN;
  HashMapSize = 0;

  CHAR8 *PtrStart = Buffer;
  CHAR8 *PtrEnd = Buffer + BufferSize;
  CHAR8 *Ptr = PtrStart;

  UINTN Line = 1;
  UINTN Column = 1;

  UINTN KeyLen = 0;
  UINTN ValueLen = 0;
  UINTN ValueEnd = 0;

  INI_STATE State = IniStartOfLine;

  while (Ptr < PtrEnd) {
    switch (*Ptr) {
      // comments
      case ';':
      case '#':
        goto EndOfLine;
      // line terminator
      case '\n': {
        if (State == IniMiddleOfLine || State == IniParseValue) {
          goto SaveVariable;
        } else if (KeyLen > 0) {
          SyntaxError(L"Unexpected EOL");
        }

        goto EndOfLine;
      }
      // assignment operator
      case '=': {
        // check if an assignment is valid
        if (KeyLen == 0) {
          // invalid syntax
          SyntaxError(L"Unexepected token '='");
          // recover by skipping line
          goto EndOfLine;
        }

        State = IniMiddleOfLine;
        break;
      }
      // whitespace
      case ' ':
      case '\t': {
        if (State == IniStartOfLine || State == IniMiddleOfLine) {
          // ignore whitespace before key and value
          break;
        } else if (State == IniParseKey) {
          if (OnlyWhitespaceBefore('=', Ptr, PtrEnd)) {
            // consume whitespace
            while (Ptr < PtrEnd && *(Ptr + 1) != '=') {
              Ptr++;
            }
            break;
          }

          // // invalid syntax
          // SyntaxError(L"Invalid whitespace in key");
          // // recover by skipping line
          // goto EndOfLine;
        }

        if (ValueEnd == 0) {
          // save the current length of the value in
          // case there are no more characters left
          // in the line, in which case we want to
          // ignore the trailing spaces
          ValueEnd = ValueLen;
        }
        // fallthrough
      }
      default: {
        if (*Ptr != ' ' && *Ptr != '\t') {
          ValueEnd = 0;
        }

        if (State == IniStartOfLine || State == IniParseKey) {
          State = IniParseKey;

          if (KeyLen >= INI_MAX_KEY_LEN) {
            // key too long
            SyntaxError(L"Key is too long");
            // recover by skipping line
            goto EndOfLine;
          }

          CurrentKey[KeyLen] = *Ptr;
          KeyLen++;
        } else {
          State = IniParseValue;

          if (ValueLen >= INI_MAX_VALUE_LEN) {
            // key too long
            SyntaxError(L"Value is too long");
            // recover by skipping line
            goto EndOfLine;
          }

          CurrentValue[ValueLen] = *Ptr;
          ValueLen++;
        }
      }
    }

    Column++;
    Ptr++;
    continue;
  SaveVariable:
    if (ValueEnd)
      ValueLen = ValueEnd;

    CurrentKey[KeyLen] = '\0';
    CurrentValue[ValueLen] = '\0';

    INI_VARIABLE *Variable = AllocatePool(sizeof(INI_VARIABLE));
    CHAR8 *Key = AllocateZeroPool(KeyLen + 1);
    CHAR8 *Value = AllocateZeroPool(ValueLen + 1);
    if (!Variable || !Key || !Value) {
      FreePool(Variable);
      FreePool(Key);
      FreePool(Value);
      return EFI_OUT_OF_RESOURCES;
    }

    CopyMem(Key, CurrentKey, KeyLen);
    CopyMem(Value, CurrentValue, ValueLen);

    Print(L"[Loader] INI: %a = '%a'\n", Key, Value);
    Variable->Key = Key;
    Variable->Value = Value;

    Status = HashMapSet(Variable);
    if (EFI_ERROR(Status)) {
      FreeVariable(Variable);
      return Status;
    }
  EndOfLine:
    // skip to the end of the current line
    while (Ptr < PtrEnd && *Ptr && *Ptr != '\n')
      Ptr++;

    // reset all state
    KeyLen = 0;
    ValueLen = 0;
    State = IniStartOfLine;

    Line++;
    Column = 1;

    Ptr++;
  }

  Print(L"[Loader] %d keys loaded\n", HashMapSize);

  return EFI_SUCCESS;
}

CHAR8 EFIAPI *ConfigGet(CHAR8 *Key) {
  if (HashMap == NULL) {
    return NULL;
  }

  INI_VARIABLE *Variable;
  EFI_STATUS Status = HashMapGet(Key, &Variable);
  if (EFI_ERROR(Status)) {
    return NULL;
  }
  return Variable->Value;
}

EFI_STATUS EFIAPI ConfigSet(CHAR8 *Key, CHAR8 *Value) {
  if (HashMap == NULL) {
    return EFI_ABORTED;
  }

  INI_VARIABLE *Variable = AllocatePool(sizeof(INI_VARIABLE));
  if (Variable == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Variable->Key = Key;
  Variable->Value = Value;

  EFI_STATUS Status = HashMapSet(Variable);
  if (EFI_ERROR(Status)) {
    FreePool(Variable);
    return Status;
  }
  return EFI_SUCCESS;
}

//
// Typed 'Get' Functions
//

UINT64 EFIAPI ConfigGetNumeric(CHAR8 *Key) {
  UINT64 Result;
  if (EFI_ERROR(ConfigGetNumericS(Key, &Result))) {
    return 0;
  }
  return Result;
}

EFI_STATUS EFIAPI ConfigGetNumericS(CHAR8 *Key, UINT64 *Result) {
  EFI_STATUS Status;

  Status = ConfigGetHexS(Key, Result);
  if (EFI_ERROR(Status)) {
    if (Status == EFI_NOT_FOUND) {
      return EFI_NOT_FOUND;
    }

    return ConfigGetDecimalS(Key, Result);
  }
  return EFI_SUCCESS;
}

//

UINT64 EFIAPI ConfigGetDecimal(CHAR8 *Key) {
  UINT64 Result;
  if (EFI_ERROR(ConfigGetDecimalS(Key, &Result))) {
    return 0;
  }
  return Result;
}

EFI_STATUS EFIAPI ConfigGetDecimalS(CHAR8 *Key, UINT64 *Result) {
  CHAR8 *Value = ConfigGet(Key);
  if (Value == NULL) {
    return EFI_NOT_FOUND;
  }

  EFI_STATUS Status = AsciiStrDecimalToUint64S(Value, NULL, Result);
  if (EFI_ERROR(Status)) {
    return EFI_UNSUPPORTED;
  }
  return EFI_SUCCESS;
}

//

UINT64 EFIAPI ConfigGetHex(CHAR8 *Key) {
  UINT64 Result;
  if (EFI_ERROR(ConfigGetHexS(Key, &Result))) {
    return 0;
  }
  return Result;
}

EFI_STATUS EFIAPI ConfigGetHexS(CHAR8 *Key, UINT64 *Result) {
  CHAR8 *Value = ConfigGet(Key);
  if (Value == NULL) {
    return EFI_NOT_FOUND;
  }

  EFI_STATUS Status = AsciiStrHexToUint64S(Value, NULL, Result);
  if (EFI_ERROR(Status)) {
    return EFI_UNSUPPORTED;
  }
  return EFI_SUCCESS;
}

//

EFI_STATUS EFIAPI ConfigGetDimensions(CHAR8 *Key, UINT32 *X, UINT32 *Y) {
  EFI_STATUS Status;
  CHAR8 *XDim;
  CHAR8 *YDim;
  UINT64 XResult;
  UINT64 YResult;

  CHAR8 *Value = ConfigGet(Key);
  if (Value == NULL) {
    return EFI_NOT_FOUND;
  }

  UINTN ValueLen = AsciiStrLen(Value);
  UINTN CharCount = 0;
  CHAR8 *Ptr = Value;
  while (*Ptr) {
    if (*Ptr == 'x' || *Ptr == ',') {
      if (CharCount > 0) {
        XDim = AllocateZeroPool(CharCount + 1);
        if (XDim == NULL) {
          return EFI_OUT_OF_RESOURCES;
        }
        CopyMem(XDim, Value, CharCount);
        CharCount = 0;
      } else {
        FreePool(XDim);
        return EFI_UNSUPPORTED;
      }
    } else if (*Ptr != ' ' && *Ptr != '\t') {
      CharCount++;
    }

    Ptr++;
  }

  if (XDim == NULL || CharCount == 0) {
    FreePool(XDim);
    return EFI_UNSUPPORTED;
  }

  YDim = AllocateZeroPool(CharCount + 1);
  if (YDim == NULL) {
    FreePool(XDim);
    return EFI_OUT_OF_RESOURCES;
  }
  CopyMem(YDim, Value + (ValueLen - CharCount), CharCount);

  Status = AsciiStrDecimalToUint64S(XDim, NULL, &XResult);
  if (!EFI_ERROR(Status)) {
    Status = AsciiStrDecimalToUint64S(YDim, NULL, &YResult);
  }

  if (EFI_ERROR(Status)) {
    FreePool(XDim);
    FreePool(YDim);
    return Status;
  }

  *X = (UINT32) XResult;
  *Y = (UINT32) YResult;

  FreePool(XDim);
  FreePool(YDim);

  return EFI_SUCCESS;
}
