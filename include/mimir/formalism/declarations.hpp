/*
 * Copyright (C) 2023 Dominik Drexler
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

// Do not include headers with transitive dependencies.
#include "mimir/common/types.hpp"
#include "mimir/declarations.hpp"
#include "mimir/formalism/predicate_tag.hpp"

#include <loki/loki.hpp>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace loki
{
template<typename HolderType, typename Hash, typename EqualTo>
class UniqueFactory;
}

namespace mimir
{
/**
 * Forward declarations
 */

class ActionImpl;
using Action = const ActionImpl*;
using ActionList = vector<Action>;

template<PredicateTag P>
class AtomImpl;
template<PredicateTag P>
using Atom = const AtomImpl<P>*;
using AnyAtom = std::variant<Atom<Fluent>, Atom<Derived>, Atom<Fluent>>;
template<PredicateTag P>
using AtomList = vector<Atom<P>>;
template<PredicateTag P>
using AtomSpan = std::span<Atom<P>>;
using AnyAtomList = vector<AnyAtom>;

class AxiomImpl;
using Axiom = const AxiomImpl*;
using AxiomList = vector<Axiom>;
using AxiomSet = unordered_set<Axiom>;

class DomainImpl;
using Domain = const DomainImpl*;
using DomainList = vector<Domain>;

class EffectSimpleImpl;
using EffectSimple = const EffectSimpleImpl*;
using EffectSimpleList = vector<EffectSimple>;

class EffectComplexImpl;
using EffectComplex = const EffectComplexImpl*;
using EffectComplexList = vector<EffectComplex>;

class FunctionExpressionNumberImpl;
using FunctionExpressionNumber = const FunctionExpressionNumberImpl*;
class FunctionExpressionBinaryOperatorImpl;
using FunctionExpressionBinaryOperator = const FunctionExpressionBinaryOperatorImpl*;
class FunctionExpressionMultiOperatorImpl;
using FunctionExpressionMultiOperator = const FunctionExpressionMultiOperatorImpl*;
class FunctionExpressionMinusImpl;
using FunctionExpressionMinus = const FunctionExpressionMinusImpl*;
class FunctionExpressionFunctionImpl;
using FunctionExpressionFunction = const FunctionExpressionFunctionImpl*;
using FunctionExpressionImpl = std::variant<FunctionExpressionNumberImpl,
                                            FunctionExpressionBinaryOperatorImpl,
                                            FunctionExpressionMultiOperatorImpl,
                                            FunctionExpressionMinusImpl,
                                            FunctionExpressionFunctionImpl>;
using FunctionExpression = const FunctionExpressionImpl*;
using FunctionExpressionList = vector<FunctionExpression>;

class FunctionSkeletonImpl;
using FunctionSkeleton = const FunctionSkeletonImpl*;
using FunctionSkeletonList = vector<FunctionSkeleton>;

class FunctionImpl;
using Function = const FunctionImpl*;
using FunctionList = vector<Function>;

template<PredicateTag P>
class GroundAtomImpl;
template<PredicateTag P>
using GroundAtom = const GroundAtomImpl<P>*;
using AnyGroundAtom = std::variant<GroundAtom<Fluent>, GroundAtom<Derived>, GroundAtom<Static>>;
template<PredicateTag P>
using GroundAtomList = vector<GroundAtom<P>>;
template<PredicateTag P>
using GroundAtomSpan = std::span<GroundAtom<P>>;
using AnyGroundAtomList = vector<AnyGroundAtom>;
template<PredicateTag P>
using GroundAtomSet = unordered_set<GroundAtom<P>>;
using AnyGroundAtomSet = unordered_set<AnyGroundAtom>;

class GroundFunctionExpressionNumberImpl;
using GroundFunctionExpressionNumber = const GroundFunctionExpressionNumberImpl*;
class GroundFunctionExpressionBinaryOperatorImpl;
using GroundFunctionExpressionBinaryOperator = const GroundFunctionExpressionBinaryOperatorImpl*;
class GroundFunctionExpressionMultiOperatorImpl;
using GroundFunctionExpressionMultiOperator = const GroundFunctionExpressionMultiOperatorImpl*;
class GroundFunctionExpressionMinusImpl;
using GroundFunctionExpressionMinus = const GroundFunctionExpressionMinusImpl*;
class GroundFunctionExpressionFunctionImpl;
using GroundFunctionExpressionFunction = const GroundFunctionExpressionFunctionImpl*;
using GroundFunctionExpressionImpl = std::variant<GroundFunctionExpressionNumberImpl,
                                                  GroundFunctionExpressionBinaryOperatorImpl,
                                                  GroundFunctionExpressionMultiOperatorImpl,
                                                  GroundFunctionExpressionMinusImpl,
                                                  GroundFunctionExpressionFunctionImpl>;
