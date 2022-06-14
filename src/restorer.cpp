// restorer.cpp : Defines the entry point for the application.
//

#include "restorer.h"

#include "rtti.h"

#include <llvm/DebugInfo/PDB/PDB.h>
#include <llvm/DebugInfo/PDB/IPDBSession.h>
#include <llvm/DebugInfo/PDB/PDBSymbolCompiland.h>
#include <llvm/DebugInfo/PDB/PDBSymbolExe.h>
#include <llvm/DebugInfo/PDB/PDBSymbolFunc.h>
#include <llvm/DebugInfo/PDB/PDBSymbolPublicSymbol.h>
#include <llvm/DebugInfo/CodeView/GlobalTypeTableBuilder.h>
#include <llvm/DebugInfo/CodeView/MergingTypeTableBuilder.h>
#include <llvm/Object/Archive.h>
#include <llvm/Object/COFFImportFile.h>
#include <llvm/Object/MachOUniversal.h>
#include <llvm/Object/ObjectFile.h>
#include <llvm/Object/WindowsResource.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/WithColor.h>

// TODO: Is it about system or LLVM version?
#ifdef __linux__ 
#include <llvm/MC/TargetRegistry.h>
#elif _WIN32
#include <llvm/Support/TargetRegistry.h>
#else

#endif

#include <llvm/Demangle/MicrosoftDemangle.h>
#include <llvm/Demangle/MicrosoftDemangleNodes.h>

#include <set>

#include <iostream>
#include <fstream>
#include <algorithm>
#include <regex>

using namespace llvm;


// TODO: Commit patch to LLVM?
//#define LLVM_RealEnumType_DEFINED

// TODO: Move to application options
constexpr bool gInsertIncludeBefore = false;
constexpr bool gInsertIncludeInside = false;
constexpr uint32_t gIndentSize = 2;
constexpr char gIndentChar = ' ';
const char* gDefaultNonUnionClassType = "class/*or struct*/";

// Inserts indents in front of code lines
inline auto ind(size_t depth, const char c = gIndentChar) -> std::string
{
  return std::string(gIndentSize * depth, c);
}

// Erases all Occurrences of given substring from main string.
void eraseAllSubStr(std::string& mainStr, const std::string& toErase)
{
  size_t pos;
  // Search for the substring in string in a loop untill nothing is found
  while ((pos = mainStr.find(toErase)) != std::string::npos)
  {
    // If found then erase it from string
    mainStr.erase(pos, toErase.length());
  }
}

// Gets string for PrimitiveKind. For RealEnumType
inline const char* toString(ms_demangle::PrimitiveKind kind)
{
  switch (kind)
  {
  case ms_demangle::PrimitiveKind::Void: return "void";
  case ms_demangle::PrimitiveKind::Bool: return "bool";
  case ms_demangle::PrimitiveKind::Char: return "char";
  case ms_demangle::PrimitiveKind::Schar: return "signed char";
  case ms_demangle::PrimitiveKind::Uchar: return "unsigned char";
  case ms_demangle::PrimitiveKind::Char8: return "char8_t";
  case ms_demangle::PrimitiveKind::Char16: return "char16_t";
  case ms_demangle::PrimitiveKind::Char32: return "char32_t";
  case ms_demangle::PrimitiveKind::Short: return "short";
  case ms_demangle::PrimitiveKind::Ushort: return "unsigned short";
  case ms_demangle::PrimitiveKind::Int: return "int";
  case ms_demangle::PrimitiveKind::Uint: return "unsigned int";
  case ms_demangle::PrimitiveKind::Long: return "long";
  case ms_demangle::PrimitiveKind::Ulong: return "unsigned long";
  case ms_demangle::PrimitiveKind::Int64: return "__int64";
  case ms_demangle::PrimitiveKind::Uint64: return "unsigned __int64";
  case ms_demangle::PrimitiveKind::Wchar: return "wchar_t";
  case ms_demangle::PrimitiveKind::Float: return "float";
  case ms_demangle::PrimitiveKind::Double: return "double";
  case ms_demangle::PrimitiveKind::Ldouble: return "long double";
  case ms_demangle::PrimitiveKind::Nullptr: return "std::nullptr_t";
  default:
    break;
  }
  return "";
}

// Collects useful information from the node depending on the type.
bool process_node(
  ms_demangle::Node* node,
  restorer::Container* root,
  const std::string& name = "",
  restorer::Module* mod = nullptr,
  uint32_t rva = 0,
  bool owner = false);

// Finds or creates a container based on the full qualified name.
restorer::Container* get_container(
  restorer::Container* root,
  const ms_demangle::QualifiedNameNode* nameNode,
  bool full = false);

