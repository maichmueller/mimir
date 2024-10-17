
#ifndef MIMIR_OPAQUE_TYPES_HPP
#define MIMIR_OPAQUE_TYPES_HPP

#include "mimir/mimir.hpp"
#include "variants.hpp"

#include <pybind11/pybind11.h>

/* Formalism */
PYBIND11_MAKE_OPAQUE(mimir::ActionList);
PYBIND11_MAKE_OPAQUE(mimir::AtomList<Static>);
PYBIND11_MAKE_OPAQUE(mimir::AtomList<Fluent>);
PYBIND11_MAKE_OPAQUE(mimir::AtomList<Derived>);
PYBIND11_MAKE_OPAQUE(mimir::AxiomList);
PYBIND11_MAKE_OPAQUE(mimir::DomainList);
PYBIND11_MAKE_OPAQUE(mimir::EffectSimpleList);
PYBIND11_MAKE_OPAQUE(mimir::EffectComplexList);
PYBIND11_MAKE_OPAQUE(pymimir::FunctionExpressionVariantList);
PYBIND11_MAKE_OPAQUE(mimir::FunctionSkeletonList);
PYBIND11_MAKE_OPAQUE(mimir::FunctionList);
PYBIND11_MAKE_OPAQUE(mimir::GroundAtomList<Static>);
PYBIND11_MAKE_OPAQUE(mimir::GroundAtomList<Fluent>);
PYBIND11_MAKE_OPAQUE(mimir::GroundAtomList<Derived>);
PYBIND11_MAKE_OPAQUE(pymimir::GroundFunctionExpressionVariantList);
PYBIND11_MAKE_OPAQUE(mimir::GroundLiteralList<Static>);
PYBIND11_MAKE_OPAQUE(mimir::GroundLiteralList<Fluent>);
PYBIND11_MAKE_OPAQUE(mimir::GroundLiteralList<Derived>);
PYBIND11_MAKE_OPAQUE(mimir::LiteralList<Static>);
PYBIND11_MAKE_OPAQUE(mimir::LiteralList<Fluent>);
PYBIND11_MAKE_OPAQUE(mimir::LiteralList<Derived>);
PYBIND11_MAKE_OPAQUE(mimir::NumericFluentList);
PYBIND11_MAKE_OPAQUE(mimir::ObjectList);
PYBIND11_MAKE_OPAQUE(mimir::PredicateList<Static>);
PYBIND11_MAKE_OPAQUE(mimir::PredicateList<Fluent>);
PYBIND11_MAKE_OPAQUE(mimir::PredicateList<Derived>);
PYBIND11_MAKE_OPAQUE(mimir::ToPredicateMap<std::string, Static>);
PYBIND11_MAKE_OPAQUE(mimir::ToPredicateMap<std::string, Fluent>);
PYBIND11_MAKE_OPAQUE(mimir::ToPredicateMap<std::string, Derived>);
PYBIND11_MAKE_OPAQUE(mimir::ProblemList);
PYBIND11_MAKE_OPAQUE(mimir::TermList);
PYBIND11_MAKE_OPAQUE(mimir::VariableList);

/* Search */
PYBIND11_MAKE_OPAQUE(mimir::StateList);
PYBIND11_MAKE_OPAQUE(mimir::GroundActionList);

#endif  // MIMIR_OPAQUE_TYPES_HPP
