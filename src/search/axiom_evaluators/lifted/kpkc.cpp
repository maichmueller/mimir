/*
 * Copyright (C) 2023 Dominik Drexler and Simon Stahlberg
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

#include "mimir/search/axiom_evaluators/lifted/kpkc.hpp"

#include "mimir/algorithms/kpkc.hpp"
#include "mimir/formalism/conjunctive_condition.hpp"
#include "mimir/formalism/domain.hpp"
#include "mimir/formalism/formatter.hpp"
#include "mimir/formalism/function.hpp"
#include "mimir/formalism/function_expressions.hpp"
#include "mimir/formalism/problem.hpp"
#include "mimir/formalism/repositories.hpp"
#include "mimir/formalism/type.hpp"
#include "mimir/search/applicability.hpp"
#include "mimir/search/assignment_set_utils.hpp"
#include "mimir/search/axiom_evaluators/lifted/kpkc/event_handlers/default.hpp"
#include "mimir/search/axiom_evaluators/lifted/kpkc/event_handlers/interface.hpp"
#include "mimir/search/satisficing_binding_generators/event_handlers/default.hpp"
#include "mimir/search/state_unpacked.hpp"

#include <absl/container/flat_hash_map.h>

using namespace mimir::formalism;

namespace mimir::search
{
namespace
{
struct IndexListHash
{
    size_t operator()(const IndexList& list) const
    {
        auto seed = list.size();
        for (const auto index : list)
        {
            loki::hash_combine(seed, index);
        }
        return seed;
    }
};

template<IsStaticOrFluentOrDerivedTag P>
using GroundAtomIndexLookup = std::vector<absl::flat_hash_map<IndexList, Index, IndexListHash>>;

template<IsStaticOrFluentOrAuxiliaryTag F>
using GroundFunctionIndexLookup = std::vector<absl::flat_hash_map<IndexList, Index, IndexListHash>>;

constexpr auto kMissingIndex = MAX_INDEX;

template<IsStaticOrFluentOrDerivedTag P>
void mark_predicate(Predicate<P> predicate, std::vector<bool>& marked)
{
    if (predicate->get_index() >= marked.size())
    {
        marked.resize(predicate->get_index() + 1, false);
    }
    marked[predicate->get_index()] = true;
}

template<IsStaticOrFluentOrAuxiliaryTag F>
void mark_function_skeleton(FunctionSkeleton<F> function_skeleton, std::vector<bool>& marked)
{
    if (function_skeleton->get_index() >= marked.size())
    {
        marked.resize(function_skeleton->get_index() + 1, false);
    }
    marked[function_skeleton->get_index()] = true;
}

void collect_function_skeletons(FunctionExpression function_expression,
                                std::vector<bool>& static_function_skeletons,
                                std::vector<bool>& fluent_function_skeletons)
{
    std::visit(
        [&](auto&& arg)
        {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, FunctionExpressionNumber>)
            {
                return;
            }
            else if constexpr (std::is_same_v<T, FunctionExpressionBinaryOperator>)
            {
                collect_function_skeletons(arg->get_left_function_expression(), static_function_skeletons, fluent_function_skeletons);
                collect_function_skeletons(arg->get_right_function_expression(), static_function_skeletons, fluent_function_skeletons);
            }
            else if constexpr (std::is_same_v<T, FunctionExpressionMultiOperator>)
            {
                for (const auto& child : arg->get_function_expressions())
                {
                    collect_function_skeletons(child, static_function_skeletons, fluent_function_skeletons);
                }
            }
            else if constexpr (std::is_same_v<T, FunctionExpressionMinus>)
            {
                collect_function_skeletons(arg->get_function_expression(), static_function_skeletons, fluent_function_skeletons);
            }
            else if constexpr (std::is_same_v<T, FunctionExpressionFunction<StaticTag>>)
            {
                mark_function_skeleton(arg->get_function()->get_function_skeleton(), static_function_skeletons);
            }
            else if constexpr (std::is_same_v<T, FunctionExpressionFunction<FluentTag>>)
            {
                mark_function_skeleton(arg->get_function()->get_function_skeleton(), fluent_function_skeletons);
            }
            else
            {
                static_assert(dependent_false<T>::value, "Missing function expression case.");
            }
        },
        function_expression->get_variant());
}

void collect_axiom_lookup_requirements(const Problem& problem,
                                       std::vector<bool>& static_predicates,
                                       std::vector<bool>& fluent_predicates,
                                       std::vector<bool>& derived_predicates,
                                       std::vector<bool>& static_function_skeletons,
                                       std::vector<bool>& fluent_function_skeletons)
{
    for (const auto& axiom : problem->get_problem_and_domain_axioms())
    {
        const auto condition = axiom->get_conjunctive_condition();
        for (const auto& literal : condition->get_literals<StaticTag>())
        {
            mark_predicate(literal->get_atom()->get_predicate(), static_predicates);
        }
        for (const auto& literal : condition->get_literals<FluentTag>())
        {
            mark_predicate(literal->get_atom()->get_predicate(), fluent_predicates);
        }
        for (const auto& literal : condition->get_literals<DerivedTag>())
        {
            mark_predicate(literal->get_atom()->get_predicate(), derived_predicates);
        }
        for (const auto& constraint : condition->get_numeric_constraints())
        {
            collect_function_skeletons(constraint->get_left_function_expression(), static_function_skeletons, fluent_function_skeletons);
            collect_function_skeletons(constraint->get_right_function_expression(), static_function_skeletons, fluent_function_skeletons);
        }
        mark_predicate(axiom->get_literal()->get_atom()->get_predicate(), derived_predicates);
    }
}

template<typename ParameterLike, typename OnBinding>
void for_each_type_legal_binding(const Problem& problem,
                                 const ParameterLike& parameters,
                                 size_t parameter_index,
                                 ObjectList& object_binding,
                                 IndexList& object_indices,
                                 OnBinding&& on_binding)
{
    if (parameter_index == parameters.size())
    {
        on_binding(object_binding, object_indices);
        return;
    }

    const auto& parameter = parameters[parameter_index];
    for (const auto& object : problem->get_problem_and_domain_objects())
    {
        if (!is_subtypeeq(object->get_bases(), parameter->get_bases()))
        {
            continue;
        }

        object_binding.push_back(object);
        object_indices.push_back(object->get_index());
        for_each_type_legal_binding(problem, parameters, parameter_index + 1, object_binding, object_indices, on_binding);
        object_indices.pop_back();
        object_binding.pop_back();
    }
}

template<IsStaticOrFluentOrDerivedTag P>
void build_ground_atom_lookup(const Problem& problem, Predicate<P> predicate, GroundAtomIndexLookup<P>& lookup_tables)
{
    if (predicate->get_index() >= lookup_tables.size())
    {
        lookup_tables.resize(predicate->get_index() + 1);
    }

    auto& lookup = lookup_tables[predicate->get_index()];
    if (!lookup.empty() || predicate->get_arity() == 0)
    {
        if (lookup.empty() && predicate->get_arity() == 0)
        {
            const auto atom = problem->get_or_create_ground_atom(predicate, ObjectList {});
            lookup.emplace(IndexList {}, atom->get_index());
        }
        return;
    }

    auto binding = ObjectList {};
    auto binding_indices = IndexList {};
    for_each_type_legal_binding(problem,
                                predicate->get_parameters(),
                                0,
                                binding,
                                binding_indices,
                                [&](const ObjectList& object_binding, const IndexList& object_indices)
                                {
                                    const auto atom = problem->get_or_create_ground_atom(predicate, object_binding);
                                    lookup.emplace(object_indices, atom->get_index());
                                });
}

template<IsStaticOrFluentOrAuxiliaryTag F>
void build_ground_function_lookup(const Problem& problem, FunctionSkeleton<F> function_skeleton, GroundFunctionIndexLookup<F>& lookup_tables)
{
    if (function_skeleton->get_index() >= lookup_tables.size())
    {
        lookup_tables.resize(function_skeleton->get_index() + 1);
    }

    auto& lookup = lookup_tables[function_skeleton->get_index()];
    if (!lookup.empty() || function_skeleton->get_arity() == 0)
    {
        if (lookup.empty() && function_skeleton->get_arity() == 0)
        {
            auto& repositories = const_cast<Repositories&>(problem->get_repositories());
            const auto function = repositories.get_or_create_ground_function(function_skeleton, ObjectList {});
            lookup.emplace(IndexList {}, function->get_index());
        }
        return;
    }

    auto binding = ObjectList {};
    auto binding_indices = IndexList {};
    for_each_type_legal_binding(problem,
                                function_skeleton->get_parameters(),
                                0,
                                binding,
                                binding_indices,
                                [&](const ObjectList& object_binding, const IndexList& object_indices)
                                {
                                    auto& repositories = const_cast<Repositories&>(problem->get_repositories());
                                    const auto function = repositories.get_or_create_ground_function(function_skeleton, object_binding);
                                    lookup.emplace(object_indices, function->get_index());
                                });
}

Index resolve_term_index(Term term, const ObjectList& binding)
{
    return std::visit(
        [&](auto&& arg) -> Index
        {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, Object>)
            {
                return arg->get_index();
            }
            else if constexpr (std::is_same_v<T, Variable>)
            {
                return binding[arg->get_parameter_index()]->get_index();
            }
            else
            {
                static_assert(dependent_false<T>::value, "Missing term variant.");
            }
        },
        term->get_variant());
}

void resolve_terms_to_indices(const TermList& terms, const ObjectList& binding, IndexList& out_indices)
{
    out_indices.clear();
    out_indices.reserve(terms.size());
    for (const auto& term : terms)
    {
        out_indices.push_back(resolve_term_index(term, binding));
    }
}

template<IsStaticOrFluentOrDerivedTag P>
Index lookup_ground_atom_index(const GroundAtomIndexLookup<P>& lookup_tables,
                               Predicate<P> predicate,
                               const TermList& terms,
                               const ObjectList& binding,
                               IndexList& scratch_indices)
{
    resolve_terms_to_indices(terms, binding, scratch_indices);
    const auto& lookup = lookup_tables.at(predicate->get_index());
    const auto it = lookup.find(scratch_indices);
    return (it == lookup.end()) ? kMissingIndex : it->second;
}

template<IsStaticOrFluentOrAuxiliaryTag F>
Index lookup_ground_function_index(const GroundFunctionIndexLookup<F>& lookup_tables,
                                   FunctionSkeleton<F> function_skeleton,
                                   const TermList& terms,
                                   const ObjectList& binding,
                                   IndexList& scratch_indices)
{
    resolve_terms_to_indices(terms, binding, scratch_indices);
    const auto& lookup = lookup_tables.at(function_skeleton->get_index());
    const auto it = lookup.find(scratch_indices);
    return (it == lookup.end()) ? kMissingIndex : it->second;
}

bool evaluate_comparator(loki::BinaryComparatorEnum comparator, ContinuousCost lhs, ContinuousCost rhs)
{
    if (std::isnan(lhs) || std::isnan(rhs))
    {
        return false;
    }

    switch (comparator)
    {
        case loki::BinaryComparatorEnum::EQUAL:
            return lhs == rhs;
        case loki::BinaryComparatorEnum::UNEQUAL:
            return lhs != rhs;
        case loki::BinaryComparatorEnum::GREATER:
            return lhs > rhs;
        case loki::BinaryComparatorEnum::GREATER_EQUAL:
            return lhs >= rhs;
        case loki::BinaryComparatorEnum::LESS:
            return lhs < rhs;
        case loki::BinaryComparatorEnum::LESS_EQUAL:
            return lhs <= rhs;
        default:
            throw std::logic_error("evaluate_comparator: Unexpected BinaryComparatorEnum.");
    }
}

class ParallelKPKCAxiomGrounder
{
public:
    explicit ParallelKPKCAxiomGrounder(const AxiomSatisficingBindingGenerator& source) :
        m_conjunctive_condition(source.get_conjunctive_condition()),
        m_static_consistency_graph(&source.get_static_consistency_graph()),
        m_full_consistency_graph(m_static_consistency_graph->get_vertices().size(),
                                 boost::dynamic_bitset<>(m_static_consistency_graph->get_vertices().size())),
        m_index_scratch()
    {
    }

    template<typename LookupTablesT, typename OnBinding>
    void for_each_binding(const Problem& problem,
                          const UnpackedStateImpl& unpacked_state,
                          const DynamicAssignmentSets& dynamic_assignment_sets,
                          const LookupTablesT& lookup_tables,
                          OnBinding&& on_binding)
    {
        auto vertex_mask = std::optional<boost::dynamic_bitset<>> { std::nullopt };

        if (m_conjunctive_condition->get_arity() == 0)
        {
            auto binding = ObjectList {};
            if (is_valid_binding(unpacked_state, lookup_tables, binding))
            {
                on_binding(binding);
            }
            return;
        }

        if (m_conjunctive_condition->get_arity() == 1)
        {
            for (const auto& vertex : m_static_consistency_graph->consistent_vertices(problem->get_static_assignment_sets(), dynamic_assignment_sets, vertex_mask))
            {
                auto binding = ObjectList { problem->get_repositories().get_object(vertex.get_object_index()) };
                if (is_valid_binding(unpacked_state, lookup_tables, binding))
                {
                    on_binding(binding);
                }
            }
            return;
        }

        clear_full_consistency_graph();
        for (const auto& edge : m_static_consistency_graph->consistent_edges(problem->get_static_assignment_sets(), dynamic_assignment_sets, vertex_mask))
        {
            const auto first_index = edge.get_src().get_index();
            const auto second_index = edge.get_dst().get_index();
            m_full_consistency_graph[first_index][second_index] = true;
            m_full_consistency_graph[second_index][first_index] = true;
        }

        const auto& vertices = m_static_consistency_graph->get_vertices();
        const auto& partitions = m_static_consistency_graph->get_vertices_by_parameter_index();
        for (const auto& clique : create_k_clique_in_k_partite_graph_generator(m_full_consistency_graph, partitions))
        {
            auto binding = ObjectList(clique.size());
            for (size_t index = 0; index < clique.size(); ++index)
            {
                const auto& vertex = vertices[clique[index]];
                binding[vertex.get_parameter_index()] = problem->get_problem_and_domain_objects()[vertex.get_object_index()];
            }
            if (is_valid_binding(unpacked_state, lookup_tables, binding))
            {
                on_binding(binding);
            }
        }
    }

private:
    template<typename LookupTablesT>
    bool is_valid_binding(const UnpackedStateImpl& unpacked_state, const LookupTablesT& lookup_tables, const ObjectList& binding)
    {
        for (const auto& literal : m_conjunctive_condition->get_literals<StaticTag>())
        {
            const auto atom_index = lookup_ground_atom_index(lookup_tables.static_predicates,
                                                             literal->get_atom()->get_predicate(),
                                                             literal->get_atom()->get_terms(),
                                                             binding,
                                                             m_index_scratch);
            if (atom_index == kMissingIndex || literal->get_polarity() != unpacked_state.get_problem().get_positive_static_initial_atoms_bitset().get(atom_index))
            {
                return false;
            }
        }

        for (const auto& literal : m_conjunctive_condition->get_literals<FluentTag>())
        {
            const auto atom_index = lookup_ground_atom_index(lookup_tables.fluent_predicates,
                                                             literal->get_atom()->get_predicate(),
                                                             literal->get_atom()->get_terms(),
                                                             binding,
                                                             m_index_scratch);
            if (atom_index == kMissingIndex || literal->get_polarity() != unpacked_state.get_atoms<FluentTag>().get(atom_index))
            {
                return false;
            }
        }

        for (const auto& literal : m_conjunctive_condition->get_literals<DerivedTag>())
        {
            const auto atom_index = lookup_ground_atom_index(lookup_tables.derived_predicates,
                                                             literal->get_atom()->get_predicate(),
                                                             literal->get_atom()->get_terms(),
                                                             binding,
                                                             m_index_scratch);
            if (atom_index == kMissingIndex || literal->get_polarity() != unpacked_state.get_atoms<DerivedTag>().get(atom_index))
            {
                return false;
            }
        }

        for (const auto& numeric_constraint : m_conjunctive_condition->get_numeric_constraints())
        {
            if (!evaluate_numeric_constraint(unpacked_state, lookup_tables, numeric_constraint, binding))
            {
                return false;
            }
        }

        return true;
    }

    template<typename LookupTablesT>
    bool evaluate_numeric_constraint(const UnpackedStateImpl& unpacked_state,
                                     const LookupTablesT& lookup_tables,
                                     NumericConstraint numeric_constraint,
                                     const ObjectList& binding)
    {
        const auto lhs = evaluate_function_expression(unpacked_state,
                                                      lookup_tables,
                                                      numeric_constraint->get_left_function_expression(),
                                                      binding);
        const auto rhs = evaluate_function_expression(unpacked_state,
                                                      lookup_tables,
                                                      numeric_constraint->get_right_function_expression(),
                                                      binding);
        return evaluate_comparator(numeric_constraint->get_binary_comparator(), lhs, rhs);
    }

    template<typename LookupTablesT>
    ContinuousCost evaluate_function_expression(const UnpackedStateImpl& unpacked_state,
                                                const LookupTablesT& lookup_tables,
                                                FunctionExpression function_expression,
                                                const ObjectList& binding)
    {
        return std::visit(
            [&](auto&& arg) -> ContinuousCost
            {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, FunctionExpressionNumber>)
                {
                    return arg->get_number();
                }
                else if constexpr (std::is_same_v<T, FunctionExpressionBinaryOperator>)
                {
                    return evaluate_binary(arg->get_binary_operator(),
                                           evaluate_function_expression(unpacked_state, lookup_tables, arg->get_left_function_expression(), binding),
                                           evaluate_function_expression(unpacked_state, lookup_tables, arg->get_right_function_expression(), binding));
                }
                else if constexpr (std::is_same_v<T, FunctionExpressionMultiOperator>)
                {
                    if (arg->get_function_expressions().empty())
                    {
                        return UNDEFINED_CONTINUOUS_COST;
                    }

                    auto value = evaluate_function_expression(unpacked_state,
                                                              lookup_tables,
                                                              arg->get_function_expressions().front(),
                                                              binding);
                    for (size_t i = 1; i < arg->get_function_expressions().size(); ++i)
                    {
                        value = evaluate_multi(arg->get_multi_operator(),
                                               value,
                                               evaluate_function_expression(unpacked_state,
                                                                            lookup_tables,
                                                                            arg->get_function_expressions()[i],
                                                                            binding));
                    }
                    return value;
                }
                else if constexpr (std::is_same_v<T, FunctionExpressionMinus>)
                {
                    const auto value = evaluate_function_expression(unpacked_state, lookup_tables, arg->get_function_expression(), binding);
                    return std::isnan(value) ? UNDEFINED_CONTINUOUS_COST : -value;
                }
                else if constexpr (std::is_same_v<T, FunctionExpressionFunction<StaticTag>>)
                {
                    const auto function_index = lookup_ground_function_index(lookup_tables.static_functions,
                                                                            arg->get_function()->get_function_skeleton(),
                                                                            arg->get_function()->get_terms(),
                                                                            binding,
                                                                            m_index_scratch);
                    if (function_index == kMissingIndex || function_index >= unpacked_state.get_problem().get_initial_function_to_value<StaticTag>().size())
                    {
                        return UNDEFINED_CONTINUOUS_COST;
                    }
                    return unpacked_state.get_problem().get_initial_function_to_value<StaticTag>()[function_index];
                }
                else if constexpr (std::is_same_v<T, FunctionExpressionFunction<FluentTag>>)
                {
                    const auto function_index = lookup_ground_function_index(lookup_tables.fluent_functions,
                                                                            arg->get_function()->get_function_skeleton(),
                                                                            arg->get_function()->get_terms(),
                                                                            binding,
                                                                            m_index_scratch);
                    if (function_index == kMissingIndex || function_index >= unpacked_state.get_numeric_variables().size())
                    {
                        return UNDEFINED_CONTINUOUS_COST;
                    }
                    return unpacked_state.get_numeric_variables()[function_index];
                }
                else
                {
                    static_assert(dependent_false<T>::value, "Missing function expression variant.");
                }
            },
            function_expression->get_variant());
    }

    void clear_full_consistency_graph()
    {
        for (auto& row : m_full_consistency_graph)
        {
            row.reset();
        }
    }

    ConjunctiveCondition m_conjunctive_condition;
    const StaticConsistencyGraph* m_static_consistency_graph;
    std::vector<boost::dynamic_bitset<>> m_full_consistency_graph;
    IndexList m_index_scratch;
};

class KPKCParallelAxiomWorkerContext final : public IParallelAxiomWorkerContext
{
public:
    KPKCParallelAxiomWorkerContext(const Problem& problem, const AxiomSatisficingBindingGeneratorList& condition_grounders) :
        dynamic_assignment_sets(*problem),
        condition_grounders_parallel(),
        applicable_head_atom_indices(),
        relevant_axioms()
    {
        condition_grounders_parallel.reserve(condition_grounders.size());
        for (const auto& condition_grounder : condition_grounders)
        {
            condition_grounders_parallel.emplace_back(condition_grounder);
        }
    }

    DynamicAssignmentSets dynamic_assignment_sets;
    std::vector<ParallelKPKCAxiomGrounder> condition_grounders_parallel;
    IndexList applicable_head_atom_indices;
    AxiomSet relevant_axioms;
    IndexList head_index_scratch;
};
}

struct KPKCLiftedAxiomEvaluatorImpl::ParallelGroundLookupTables
{
    GroundAtomIndexLookup<StaticTag> static_predicates;
    GroundAtomIndexLookup<FluentTag> fluent_predicates;
    GroundAtomIndexLookup<DerivedTag> derived_predicates;
    GroundFunctionIndexLookup<StaticTag> static_functions;
    GroundFunctionIndexLookup<FluentTag> fluent_functions;
};

/**
 * LiftedAxiomEvaluator
 */