bool process_node(
  ms_demangle::Node* node,
  restorer::Container* root,
  const std::string& name,
  restorer::Module* mod,
  uint32_t rva,
  bool owner)
{
  if (node == nullptr)
    return false;

  const std::string str = node->toString(ms_demangle::OF_NoCallingConvention); // debug

  switch (node->kind())
  {
  case ms_demangle::NodeKind::Unknown: return false; break;
  case ms_demangle::NodeKind::Md5Symbol: return false; break;
  case ms_demangle::NodeKind::PrimitiveType:
    break;
  case ms_demangle::NodeKind::FunctionSignature:
  {
    auto n = static_cast<llvm::ms_demangle::FunctionSignatureNode*>(node);

    process_node(n->ReturnType, root);
    if (n->Params)
    {
      for (size_t i = 0; i < n->Params->Count; ++i)
      {
        process_node(n->Params->Nodes[i], root);
      }
    }
  }
  break;
  case ms_demangle::NodeKind::Identifier: return false; break;
  case ms_demangle::NodeKind::NamedIdentifier: return false; break;
  case ms_demangle::NodeKind::VcallThunkIdentifier: return false; break;
  case ms_demangle::NodeKind::LocalStaticGuardIdentifier: return false; break;
  case ms_demangle::NodeKind::IntrinsicFunctionIdentifier: return false; break;
  case ms_demangle::NodeKind::ConversionOperatorIdentifier: return false; break;
  case ms_demangle::NodeKind::DynamicStructorIdentifier: return false; break;
  case ms_demangle::NodeKind::StructorIdentifier: return false; break;
  case ms_demangle::NodeKind::LiteralOperatorIdentifier: return false; break;
  case ms_demangle::NodeKind::ThunkSignature: return false; break;
  case ms_demangle::NodeKind::PointerType:
  {
    auto n = static_cast<llvm::ms_demangle::PointerTypeNode*>(node);
    process_node(n->Pointee, root);
  }
  break;
  case ms_demangle::NodeKind::TagType:
  {
    auto n = static_cast<llvm::ms_demangle::TagTypeNode*>(node);
    switch (n->Tag) {
    case ms_demangle::TagKind::Class:
    {
      restorer::Container* container = get_container(root, n->QualifiedName, true);
      container->type = restorer::Container::tClass;
    }
    break;
    case ms_demangle::TagKind::Struct:
    {
      restorer::Container* container = get_container(root, n->QualifiedName, true);
      container->type = restorer::Container::tStruct;
    }
    break;
    case ms_demangle::TagKind::Union:
    {
      restorer::Container* container = get_container(root, n->QualifiedName, true);
      container->type = restorer::Container::tUnion;
    }
    break;
    case ms_demangle::TagKind::Enum:
    {
      restorer::Container* container = get_container(root, n->QualifiedName, false);
      const std::string str = n->QualifiedName->Components->Nodes[n->QualifiedName->Components->Count - 1]->toString();
      auto& enumeration = container->enumerations[str];
      enumeration.name = str;
#ifdef LLVM_RealEnumType_DEFINED
      enumeration.type = toString(n->RealEnumType);
#endif
      enumeration.parent = container;
    }
    break;
    default:;
    }
  }
  break;
  case ms_demangle::NodeKind::ArrayType:
  {
    auto n = static_cast<llvm::ms_demangle::ArrayTypeNode*>(node);
    process_node(n->ElementType, root);
  }
  break;
  case ms_demangle::NodeKind::Custom: return false; break;
  case ms_demangle::NodeKind::IntrinsicType: return false; break;
  case ms_demangle::NodeKind::NodeArray: return false; break;
  case ms_demangle::NodeKind::QualifiedName: return false; break;
  case ms_demangle::NodeKind::TemplateParameterReference:
  {
    auto n = static_cast<llvm::ms_demangle::TemplateParameterReferenceNode*>(node);
    process_node(n->Symbol, root);
  }
  break;
  case ms_demangle::NodeKind::EncodedStringLiteral: return false; break;
  case ms_demangle::NodeKind::IntegerLiteral: return false; break;
  case ms_demangle::NodeKind::RttiBaseClassDescriptor: return false; break;
  case ms_demangle::NodeKind::LocalStaticGuardVariable: return false; break;
  case ms_demangle::NodeKind::FunctionSymbol:
  {
    auto n = static_cast<llvm::ms_demangle::FunctionSymbolNode*>(node);

    restorer::Container* container = get_container(root, n->Name, false);

    restorer::Function* symbol;

    if (n->Signature->FunctionClass & ms_demangle::FC_Virtual)
    {
      symbol = &container->functionsVirtual[name];
      if (container->type == restorer::Container::tUnknown)
      {
        container->type = restorer::Container::tNonUnionClass;
      }
      container->functionsVirtualMap[mod][rva].push_back(symbol);

      if (str.find('~') != std::string::npos)
      {
        container->virtual_destructor = symbol;
      }
    }
    else if (n->Signature->FunctionClass & ms_demangle::FC_Static)
    {
      symbol = &container->functionsStatic[name];
      if (container->type == restorer::Container::tUnknown)
      {
        container->type = restorer::Container::tNonUnionClass;
      }
    }
    else
    {
      symbol = &container->functions[name];
      if (container->type == restorer::Container::tUnknown)
      {
        container->type = restorer::Container::tNonUnionClass;
      }
    }

    symbol->name = str;
    symbol->nameMangled = name;
    symbol->parent = container;
    symbol->occurrences.push_back({ mod , rva , owner });

    if (mod)
    {
      mod->symbols[rva].push_back(symbol);
    }

    process_node(n->Signature->ReturnType, root);
    if (n->Signature->Params)
    {
      for (size_t i = 0; i < n->Signature->Params->Count; ++i)
      {
        process_node(n->Signature->Params->Nodes[i], root);
      }
    }
  }
  break;
  case ms_demangle::NodeKind::VariableSymbol:
  {
    auto n = static_cast<llvm::ms_demangle::VariableSymbolNode*>(node);
    restorer::Container* container = get_container(root, n->Name, false);
    auto& symbol = container->variableSymbols[name];
    symbol.name = str;
    symbol.nameMangled = name;
    symbol.parent = container;
    symbol.occurrences.push_back({ mod , rva , owner });

    if (mod)
    {
      mod->symbols[rva].push_back(&symbol);
    }

    process_node(n->Type, root);
  }
  break;
  case ms_demangle::NodeKind::SpecialTableSymbol:
  {
    auto n = static_cast<llvm::ms_demangle::SpecialTableSymbolNode*>(node);
    restorer::Container* container = get_container(root, n->Name, false);
    auto& symbol = container->specialTableSymbols[name];
    symbol.name = str;
    symbol.nameMangled = name;
    symbol.parent = container;
    symbol.occurrences.push_back({ mod , rva , owner });

    if (mod)
    {
      mod->symbols[rva].push_back(&symbol);
    }
  }
  break;
  default: return false; break;
  }

  return true;
}

restorer::Container* get_container(restorer::Container* root, const ms_demangle::QualifiedNameNode* nameNode, bool full)
{
  restorer::Container* container = root;
  auto end = nameNode->Components->Count;
  if (!full && end != 0)
  {
    --end;
  }
  for (size_t i = 0; i < end; ++i)
  {
    const auto curr = static_cast<ms_demangle::NamedIdentifierNode*>(nameNode->Components->Nodes[i]);
    std::string str = curr->toString();
    if (curr->TemplateParams)
    {
      // Process types in name
      for (size_t j = 0; j < curr->TemplateParams->Count; ++j)
      {
        process_node(curr->TemplateParams->Nodes[j], root);
      }

      // Split template class name
      const auto pos = str.find('<');
      if (pos == std::string::npos)
      {
        continue; // error
      }
      const auto name = str.substr(0, pos);
      const auto args = str.substr(pos);

      auto& deeper = container->children[name];
      deeper.parent = container;
      deeper.name = name;
      if (deeper.type == restorer::Container::tUnknown)
      {
        deeper.type = restorer::Container::tNonUnionClass;
      }

      auto& instance = deeper.instances[args];
      instance.parent = &deeper;
      instance.name = str;
      container = &instance;

      deeper.templateParamsCount = curr->TemplateParams->Count;
    }
    else
    {
      auto& deeper = container->children[str];
      deeper.parent = container;
      deeper.name = str;
      container = &deeper;
    }
  }
  return container;
}

// Finds or creates a container for class based on the full qualified name.
restorer::Container* get_class(restorer::Container* root, StringView name)
{
  llvm::ms_demangle::Demangler dem;
  const auto res = dem.parse(name);

  if (dem.Error || res->kind() != ms_demangle::NodeKind::VariableSymbol)
    return nullptr;

  const auto t = static_cast<llvm::ms_demangle::VariableSymbolNode*>(res);
  const auto n = static_cast<llvm::ms_demangle::TagTypeNode*>(t->Type);

  return get_container(root, n->QualifiedName, true);
}

