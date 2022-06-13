#pragma once

#include <vector>
#include <cstdint>

namespace ms_rtti
{
  namespace _32
  {
#pragma pack(push, 4)
    // TODO: 32bit
#pragma pack(pop)
  }
  namespace _64
  {
#pragma pack(push, 8)
    struct ClassHierarchyDescriptor;

    struct TypeDescriptor
    {
      uintptr_t pVFTable; // Field overloaded by RTTI
      uintptr_t spare; // reserved, possible for RTTI
      char name[0x400/*array of runtime bound*/]; // The decorated name of the type; 0 terminated.
    };

    struct PMD
    {
      int32_t mdisp;  // Offset of intended data within base
      int32_t pdisp;  // Displacement to virtual base pointer
      int32_t vdisp;  // Index within vbTable to offset of base
    };

    struct BaseClassDescriptor
    {
      enum Attributes : uint32_t
      {
        eNOTVISIBLE = 0x00000001,
        eAMBIGUOUS = 0x00000002,
        ePRIVORPROTBASE = 0x00000004,
        ePRIVORPROTINCOMPOBJ = 0x00000008,
        eVBOFCONTOBJ = 0x00000010,
        eNONPOLYMORPHIC = 0x00000020,
        eHASPCHD = 0x00000040,
      };

      int32_t pTypeDescriptor;  // Image relative offset of TypeDescriptor
      uint32_t numContainedBases;
      PMD where;
      Attributes attributes;
      int32_t pClassDescriptor;  // Image relative offset of _RTTIClassHierarchyDescriptor

      const TypeDescriptor& TD(uintptr_t moduleBase) const;
      const ClassHierarchyDescriptor& CHD(uintptr_t moduleBase) const;
    };

    struct BaseClassArray
    {
      int32_t arrayOfBaseClassDescriptors[0x400/*array of runtime bound*/]; // Image relative offset of _RTTIBaseClassDescriptor
    };

    struct ClassHierarchyDescriptor
    {
      enum Attributes : uint32_t
      {
        eMULTINH = 0x00000001,
        eVIRTINH = 0x00000002,
        eAMBIGUOUS = 0x00000004,
      };

      uint32_t signature;
      Attributes attributes;
      uint32_t numBaseClasses;
      int32_t pBaseClassArray;    // Image relative offset of _RTTIBaseClassArray

      const BaseClassDescriptor& at(std::size_t idx, uintptr_t moduleBase) const;

      inline const BaseClassArray& BCA(uintptr_t moduleBase) const;

      std::vector<const BaseClassDescriptor*> getBaseClasses(uintptr_t moduleBase) const;
      std::vector<const BaseClassDescriptor*> getBaseClasses(uintptr_t moduleBase, int mdisp) const;
      std::vector<const BaseClassDescriptor*> getNearestBaseClasses(uintptr_t moduleBase) const;
    };

    struct CompleteObjectLocator
    {
      uint32_t signature;
      uint32_t offset;
      uint32_t cdOffset;
      int32_t pTypeDescriptor;  // Image relative offset of TypeDescriptor
      int32_t pClassDescriptor;  // Image relative offset of _RTTIClassHierarchyDescriptor
      int32_t pSelf;        // Image relative offset of this object

      const TypeDescriptor& TD() const;
      const ClassHierarchyDescriptor& CHD() const;
      std::vector<const BaseClassDescriptor*> getBaseClasses() const;
      std::vector<const BaseClassDescriptor*> getBaseClasses(int mdisp) const;
      std::vector<const BaseClassDescriptor*> getNearestBaseClasses() const;
      uintptr_t getModuleBase() const;
    };
#pragma pack(pop)
  }
}