KPKCLiftedAxiomEvaluatorImpl::KPKCLiftedAxiomEvaluatorImpl(Problem problem,
                                                           EventHandler event_handler,
                                                           satisficing_binding_generator::EventHandler binding_event_handler) :
    m_problem(problem),
    m_event_handler(event_handler ? event_handler : DefaultEventHandlerImpl::create()),
    m_binding_event_handler(binding_event_handler ? binding_event_handler : satisficing_binding_generator::DefaultEventHandlerImpl::create()),
    m_condition_grounders(),
    m_dynamic_assignment_sets(*m_problem),
    m_parallel_lookup_tables_once_flag(),
    m_parallel_lookup_tables()
{
    /* 3. Initialize condition grounders */
    const auto& axioms = m_problem->get_problem_and_domain_axioms();
    for (size_t i = 0; i < axioms.size(); ++i)
    {
        const auto& axiom = axioms[i];
        assert(axiom->get_index() == i);
        m_condition_grounders.emplace_back(AxiomSatisficingBindingGenerator(axiom, m_problem, m_binding_event_handler));
    }
}

KPKCLiftedAxiomEvaluator
KPKCLiftedAxiomEvaluatorImpl::create(Problem problem, EventHandler event_handler, satisficing_binding_generator::EventHandler binding_event_handler)
{
    return std::shared_ptr<KPKCLiftedAxiomEvaluatorImpl>(new KPKCLiftedAxiomEvaluatorImpl(problem, event_handler, binding_event_handler));
}