bool restorer::Enum::output_definition(std::ofstream& out, size_t indent) const
{
  out << ind(indent) << "enum " << name << " : " << type << " {" << std::endl;
  const std::string inc = "\"" + get_name("__") + ".inl" + "\"";

  if (gInsertIncludeInside)
  {
    out << "#if __has_include(" << inc << ")" << std::endl;
    out << "#include " << inc << std::endl;
    out << "#endif" << std::endl;
  }

  out << ind(indent) << "};";
  return true;
}

bool restorer::Symbol::output_definition(std::ofstream& out, size_t indent) const
{
  const auto qualifications = parent->get_name() + "::";
  std::string output = name;
  eraseAllSubStr(output, qualifications);

  // TODO: Add stl/atl/mfc regexp here?

  out << ind(indent) << output;
  return true;
}

const restorer::BaseClass* restorer::Hierarchy::get_top_base(int mdisp) const
{
  for (auto& base : bases)
  {
    if (base.mdisp == mdisp)
    {
      return &base;
    }
  }
  return nullptr;
}

std::vector<restorer::BaseClass> restorer::Hierarchy::get_topmost_bases() const
{
  std::vector<restorer::BaseClass> result;

  for (size_t i = 0; i < bases.size(); ++i)
  {
    result.push_back(bases[i]);
    i += bases[i].numContainedBases;
  }

  return result;
}

std::string restorer::Node::get_name(const char* delim) const
{
  std::string res(name);
  for (const auto* c = parent; c && !c->name.empty(); c = c->parent)
  {
    res.insert(0, parent->name + delim);
  }
  return res;
}

const char* restorer::Container::get_type_str() const
{
  switch (type)
  {
  case tUnknown: return "namespace/*or class/struct?*/";
  case tNamespace: return "namespace";
  case tNonUnionClass: return gDefaultNonUnionClassType;
  case tClass: return "class";
  case tStruct: return "struct";
  case tUnion: return "union";
  default: break;
  }
  return "";
}

bool restorer::Container::is_non_union_class() const
{
  return type == tClass || type == tStruct || type == tNonUnionClass;
}

void restorer::Container::force_non_union_class()
{
  if (is_non_union_class())
    return;

  type = tNonUnionClass;

  for (auto& child : children)
  {
    child.second.force_non_union_class();
  }
}

bool restorer::Container::process_step_0(Container& root)
{
  // Lock for recursion
  if (processed[0])
    return true;
  processed[0] = true; // TODO: Temp bugfix?

  // Bases
  for (auto& base : hierarchy.bases)
  {
    base.ptr->process_step_0(root);
  }

  // Template instances
  for (auto& instance : instances)
  {
    instance.second.process_step_0(root);
    if (instance.second.type != tUnknown && (type == tUnknown || type == tNonUnionClass))
    {
      type = instance.second.type;
    }
  }

  // Children
  for (auto& child : children)
  {
    child.second.process_step_0(root);
  }

  // TODO: Yes, looks like performance beast
  // Virtual functions tables
  for (auto& offset : vftables)
  {
    for (auto& vftable : offset.second)
    {
      for (size_t i = 0; i < vftable.second.functions.size(); ++i)
      {
        auto& func = vftable.second.functions[i];
        if (!func.importName.empty())
        {
          llvm::ms_demangle::Demangler dem;
          StringView view(func.importName.c_str(), func.importName.size());
          const auto res = dem.parse(view);
          if (!dem.Error)
          {
            auto n = static_cast<llvm::ms_demangle::FunctionSymbolNode*>(res);
            auto symowner = get_container(&root, n->Name, false);

            // Find
            for (const auto& b : hierarchy.bases)
            {
              if (b.ptr == symowner)
              {
                auto shift = offset.first - b.mdisp;
                auto& v = symowner->vftables[shift][vftable.first];
                if (i >= v.functions.size())
                {
                  v.functions.resize(i + 1);
                }
                v.functions[i].address = func.address;
                auto& mp = symowner->functionsVirtualMap[vftable.first];
                auto abc = mp.find(func.address);
                if (abc != mp.end() && abc->second.size() == 1)
                {
                  v.functions[i].best = abc->second.front();
                }
                break;
              }
            }
          }
        }

        auto& m = functionsVirtualMap[vftable.first];
        auto symbols = m.find(func.address);
        if (symbols != m.end())
        {
          if (symbols->second.size() == 1)
          {
            func.best = symbols->second.front();
          }
          else
          {
            func.candidates = &symbols->second;
          }
        }
      }
    }
  }

  processed[0] = true;

  return true;
}

bool restorer::Container::process_step_1(Container& root)
{
  // Lock for recursion
  if (processed[1])
    return true;
  processed[1] = true; // TODO: Temp bugfix?

  // Template instances
  for (auto& instance : instances)
    instance.second.process_step_1(root);

  // Children
  for (auto& child : children)
    child.second.process_step_1(root);

  if (vftables.empty())
  {
    return true;
  }

  // Virtual functions tables
  for (auto& offset : vftables)
  {
    auto& vftable0 = offset.second[nullptr];

    size_t biggest = 0;
    for (const auto& vftable : offset.second)
    {
      biggest = std::max(biggest, vftable.second.functions.size());
    }
    if (biggest == 0)
    {
      continue;
    }
    vftable0.functions.resize(biggest);
    if (offset.first != 0)
    { // No vfunctions will be added for vftables with non-zero offset
      // so we can use this size for base class
      if (auto* base = hierarchy.get_top_base(offset.first))
      {
        auto& basevf = base->ptr->vftables[0][nullptr];
        if (basevf.functions.size() < biggest)
        {
          ++basevf.guessedSizes[biggest].count;
        }
      }
    }
  }

  processed[1] = true;

  return true;
}

bool restorer::Container::process_step_2(Container& root)
{
  // Lock for recursion
  if (processed[2])
    return true;
  processed[2] = true; // TODO: Temp bugfix?

  // Template instances
  for (auto& instance : instances)
    instance.second.process_step_2(root);

  // Children
  for (auto& child : children)
    child.second.process_step_2(root);

  if (vftables.empty())
  {
    return true;
  }

  // Virtual function tables
  for (auto& offset : vftables)
  {
    auto& vftable0 = offset.second[nullptr];

    size_t max = 0;
    size_t result = 0;
    for (auto sz : vftable0.guessedSizes)
    {
      if (max < sz.second.count)
      {
        max = sz.second.count;
        result = sz.first;
      }
    }

    if (vftable0.functions.size() < result)
    {
      vftable0.functions.resize(result);
    }
  }

  processed[2] = true;

  return true;
}

