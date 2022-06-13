
#include <iostream>

#include <llvm/DebugInfo/CodeView/GlobalTypeTableBuilder.h>
#include <llvm/Support/InitLLVM.h>

#include "restorer.h"

namespace opts
{
  std::string OutputFolder;
}

void print_help()
{
  std::cout << "OVERVIEW: Class header dumper" << std::endl << std::endl;
  std::cout << "USAGE: restorer.exe [options] <input object files>" << std::endl << std::endl;
  std::cout << "OPTIONS:" << std::endl;
  std::cout << "  --recursive            Collect all binary files recursively" << std::endl;
  std::cout << "  --input-folder=path    Input folder path" << std::endl;
  std::cout << "  --pdb-folder=path      Pdb folder path" << std::endl;
  std::cout << "  --output-folder=path   Output folder path" << std::endl;
  std::cout << "  --help                 Display available options" << std::endl;
}

int main(int argc, const char* argv[])
{
  llvm::InitLLVM X(argc, argv);

  restorer::SymbolsTree tree;

  // Simple parameters parsing
  // TODO: Use some library for command line apps
  std::filesystem::path OutputFolder = "./restorer";
  std::filesystem::path InputFolder = "";
  std::filesystem::path PDBFolder = "";
  bool recursive = false;
  for (int i = 1; i < argc; ++i)
  {
    std::string arg(argv[i]);
    if (arg[0] == '-')
    {
      if (arg.rfind("--help", 0) == 0)
      {
        print_help();
        return 0;
      }
      else if (arg.rfind("--output-folder=", 0) == 0)
      {
        OutputFolder = arg.substr(16);
      }
      else if (arg.rfind("--input-folder=", 0) == 0)
      {
        InputFolder = arg.substr(15);
      }
      else if (arg.rfind("--pdb-folder=", 0) == 0)
      {
        PDBFolder = arg.substr(13);
      }
      else if (arg.rfind("--recursive", 0) == 0)
      {
        recursive = true;
      }
    }
    else
    {
      // TODO: Add some way to specify pdb file from command line
      tree.inputs.push_back({ arg });
    }
  }

  if (!InputFolder.empty())
  {
    if (recursive)
    {
      for (const auto& entry : std::filesystem::recursive_directory_iterator(InputFolder))
      {
        tree.inputs.push_back({ entry.path().string() });
      }
    }
    else
    {
      for (const auto& entry : std::filesystem::directory_iterator(InputFolder))
      {
        tree.inputs.push_back({ entry.path().string() });
      }
    }
  }

  if (tree.inputs.empty())
  {
    print_help();
    return 0;
  }

  tree.PDBFolder = PDBFolder;

  // Do work
  tree.collect();
  tree.process();

  // Output
  tree.output_to_folder(OutputFolder);

  return 0;
}
