# Em_EEPROM Host CLI

This CLI runs the Em_EEPROM middleware against a RAM-backed virtual flash device so you can inspect logical reads/writes and raw physical flash layout without hardware.

## Build

### macOS / Linux

```sh
make host-cli
```

This produces:

```text
build/emeeprom_cli
```

### Windows

Use CMake with either MSVC or another Windows C compiler:

```powershell
cmake -S . -B out
cmake --build out --config Release
```

This produces:

```text
out/build/emeeprom_cli.exe
```

## Run

```sh
build/emeeprom_cli --eeprom-size 128 --wear 2 --redundant 0
```

## Startup Options

- `--flash-size <n>`
  Total virtual flash size in bytes.
  Default: `4096`

- `--program-size <n>`
  Virtual flash program granularity in bytes.
  Default: `128`

- `--erase-size <n>`
  Virtual flash erase granularity in bytes.
  Default: `256`

- `--eeprom-size <n>`
  Logical EEPROM size passed into `Cy_Em_EEPROM_Init_BD()`.
  Default: `128`

- `--simple <0|1>`
  `0` for extended mode, `1` for simple mode.
  Default: `0`

- `--wear <1..10>`
  Wear-leveling factor.
  Default: `2`

- `--redundant <0|1>`
  Enable or disable redundant copy support.
  Default: `0`

## REPL Commands

- `help`
  Print the available commands.

- `info`
  Print the current middleware geometry and state:
  - `row_size`
  - `byte_in_row`
  - `rows`
  - `wear`
  - `simple`
  - `redundant`
  - `last_row`

- `stats`
  Print virtual flash operation counters:
  - `reads`
  - `programs`
  - `erases`

- `numwrites`
  Print the result of `Cy_Em_EEPROM_NumWrites()`.

- `read <addr> <len>`
  Read logical EEPROM bytes starting at logical address `addr` for `len` bytes.

  Example:

  ```text
  read 0 16
  ```

- `write <addr> <hex-byte> [hex-byte ...]`
  Write one or more bytes at logical EEPROM address `addr`.

  Byte arguments are parsed as hexadecimal.

  Example:

  ```text
  write 0 41 42 43 44
  ```

  This writes `ABCD`.

- `write-str <addr> <text>`
  Write literal text bytes at logical EEPROM address `addr`.

  Example:

  ```text
  write-str 8 hello world
  ```

- `erase`
  Call `Cy_Em_EEPROM_Erase()`.

- `dump-flash <offset> <len>`
  Dump raw physical virtual flash contents, starting at flash-relative byte offset `offset` for `len` bytes.

  This is useful for inspecting extended-mode row layout directly.

  Example:

  ```text
  dump-flash 0 128
  ```

- `corrupt <offset> <hex-byte>`
  Overwrite one raw physical flash byte at flash-relative offset `offset`.

  This is useful for checksum and recovery experiments.

  Example:

  ```text
  corrupt 0 ff
  ```

- `fail-program`
  Force the next virtual flash `program()` call to fail.

- `fail-erase`
  Force the next virtual flash `erase()` call to fail.

- `reset-flash`
  Reset the virtual flash to erased state and reinitialize the middleware with the original startup configuration.

- `quit`
  Exit the CLI.

- `exit`
  Same as `quit`.

## Addressing Model

- `read`, `write`, and `write-str` use logical EEPROM addresses.
- `dump-flash` and `corrupt` use raw physical flash offsets.

## Output Format

Successful operations print a middleware status such as:

```text
CY_EM_EEPROM_SUCCESS
```

`read` and `dump-flash` print:

- left column: address
- middle columns: hex bytes
- right column: ASCII rendering

## Example Session

```text
write 0 41 42 43 44
read 0 4
write-str 8 hello world
dump-flash 0 128
stats
numwrites
erase
quit
```
