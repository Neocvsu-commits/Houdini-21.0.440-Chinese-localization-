#include "../../source/hooks/native_rtti.h"

#include <cassert>
#include <iostream>

class NativeRttiBase {
  public:
    virtual ~NativeRttiBase() = default;
};

class NativeRttiDerived final : public NativeRttiBase {};

class NativeRttiOther {
  public:
    virtual ~NativeRttiOther() = default;
};

class NativeRttiBoth final : public NativeRttiBase, public NativeRttiOther {};
class NativeRttiVirtual final : public virtual NativeRttiBase {};
class NativeRttiBranchA : public NativeRttiBase {};
class NativeRttiBranchB : public NativeRttiBase {};
class NativeRttiDiamond final : public NativeRttiBranchA,
                                public NativeRttiBranchB {};

int main() {
    NativeRttiDerived derived;
    NativeRttiOther other;

    const char *derivedName =
        HoudiniChineseNativeRtti::dynamicTypeName(&derived);
    assert(HoudiniChineseNativeRtti::typeNameEquals(derivedName,
                                                    ".?AVNativeRttiDerived@@"));
    assert(
        HoudiniChineseNativeRtti::hasBase(&derived, ".?AVNativeRttiDerived@@"));
    assert(HoudiniChineseNativeRtti::hasBase(&derived, ".?AVNativeRttiBase@@"));
    assert(
        !HoudiniChineseNativeRtti::hasBase(&derived, ".?AVNativeRttiOther@@"));
    assert(HoudiniChineseNativeRtti::hasBase(&other, ".?AVNativeRttiOther@@"));
    assert(!HoudiniChineseNativeRtti::hasBase(nullptr, ".?AVNativeRttiBase@@"));
    assert(!HoudiniChineseNativeRtti::typeNameEquals(derivedName, {}));

    // Calling from a secondary vtable must still locate the complete object
    // before applying the destination base's displacement.
    using HoudiniChineseNativeRtti::nonVirtualBase;
    NativeRttiBoth both;
    NativeRttiOther *right = &both;
    assert(nonVirtualBase(right, ".?AVNativeRttiBase@@") ==
           static_cast<NativeRttiBase *>(&both));
    assert(nonVirtualBase(&both, ".?AVNativeRttiOther@@") == right);
    assert(nonVirtualBase(right, ".?AVNativeRttiBoth@@") == &both);
    assert(nonVirtualBase(right, ".?AVUnknown@@") == nullptr);
    NativeRttiVirtual virtualBase;
    assert(nonVirtualBase(&virtualBase, ".?AVNativeRttiBase@@") == nullptr);
    NativeRttiDiamond diamond;
    assert(nonVirtualBase(&diamond, ".?AVNativeRttiBase@@") == nullptr);
    assert(nonVirtualBase(&diamond, ".?AVNativeRttiBranchB@@") ==
           static_cast<NativeRttiBranchB *>(&diamond));
    assert(nonVirtualBase(nullptr, ".?AVNativeRttiBase@@") == nullptr);

    std::cout << "native_rtti_test: OK\n";
    return 0;
}
