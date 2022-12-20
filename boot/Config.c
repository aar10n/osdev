//
// Created by Aaron Gill-Braun on 2020-09-27.
//

#include <Config.h>
#include <File.h>

#define SYNTAX_ERROR(string) \
  PRINT_WARN(string L" (%d:%d)", Line, Column)

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
          SYNTAX_ERROR("Unexpected EOL");
        }

        goto EndOfLine;
      }
      // assignment operator
      case '=': {
        // check if an assignment is valid
        if (KeyLen == 0) {
          // invalid syntax
          SYNTAX_ERROR("Unexepected token '='");
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

          // invalid syntax
          SYNTAX_ERROR("Invalid whitespace in key");
          // recover by skipping line
          goto EndOfLine;
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
            SYNTAX_ERROR("Key is too long");
            // recover by skipping line
            goto EndOfLine;
          }

          CurrentKey[KeyLen] = *Ptr;
          KeyLen++;
        } else {
          State = IniParseValue;

          if (ValueLen >= INI_MAX_VALUE_LEN) {
            // key too long
            SYNTAX_ERROR("Value is too long");
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

    PRINT_INFO("  %d | %a=%a", Line, Key, Value);
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

  return EFI_SUCCESS;
}

//

EFI_STATUS EFIAPI InitializeConfig() {
  EFI_STATUS Status;

  EFI_FILE *File = NULL;
  Status = LocateFileByName(NULL, L"config.ini", TRUE, &File);
  if (EFI_ERROR(Status)) {
    if (Status == EFI_NOT_FOUND) {
      PRINT_WARN("No config file found");
    } else {
      PRINT_ERROR("Failed to open config file");
    }
    return Status;
  }

  PRINT_INFO("Loading config");

  UINTN ConfigFileSize = 0;
  VOID *ConfigFile = NULL;
  Status = ReadFile(File, &ConfigFileSize, &ConfigFile);
  if (EFI_ERROR(Status)) {
    PRINT_ERROR("Failed to read config file");
    File->Close(File);
    return Status;
  }

  Status = ConfigParse(ConfigFile, ConfigFileSize);
  if (EFI_ERROR(Status)) {
    PRINT_ERROR("Failed to parse config file");
    File->Close(File);
    return Status;
  }

  PRINT_INFO("Config loaded");

  File->Close(File);
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

BOOLEAN EFIAPI ConfigGetBooleanD(CHAR8 *Key, BOOLEAN Default) {
  BOOLEAN Value;
  EFI_STATUS Status = ConfigGetBooleanS(Key, &Value);
  if (EFI_ERROR(Status)) {
    return Default;
  }
  return Value;
}

EFI_STATUS EFIAPI ConfigGetBooleanS(CHAR8 *Key, BOOLEAN *Value) {
  CHAR8 *ValueStr = ConfigGet(Key);
  if (ValueStr == NULL) {
    return EFI_NOT_FOUND;
  }

  UINTN ValueLen = AsciiStrLen(ValueStr);
  CHAR8 Tmp[ValueLen + 1];
  AsciiStrCpyS(Tmp, ValueLen + 1, ValueStr);
  Tmp[ValueLen] = '\0';
  for (UINTN i = 0; i < ValueLen; i++) {
    if (Tmp[i] >= 'A' && Tmp[i] <= 'Z') {
      Tmp[i] += 'A' - 'a';
    }
  }

  if (AsciiStrCmp(ValueStr, "true") == 0 || (AsciiStrCmp(ValueStr, "1") == 0)) {
    *Value = TRUE;
  } else if (AsciiStrCmp(ValueStr, "false") == 0 || (AsciiStrCmp(ValueStr, "0") == 0)) {
    *Value = FALSE;
  } else {
    return EFI_INVALID_PARAMETER;
  }
  return EFI_SUCCESS;
}

CHAR16 *EFIAPI ConfigGetStringD(CHAR8 *Key, CONST CHAR16 *Default) {
  CHAR16 *String;
  EFI_STATUS Status = ConfigGetStringS(Key, &String);
  if (EFI_ERROR(Status)) {
    if (Default == NULL) {
      return NULL;
    }

    UINTN Length = StrSize(Default);
    String = AllocatePool((Length + 1) * sizeof(CHAR16));
    if (String == NULL) {
      return NULL;
    }

    CopyMem(String, Default, Length + 1);
    return String;
  }

  return String;
}

EFI_STATUS EFIAPI ConfigGetStringS(CHAR8 *Key, CHAR16 **String) {
  CHAR8 *Value = ConfigGet(Key);
  if (String == NULL) {
    return EFI_NOT_FOUND;
  }

  UINTN Length = AsciiStrLen(Value);
  CHAR16 *WideString = AllocateZeroPool(Length + 1);
  EFI_STATUS Status = AsciiStrToUnicodeStrS(Value, WideString, Length + 1);
  if (EFI_ERROR(Status)) {
    FreePool(WideString);
    return Status;
  }

  *String = WideString;
  return EFI_SUCCESS;
}

UINT64 EFIAPI ConfigGetNumericD(CHAR8 *Key, UINT64 Default) {
  UINT64 Result;
  if (EFI_ERROR(ConfigGetNumericS(Key, &Result))) {
    return Default;
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

UINT64 EFIAPI ConfigGetDecimalD(CHAR8 *Key, UINT64 Default) {
  UINT64 Result;
  if (EFI_ERROR(ConfigGetDecimalS(Key, &Result))) {
    return Default;
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

UINT64 EFIAPI ConfigGetHexD(CHAR8 *Key, UINT64 Default) {
  UINT64 Result;
  if (EFI_ERROR(ConfigGetHexS(Key, &Result))) {
    return Default;
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

EFI_STATUS EFIAPI ConfigGetDuration(CHAR8 *Key, UINT64 *Result) {
  CHAR8 *Value = ConfigGet(Key);
  if (Value == NULL) {
    return EFI_NOT_FOUND;
  }

  UINT64 Number = 0;
  CHAR8 *UnitPtr = Value;
  while (*UnitPtr) {
    if (*UnitPtr == 'u' || *UnitPtr == 'm' || *UnitPtr == 's' || *UnitPtr == 'h') {
      break;
    } else if (*UnitPtr >= '0' && *UnitPtr <= '9') {
      Number = Number * 10 + (*UnitPtr - '0');
      UnitPtr++;
      continue;
    }

    return EFI_INVALID_PARAMETER;
  }

  if (*UnitPtr == 0) {
    return EFI_INVALID_PARAMETER;
  }

  UINTN UnitLen = 0;
  UINT64 ResultUs = 0;
  if (UnitPtr[0] == 'u' && UnitPtr[1] == 's') {
    ResultUs = Number;
    UnitLen = 2;
  } else if (UnitPtr[0] == 'm' && UnitPtr[1] == 's') {
    ResultUs = Number * 1000;
    UnitLen = 2;
  } else if (UnitPtr[0] == 's') {
    ResultUs = Number * 1000000;
    UnitLen = 1;
  } else if (*UnitPtr == 'm') {
    ResultUs = Number * 60000000;
    UnitLen = 1;
  } else if (*UnitPtr == 'h') {
    *Result = Number * 3600000;
    UnitLen = 1;
  }

  if (UnitPtr[UnitLen] != 0) {
    return EFI_INVALID_PARAMETER;
  }

  *Result = ResultUs;
  return EFI_SUCCESS;
}
