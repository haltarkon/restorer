# restorer

An application that allows you to recover header files for C ++ classes and namespaces from dll and pdb files built by Visual C ++.

The current implementation just meets my requirements and is not a finished product. Tested on Windows only.

## Features or Example

It easy to get current options with help:

    $ restorer.exe --help

    OVERVIEW: Class header dumper

    USAGE: restorer.exe [options] <input object files>

    OPTIONS:
    --recursive            Collect all binary files recursively
    --input-folder=path    Input folder path
    --pdb-folder=path      Pdb folder path
    --output-folder=path   Output folder path
    --help                 Display available options

You can pass a list of dlls or a directory path (for recursive traversal) to the input and get a set of header files at the output, in which all the found characters keep their nesting.

    $ ./restorer.exe libA.dll libB.dll

## How it works

A lot of information can be obtained from [decorated names](https://en.wikiversity.org/wiki/Visual_C++_name_mangling).
It is possible to restore the inheritance hierarchy if the library you are using uses [Microsoft Visual C ++ RTTI](http://www.openrce.org/articles/full_view/23).

## Requirements

+ [LLVM](https://llvm.org)
+ DIA

## Possible improvement directions (TODO)

+ Add a set of options for more flexibility;
+ Restore field lists for class/struct/union;
+ Add also generation of source files (.cpp) for linkless function calling;
+ Investigate Linux support;
+ Make pre-built packages.

## Inspiration and ideas

Thanks for [GrandpaGameHacker](https://github.com/GrandpaGameHacker):
+ [ClassDumper](https://github.com/GrandpaGameHacker/ClassDumper)
+ [ClassDumper2](https://github.com/GrandpaGameHacker/ClassDumper2)

LLVM utils:
+ [llvm-pdbutil](https://llvm.org/docs/CommandGuide/llvm-pdbutil.html)
+ [llvm-readobj](https://llvm.org/docs/CommandGuide/llvm-readobj.html)
+ [llvm-objdump](https://llvm.org/docs/CommandGuide/llvm-objdump.html)
