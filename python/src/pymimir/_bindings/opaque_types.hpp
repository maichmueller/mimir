
#pragma once

#include "pymimir.hpp"

#include <pybind11/pybind11.h>

/* Formalism */
PYBIND11_MAKE_OPAQUE(pymimir::ActionList);
PYBIND11_MAKE_OPAQUE(pymimir::AtomList<pymimir::Static>);
PYBIND11_MAKE_OPAQUE(pymimir::AtomList<pymimir::Fluent>);
PYBIND11_MAKE_OPAQUE(pymimir::AtomList<pymimir::Derived>);
PYBIND11_MAKE_OPAQUE(pymimir::AxiomList);
PYBIND11_MAKE_OPAQUE(pymimir::DomainList);
PYBIND11_MAKE_OPAQUE(pymimir::EffectSimpleList);
PYBIND11_MAKE_OPAQUE(pymimir::EffectComplexList);
PYBIND11_MAKE_OPAQUE(pymimir::FunctionExpressionList);
PYBIND11_MAKE_OPAQUE(pymimir::FunctionSkeletonList);
PYBIND11_MAKE_OPAQUE(pymimir::FunctionList);
PYBIND11_MAKE_OPAQUE(pymimir::GroundAtomList<pymimir::Static>);
PYBIND11_MAKE_OPAQUE(pymimir::GroundAtomList<pymimir::Fluent>);
PYBIND11_MAKE_OPAQUE(pymimir::GroundAtomList<pymimir::Derived>);
PYBIND11_MAKE_OPAQUE(pymimir::GroundFunctionExpressionList);
PYBIND11_MAKE_OPAQUE(pymimir::GroundLiteralList<pymimir::Static>);
PYBIND11_MAKE_OPAQUE(pymimir::GroundLiteralList<pymimir::Fluent>);
PYBIND11_MAKE_OPAQUE(pymimir::GroundLiteralList<pymimir::Derived>);
PYBIND11_MAKE_OPAQUE(pymimir::LiteralList<pymimir::Static>);
PYBIND11_MAKE_OPAQUE(pymimir::LiteralList<pymimir::Fluent>);
PYBIND11_MAKE_OPAQUE(pymimir::LiteralList<pymimir::Derived>);
PYBIND11_MAKE_OPAQUE(pymimir::NumericFluentList);
PYBIND11_MAKE_OPAQUE(pymimir::ObjectList);
PYBIND11_MAKE_OPAQUE(pymimir::PredicateList<pymimir::Static>);
PYBIND11_MAKE_OPAQUE(pymimir::PredicateList<pymimir::Fluent>);
PYBIND11_MAKE_OPAQUE(pymimir::PredicateList<pymimir::Derived>);
PYBIND11_MAKE_OPAQUE(pymimir::ToPredicateMap<std::string, pymimir::Static>);
PYBIND11_MAKE_OPAQUE(pymimir::ToPredicateMap<std::string, pymimir::Fluent>);
PYBIND11_MAKE_OPAQUE(pymimir::ToPredicateMap<std::string, pymimir::Derived>);
PYBIND11_MAKE_OPAQUE(pymimir::ProblemList);
PYBIND11_MAKE_OPAQUE(pymimir::TermList);
PYBIND11_MAKE_OPAQUE(pymimir::VariableList);

/* Search */
PYBIND11_MAKE_OPAQUE(pymimir::StateList);
PYBIND11_MAKE_OPAQUE(pymimir::GroundActionList);
