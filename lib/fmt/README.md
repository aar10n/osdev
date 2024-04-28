### fmt — a freestanding C string formatting library


```
Format Strings
==============

A format string is a string that contains zero or more format specifiers. A specifier
is a sequence of characters enclosed between '{' and '}', but printf style specifiers
using '%' are also supported. To specify a literal '{' use '{{' and for '}', use '}' 
or '}}'.
The overall syntax of a format specifier is:

    {[index]:[[$fill]align][flags][width][.precision][type]}

Printf style specifiers are supported as well:

    %[flags][width][.precision]type

index
    The index field is an optional positive integer that specifies the index of
    the argument to use for the value. Implicitly assigned indices begin at the
    first argument (0) and are incremented by 1 for each argument that is not
    explicitly assigned an index.

align
    The align field is an optional character that specifies the alignment of the output
    within the width of the field. A sequence of a '$' followed by a single non-null
    character may immediately precede the alignment marker to specify the character
    used for padding. If no alignment is specified, the output is left aligned using
    spaces. The following alignments are supported:

        '[$fill]<' - left justify
        '[$fill]^' - center justify
        '[$fill]>' - right justify

flags
    The flags field is a set of optional flags that modify the output.
    The following flags are supported:

        '#'       - alternate form
        '!'       - uppercase form
        '0'       - sets the fill character to '0'
                    for numeric values, pad with leading zeros up to width (conflicts with `align`)
        '+'       - always print sign for numeric values
        '-'       - right align (overrides align and zero)
        ' '       - leave a space in front of positive numeric values (conflicts with '+')

width
    The width field is an optional positive integer that specifies the minimum width
    of the output. After all other formatting is applied, the output is padded to the
    specified width using spaces or the fill character if specified in the align field.

    The width may also be specified using a '*' which will cause the next implicit argument
    to be used as the width, or as '*index' where index is a positive integer, which will
    use the specified argument as the width. When using the '*' syntax, the argument must
    be an integer.

precision
    The precision field is an optional positive integer.
    For floating point numbers, it specifies the number of digits to display after the
    decimal point. The default precision is 6 and the maximum precision is 9. The output
    is padded with trailing zeros if necessary.
    For integers, it specifies the minimum number of digits to display. By default, there
    is no minimum number of digits. The output is padded with leading zeros if necessary.
    For strings, it specifies the maximum number of characters to display. By default,
    strings are read until the first null character is found, but the precision field can
    be used to limit the number of characters read.

    The precision may be specified using a '*' or '*index' as described in the width field.

type
    The type field is an optional character or string that specifies the type of the
    argument. If no type is specified, the width and fill are respected, but no other
    formatting is applied.
    The following built-in types are supported:

        '[<type>]d'   - signed decimal integer
        '[<type>]u'   - unsigned decimal integer
        '[<type>]b'   - unsigned binary integer
        '[<type>]o'   - unsigned octal integer
        '[<type>]x'   - unsigned hexadecimal integer
        where <type> is one of the following:
          'll' - 64-bit integer
          'z'  - size_t
        or a 32-bit integer if no type is specified

        'f'             - floating point number (double)
        'F'             - floating point number capitalized

        's'             - string
        'c'             - character
        'p'             - pointer

Notes:
  - The maximum number of arguments supported by the fmt funcions is defined by the
    `FMT_MAX_ARGS` macro.
  - Implicit arguments are limited to `max_args` (default FMT_MAX_ARGS) and will ignore
    any specifiers which consume further arguments.  

Examples:
    {:d}      - integer
    {:05d}    - integer, sign-aware zero padding
    {:.2f}    - double, 2 decimal places
    {:>10u}   - unsigned, right justified with spaces
    {:$#^10d} - integer, center justified with '#'
    {:s}      - string
    {:.3s}    - string of specific length
    
```

##### Benchmarks/Tests:
I would never claim this to be the fastest string formatting library, and performance
isn't a primary concern. However, the test suite also benchmarks the library to make
sure there isn't any obvious slowdowns.

M1 Macbook Pro 10C 32GB
```
[PASS] "Hello, world!" in 186 ns
[PASS] "Hello, world!" in 158 ns
[PASS] "42" in 163 ns
[PASS] "2a" in 159 ns
[PASS] "3.14" in 156 ns
[PASS] "3.14, 42" in 177 ns
[PASS] "42, 3.14" in 185 ns
[PASS] "3.14, string, 42" in 233 ns
[PASS] "0x2a" in 144 ns
[PASS] "2A" in 143 ns
[PASS] "007" in 136 ns
[PASS] "-007" in 143 ns
[PASS] "+007" in 143 ns
[PASS] "1   " in 143 ns
[PASS] "   1" in 140 ns
[PASS] " 42" in 131 ns
[PASS] "-42" in 128 ns
[PASS] "3" in 127 ns
[PASS] "3.1" in 132 ns
[PASS] "42  " in 140 ns
[PASS] " 42 " in 144 ns
[PASS] "  42" in 137 ns
[PASS] "===== hello =====" in 153 ns
[PASS] "............101" in 169 ns
[PASS] "101............" in 176 ns
[PASS] "          " in 147 ns
[PASS] "42" in 122 ns
[PASS] "2a" in 121 ns
[PASS] "3.14" in 141 ns
[PASS] "FFFFFFFFFFFFFFFF" in 164 ns
[PASS] "1, hi, f" in 160 ns
[PASS] "->  <-" in 117 ns
```
See `test.c` for the full test suite.
