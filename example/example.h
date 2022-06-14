#pragma once

#define EXPORT __declspec(dllexport)

namespace example
{
  class EXPORT A
  {
    enum enum_A_1 {};

    enum enum_A_2 : char {}; // Enum with char real type

    void function_A_1(enum_A_1) {}
    void function_A_2(enum_A_2) {}
  };

  namespace BC // nested namespace
  {
    enum enum_BC_1 {};

    enum enum_BC_2 {}; // Unused

    union EXPORT U // Union
    {
      int i;
      char c;
    };

    struct EXPORT B
    {
      virtual ~B() = default;
    };

    struct EXPORT C : public B
    {
      virtual ~C() = default;
      virtual void virtual_function_C_1(enum_BC_1) {}
      virtual void virtual_function_C_2(U) {}
    };
  }

  struct EXPORT D : protected A, public BC::C
  {
    virtual ~D() = default;
    virtual void virtual_function_D_1() {}
    virtual void virtual_function_D_2(int) {}
  };

  class E : public D
  {
    enum enum_E_1 {};
    enum enum_E_2 {};

    EXPORT static void static_function_E_0(enum_E_2) {}

    EXPORT void function_E_1(enum_E_1) {}
    /*  */ void function_E_2(enum_E_2) {}
    
    virtual ~E() = default;
  };
}
