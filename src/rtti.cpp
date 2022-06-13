
#include "rtti.h"

namespace ms_rtti
{
  namespace _64
  {
    const TypeDescriptor& BaseClassDescriptor::TD(uintptr_t moduleBase) const
    {
      return *(TypeDescriptor*)(moduleBase + pTypeDescriptor);
    }

    const ClassHierarchyDescriptor& BaseClassDescriptor::CHD(uintptr_t moduleBase) const
    {
      return *reinterpret_cast<const ClassHierarchyDescriptor*>(moduleBase + pClassDescriptor);
    }

    const BaseClassDescriptor& ClassHierarchyDescriptor::at(std::size_t idx, uintptr_t moduleBase) const
    {
      return *reinterpret_cast<BaseClassDescriptor*>(moduleBase
        + BCA(moduleBase).arrayOfBaseClassDescriptors[idx]);
    }

    const BaseClassArray& ClassHierarchyDescriptor::BCA(uintptr_t moduleBase) const
    {
      return *(BaseClassArray*)(moduleBase + pBaseClassArray);
    }

    std::vector<const BaseClassDescriptor*> ClassHierarchyDescriptor::getBaseClasses(uintptr_t moduleBase) const
    {
      std::vector<const BaseClassDescriptor*> bases;

      for (size_t i = 1; i < numBaseClasses; ++i)
      {
        auto& bcd = at(i, moduleBase);
        bases.push_back(&bcd);
      }

      return bases;
    }

    std::vector<const BaseClassDescriptor*> ClassHierarchyDescriptor::getBaseClasses(uintptr_t moduleBase, int mdisp) const
    {
      std::vector<const BaseClassDescriptor*> bases;

      for (size_t i = 1; i < numBaseClasses; ++i)
      {
        auto& bcd = at(i, moduleBase);
        if (bcd.where.mdisp == mdisp)
        {
          bases.push_back(&bcd);
        }
      }

      return bases;
    }

    std::vector<const BaseClassDescriptor*> ClassHierarchyDescriptor::getNearestBaseClasses(uintptr_t moduleBase) const
    {
      std::vector<const BaseClassDescriptor*> bases;

      for (size_t i = 1; i < numBaseClasses; ++i)
      {
        auto& bcd = at(i, moduleBase);
        bases.push_back(&bcd);
        i += bcd.numContainedBases;
      }

      return bases;
    }

    const TypeDescriptor& CompleteObjectLocator::TD() const
    {
      return *(TypeDescriptor*)(getModuleBase() + pTypeDescriptor);
    }

    const ClassHierarchyDescriptor& CompleteObjectLocator::CHD() const
    {
      return *reinterpret_cast<const ClassHierarchyDescriptor*>(getModuleBase() + pClassDescriptor);
    }

    std::vector<const BaseClassDescriptor*> CompleteObjectLocator::getBaseClasses() const
    {
      const ClassHierarchyDescriptor& chd = CHD();
      return chd.getBaseClasses(getModuleBase());
    }

    std::vector<const BaseClassDescriptor*> CompleteObjectLocator::getBaseClasses(int mdisp) const
    {
      const ClassHierarchyDescriptor& chd = CHD();
      return chd.getBaseClasses(getModuleBase(), mdisp);
    }

    std::vector<const BaseClassDescriptor*> CompleteObjectLocator::getNearestBaseClasses() const
    {
      const ClassHierarchyDescriptor& chd = CHD();
      return chd.getNearestBaseClasses(getModuleBase());
    }

    uintptr_t CompleteObjectLocator::getModuleBase() const
    {
      return reinterpret_cast<uintptr_t>(this) - pSelf;
    }
  }
}
