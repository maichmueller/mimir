
#pragma once


#include "mimir/mimir.hpp"
#include "variants.hpp"

#include <pybind11/pybind11.h>

/* Formalism */
PYBIND11_MAKE_OPAQUE(mimir::ActionList);
PYBIND11_MAKE_OPAQUE(mimir::AtomList<mimir::Static>);
PYBIND11_MAKE_OPAQUE(mimir::AtomList<mimir::Fluent>);
PYBIND11_MAKE_OPAQUE(mimir::AtomList<mimir::Derived>);
PYBIND11_MAKE_OPAQUE(mimir::AxiomList);
PYBIND11_MAKE_OPAQUE(mimir::DomainList);
PYBIND11_MAKE_OPAQUE(mimir::ConditionalEffectList);
PYBIND11_MAKE_OPAQUE(mimir::EffectSimpleList);
PYBIND11_MAKE_OPAQUE(mimir::EffectComplexList);
PYBIND11_MAKE_OPAQUE(mimir::pymimir::FunctionExpressionVariantList);
PYBIND11_MAKE_OPAQUE(mimir::FunctionSkeletonList);
PYBIND11_MAKE_OPAQUE(mimir::FunctionList);
PYBIND11_MAKE_OPAQUE(mimir::GroundAtomList<mimir::Static>);
PYBIND11_MAKE_OPAQUE(mimir::GroundAtomList<mimir::Fluent>);
PYBIND11_MAKE_OPAQUE(mimir::GroundAtomList<mimir::Derived>);
PYBIND11_MAKE_OPAQUE(mimir::pymimir::GroundFunctionExpressionVariantList);
PYBIND11_MAKE_OPAQUE(mimir::GroundLiteralList<mimir::Static>);
PYBIND11_MAKE_OPAQUE(mimir::GroundLiteralList<mimir::Fluent>);
PYBIND11_MAKE_OPAQUE(mimir::GroundLiteralList<mimir::Derived>);
PYBIND11_MAKE_OPAQUE(mimir::LiteralList<mimir::Static>);
PYBIND11_MAKE_OPAQUE(mimir::LiteralList<mimir::Fluent>);
PYBIND11_MAKE_OPAQUE(mimir::LiteralList<mimir::Derived>);
PYBIND11_MAKE_OPAQUE(mimir::NumericFluentList);
PYBIND11_MAKE_OPAQUE(mimir::ObjectList);
PYBIND11_MAKE_OPAQUE(mimir::PredicateList<mimir::Static>);
PYBIND11_MAKE_OPAQUE(mimir::PredicateList<mimir::Fluent>);
PYBIND11_MAKE_OPAQUE(mimir::PredicateList<mimir::Derived>);
PYBIND11_MAKE_OPAQUE(mimir::ToPredicateMap<std::string, mimir::Static>);
PYBIND11_MAKE_OPAQUE(mimir::ToPredicateMap<std::string, mimir::Fluent>);
PYBIND11_MAKE_OPAQUE(mimir::ToPredicateMap<std::string, mimir::Derived>);
PYBIND11_MAKE_OPAQUE(mimir::ProblemList);
PYBIND11_MAKE_OPAQUE(mimir::TermList);
PYBIND11_MAKE_OPAQUE(mimir::VariableList);

/* Search */
PYBIND11_MAKE_OPAQUE(mimir::StateList);
PYBIND11_MAKE_OPAQUE(mimir::GroundActionList);

