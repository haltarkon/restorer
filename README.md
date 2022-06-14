# restorer

An application that allows you to recover header files for C++ classes and namespaces from dll and pdb files built by [Microsoft Visual C++](https://en.wikipedia.org/wiki/Microsoft_Visual_C++).

The current implementation just meets my requirements and is not a finished product. Tested on Windows only.

## Example

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

You can find here example folder with [example.h header file](example/example.h). After restoration we get something like in [example_restored.h](example/example_restored.h).

## How it works

+ A lot of information can be obtained from [decorated names](https://en.wikiversity.org/wiki/Visual_C++_name_mangling). 
+ It is possible to restore the inheritance hierarchy if the library you are using uses [Microsoft Visual C++ RTTI](http://www.openrce.org/articles/full_view/23). 
+ [PDB files](https://github.com/microsoft/microsoft-pdb) can be used to obtain additional information that will increase the amount of information recovered.

We can represent the nesting of classes and namespaces in the form of a tree for each of the modules (exe/dll). Combined trees of all modules are combined gives us a more complete picture, but this requires no collisions.
The collected information is displayed in C++ header files, which can even be included in other C++ projects. Unfortunately, RTTI does not keep a list of class fields, so the task is much more complicated. At this point, the class fields need to be restored manually.

## Requirements

+ [LLVM](https://llvm.org)
+ DIA (for processing PDB)

## Possible improvement directions (TODO)

+ Add a set of options for more flexibility;
+ Full support for PDB processing (structure/class layout info)
+ Restore fields lists for class/struct/union;
+ Add also generation of source files (.cpp) for linkless function calling;
+ Investigate Linux support;
+ Make pre-built packages.

## Inspiration and ideas

Thanks for idea to [GrandpaGameHacker](https://github.com/GrandpaGameHacker):
+ [ClassDumper](https://github.com/GrandpaGameHacker/ClassDumper)
+ [ClassDumper2](https://github.com/GrandpaGameHacker/ClassDumper2)

LLVM:
+ [LLVM](https://github.com/llvm/llvm-project)
+ [llvm-pdbutil](https://llvm.org/docs/CommandGuide/llvm-pdbutil.html)
+ [llvm-readobj](https://llvm.org/docs/CommandGuide/llvm-readobj.html)
+ [llvm-objdump](https://llvm.org/docs/CommandGuide/llvm-objdump.html)

+ [Pharos Visual C++ Demangler](https://github.com/cmu-sei/pharos-demangle) - Another good library for demangling MSVC symbols that used before I found everything needed in [LLVM](https://github.com/llvm/llvm-project).