bool restorer::Container::process_step_3(Container& root)
{
  // Lock for recursion
  if (processed[3])
    return true;
  processed[3] = true; // TODO: Temp bugfix?

  // Bases
  for (auto& base : hierarchy.bases)
    base.ptr->process_step_3(root);

  // Template instances
  for (auto& instance : instances)
    instance.second.process_step_3(root);

  // Children
  for (auto& child : children)
    child.second.process_step_3(root);

  if (vftables.empty())
  {
    return true;
  }

  // Virtual functions tables
  for (auto& offset : vftables)
  {
    auto& vftable0 = offset.second[nullptr];

    for (const auto& vf : offset.second)
    {
      if (vf.first == nullptr)
      {
        continue;
      }

      for (size_t i = 0; i < vf.second.functions.size(); ++i)
      {
        const auto& curr = vf.second.functions[i];
        auto& curr0 = vftable0.functions[i];
        if (curr.best && (!curr0.best || !curr.duplicated))
        {
          curr0.best = curr.best;
          curr0.duplicated = curr.duplicated;

          auto* base = hierarchy.get_top_base(offset.first);
          VFTable* pVFTable = nullptr;
          for (/**/; base != nullptr; base = base->ptr->hierarchy.get_top_base(0))
          {
            auto& basevf = base->ptr->vftables[0][nullptr];
            if (basevf.functions.size() > i)
            {
              pVFTable = &basevf;
            }
          }
          if (pVFTable && pVFTable->functions.size() > i)
          {
            if ((!curr.duplicated && !pVFTable->functions[i].best) || !curr.importName.empty())
            {
              pVFTable->functions[i].best = curr.best;
              pVFTable->functions[i].from = this;
              pVFTable->functions[i].duplicated = false;
            }
          }
        }
        if (curr.candidates && (!curr0.candidates || curr0.candidates->size() > curr.candidates->size()))
        {
          curr0.candidates = curr.candidates;
        }
        if (!curr.importName.empty())
        {
          curr0.importName = curr.importName;
          curr0.pure = curr0.importName == "_purecall";
        }
      }
    }
  }

  processed[3] = true;

  return true;
}