bool KPKCLiftedAxiomEvaluatorImpl::supports_parallel_staged_successor_evaluation() const { return true; }

void KPKCLiftedAxiomEvaluatorImpl::prepare_parallel_staged_successor_evaluation()
{
    std::call_once(
        m_parallel_lookup_tables_once_flag,
        [this]()
        {
            auto lookup_tables = std::make_shared<ParallelGroundLookupTables>();

            auto static_predicates = std::vector<bool> {};
            auto fluent_predicates = std::vector<bool> {};
            auto derived_predicates = std::vector<bool> {};
            auto static_function_skeletons = std::vector<bool> {};
            auto fluent_function_skeletons = std::vector<bool> {};

            collect_axiom_lookup_requirements(m_problem,
                                              static_predicates,
                                              fluent_predicates,
                                              derived_predicates,
                                              static_function_skeletons,
                                              fluent_function_skeletons);

            const auto& static_predicate_repository =
                boost::hana::at_key(m_problem->get_repositories().get_hana_repositories(), boost::hana::type<PredicateImpl<StaticTag>> {});
            for (Index i = 0; i < static_predicates.size(); ++i)
            {
                if (static_predicates[i])
                {
                    build_ground_atom_lookup(m_problem, static_predicate_repository.at(i), lookup_tables->static_predicates);
                }
            }

            const auto& fluent_predicate_repository =
                boost::hana::at_key(m_problem->get_repositories().get_hana_repositories(), boost::hana::type<PredicateImpl<FluentTag>> {});
            for (Index i = 0; i < fluent_predicates.size(); ++i)
            {
                if (fluent_predicates[i])
                {
                    build_ground_atom_lookup(m_problem, fluent_predicate_repository.at(i), lookup_tables->fluent_predicates);
                }
            }

            const auto& derived_predicate_repository =
                boost::hana::at_key(m_problem->get_repositories().get_hana_repositories(), boost::hana::type<PredicateImpl<DerivedTag>> {});
            for (Index i = 0; i < derived_predicates.size(); ++i)
            {
                if (derived_predicates[i])
                {
                    build_ground_atom_lookup(m_problem, derived_predicate_repository.at(i), lookup_tables->derived_predicates);
                }
            }

            const auto& static_function_repository =
                boost::hana::at_key(m_problem->get_repositories().get_hana_repositories(), boost::hana::type<FunctionSkeletonImpl<StaticTag>> {});
            for (Index i = 0; i < static_function_skeletons.size(); ++i)
            {
                if (static_function_skeletons[i])
                {
                    build_ground_function_lookup(m_problem, static_function_repository.at(i), lookup_tables->static_functions);
                }
            }

            const auto& fluent_function_repository =
                boost::hana::at_key(m_problem->get_repositories().get_hana_repositories(), boost::hana::type<FunctionSkeletonImpl<FluentTag>> {});
            for (Index i = 0; i < fluent_function_skeletons.size(); ++i)
            {
                if (fluent_function_skeletons[i])
                {
                    build_ground_function_lookup(m_problem, fluent_function_repository.at(i), lookup_tables->fluent_functions);
                }
            }

            m_parallel_lookup_tables = std::move(lookup_tables);
        });
}

