#pragma once

#include <string>
#include <list>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

// C++17
#include <filesystem>

#include <llvm/DebugInfo/PDB/IPDBSession.h>
#include <llvm/Object/ObjectFile.h>

namespace restorer
{
  struct Container;
  struct Module;

  struct Node
  {
    Node* parent = nullptr;
    std::string name;

    std::string get_name(const char* delim = "::") const;
  };

  // enum, independent
  struct Enum : public Node
  {
    const char* type = "int";

    bool output_definition(std::ofstream& out, size_t indent = 0) const;
  };

  struct Occurrence
  {
    Module* modulePtr = nullptr;
    uintptr_t address = 0;
    bool owner = false;
  };

  // static variables
  struct Symbol : public Node
  {
    std::string nameMangled;
    std::list<Occurrence> occurrences;

    bool output_definition(std::ofstream& out, size_t indent = 0) const;
  };

  // template/regular functions
  struct Function : public Symbol
  {
  };

  struct VFTable
  {
    Occurrence place;

    struct VFunction
    {
      uintptr_t address = 0;
      Function* best = nullptr;
      std::list<Function*>* candidates = nullptr;
      std::string importName;
      bool pure = false;
      bool duplicated = false;
      Container* from = nullptr;
    };

    std::vector<VFunction> functions;
    struct SZ
    {
      size_t count = 0;
    };
    std::unordered_map<size_t, SZ> guessedSizes;
  };

  struct BaseClass
  {
    Container* ptr;
    unsigned long	numContainedBases;
    int	mdisp;	// Offset of intended data within base
    int	pdisp;	// Displacement to virtual base pointer
    int	vdisp;	// Index within vbTable to offset of base
  };

  struct Hierarchy
  {
    std::vector<BaseClass> bases;
    std::set<Container*> successors;

    const BaseClass* get_top_base(int mdisp) const;
    [[nodiscard]] std::vector<BaseClass> get_topmost_bases() const;
  };

  struct FieldsData
  {
    size_t size = 0;
    // TODO: collect information about fields and offsets.
  };

  // class/struct/union/namespace
  struct Container : public Node
  {
    enum Type
    {
      tUnknown,
      tNamespace,
      tOOP,
      tClass,
      tStruct,
      tUnion,
    };

    Type type = tUnknown;

    Hierarchy hierarchy;
    //std::unordered_set<Node*> dependencies; // TODO: should store all things, which required to be included before

    std::map<std::string, Container> instances; // For templates
    size_t templateParamsCount = 0;

    std::map<std::string, Enum> enumerations;
    std::map<std::string, Container> children;

    std::map<std::string, Function> functions;
    std::map<std::string, Function> functionsVirtual;
    std::map<std::string, Function> functionsStatic;
    std::map<std::string, Symbol> variableSymbols;
    std::map<std::string, Symbol> specialTableSymbols;
    std::map<std::string, Symbol> others;

    Function* virtual_destructor = nullptr;
    std::map<size_t, std::unordered_map<Module*, VFTable>> vftables;
    std::unordered_map<Module*, std::map<size_t, std::list<Function*>>> functionsVirtualMap;

    FieldsData data;

    const char* get_type_str() const;
    bool is_class_or_struct() const;
    void force_oop();

    bool process_step_0(Container& root);
    bool process_step_1(Container& root);
    bool process_step_2(Container& root);
    bool process_step_3(Container& root);

    bool output_definition(std::ofstream& out, size_t indent = 0) const;

    bool processed[4] = { false, false, false, false };
  };

  // dll/exe
  struct Module
  {
    std::string name;
    std::string path;
    llvm::object::ObjectFile* object = nullptr;
    std::unique_ptr<llvm::pdb::IPDBSession> session;

    std::map<uintptr_t, std::list<Symbol*>> symbols;

    struct SectionInfo
    {
      uint32_t SectionStart = 0;
      uint32_t SectionEnd = 0;
      bool Text = false;
      bool Data = false;
    };
    std::vector<SectionInfo> sections;

    std::map<uint32_t, std::string> imports;
  };

  struct SymbolsTree
  {
    struct Input
    {
      std::string object;
      std::string pdb;
      // Other configs
    };

    std::filesystem::path PDBFolder;

    std::list<Input> inputs;
    llvm::pdb::PDB_ReaderType reader = llvm::pdb::PDB_ReaderType::DIA; // Unfortunately, currently only DIA works as expected.

    std::list<Module> modules;
    Container root;

    bool add_symbol(Module* mod, const std::string& name, uint32_t rva, bool owner);

    bool parse_object(Module* mod);
    bool parse_COFF_pdb(Module* mod);
    bool parse_COFF_exports(Module* mod);
    bool parse_COFF_imports(Module* mod);
    bool parse_COFF_vftables(Module* mod);

    bool collect();
    bool process();
    bool output_to_folder(const std::filesystem::path& path);
  };
}