bool restorer::Container::output_definition(std::ofstream& out, size_t indent) const
{
  bool bInstance = false;
  if (!instances.empty() && templateParamsCount > 0)
  {
    out << ind(indent) << "template<";
    size_t i = 0;
    for (; i < templateParamsCount - 1; ++i)
    {
      out << "typename T" << i << ", ";
    }
    out << "typename T" << i + 1;
    out << ind(indent) << ">" << std::endl;
  }
  else if (name.find('<') != std::string::npos)
  {
    bInstance = true;
    out << "#if 0" << std::endl;
    out << ind(indent) << "template<>" << std::endl;
  }

  out << ind(indent) << get_type_str() << " " << name;
  auto bases = hierarchy.get_topmost_bases();
  for (auto it = bases.begin(); it != bases.end(); ++it)
  {
    out << (it == bases.begin() ? " :" : ",") << " public " << (*it).ptr->name;
  }
  out << std::endl;

  // TODO: Output successors?

  out << ind(indent++) << "{" << std::endl; // Open container
  {
    if (is_non_union_class() && (!enumerations.empty() || !children.empty()))
    { // Force publicity for next things
      out << ind(indent - 1) << "public:" << std::endl;
    }

    if (!enumerations.empty())
    {
      out << ind(indent) << "// Enumerations:" << std::endl;
      for (const auto& enumeration : enumerations)
      {
        enumeration.second.output_definition(out, indent);
        out << std::endl;
      }
      out << std::endl;
    }

    if (!children.empty())
    {
      out << ind(indent) << "// Nested containers:" << std::endl;
      for (const auto& child : children)
      {
        child.second.output_definition(out, indent);
        out << std::endl;
      }
      out << std::endl;
    }

    if (!functions.empty())
    {
      out << "#if 0" << std::endl;
      out << ind(indent) << "// Functions:" << std::endl;
      for (const auto& function : functions)
      {
        function.second.output_definition(out, indent);
        out << "; // ";
        bool first = true;
        for (const auto& occurrence : function.second.occurrences)
        {
          if (!occurrence.modulePtr || !occurrence.owner)
            continue;

          out << (first ? "" : ",") << occurrence.modulePtr->name << ":" << (void*)occurrence.address;

          if (first)
            first = false;
        }
        out << std::endl;
      }
      out << "#endif" << std::endl << std::endl;
    }

    if (!functionsStatic.empty())
    {
      out << "#if 0" << std::endl;
      out << ind(indent) << "// Static functions:" << std::endl;
      for (const auto& function : functionsStatic)
      {
        function.second.output_definition(out, indent);
        out << "; // ";
        bool first = true;
        for (const auto& occurrence : function.second.occurrences)
        {
          if (!occurrence.modulePtr || !occurrence.owner)
            continue;

          out << (first ? "" : ",") << occurrence.modulePtr->name << ":" << (void*)occurrence.address;

          if (first)
            first = false;
        }
        out << std::endl;
      }
      out << "#endif" << std::endl << std::endl;
    }

    if (!variableSymbols.empty())
    {
      out << "#if 0" << std::endl;
      out << ind(indent) << "// Variables:" << std::endl;
      for (const auto& variable : variableSymbols)
      {
        variable.second.output_definition(out, indent);
        out << "; // ";
        bool first = true;
        for (const auto& occurrence : variable.second.occurrences)
        {
          if (!occurrence.modulePtr || !occurrence.owner)
            continue;

          out << (first ? "" : ",") << occurrence.modulePtr->name << ":" << (void*)occurrence.address;

          if (first)
            first = false;
        }
        out << std::endl;
      }
      out << "#endif" << std::endl << std::endl;
    }

    if (!functionsVirtual.empty())
    {
      out << "#if 0 // All found virtual functions names:" << std::endl;
      for (const auto& function : functionsVirtual)
      {
        function.second.output_definition(out, indent);
        out << "; // ";
        bool first = true;
        for (const auto& occurrence : function.second.occurrences)
        {
          if (!occurrence.modulePtr || !occurrence.owner)
            continue;

          out << (first ? "" : ",") << occurrence.modulePtr->name << ":" << (void*)occurrence.address;

          if (first)
            first = false;
        }
        out << std::endl;
      }
      out << "#endif" << std::endl;
      out << std::endl;
    }

    if (!vftables.empty())
    {
      // TODO: Move it from output_definition to process step.
      for (const auto& vftable : vftables)
      {
        // Get base vftable size
        size_t baseVFTableLen = 0;
        const auto base = hierarchy.get_top_base(vftable.first);
        if (base)
        {
          if (auto v = base->ptr->vftables.find(0); v != base->ptr->vftables.end())
          {
            if (auto v1 = v->second.find(nullptr); v1 != v->second.end())
            {
              baseVFTableLen = v1->second.functions.size();
            }
          }
        }

        // Get vftable size
        auto it = vftable.second.find(nullptr);
        if (it == vftable.second.end())
        {
          std::cerr << "Warning: vftable for" << get_name() << " skipped due to missed accumulative vftable";
          continue;
        }
        size_t VFTableLen = it->second.functions.size();

        // Check size validity
        auto end = std::min(baseVFTableLen, VFTableLen);
        if (baseVFTableLen > VFTableLen)
        {
          out << ind(indent)
            << "// Warning: Greater base vftable size!"
            << std::endl;

          std::cout << "Warning: " << get_name() << " offset " << vftable.first << " has short vftable." << std::endl;
        }

        out << ind(indent)
          << "// VFTable. Offset=" << vftable.first
          << ", Size=" << VFTableLen
          << ", Start=" << baseVFTableLen
          << std::endl;

        if (base)
        {
          out << ind(indent) << "// Corresponding base: " + base->ptr->get_name() << std::endl;
        }

        for (const auto& vft : vftable.second)
        {
          if (vft.first == nullptr || !vft.second.place.modulePtr)
            continue;

          out << ind(indent) << "// ";
          if (vft.second.place.owner)
          {
            out << "Owner: ";
          }
          out << vft.second.place.modulePtr->name << std::endl;
          //out << vft.second.place.modulePtr->name << ":" << (void*)vft.second.place.address << std::endl;
        }

        size_t i = 0;
        bool overrides = false;
        for (/**/; i < end; ++i)
        {
          if ((it->second.functions[i].best && !it->second.functions[i].duplicated)
            && it->second.functions[i].best->nameMangled.find("??_E") == std::string::npos
            && it->second.functions[i].best->nameMangled.find("??_G") == std::string::npos)
          {
            if (!overrides)
            {
              overrides = true;
              out << ind(indent) << "// Overrides:" << std::endl;
            }
            it->second.functions[i].best->output_definition(out, indent);
            out << " override" << ";" << " // " << i << std::endl;
          }
          else if (i == 0 && virtual_destructor != nullptr
            && it->second.functions[i].best == nullptr
            && it->second.functions[i].candidates == nullptr)
          {
            out << ind(indent) << "// ";
            virtual_destructor->output_definition(out);
            out << ";" << " // " << i << std::endl;
          }
          else
          {

          }
        }
        if (VFTableLen > end)
        {
          out << ind(indent) << "// Added virtual functions:" << std::endl;
          if (vftable.first != 0)
          {
            out << "#if 0 // Warning: There are virtual functions probably from base class." << std::endl;
          }
        }

        for (/**/; i < VFTableLen; ++i)
        {
          auto candidates = it->second.functions[i].candidates;
          if (it->second.functions[i].best
            && (it->second.functions[i].best->nameMangled.find("??_E") == 0
              || it->second.functions[i].best->nameMangled.find("??_G") == 0))
          { // vector/scalar Destructor
            if (virtual_destructor != nullptr)
            {
              virtual_destructor->output_definition(out, indent);
              out << "; " << "// vector/scalar Destructor" << std::endl;
            }
            else
            {
              out << ind(indent) << "// vector/scalar Destructor" << std::endl;
            }
            continue;
          }
          else if (it->second.functions[i].candidates && !it->second.functions[i].candidates->empty()
            && (it->second.functions[i].candidates->front()->nameMangled.find("??_E") == 0
              || it->second.functions[i].candidates->front()->nameMangled.find("??_G") == 0))
          {
            if (virtual_destructor != nullptr)
            {
              virtual_destructor->output_definition(out, indent);
              out << "; " << "// vector/scalar Destructor" << std::endl;
            }
            else
            {
              out << ind(indent) << "// vector/scalar Destructor" << std::endl;
            }
            candidates = nullptr;

            continue;
          }
          else if (it->second.functions[i].best && !it->second.functions[i].duplicated)
          {
            it->second.functions[i].best->output_definition(out, indent);
            candidates = nullptr;
          }
          else if (i == 0 && candidates == nullptr && virtual_destructor != nullptr)
          {
            virtual_destructor->output_definition(out, indent);
          }
          else
          {
            out << ind(indent) << "public: virtual void unknown_vf_" << vftable.first << "_" << i << "(void)";
          }

          if (it->second.functions[i].pure)
          {
            out << " = 0";
          }

          out << "; // " << i;

          if (it->second.functions[i].from)
          {
            out << " (" << it->second.functions[i].from->get_name() << ")";
          }

          if (candidates)
          {
            out << std::endl;
            out << ind(indent) << "// Candidates:";
            for (const auto& candidate : *candidates)
            {
              out << std::endl << ind(indent) << "// ";
              candidate->output_definition(out, 0);
              out << "; // " << i;
            }
          }

          out << std::endl;
        }

        if (vftable.first != 0 && VFTableLen > end)
        {
          out << "#endif" << std::endl;
        }

        out << ind(indent) << "// VFTable with " << vftable.first << " offset end" << std::endl;
        out << std::endl;
      }
    }



    if (is_non_union_class())
    { // Force publicity for next things
      out << ind(indent - 1) << "public:" << std::endl;
    }
    if (gInsertIncludeInside)
    {
      std::string inc = "\"" + get_name("__") + ".inl" + "\"";
      out << "#if __has_include(" << inc << ")" << std::endl;
      out << "// Optional file with data fields and other stuff" << std::endl;
      out << "#include " << inc << "" << std::endl;
      out << "#endif" << std::endl;
    }
  }
  out << ind(--indent) << "};" << std::endl; // Close container

  if (!instances.empty())
  {
    // TODO: Investigate how to do it better
    out << "#if 0 // Instances:" << std::endl;
    for (const auto& instance : instances)
    {
      out << "// " << instance.first << std::endl;
      instance.second.output_definition(out, indent);
    }
    out << "#endif" << std::endl;
    out << std::endl;
  }

  if (bInstance)
  {
    out << "#endif" << std::endl;
  }

  return true;
}

bool restorer::SymbolsTree::add_symbol(Module* mod, const std::string& name, uint32_t rva, bool owner)
{
  if (name[0] != '?') // C name?
  {
    // TODO: Investigate
    restorer::Container* container = &root;
    auto& symbol = container->others[name];
    symbol.name = name;
    symbol.nameMangled = name;
    symbol.parent = container;
    symbol.occurrences.push_back({ mod , rva , owner });

    if (mod)
    {
      mod->symbols[rva].push_back(&symbol);
    }
    return true;
  }

  llvm::ms_demangle::Demangler dem;
  StringView view(name.data(), name.size());
  auto res = dem.parse(view);

  if (dem.Error)
  {
    return false;
  }

  return process_node(res, &root, name, mod, rva, owner);
}

