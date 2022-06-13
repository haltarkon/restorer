#pragma once

#include <vector>
#include <cstdint>

namespace ms_rtti
{
  namespace _64
  {
    struct ClassHierarchyDescriptor;

    struct TypeDescriptor
    {
      const void* pVFTable; // Field overloaded by RTTI
      void* spare; // reserved, possible for RTTI
      char name[0x400/*array of runtime bound*/]; // The decorated name of the type; 0 terminated.
    };

    struct PMD
    {
      int  mdisp;  // Offset of intended data within base
      int  pdisp;  // Displacement to virtual base pointer
      int  vdisp;  // Index within vbTable to offset of base
    };

    struct BaseClassDescriptor
    {
      enum Attributes : unsigned long
      {
        eNOTVISIBLE = 0x00000001,
        eAMBIGUOUS = 0x00000002,
        ePRIVORPROTBASE = 0x00000004,
        ePRIVORPROTINCOMPOBJ = 0x00000008,
        eVBOFCONTOBJ = 0x00000010,
        eNONPOLYMORPHIC = 0x00000020,
        eHASPCHD = 0x00000040,
      };

      int pTypeDescriptor;  // Image relative offset of TypeDescriptor
      unsigned long  numContainedBases;
      PMD where;
      Attributes attributes;
      int pClassDescriptor;  // Image relative offset of _RTTIClassHierarchyDescriptor

      const TypeDescriptor& TD(uintptr_t moduleBase) const;
      const ClassHierarchyDescriptor& CHD(uintptr_t moduleBase) const;
    };

    struct BaseClassArray
    {
      int arrayOfBaseClassDescriptors[0x400/*array of runtime bound*/]; // Image relative offset of _RTTIBaseClassDescriptor
    };

    struct ClassHierarchyDescriptor
    {
      enum Attributes : unsigned long
      {
        eMULTINH = 0x00000001,
        eVIRTINH = 0x00000002,
        eAMBIGUOUS = 0x00000004,
      };

      unsigned long signature;
      Attributes attributes;
      unsigned long numBaseClasses;
      int  pBaseClassArray;    // Image relative offset of _RTTIBaseClassArray

      const BaseClassDescriptor& at(std::size_t idx, uintptr_t moduleBase) const;

      inline const BaseClassArray& BCA(uintptr_t moduleBase) const;

      std::vector<const BaseClassDescriptor*> getBaseClasses(uintptr_t moduleBase) const;
      std::vector<const BaseClassDescriptor*> getBaseClasses(uintptr_t moduleBase, int mdisp) const;
      std::vector<const BaseClassDescriptor*> getNearestBaseClasses(uintptr_t moduleBase) const;
    };

    struct CompleteObjectLocator
    {
      unsigned long  signature;
      unsigned long  offset;
      unsigned long  cdOffset;
      int  pTypeDescriptor;  // Image relative offset of TypeDescriptor
      int  pClassDescriptor;  // Image relative offset of _RTTIClassHierarchyDescriptor
      int  pSelf;        // Image relative offset of this object

      const TypeDescriptor& TD() const;
      const ClassHierarchyDescriptor& CHD() const;
      std::vector<const BaseClassDescriptor*> getBaseClasses() const;
      std::vector<const BaseClassDescriptor*> getBaseClasses(int mdisp) const;
      std::vector<const BaseClassDescriptor*> getNearestBaseClasses() const;
      uintptr_t getModuleBase() const;
    };
  }
}