ParallelAxiomWorkerContext KPKCLiftedAxiomEvaluatorImpl::create_parallel_worker_context() const
{
    assert(m_parallel_lookup_tables);
    return std::make_unique<KPKCParallelAxiomWorkerContext>(m_problem, m_condition_grounders);
}

void KPKCLiftedAxiomEvaluatorImpl::generate_and_apply_axioms(UnpackedStateImpl& unpacked_state)
{
    initialize(unpacked_state, m_dynamic_assignment_sets);

    /* 2. Fixed point computation */

    const auto& ground_axiom_repository =
        boost::hana::at_key(unpacked_state.get_problem().get_repositories().get_hana_repositories(), boost::hana::type<GroundAxiomImpl> {});

    auto applicable_axioms = GroundAxiomList {};

    for (const auto& partition : unpacked_state.get_problem().get_problem_and_domain_axiom_partitioning())
    {
        bool reached_partition_fixed_point;

        auto relevant_axioms = partition.get_initially_relevant_axioms();

        do
        {
            reached_partition_fixed_point = true;

            applicable_axioms.clear();
            for (const auto& axiom : relevant_axioms)
            {
                if (!nullary_conditions_hold(axiom->get_conjunctive_condition(), unpacked_state))
                {
                    continue;
                }

                auto& condition_grounder = m_condition_grounders.at(axiom->get_index());
                auto vertex_mask = std::optional<boost::dynamic_bitset<>> { std::nullopt };

                for (auto&& binding : condition_grounder.create_binding_generator(unpacked_state, m_dynamic_assignment_sets, vertex_mask))
                {
                    const auto num_ground_axioms = ground_axiom_repository.size();
                    const auto ground_axiom = m_problem->ground(axiom, std::move(binding));

                    assert(is_applicable(ground_axiom, unpacked_state));

                    m_event_handler->on_ground_axiom(ground_axiom);

                    (ground_axiom_repository.size() > num_ground_axioms) ? m_event_handler->on_ground_axiom_cache_miss(ground_axiom) :
                                                                           m_event_handler->on_ground_axiom_cache_hit(ground_axiom);

                    applicable_axioms.emplace_back(ground_axiom);
                }
            }

            relevant_axioms.clear();

            for (const auto& grounded_axiom : applicable_axioms)
            {
                assert(grounded_axiom->get_literal()->get_polarity());

                const auto grounded_atom_index = grounded_axiom->get_literal()->get_atom()->get_index();

                if (!unpacked_state.get_atoms<DerivedTag>().get(grounded_atom_index))
                {
                    const auto new_ground_atom = m_problem->get_repositories().get_ground_atom<DerivedTag>(grounded_atom_index);
                    reached_partition_fixed_point = false;

                    m_dynamic_assignment_sets.derived_predicate_assignment_sets.insert_ground_atom(new_ground_atom);
                    unpacked_state.get_atoms<DerivedTag>().set(grounded_atom_index);

                    partition.retrieve_axioms_with_same_body_predicate(new_ground_atom, relevant_axioms);
                }
            }
        } while (!reached_partition_fixed_point);
    }

    m_event_handler->on_end_generating_applicable_axioms();
}

