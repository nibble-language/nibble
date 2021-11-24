**THIS PROGRAMMING LANGUAGE IS A WORK IN PROGRESS. ANYTHING CAN CHANGE AT ANY MOMENT. USE AT YOUR OWN RISK!**
# Nibble
A programming language based on C. The goal is to create a compiled, statically-typed programming language that adds the following features to C:

- [x] Order-independent declarations
- [x] Module import system (based on javascript ES6)
- [x] Type inference
- [ ] Multiple return values (return anonymous structure object)
- [ ] Defer statement (like go)
- [ ] Default procedure arguments
- [ ] Named procedure arguments
- [ ] Generic procedures (like c++ templates)

Nibble supports the followng operating systems:
- [x] x64 linux
- [x] x64 windows
- [ ] x86 linux
- [ ] x86 windows
- [ ] arm64 macOS

## Quickstart
### Build the Nibble compiler
The only library required to build the Nibble compiler is the C standard library.
#### Linux
The following generates an executable called `nibble` in the root project directory.
```console
$ gcc -I./src -O2 -o nibble src/main.c
```
#### Windows
The following generates an executable called `nibble.exe` in the root project directory. Note that you must [install C11/C17 support in Visual Studio](https://docs.microsoft.com/en-us/cpp/overview/install-c17-support?view=msvc-170).
```console
$ cl.exe -std:c11 /Fe:nibble.exe .\src\main.c
```
### Compiling a nibble program
The Nibble compiler currently generates an assembly file that is assembled with NASM and then linked with your operating system's linker. Therefore, you'll need to install the [NASM assembler](https://nasm.us/) and add it to your system `PATH`.

Here's an example that compiles the ["Hello World" example program](examples/hello_world/main.nib) on linux.

```console
$ ./nibble examples/hello_world/main.nib -o hello_world
[INFO]: Parsing module /main.nib ...
[INFO]: Generating NASM assembly output: hello_world.s ...
[CMD]: nasm -f elf64 hello_world.s -o hello_world.o
[CMD]: ld -o hello_world hello_world.o
$ ./hello_world
Hello, World

```

To compile a Nibble program, the compiler only needs the file (i.e., module) containing your program's `main()` procedure. The compiler can automatically pickup any imported or included files. Refer to the language reference to learn more about importing or including other files.

### Command-line options
Run `./nibble -h` for available command-line options.

```console
$ ./nibble -h
Usage: ./nibble [OPTIONS] <input.nib>
OPTIONS:
    -h                              Print this help message
    -os   [linux | win32 | osx]     Target OS
    -arch [x64 | x86]               Target architecture
    -o    <output_file>             Output binary file name. Defaults to `out`
```
### Running tests
TODO

## Code examples
Note that Nibble provides a builtin procedure called `#writeout()` that writes bytes to stdout. Refer to the language reference for a description of all builtin procedures.

### Hello, World

main.nib:
```c
proc main(argc : int, argv : ^^char) => int
{
    var msg : []char = "Hello, World\n";
    const len : usize = #sizeof(#typeof(msg)) - 1;

    #writeout(msg, len);
    return 0;
}
```

```console
$ ./nibble main.nib
[INFO]: Parsing module /main.nib ...
[INFO]: Generating NASM assembly output: out.s ...
[CMD]: nasm -f elf64 out.s -o out.o
[CMD]: ld -o out out.o
$ ./out
Hello, World

```
### Merge sort
This [example](examples/merge_sort/) demonstrates basic use of the module import system. Refer to the language reference to learn more about importing.

main.nib:
```c
import { merge_sort } from "./sort.nib";

proc main() => int {
    var a : []int = {5, 4, 3, 2, 1, 0};
    var b : [6]int;

    var c : ^int = merge_sort(a, b, 6);

    var i := 0;
    while (i < 6) {
        var ch : char = c[i] + '0';

        #writeout(^ch, 1);
        #writeout(" ", 1);

        i = i + 1;
    }

    #writeout("\n", 1);

    return 0;
}
```

sort.nib:
```c
// Returns a pointer to the sorted array (either a or b).
@exported
proc merge_sort(a: ^int, b: ^int, n : int) => ^int {
    var width := 1;

    while (width < n) {
        var d := width * 2;
        var i := 0;

        while (i < n) {
            var m := min(i + width, n);
            var r := min(i + d, n);

            merge(a, i, m, r, b);

            i = i + d;
        }

        // Swap.
        var t := a;
        a = b;
        b = t;

        width = d;
    }

    return a;
}

//
// Internal procedures.
//

proc merge(a: ^int, l: int, m: int, r: int, b: ^int) {
    var i := l;
    var j := m;

    var k := l;

    while (k < r) {
        if (i < m && (j >= r || a[i] <= a[j])) {
            b[k] = a[i];
            i = i + 1;
        }
        else {
            b[k] = a[j];
            j = j + 1;
        }

        k = k + 1;
    }
}

proc min(a: int, b: int) => int {
    var r := a;

    if (b < a) {
        r = b;
    }

    return r;
}
```

```console
$ ./nibble main.nib
[INFO]: Parsing module /main.nib ...
[INFO]: Parsing module /sort.nib ...
[INFO]: Generating NASM assembly output: out.s ...
[CMD]: nasm -f elf64 out.s -o out.o
[CMD]: ld -o out out.o
$ ./out
0 1 2 3 4 5 

```


## Status of the project

Nibble does not yet support all basic C features:
- [x] Integer types
- [ ] Floating-point types
- [ ] Structure types
- [ ] Union types
- [ ] Enum types
- [ ] Procedures
    - [x] Basic procedures with non-variadic parameters
    - [ ] Varidic parameters
- [ ] Statements
    - [x] if/else
    - [x] while 
    - [x] do while
    - [ ] for loop
    - [ ] switch
    - [ ] break
    - [ ] continue
    - [ ] goto! (yes, I will)
    - [x] return 
    - [x] Expressions
    - [x] Assignments
- [ ] Ternary operator
- [x] Binary operators
- [x] Unary operators

## Language reference
TODO