bool restorer::SymbolsTree::parse_object(Module* mod)
{
  if (mod->object->isCOFF())
  {
    // Get info from PDB
    const auto coff = dyn_cast<llvm::object::COFFObjectFile>(mod->object);
    StringRef PdbPath;
    const llvm::codeview::DebugInfo* PdbInfo = nullptr;
    Error E = coff->getDebugPDBInfo(PdbInfo, PdbPath);

    std::filesystem::path PDBOriginal(std::string(PdbPath.data(), PdbPath.size()));
    std::error_code _Ec;
    if (std::filesystem::is_regular_file(PDBOriginal, _Ec))
    {
      Error E = llvm::pdb::loadDataForPDB(reader, PdbPath, mod->session);
    }
    else if (PdbInfo)
    {
      struct MSGuid {
        support::ulittle32_t Data1;
        support::ulittle16_t Data2;
        support::ulittle16_t Data3;
        int8_t Data4[8];
      };
      const MSGuid& guid = *reinterpret_cast<const MSGuid*>(PdbInfo->PDB70.Signature);
      char buff[200];
      sprintf(buff, "%08lX%04hX%04hX%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX%x",
        (long unsigned int)guid.Data1, (uint16_t)guid.Data2, (uint16_t)guid.Data3,
        guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
        guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7],
        (uint32_t)PdbInfo->PDB70.Age);

      std::filesystem::path PDBOriginalFN = PDBOriginal.filename();
      auto PDBFromServer = PDBFolder / PDBOriginalFN / buff / PDBOriginalFN;
      if (std::filesystem::is_regular_file(PDBFromServer))
      {
        std::string s = PDBFromServer.string();
        StringRef rerPDBFromServer(s.c_str(), s.size());
        Error E = llvm::pdb::loadDataForPDB(reader, rerPDBFromServer, mod->session);
        std::cout << "Pdb: " << PDBFromServer << std::endl;
      }
    }

    if (mod->session)
    {
      parse_COFF_pdb(mod);
    }
    else
    { // Parse as plain exe/dll
      parse_COFF_exports(mod);
    }

    parse_COFF_imports(mod);
    parse_COFF_vftables(mod);
  }

  return true;
}

bool restorer::SymbolsTree::parse_COFF_pdb(Module* mod)
{
  //const auto coff = dyn_cast<llvm::object::COFFObjectFile>(mod->object);
  const auto& pdb = mod->session;

  auto GlobalScope(pdb->getGlobalScope());
  if (!GlobalScope)
    return false;

  if (auto Symbols = GlobalScope->findAllChildren())
  {
    while (auto Symbol = Symbols->getNext())
    {
      switch (Symbol->getSymTag())
      {
        //case pdb::PDB_SymType::None: break;
        //case pdb::PDB_SymType::Exe: break;
      case pdb::PDB_SymType::Compiland:
      {
        std::unique_ptr<pdb::PDBSymbolCompiland> C =
          llvm::unique_dyn_cast<pdb::PDBSymbolCompiland>(std::move(Symbol));
        // TODO:
      }
      break;
      //case pdb::PDB_SymType::CompilandDetails: break;
      //case pdb::PDB_SymType::CompilandEnv: break;
      case pdb::PDB_SymType::Function:
      {
        std::unique_ptr<pdb::PDBSymbolFunc> F =
          llvm::unique_dyn_cast<pdb::PDBSymbolFunc>(std::move(Symbol));
        //add_symbol(mod, F->getName(), F->getRelativeVirtualAddress(), true);
      }
      break;
      //case pdb::PDB_SymType::Block: break;
      //case pdb::PDB_SymType::Data: break;
      //case pdb::PDB_SymType::Annotation: break;
      //case pdb::PDB_SymType::Label: break;
      case pdb::PDB_SymType::PublicSymbol:
      {
        std::unique_ptr<pdb::PDBSymbolPublicSymbol> PS =
          llvm::unique_dyn_cast<pdb::PDBSymbolPublicSymbol>(std::move(Symbol));
        add_symbol(mod, PS->getName(), PS->getRelativeVirtualAddress(), true);
      }
      break;
      //case pdb::PDB_SymType::UDT: break;
      //case pdb::PDB_SymType::Enum: break;
      //case pdb::PDB_SymType::FunctionSig: break;
      //case pdb::PDB_SymType::PointerType: break;
      //case pdb::PDB_SymType::ArrayType: break;
      //case pdb::PDB_SymType::BuiltinType: break;
      //case pdb::PDB_SymType::Typedef: break;
      //case pdb::PDB_SymType::BaseClass: break;
      //case pdb::PDB_SymType::Friend: break;
      //case pdb::PDB_SymType::FunctionArg: break;
      //case pdb::PDB_SymType::FuncDebugStart: break;
      //case pdb::PDB_SymType::FuncDebugEnd: break;
      //case pdb::PDB_SymType::UsingNamespace: break;
      //case pdb::PDB_SymType::VTableShape: break;
      //case pdb::PDB_SymType::VTable: break;
      //case pdb::PDB_SymType::Custom: break;
      //case pdb::PDB_SymType::Thunk: break;
      //case pdb::PDB_SymType::CustomType: break;
      //case pdb::PDB_SymType::ManagedType: break;
      //case pdb::PDB_SymType::Dimension: break;
      //case pdb::PDB_SymType::CallSite: break;
      //case pdb::PDB_SymType::InlineSite: break;
      //case pdb::PDB_SymType::BaseInterface: break;
      //case pdb::PDB_SymType::VectorType: break;
      //case pdb::PDB_SymType::MatrixType: break;
      //case pdb::PDB_SymType::HLSLType: break;
      //case pdb::PDB_SymType::Caller: break;
      //case pdb::PDB_SymType::Callee: break;
      //case pdb::PDB_SymType::Export: break;
      //case pdb::PDB_SymType::HeapAllocationSite: break;
      //case pdb::PDB_SymType::CoffGroup: break;
      //case pdb::PDB_SymType::Inlinee: break;
      //case pdb::PDB_SymType::Max: break;
      default:
      {
      }
      }
    }
  }
  return true;
}

bool restorer::SymbolsTree::parse_COFF_exports(Module* mod)
{
  const auto coff = dyn_cast<llvm::object::COFFObjectFile>(mod->object);

  for (const auto& E : coff->export_directories())
  {
    StringRef Name;
    uint32_t Ordinal, RVA;
    if (auto EC = E.getSymbolName(Name))
      continue;
    if (auto EC = E.getOrdinal(Ordinal))
      continue;
    if (auto EC = E.getExportRVA(RVA))
      continue;

    std::string mangled(Name.begin(), Name.end());
    add_symbol(mod, mangled, RVA, true);
  }

  return false;
}