void KPKCLiftedAxiomEvaluatorImpl::generate_and_apply_axioms_parallel(UnpackedStateImpl& unpacked_state,
                                                                      IParallelAxiomWorkerContext& worker_context) const
{
    assert(m_parallel_lookup_tables);

    auto& context = static_cast<KPKCParallelAxiomWorkerContext&>(worker_context);
    initialize(unpacked_state, context.dynamic_assignment_sets);

    for (const auto& partition : unpacked_state.get_problem().get_problem_and_domain_axiom_partitioning())
    {
        auto reached_partition_fixed_point = false;
        context.relevant_axioms = partition.get_initially_relevant_axioms();

        do
        {
            reached_partition_fixed_point = true;
            context.applicable_head_atom_indices.clear();

            for (const auto& axiom : context.relevant_axioms)
            {
                if (!nullary_conditions_hold(axiom->get_conjunctive_condition(), unpacked_state))
                {
                    continue;
                }

                auto& condition_grounder = context.condition_grounders_parallel.at(axiom->get_index());
                condition_grounder.for_each_binding(
                    m_problem,
                    unpacked_state,
                    context.dynamic_assignment_sets,
                    *m_parallel_lookup_tables,
                    [&](const ObjectList& binding)
                    {
                        const auto head_index = lookup_ground_atom_index(m_parallel_lookup_tables->derived_predicates,
                                                                         axiom->get_literal()->get_atom()->get_predicate(),
                                                                         axiom->get_literal()->get_atom()->get_terms(),
                                                                         binding,
                                                                         context.head_index_scratch);
                        if (head_index != kMissingIndex)
                        {
                            context.applicable_head_atom_indices.push_back(head_index);
                        }
                    });
            }

            context.relevant_axioms.clear();
            for (const auto grounded_atom_index : context.applicable_head_atom_indices)
            {
                if (!unpacked_state.get_atoms<DerivedTag>().get(grounded_atom_index))
                {
                    const auto new_ground_atom = m_problem->get_repositories().get_ground_atom<DerivedTag>(grounded_atom_index);
                    reached_partition_fixed_point = false;
                    context.dynamic_assignment_sets.derived_predicate_assignment_sets.insert_ground_atom(new_ground_atom);
                    unpacked_state.get_atoms<DerivedTag>().set(grounded_atom_index);
                    partition.retrieve_axioms_with_same_body_predicate(new_ground_atom, context.relevant_axioms);
                }
            }
        } while (!reached_partition_fixed_point);
    }
}

void KPKCLiftedAxiomEvaluatorImpl::on_finish_search_layer()
{
    m_event_handler->on_finish_search_layer();
    m_binding_event_handler->on_finish_search_layer();
}

void KPKCLiftedAxiomEvaluatorImpl::on_end_search()
{
    m_event_handler->on_end_search();
    m_binding_event_handler->on_end_search();
}

const Problem& KPKCLiftedAxiomEvaluatorImpl::get_problem() const { return m_problem; }

const KPKCLiftedAxiomEvaluatorImpl::EventHandler& KPKCLiftedAxiomEvaluatorImpl::get_event_handler() const { return m_event_handler; }
}
