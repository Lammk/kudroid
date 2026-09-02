#include "kudroid/kuart/DexClass.h"

#include <cstring>

namespace kudroid {
namespace kuart {

namespace {

bool NameAndSigMatch(const DexMethod& m, const char* name, const char* signature) {
    if (std::strcmp(m.name, name) != 0) return false;
    return signature == nullptr || std::strcmp(m.signature, signature) == 0;
}

bool FieldMatch(const DexField& f, const char* name, const char* type_descriptor) {
    if (std::strcmp(f.name, name) != 0) return false;
    return type_descriptor == nullptr || std::strcmp(f.type_descriptor, type_descriptor) == 0;
}

// The best interface-declared method for `name`/`signature` reachable from `klass`.
//
// Two things are wanted from one walk, so both are returned:
//
//   *best_default  — the maximally specific CONCRETE default. This is what runs.
//   *best_abstract — an abstract interface declaration, when no default exists. This is
//                    still a valid RESOLUTION result and must not be discarded.
//
// The second output is not a nicety. JVMS separates resolution (5.4.3.3), which yields a
// method reference and legitimately lands on an abstract interface declaration, from
// selection (5.4.6), which picks the concrete body from the receiver's class at call time.
// FindVirtualMethod serves both kinds of caller, so returning null for
// `List.clear()` — abstract, no default anywhere — broke callers that only needed the
// reference: Security.getProviders started handing back [Ljava/lang/Object; and the
// SharedPreferences editor threw NullPointerException. Skipping abstract candidates while
// searching for a default is right; dropping them from the result is not.
//
// "Maximally specific" is the JVMS term and it is not the same as "the first one found".
// When a class implements both J and I, and I extends J overriding its default, I's version
// must run regardless of the order the class lists them in. A depth-first search returns
// whichever it reaches first, so on `class C implements J, I` it returned J's and the guest
// silently ran the superseded implementation.
//
// Ambiguity between unrelated interfaces keeps the first candidate rather than raising
// IncompatibleClassChangeError as a real JVM would: javac rejects the genuinely ambiguous
// case at compile time, so an interpreter refusing to run code javac already accepted is
// the worse failure.
void FindInterfaceMethod(DexClass* klass, const char* name, const char* signature,
                         DexMethod** best_default, DexMethod** best_abstract) {
    if (klass == nullptr) return;

    // Walk the whole interface graph, not just the direct list: a default may be declared
    // on a grandparent interface.
    for (DexClass* k = klass; k != nullptr; k = k->superclass) {
        for (DexClass* iface : k->interfaces) {
            if (iface == nullptr) continue;

            // The interface's own declaration first, so a sub-interface that overrides a
            // default is considered before the one it overrides.
            DexMethod* declared = nullptr;
            for (DexMethod& m : iface->virtual_methods) {
                if (NameAndSigMatch(m, name, signature)) { declared = &m; break; }
            }

            if (declared != nullptr && !declared->IsAbstract()) {
                if (*best_default == nullptr) {
                    *best_default = declared;
                } else {
                    // Prefer the more specific declaring interface. IsSubClassOf covers
                    // interface extension, so I extends J makes I's version win.
                    DexClass* best_owner = (*best_default)->declaring_class;
                    DexClass* cand_owner = declared->declaring_class;
                    if (cand_owner != nullptr && best_owner != nullptr &&
                        cand_owner != best_owner && cand_owner->IsSubClassOf(best_owner)) {
                        *best_default = declared;
                    }
                }
            } else if (declared != nullptr && *best_abstract == nullptr) {
                // Remembered, not returned yet: a default further up the graph still wins.
                *best_abstract = declared;
            }

            // Recurse regardless of what this interface declared. An abstract declaration
            // here does not mean there is no default above it — that is precisely the
            // `interface I extends J` shape, where I re-declares f() abstract and J
            // supplies the body.
            FindInterfaceMethod(iface, name, signature, best_default, best_abstract);
        }
    }
}

}  // namespace

// Resolution for invoke-virtual and invoke-interface, following JVMS 5.4.3.3 and 5.4.6.
//
// Precedence, and every rank is load-bearing:
//
//   1. a CONCRETE method in the class chain — a class's own body always beats a default,
//      which is what lets a class override one.
//   2. the maximally specific CONCRETE interface default.
//   3. an ABSTRACT declaration in the class chain.
//   4. an ABSTRACT interface declaration.
//
// Rank 2 above rank 3 is the fix: the old code returned the first thing the class chain
// matched, so an abstract declaration there hid a concrete default. And the selection among
// interfaces was depth-first rather than maximally specific, so an overridden default could
// run in place of the one that overrode it — silently, which is why nothing pointed at it.
//
// Ranks 3 and 4 exist because resolution and selection are different questions. Many callers
// here only need the method REFERENCE — its signature, its declaring class, a vtable slot —
// and for an interface method with no implementation anywhere the abstract declaration IS
// the correct answer; the caller decides whether that is an error. Dropping rank 4 while
// fixing rank 2 is a mistake I made and it broke Security.getProviders and the
// SharedPreferences editor, neither of which involves a default method at all.
DexMethod* DexClass::FindVirtualMethod(const char* name, const char* signature) {
    DexMethod* abstract_in_chain = nullptr;

    for (DexClass* k = this; k != nullptr; k = k->superclass) {
        for (DexMethod& m : k->virtual_methods) {
            if (!NameAndSigMatch(m, name, signature)) continue;
            if (!m.IsAbstract()) return &m;  // rank 1
            // Keep the FIRST abstract match: it is the most derived declaration, and the
            // one whose name belongs in an AbstractMethodError.
            if (abstract_in_chain == nullptr) abstract_in_chain = &m;
        }
    }

    DexMethod* interface_default = nullptr;
    DexMethod* abstract_in_interface = nullptr;
    FindInterfaceMethod(this, name, signature, &interface_default, &abstract_in_interface);

    if (interface_default != nullptr) return interface_default;  // rank 2
    if (abstract_in_chain != nullptr) return abstract_in_chain;  // rank 3
    return abstract_in_interface;                                // rank 4
}

DexMethod* DexClass::FindDirectMethod(const char* name, const char* signature) {
    for (DexClass* k = this; k != nullptr; k = k->superclass) {
        for (DexMethod& m : k->direct_methods) {
            if (NameAndSigMatch(m, name, signature)) return &m;
        }
    }
    return nullptr;
}

DexField* DexClass::FindInstanceField(const char* name, const char* type_descriptor) {
    for (DexClass* k = this; k != nullptr; k = k->superclass) {
        for (DexField& f : k->instance_fields) {
            if (FieldMatch(f, name, type_descriptor)) return &f;
        }
    }
    return nullptr;
}

DexField* DexClass::FindStaticField(const char* name, const char* type_descriptor) {
    for (DexClass* k = this; k != nullptr; k = k->superclass) {
        for (DexField& f : k->static_fields) {
            if (FieldMatch(f, name, type_descriptor)) return &f;
        }
    }
    // The static field declared in the interface is also visible from the child class.
    for (DexClass* k = this; k != nullptr; k = k->superclass) {
        for (DexClass* iface : k->interfaces) {
            if (iface == nullptr) continue;
            if (DexField* f = iface->FindStaticField(name, type_descriptor)) return f;
        }
    }
    return nullptr;
}

bool DexClass::IsSubClassOf(const DexClass* other) const {
    if (other == nullptr) return false;
    if (this == other) return true;

    // java.lang.Object is the universal superclass of all reference types and arrays.
    if (!is_primitive && other->descriptor != nullptr &&
        std::strcmp(other->descriptor, "Ljava/lang/Object;") == 0) {
        return true;
    }

    // Arrays implement Cloneable & Serializable, and are covariant for reference components.
    if (is_array) {
        if (other->descriptor != nullptr &&
            (std::strcmp(other->descriptor, "Ljava/lang/Cloneable;") == 0 ||
             std::strcmp(other->descriptor, "Ljava/io/Serializable;") == 0)) {
            return true;
        }
        if (other->is_array) {
            // Any reference array (object array or multi-dim array) is a subtype of Object[]
            if (other->descriptor != nullptr && std::strcmp(other->descriptor, "[Ljava/lang/Object;") == 0) {
                if (descriptor != nullptr && (descriptor[1] == 'L' || descriptor[1] == '[')) {
                    return true;
                }
            }
            if (component_type != nullptr && other->component_type != nullptr) {
                if (component_type->is_primitive || other->component_type->is_primitive) {
                    return component_type == other->component_type;
                }
                return component_type->IsSubClassOf(other->component_type);
            }
            if (descriptor != nullptr && other->descriptor != nullptr) {
                return std::strcmp(descriptor, other->descriptor) == 0;
            }
            return false;
        }
        return false;
    }

    for (const DexClass* k = this; k != nullptr; k = k->superclass) {
        if (k == other) return true;
        for (const DexClass* iface : k->interfaces) {
            if (iface != nullptr && iface->IsSubClassOf(other)) return true;
        }
    }
    return false;
}

std::string DexClass::PrettyName() const {
    if (descriptor == nullptr) return "<null>";
    const size_t len = std::strlen(descriptor);
    // "Lcom/foo/Bar;" -> "com.foo.Bar"
    if (len >= 2 && descriptor[0] == 'L' && descriptor[len - 1] == ';') {
        std::string out(descriptor + 1, len - 2);
        for (char& c : out) {
            if (c == '/') c = '.';
        }
        return out;
    }
    return descriptor;
}

}  // namespace kuart
}  // namespace kudroid