void parse_imported_symbols(
  restorer::SymbolsTree& tree,
  restorer::Module* mod,
  iterator_range<object::imported_symbol_iterator> Range,
  uint32_t beginRVA)
{
  const auto coff = dyn_cast<llvm::object::COFFObjectFile>(mod->object);
  const auto BytesInAddress = coff->getBytesInAddress();

  size_t Index = 0;

  for (const auto& I : Range)
  {
    StringRef Name;
    uint16_t Ordinal;
    if (Error E = I.getSymbolName(Name))
      continue;
    if (Error E = I.getOrdinal(Ordinal))
      continue;

    auto RVA = beginRVA + Index * BytesInAddress;

    std::string mangled(Name.begin(), Name.end());
    tree.add_symbol(mod, mangled, RVA, false);
    mod->imports[RVA] = mangled;

    ++Index;
  }
}

void parse_delay_imported_symbols(
  restorer::SymbolsTree& tree,
  restorer::Module* mod,
  const llvm::object::DelayImportDirectoryEntryRef& I,
  iterator_range<object::imported_symbol_iterator> Range)
{
  const auto coff = dyn_cast<llvm::object::COFFObjectFile>(mod->object);
  const auto base = coff->getImageBase();

  int Index = 0;
  for (const auto& S : Range)
  {
    StringRef Name;
    uint16_t Ordinal;
    uint64_t Addr;
    if (Error E = S.getSymbolName(Name))
      continue;
    if (Error E = S.getOrdinal(Ordinal))
      continue;
    if (Error E = I.getImportAddress(Index++, Addr))
      continue;

    const auto RVA = static_cast<uint32_t>(Addr - base);

    std::string mangled(Name.begin(), Name.end());
    tree.add_symbol(mod, mangled, RVA, false);
    mod->imports[RVA] = mangled;
  }
}

bool restorer::SymbolsTree::parse_COFF_imports(restorer::Module* mod)
{

  const auto coff = dyn_cast<llvm::object::COFFObjectFile>(mod->object);

  // Regular imports
  for (const llvm::object::ImportDirectoryEntryRef& I : coff->import_directories())
  {
    StringRef Name;
    uint32_t ILTAddr;
    uint32_t IATAddr;
    if (auto EC = I.getName(Name))
      continue;
    if (auto EC = I.getImportLookupTableRVA(ILTAddr))
      continue;
    if (auto EC = I.getImportAddressTableRVA(IATAddr))
      continue;

    // The import lookup table can be missing with certain older linkers, so
    // fall back to the import address table in that case.
    if (ILTAddr)
      parse_imported_symbols(*this, mod, I.lookup_table_symbols(), IATAddr);
    else
      parse_imported_symbols(*this, mod, I.imported_symbols(), IATAddr);
  }

  // Delay imports
  for (const llvm::object::DelayImportDirectoryEntryRef& I : coff->delay_import_directories())
  {
    StringRef Name;
    const llvm::object::delay_import_directory_table_entry* Table;
    if (auto EC = I.getName(Name))
      continue;
    if (auto EC = I.getDelayImportTable(Table))
      continue;

    parse_delay_imported_symbols(*this, mod, I, I.imported_symbols());
  }

  return true;
}

