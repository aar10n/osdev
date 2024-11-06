#!/usr/bin/env python3
import argparse
from elftools.elf.elffile import ELFFile
from elftools.elf.sections import SymbolTableSection
import sys


def get_symbol_address(elf_path, symbol_name):
    """
    Get the address of a symbol from an ELF file.
    
    Args:
        elf_path (str): Path to the ELF file
        symbol_name (str): Name of the symbol to look up
        
    Returns:
        int: Address of the symbol if found, None otherwise
    """
    with open(elf_path, 'rb') as f:
        elf = ELFFile(f)
        
        # Look in all symbol tables
        for section in elf.iter_sections():
            if isinstance(section, SymbolTableSection):
                # Get symbol table entries
                symbols = section.get_symbol_by_name(symbol_name)
                if symbols:
                    symbol = symbols[0]  # Get first matching symbol
                    return symbol.entry.st_value
                    
    return None

def list_all_symbols(elf_path):
    """
    List all symbols and their addresses from an ELF file.
    
    Args:
        elf_path (str): Path to the ELF file
        
    Returns:
        dict: Dictionary mapping symbol names to their addresses
    """
    symbols_dict = {}
    with open(elf_path, 'rb') as f:
        elf = ELFFile(f)
        
        for section in elf.iter_sections():
            if isinstance(section, SymbolTableSection):
                for symbol in section.iter_symbols():
                    if symbol.name:  # Skip unnamed symbols
                        symbols_dict[symbol.name] = symbol.entry.st_value
                        
    return symbols_dict

def main():
    parser = argparse.ArgumentParser(description='Extract symbol addresses from ELF files')
    parser.add_argument('elf_file', help='Path to the ELF file')
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument('-s', '--symbol', help='Name of the symbol to look up')
    group.add_argument('-a', '--all', action='store_true', help='List all symbols')
    parser.add_argument('--no-header', action='store_true', help='Omit header in output')
    args = parser.parse_args()

    try:
        if args.symbol:
            address = get_symbol_address(args.elf_file, args.symbol)
            if address is not None:
                if not args.no_header:
                    print(f"Symbol: {args.symbol}")
                print(f"{hex(address)}")
            else:
                print(f"Symbol '{args.symbol}' not found", file=sys.stderr)
                sys.exit(1)
        
        elif args.all:
            symbols = list_all_symbols(args.elf_file)
            if not symbols:
                print("No symbols found", file=sys.stderr)
                sys.exit(1)
                
            # Find the longest symbol name for pretty printing
            max_length = max(len(name) for name in symbols.keys())
            
            if not args.no_header:
                print(f"{'Symbol':<{max_length}} | Address")
                print("-" * max_length + "-+-" + "-" * 18)
            
            for name, addr in sorted(symbols.items()):
                print(f"{name:<{max_length}} | {hex(addr)}")

    except FileNotFoundError:
        print(f"Error: File '{args.elf_file}' not found", file=sys.stderr)
        sys.exit(1)
    except PermissionError:
        print(f"Error: Permission denied reading '{args.elf_file}'", file=sys.stderr)
        sys.exit(1)
    except Exception as e:
        print(f"Error: {str(e)}", file=sys.stderr)
        sys.exit(1)

if __name__ == '__main__':
    main()
