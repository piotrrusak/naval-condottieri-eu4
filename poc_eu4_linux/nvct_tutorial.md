# EU4 Naval Condottieri — build, inject and test

A short procedure for compiling the library, injecting it into a running EU4 process with GDB, and manually adding a test contract for a fleet.

## 1. Build the library

```bash
just build
```

Every `nvct_*.c` file is compiled to `build/<name>.so`.

---

## 2. Launch EU4

Start the game as usual and load a save.

Then find its PID in a terminal:

```bash
ps aux | grep eu4
```

---

## 3. Attach GDB

Pass the EU4 PID when launching GDB:

```bash
gdb -p <pid>
```

The game freezes while GDB is attached. That's normal.

---

## 4. Loading and unloading the dynamic library

Loading:

```gdb
set $h = (void*) dlopen("/home/piotrrusak/.local/share/Steam/steamapps/common/Europa Universalis IV/<libname>.so", 2)
```

`2` is `RTLD_NOW`. If `$h` is `0`, check the error with:

```gdb
p (char*) dlerror()
```

On a successful load, the library's constructor prints a `[nav-condottieri] ... ready` banner to the game's stdout.

Unloading:

```gdb
p (int) dlclose($h)
```

---

## 5. Quick GDB tutorial

### Running and stopping

Attaching GDB stops the process automatically. To resume it:

```gdb
continue
```

(or `c` for short). Press Ctrl+C (SIGINT) to stop the process again.

To detach and leave the game running:

```gdb
detach
quit
```

### Breakpoints

Set a breakpoint on a function by name or by address:

```gdb
break <function>
break *0x18fee80
```

`0x18fee80` is `CUnit::MayFight(CUnit const*) const` (`ADDR_MAY_FIGHT` in the source).

Manage breakpoints:

```gdb
info breakpoints
disable <n>
enable <n>
delete <n>
```

`MayFight` is called very often, so delete or disable its breakpoint once you have what you need. Otherwise the game is practically unplayable.

### Inspecting state when stopped

Print the backtrace:

```gdb
bt
```

Show registers. For x86-64 SysV, the arguments are `rdi`, `rsi`, `rdx`, `rcx`, `r8` and `r9`:

```gdb
info registers
p/x $rdi
```

When stopped at the start of `MayFight`, `$rdi` is `this` (unit A) and `$rsi` is `other` (unit B).

### Reading memory

GDB doesn't know EU4's classes, so you read raw memory at a pointer plus an offset.

```
x/<count><format><size> <address>
```

- **format:** `x` hex, `d` decimal, `s` string, `c` char, `a` address
- **size:** `b` 1 byte, `h` 2, `w` 4, `g` 8

The count comes first. Format and size can be in either order (`x/8xg` = `x/8gx`).

Examples:

```gdb
x/8xg $rdi          # 8 x 8 bytes from the unit, in hex
x/s   $rdi+0xc8     # owner tag as a string, e.g. "TEU"
x/dh  $rdi+0xcc     # owner index (int16 after the 4-byte tag)
```

The same with `p` and a cast:

```gdb
p/x *(long*)($rdi+0xc8)
```

Put `$rdi+offset` in parentheses before casting. Otherwise the offset gets multiplied by the type size.

To follow a pointer stored in a field:

```gdb
set $p = *(void**)($rdi+0x10)
x/8xg $p
```

`Cannot access memory at address ...` means the pointer or the offset is wrong, or the object no longer exists.

### Other useful commands

Search for functions by name:

```gdb
info functions <regex>
```

Continue until the current function returns, and print its return value:

```gdb
finish
```

### Calling library functions (test API)

While the process is stopped and the library is loaded, you can call its exported functions directly:

```gdb
p (int) nvct_contract_count()
p (void) nvct_contract_clear()
```
