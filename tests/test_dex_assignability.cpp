#include "kudroid/kuart/DexClass.h"

#include <chrono>
#include <cstdio>
#include <vector>

int main() {
    using kudroid::kuart::DexClass;
    DexClass object, parent, child, unrelated, rootInterface, subInterface, primitive;
    object.descriptor = "Ljava/lang/Object;";
    parent.descriptor = "LParent;";
    child.descriptor = "LChild;";
    unrelated.descriptor = "LUnrelated;";
    rootInterface.descriptor = "LRootInterface;";
    subInterface.descriptor = "LSubInterface;";
    rootInterface.access_flags = subInterface.access_flags = art::kAccInterface;
    parent.superclass = &object;
    child.superclass = &parent;
    rootInterface.superclass = subInterface.superclass = &object;
    subInterface.interfaces = {&rootInterface};
    parent.interfaces = {&subInterface};
    primitive.descriptor = "I";
    primitive.is_primitive = true;
    int checks = 0, failures = 0;
    auto check = [&](bool value, const char* name) {
        ++checks;
        failures += !value;
        std::printf("%s %s\n", value ? "PASS" : "FAIL", name);
    };
    check(child.IsSubClassOf(&parent), "concrete superclass");
    check(child.IsSubClassOf(&object), "universal reference superclass");
    check(child.IsSubClassOf(&rootInterface), "inherited transitive interface");
    check(subInterface.IsSubClassOf(&rootInterface), "interface ancestry");
    check(!rootInterface.IsSubClassOf(&parent), "interface cannot inherit concrete class");
    check(!child.IsSubClassOf(&unrelated), "unrelated class");
    check(!primitive.IsSubClassOf(&object), "primitive is not a reference");
    check(primitive.IsSubClassOf(&primitive), "primitive identity");
    DexClass parentArray, childArray, intArray, otherIntArray, cloneable;
    parentArray.descriptor = "[LParent;";
    childArray.descriptor = "[LChild;";
    intArray.descriptor = otherIntArray.descriptor = "[I";
    parentArray.is_array = childArray.is_array = intArray.is_array = otherIntArray.is_array = true;
    parentArray.component_type = &parent;
    childArray.component_type = &child;
    intArray.component_type = otherIntArray.component_type = &primitive;
    cloneable.descriptor = "Ljava/lang/Cloneable;";
    cloneable.access_flags = art::kAccInterface;
    check(childArray.IsSubClassOf(&parentArray), "reference array covariance");
    check(!parentArray.IsSubClassOf(&childArray), "array covariance direction");
    check(intArray.IsSubClassOf(&otherIntArray), "primitive array identity");
    check(!intArray.IsSubClassOf(&parentArray), "primitive array is not reference array");
    check(childArray.IsSubClassOf(&cloneable), "array implements marker interface");
    check(!child.IsSubClassOf(nullptr), "null target");
    DexClass unresolved;
    unresolved.descriptor = "LUnresolved;";
    unresolved.is_stub = true;
    subInterface.interfaces.push_back(&unresolved);
    check(child.IsSubClassOf(&unresolved), "unresolved interface ancestry is preserved");
    std::vector<DexClass> interfaces(256);
    for (auto& iface : interfaces) {
        iface.descriptor = "LInterface;";
        iface.access_flags = art::kAccInterface;
        iface.superclass = &object;
        child.interfaces.push_back(&iface);
    }
    auto start = std::chrono::steady_clock::now();
    volatile unsigned matches = 0;
    for (unsigned i = 0; i < 100000; ++i) matches += child.IsSubClassOf(&parent);
    auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    check(matches == 100000, "repeated field-owner ancestry");
    std::printf("Assignability: %d checks, %d failures; concrete checks %.3f ms\n", checks, failures, ms);
    return failures ? 1 : 0;
}