bool restorer::SymbolsTree::parse_COFF_vftables(Module* mod)
{
  const uint8_t BytesInAddress = mod->object->getBytesInAddress();
  if (BytesInAddress != 8)
  {
    //TODO: Support 32bit
    return false;
  }

  const auto coff = dyn_cast<llvm::object::COFFObjectFile>(mod->object);
  const auto data = coff->getData();

  // Collect info about sections
  for (const object::SectionRef& S : coff->sections()) {
    const object::coff_section* Section = coff->getCOFFSection(S);
    const uint32_t SectionStart = Section->VirtualAddress;
    const uint32_t SectionEnd = Section->VirtualAddress + Section->VirtualSize;

    mod->sections.push_back({ SectionStart, SectionEnd, S.isText(), S.isData() });
  }

  /*
   * This implementation is naive and can be fooled by some data portion with valid-like rtti objects.
   * TODO: Implement robust detection.
   */

  // Find all CompleteObjectLocator structures
  std::set<const ms_rtti::_64::CompleteObjectLocator*> locators;
  auto i = data.bytes_begin();
  auto e = data.bytes_end() - sizeof(ms_rtti::_64::CompleteObjectLocator);
  for (/**/; i < e; i += BytesInAddress)
  {
    auto* COL = reinterpret_cast<const ms_rtti::_64::CompleteObjectLocator*>(i);

    // Validity check
    // TODO: Proper validity check
    uintptr_t p;
    if (coff->getRvaPtr(COL->pSelf, p) || (uintptr_t)COL != p)
      continue;
    if (coff->getRvaPtr(COL->pTypeDescriptor, p))
      continue;
    const auto& TD = *(ms_rtti::_64::TypeDescriptor*)(p);
    if (TD.name[0] != '.'
      || TD.name[1] != '?'
      || TD.name[2] != 'A'
      || TD.name[3] != 'V')
      continue;

    // Get container node from tree
    auto container = get_class(&root, (const char*)TD.name);
    if (!container)
      continue;

    // Now we know type: struct or class
    container->force_non_union_class();

    // Insert to collection
    locators.insert(COL);

    // Get base classes
    auto bases = COL->getBaseClasses();
    container->hierarchy.bases.resize(bases.size());
    for (size_t j = 0; j < bases.size(); ++j)
    {
      if (bases[j]->where.vdisp)
      {
        std::cerr << "Warning: Unsupported non-zero base PMD::vdisp field for "
          << container->get_name() << std::endl;
        container->hierarchy.bases.clear();
        break;
      }

      uintptr_t p;
      if (coff->getRvaPtr(bases[j]->pTypeDescriptor, p))
      {
        container->hierarchy.bases.clear();
        break;
      }
      const auto& baseTD = *(ms_rtti::_64::TypeDescriptor*)(p);
      auto base = get_class(&root, (const char*)baseTD.name);
      if (!base)
      {
        container->hierarchy.bases.clear();
        break;
      }
      container->hierarchy.bases[j] = {
        base,
        bases[j]->numContainedBases,
        bases[j]->where.mdisp,
        bases[j]->where.pdisp,
        bases[j]->where.vdisp,
      };
      base->hierarchy.successors.insert(container);
      base->force_non_union_class();

      base->hierarchy.bases.resize(bases[j]->numContainedBases);
      for (size_t k = 0; k < bases[j]->numContainedBases; ++k)
      {
        if (bases[j + 1 + k]->where.vdisp)
        {
          std::cerr << "Warning: Unsupported non-zero base PMD::vdisp field for "
            << base->get_name() << std::endl;
          base->hierarchy.bases.clear();
          break;
        }

        uintptr_t p;
        if (coff->getRvaPtr(bases[j + 1 + k]->pTypeDescriptor, p))
        {
          base->hierarchy.bases.clear();
          break;
        }
        const auto& baseTD = *(ms_rtti::_64::TypeDescriptor*)(p);
        auto base1 = get_class(&root, (const char*)baseTD.name);
        if (!base1)
        {
          base->hierarchy.bases.clear();
          break;
        }
        base->hierarchy.bases[k] = {
          base1,
          bases[j + 1 + k]->numContainedBases,
          bases[j + 1 + k]->where.mdisp,
          bases[j + 1 + k]->where.pdisp,
          bases[j + 1 + k]->where.vdisp,
        };
        base1->hierarchy.successors.insert(base);
      }
    }
  }

  // Work with vftables
  i = data.bytes_begin() + BytesInAddress;
  e = data.bytes_end();
  for (; i < e; i += BytesInAddress)
  {
    size_t COLOffset = 0;
    memcpy(&COLOffset, i - BytesInAddress, BytesInAddress);
    COLOffset -= coff->getImageBase();
    uintptr_t p;
    if (coff->getRvaPtr(COLOffset, p))
      continue;

    auto COL = (const ms_rtti::_64::CompleteObjectLocator*)(p);
    if (locators.find(COL) == locators.end())
      continue;
    if (coff->getRvaPtr(COL->pTypeDescriptor, p))
      continue;

    const auto& TD = *(ms_rtti::_64::TypeDescriptor*)(p);

    auto container = get_class(&root, (const char*)TD.name);
    if (!container)
      continue;

    // Calculate vftable length
    auto vfend = i;
    for (bool valid = true; valid; vfend += BytesInAddress)
    {
      size_t offset = 0;
      memcpy(&offset, vfend, BytesInAddress);
      const auto rva = offset - coff->getImageBase();
      valid = false;
      for (const auto& section : mod->sections)
      {
        if (section.Text && rva >= section.SectionStart && rva < section.SectionEnd)
        {
          valid = true;
          break;
        }
      }
    }

    const auto vfcount = (vfend - i) / BytesInAddress - 1;
    //std::cout << (const char*)TD.name << " " << vfcount << std::endl;

    if (vfcount == 0) // Invalid vftable found?
      continue;

    auto& vftable = container->vftables[COL->offset][mod];
    vftable.place.modulePtr = mod;
    vftable.place.address = (uintptr_t)i;
    vftable.place.owner = true;

    vftable.functions.resize(vfcount);

    std::unordered_map<uint64_t, VFTable::VFunction*> duplicates;
    for (size_t k = 0; k < vftable.functions.size(); ++k)
    {
      size_t offset = 0;
      memcpy(&offset, i + k * BytesInAddress, BytesInAddress);
      auto rva = offset - coff->getImageBase();

      const int oplen = 6;
      ArrayRef<uint8_t> op;
      if (coff->getRvaAndSizeAsBytes(rva, oplen, op))
        continue;

      auto& func = vftable.functions[k];

      auto it = duplicates.find(rva);
      if (it != duplicates.end())
      {
        it->second->duplicated = true;
        func.duplicated = true;
      }
      else
      {
        duplicates[rva] = &func;
      }

      // Decode instruction in case of Import
      // TODO: Use X86Disassembler
      const auto operand = reinterpret_cast<const int32_t*>(op.data() + 2);

      if (op[0] == 0xFF && op[1] == 0x25) // JMP
      {
        auto rvaimp = rva + *operand + oplen;

        auto f1 = mod->imports.find(rvaimp);
        if (f1 != mod->imports.end())
        { // Call of external function
          rva = rvaimp;
          func.importName = f1->second;
        }
        else
        {
          std::cerr << "Warning: Unnamed import function call candidate in "
            << container->get_name() << " | ind = " << k << std::endl;
          continue; // :(
          // TODO: Something may be wrong with delay imports addresses.
        }
      }

      func.address = rva;

    }
  }

  return true;
}

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#endif
bool restorer::SymbolsTree::collect()
{
  for (const auto& input : inputs)
  {
    std::cout << std::endl;

    if (!input.pdb.empty())
    {
      // TODO: Use this option
    }

    if (!input.object.empty())
    {
      std::cout << "Module: " << input.object << std::endl;
      // Attempt to open the binary.
      StringRef File(input.object);
      Expected<llvm::object::OwningBinary<llvm::object::Binary>> BinaryOrErr = llvm::object::createBinary(File);
      if (!BinaryOrErr)
      {
        continue;
        //reportError(BinaryOrErr.takeError(), File);
      }
      llvm::object::Binary& Binary = *BinaryOrErr.get().getBinary();

      if (auto Arc = dyn_cast<llvm::object::Archive>(&Binary))
      {
        // TODO: Unsupported
        continue;
      }
      else if (auto UBinary = dyn_cast<llvm::object::MachOUniversalBinary>(&Binary))
      {
        // TODO: Unsupported
        continue;
      }
      else if (auto Obj = dyn_cast<llvm::object::ObjectFile>(&Binary))
      {
        modules.push_back(Module());
        Module& mod = *modules.rbegin();
        std::filesystem::path path(input.object);
        mod.path = input.object;
        mod.name = path.filename().string();
        mod.object = Obj;

        parse_object(&mod);
      }
      else if (auto Import = dyn_cast<llvm::object::COFFImportFile>(&Binary))
      {
        // TODO: Unsupported
        continue;
      }
      else if (auto WinRes = dyn_cast<llvm::object::WindowsResource>(&Binary))
      {
        // TODO: Unsupported
        continue;
      }
      else
      {
        // Error
        continue;
      }
    }
  }

  return false;
}
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

bool restorer::SymbolsTree::process()
{
  root.process_step_0(root);
  root.process_step_1(root);
  root.process_step_2(root);
  root.process_step_3(root);
  return true;
}

bool restorer::SymbolsTree::output_to_folder(const std::filesystem::path& path)
{
  auto pathInc = path / "include";
  auto pathSrc = path / "source";

  std::error_code ec;
  if (!std::filesystem::is_directory(pathInc, ec)
    && !std::filesystem::create_directories(pathInc, ec))
  {
    return false;
  }

  {
    std::ofstream inc(pathInc / "_global.h");
    root.output_definition(inc);
  }

  for (const auto& container : root.children)
  {
    std::ofstream out(path / "include" / (container.first + ".hpp"));
    out << "/* Autogenerated header */" << std::endl;
    out << "#pragma once" << std::endl << std::endl;

    if (gInsertIncludeBefore)
    {
      const std::string inc = "\"" + container.second.get_name("__") + ".inc" + "\"";
      out << "#if __has_include(" << inc << ")" << std::endl;
      out << "// Optional file with additional includes" << std::endl;
      out << "#include " << inc << std::endl;
      out << "#endif" << std::endl;
      out << std::endl;
    }

    container.second.output_definition(out);
    out.close();
  }

  return true;
}