using GroundFunctionExpression = const GroundFunctionExpressionImpl*;
using GroundFunctionExpressionList = vector<GroundFunctionExpression>;

class GroundFunctionImpl;
using GroundFunction = const GroundFunctionImpl*;
using GroundFunctionList = vector<GroundFunction>;

template<PredicateTag P>
class GroundLiteralImpl;
template<PredicateTag P>
using GroundLiteral = const GroundLiteralImpl<P>*;
using AnyGroundLiteral = std::variant<GroundLiteral<Fluent>, GroundLiteral<Derived>, GroundLiteral<Static>>;
template<PredicateTag P>
using GroundLiteralList = vector<GroundLiteral<P>>;
template<PredicateTag P>
using GroundLiteralSpan = std::span<GroundLiteral<P>>;
using AnyGroundLiteralList = vector<AnyGroundLiteral>;
template<PredicateTag P>
using GroundLiteralSet = unordered_set<GroundLiteral<P>>;
using AnyGroundLiteralSet = unordered_set<AnyGroundLiteral>;

template<PredicateTag P>
class LiteralImpl;
template<PredicateTag P>
using Literal = const LiteralImpl<P>*;
using AnyLiteral = std::variant<Literal<Fluent>, Literal<Derived>, Literal<Static>>;
template<PredicateTag P>
using LiteralList = vector<Literal<P>>;
template<PredicateTag P>
using LiteralSpan = std::span<Literal<P>>;
using AnyLiteralList = vector<AnyLiteral>;
template<PredicateTag P>
using LiteralSet = unordered_set<Literal<P>>;
using AnyLiteralSet = unordered_set<AnyLiteral>;

class OptimizationMetricImpl;
using OptimizationMetric = const OptimizationMetricImpl*;

class NumericFluentImpl;
using NumericFluent = const NumericFluentImpl*;
using NumericFluentList = vector<NumericFluent>;

struct ObjectImpl;
using Object = const ObjectImpl*;
using ObjectList = vector<Object>;
template<typename Key, typename Hash = std::hash<Key>, typename KeyEqual = std::equal_to<Key>>
using ToObjectMap = unordered_map<Key, Object, Hash, KeyEqual>;

class PDDLRepositories;

template<PredicateTag P>
class PredicateImpl;
template<PredicateTag P>
using Predicate = const PredicateImpl<P>*;
using AnyPredicate = std::variant<Predicate<Fluent>, Predicate<Derived>, Predicate<Static>>;
template<PredicateTag P>
using PredicateList = vector<Predicate<P>>;
template<PredicateTag P>
using PredicateSpan = std::span<Predicate<P>>;
using AnyPredicateList = vector<AnyPredicate>;
template<PredicateTag P>
using PredicateSet = unordered_set<Predicate<P>>;
using AnyPredicateSet = unordered_set<AnyPredicate>;
template<typename Key, PredicateTag P, typename Hash = std::hash<Key>, typename KeyEqual = std::equal_to<Key>>
using ToPredicateMap = unordered_map<Key, Predicate<P>, Hash, KeyEqual>;

class ProblemImpl;
using Problem = const ProblemImpl*;
using ProblemList = vector<Problem>;

class RequirementsImpl;
using Requirements = const RequirementsImpl*;

class TermObjectImpl;
using TermObject = const TermObjectImpl*;
class TermVariableImpl;
using TermVariable = const TermVariableImpl*;
using TermImpl = std::variant<TermObjectImpl, TermVariableImpl>;
using Term = const TermImpl*;
using TermList = vector<Term>;

struct VariableImpl;
using Variable = const VariableImpl*;
using VariableList = vector<Variable>;
using VariableSet = unordered_set<Variable>;

}
