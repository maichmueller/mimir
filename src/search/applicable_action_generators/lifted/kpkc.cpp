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

#include "mimir/search/applicable_action_generators/lifted/kpkc.hpp"

#include "mimir/common/formatter.hpp"
#include "mimir/datasets/object_graph.hpp"
#include "mimir/formalism/domain.hpp"
#include "mimir/formalism/effects.hpp"
#include "mimir/formalism/formatter.hpp"
#include "mimir/formalism/function_expressions.hpp"
#include "mimir/formalism/ground_action.hpp"
#include "mimir/formalism/object.hpp"
#include "mimir/formalism/problem.hpp"
#include "mimir/formalism/repositories.hpp"
#include "mimir/graphs/algorithms/color_refinement.hpp"
#include "mimir/graphs/algorithms/nauty.hpp"
#include "mimir/graphs/formatter.hpp"
#include "mimir/algorithms/BS_thread_pool.hpp"
#include "mimir/search/applicability.hpp"
#include "mimir/search/applicable_action_generators/lifted/kpkc/event_handlers/default.hpp"
#include "mimir/search/applicable_action_generators/lifted/kpkc/event_handlers/interface.hpp"
#include "mimir/search/algorithms/strategies/layer_ordering_strategy.hpp"
#include "mimir/search/algorithms/strategies/pruning_strategy.hpp"
#include "mimir/search/assignment_set_utils.hpp"
#include "mimir/search/axiom_evaluators/interface.hpp"
#include "mimir/search/satisficing_binding_generators/event_handlers/default.hpp"
#include "mimir/search/satisficing_binding_generators/event_handlers/interface.hpp"
#include "mimir/search/satisficing_binding_generators/base_impl.hpp"
#include "mimir/search/state.hpp"
#include "mimir/search/state_repository.hpp"

#include <absl/container/flat_hash_map.h>
#include <boost/dynamic_bitset.hpp>
#include <chrono>
#include <future>
#include <stdexcept>
#include <vector>

using namespace mimir::formalism;

using namespace std::string_literals;

namespace mimir::search
{
namespace
{
struct ParallelActionSchemaTask
{
    size_t schema_index;
    std::optional<boost::dynamic_bitset<>> vertex_mask;
};

struct ParallelActionSchemaResult
{
    size_t schema_index;
    size_t binding_arity;
    size_t num_candidate_bindings;
    IndexList candidate_binding_object_indices;
};

struct ParallelActionPartitionResult
{
    std::vector<ParallelActionSchemaResult> schema_results;
};

template<typename Candidate>
struct LocalBeamRanking
{
    bool prefer_higher_scores;

    bool better(const Candidate& lhs, const Candidate& rhs) const
    {
        if (lhs.score != rhs.score)
        {
            return prefer_higher_scores ? (lhs.score > rhs.score) : (lhs.score < rhs.score);
        }

        if (lhs.tie_token != rhs.tie_token)
        {
            return lhs.tie_token < rhs.tie_token;
        }

        return lhs.generation_sequence < rhs.generation_sequence;
    }
};

template<typename Candidate, typename RejectFn>
void add_candidate_to_small_local_beam(std::vector<Candidate>& candidates,
                                       Candidate candidate,
                                       size_t beam_width,
                                       const LocalBeamRanking<Candidate>& ranking,
                                       RejectFn&& reject_candidate)
{
    if ((candidates.size() >= beam_width) && !ranking.better(candidate, candidates.back()))
    {
        reject_candidate(candidate);
        return;
    }

    auto insert_it = candidates.begin();
    while ((insert_it != candidates.end()) && ranking.better(*insert_it, candidate))
    {
        ++insert_it;
    }
    candidates.insert(insert_it, std::move(candidate));

    if (candidates.size() > beam_width)
    {
        auto evicted = std::move(candidates.back());
        candidates.pop_back();
        reject_candidate(evicted);
    }
}

template<typename Candidate>
struct LocalBeamHeapCompare
{
    LocalBeamRanking<Candidate> ranking;

    bool operator()(const Candidate& lhs, const Candidate& rhs) const { return ranking.better(lhs, rhs); }
};

template<typename Candidate, typename RejectFn>
void add_candidate_to_heap_local_beam(std::vector<Candidate>& candidates,
                                      Candidate candidate,
                                      size_t beam_width,
                                      const LocalBeamRanking<Candidate>& ranking,
                                      const LocalBeamHeapCompare<Candidate>& heap_compare,
                                      RejectFn&& reject_candidate)
{
    if (candidates.size() < beam_width)
    {
        candidates.push_back(std::move(candidate));
        std::push_heap(candidates.begin(), candidates.end(), heap_compare);
        return;
    }

    if (!ranking.better(candidate, candidates.front()))
    {
        reject_candidate(candidate);
        return;
    }

    std::pop_heap(candidates.begin(), candidates.end(), heap_compare);
    auto evicted = std::move(candidates.back());
    candidates.pop_back();
    reject_candidate(evicted);

    candidates.push_back(std::move(candidate));
    std::push_heap(candidates.begin(), candidates.end(), heap_compare);
}

inline uint64_t make_generation_sequence(uint32_t task_order, uint32_t local_index)
{
    return (static_cast<uint64_t>(task_order) << 32U) | static_cast<uint64_t>(local_index);
}

inline uint64_t splitmix64(uint64_t x)
{
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27U)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31U);
}

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

enum class PartialBindingMaskStatus
{
    COMPATIBLE,
    IMPOSSIBLE,
};

std::pair<PartialBindingMaskStatus, std::optional<boost::dynamic_bitset<>>>
create_partial_binding_vertex_mask(ActionSatisficingBindingGenerator& condition_grounder, const PartialGroundActionSeed& seed)
{
    const auto action = condition_grounder.get_action();
    const auto arity = action->get_arity();

    if (seed.action_schema != action)
    {
        throw std::invalid_argument("create_partial_binding_vertex_mask: action schema mismatch.");
    }

    if (seed.bound_parameter_object_indices.size() != arity || seed.parameter_is_bound.size() != arity)
    {
        throw std::invalid_argument("create_partial_binding_vertex_mask: seed arrays must match the action arity.");
    }

    if (arity == 0)
    {
        return { PartialBindingMaskStatus::COMPATIBLE, std::nullopt };
    }

    auto has_bound_parameter = false;
    for (size_t parameter_index = 0; parameter_index < arity; ++parameter_index)
    {
        if (seed.parameter_is_bound[parameter_index])
        {
            has_bound_parameter = true;
            break;
        }
    }

    if (!has_bound_parameter)
    {
        return { PartialBindingMaskStatus::COMPATIBLE, std::nullopt };
    }

    const auto& static_consistency_graph = condition_grounder.get_static_consistency_graph();
    auto vertex_mask = boost::dynamic_bitset<>(static_consistency_graph.get_num_vertices());

    const auto& vertices_by_parameter_index = static_consistency_graph.get_vertices_by_parameter_index();
    const auto& objects_by_parameter_index = static_consistency_graph.get_objects_by_parameter_index();

    for (size_t parameter_index = 0; parameter_index < arity; ++parameter_index)
    {
        if (!seed.parameter_is_bound[parameter_index])
        {
            for (const auto vertex_index : vertices_by_parameter_index[parameter_index])
            {
                vertex_mask.set(vertex_index);
            }
            continue;
        }

        const auto required_object_index = seed.bound_parameter_object_indices[parameter_index];
        bool matched_vertex = false;
        for (size_t position = 0; position < objects_by_parameter_index[parameter_index].size(); ++position)
        {
            if (objects_by_parameter_index[parameter_index][position] != required_object_index)
            {
                continue;
            }

            vertex_mask.set(vertices_by_parameter_index[parameter_index][position]);
            matched_vertex = true;
        }

        if (!matched_vertex)
        {
            return { PartialBindingMaskStatus::IMPOSSIBLE, std::nullopt };
        }
    }

    return { PartialBindingMaskStatus::COMPATIBLE, std::optional<boost::dynamic_bitset<>>(std::move(vertex_mask)) };
}

template<IsStaticOrFluentOrDerivedTag P>
using GroundAtomIndexLookup = std::vector<absl::flat_hash_map<IndexList, Index, IndexListHash>>;

template<IsStaticOrFluentTag F>
using GroundFunctionIndexLookup = std::vector<absl::flat_hash_map<IndexList, Index, IndexListHash>>;

constexpr auto kMissingIndex = MAX_INDEX;

struct GenerationStatisticsScope
{
    KPKCLiftedApplicableActionGeneratorImpl::GenerationStatistics* statistics;
    std::chrono::steady_clock::time_point generation_start;
    std::chrono::nanoseconds dynamic_assignment_initialization_time;
    std::chrono::nanoseconds symmetry_setup_time;

    ~GenerationStatisticsScope()
    {
        const auto generation_time = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - generation_start);
        statistics->record_generation(static_cast<uint64_t>(generation_time.count()),
                                      static_cast<uint64_t>(dynamic_assignment_initialization_time.count()),
                                      static_cast<uint64_t>(symmetry_setup_time.count()));
    }
};

template<IsStaticOrFluentOrDerivedTag P>
void mark_predicate(Predicate<P> predicate, std::vector<bool>& marked)
{
    if (predicate->get_index() >= marked.size())
    {
        marked.resize(predicate->get_index() + 1, false);
    }
    marked[predicate->get_index()] = true;
}

template<IsStaticOrFluentTag F>
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
            else if constexpr (std::is_same_v<T, FunctionExpressionFunction<AuxiliaryTag>>)
            {
                throw std::logic_error(
                    "collect_function_skeletons: unexpected FunctionExpressionFunction<AuxiliaryTag> in lifted applicable-action generation.");
            }
            else
            {
                static_assert(dependent_false<T>::value, "Missing function expression case.");
            }
        },
        function_expression->get_variant());
}

void collect_action_lookup_requirements(const Problem& problem,
                                        std::vector<bool>& static_predicates,
                                        std::vector<bool>& fluent_predicates,
                                        std::vector<bool>& derived_predicates,
                                        std::vector<bool>& static_function_skeletons,
                                        std::vector<bool>& fluent_function_skeletons)
{
    const auto collect_condition = [&](ConjunctiveCondition condition)
    {
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
    };

    const auto collect_effect = [&](ConjunctiveEffect effect)
    {
        for (const auto& literal : effect->get_literals())
        {
            mark_predicate(literal->get_atom()->get_predicate(), fluent_predicates);
        }

        for (const auto& effect : effect->get_fluent_numeric_effects())
        {
            mark_function_skeleton(effect->get_function()->get_function_skeleton(), fluent_function_skeletons);
            collect_function_skeletons(effect->get_function_expression(), static_function_skeletons, fluent_function_skeletons);
        }

        if (const auto& auxiliary_effect = effect->get_auxiliary_numeric_effect(); auxiliary_effect.has_value())
        {
            collect_function_skeletons(auxiliary_effect.value()->get_function_expression(), static_function_skeletons, fluent_function_skeletons);
        }
    };

    for (const auto& action : problem->get_domain()->get_actions())
    {
        collect_condition(action->get_conjunctive_condition());

        for (const auto& conditional_effect : action->get_conditional_effects())
        {
            collect_condition(conditional_effect->get_conjunctive_condition());
            collect_effect(conditional_effect->get_conjunctive_effect());
        }
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

template<IsStaticOrFluentTag F>
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

Index resolve_term_index(Term term, const IndexList& binding_object_indices)
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
                return binding_object_indices[arg->get_parameter_index()];
            }
            else
            {
                static_assert(dependent_false<T>::value, "Missing term variant.");
            }
        },
        term->get_variant());
}

template<typename BindingT>
void resolve_terms_to_indices(const TermList& terms, const BindingT& binding, IndexList& out_indices)
{
    out_indices.clear();
    out_indices.reserve(terms.size());
    for (const auto& term : terms)
    {
        out_indices.push_back(resolve_term_index(term, binding));
    }
}

template<IsStaticOrFluentOrDerivedTag P, typename BindingT>
Index lookup_ground_atom_index(const GroundAtomIndexLookup<P>& lookup_tables,
                               Predicate<P> predicate,
                               const TermList& terms,
                               const BindingT& binding,
                               IndexList& scratch_indices)
{
    resolve_terms_to_indices(terms, binding, scratch_indices);
    const auto& lookup = lookup_tables.at(predicate->get_index());
    const auto it = lookup.find(scratch_indices);
    return (it == lookup.end()) ? kMissingIndex : it->second;
}

template<IsStaticOrFluentTag F, typename BindingT>
Index lookup_ground_function_index(const GroundFunctionIndexLookup<F>& lookup_tables,
                                   FunctionSkeleton<F> function_skeleton,
                                   const TermList& terms,
                                   const BindingT& binding,
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

class ParallelKPKCActionGrounder
{
public:
    explicit ParallelKPKCActionGrounder(const ActionSatisficingBindingGenerator& source) :
        m_action(source.get_action()),
        m_conjunctive_condition(source.get_conjunctive_condition()),
        m_fluent_numeric_changes(),
        m_auxiliary_numeric_change(detail::EffectFamily::NONE),
        m_index_scratch()
    {
    }

    template<typename LookupTablesT>
    bool test_binding(const UnpackedStateImpl& unpacked_state, const LookupTablesT& lookup_tables, const IndexList& binding_object_indices)
    {
        if (!condition_holds(unpacked_state, lookup_tables, m_conjunctive_condition, binding_object_indices))
        {
            return false;
        }

        m_fluent_numeric_changes.assign(unpacked_state.get_numeric_variables().size(), detail::EffectFamily::NONE);
        m_auxiliary_numeric_change = detail::EffectFamily::NONE;

        for (const auto& conditional_effect : m_action->get_conditional_effects())
        {
            if (!condition_holds(unpacked_state, lookup_tables, conditional_effect->get_conjunctive_condition(), binding_object_indices))
            {
                continue;
            }

            if (!effect_holds(unpacked_state, lookup_tables, conditional_effect->get_conjunctive_effect(), binding_object_indices))
            {
                return false;
            }
        }

        return true;
    }

    template<typename LookupTablesT>
    ParallelRelaxedBeamSuccessorCandidate compute_staged_successor_candidate(const State& state,
                                                                             const LookupTablesT& lookup_tables,
                                                                             const IndexList& binding_object_indices,
                                                                             ContinuousCost state_metric_value,
                                                                             const AxiomEvaluator& axiom_evaluator,
                                                                             StateRepositoryImpl::StagedSuccessorScratch& scratch)
    {
        auto unpacked_state = scratch.unpacked_state_pool.get_or_allocate(state.get_problem());
        auto& dense_fluent_atoms = unpacked_state->get_atoms<FluentTag>();
        dense_fluent_atoms = state.get_unpacked_state().get_atoms<FluentTag>();
        auto& dense_derived_atoms = unpacked_state->get_atoms<DerivedTag>();
        dense_derived_atoms = state.get_unpacked_state().get_atoms<DerivedTag>();
        auto& dense_fluent_numeric_variables = unpacked_state->get_numeric_variables();
        dense_fluent_numeric_variables = state.get_unpacked_state().get_numeric_variables();

        scratch.applied_negative_effect_atoms.unset_all();
        scratch.applied_positive_effect_atoms.unset_all();

        auto successor_metric_value = state_metric_value;
        apply_action_effects(state,
                             *unpacked_state,
                             lookup_tables,
                             binding_object_indices,
                             dense_fluent_atoms,
                             scratch.applied_negative_effect_atoms,
                             scratch.applied_positive_effect_atoms,
                             dense_fluent_numeric_variables,
                             successor_metric_value);

        if (!axiom_evaluator->get_problem()->get_problem_and_domain_axioms().empty())
        {
            dense_derived_atoms.unset_all();
            if (axiom_evaluator->supports_parallel_staged_successor_evaluation())
            {
                if (!scratch.axiom_worker_context)
                {
                    scratch.axiom_worker_context = axiom_evaluator->create_parallel_worker_context();
                }
                assert(scratch.axiom_worker_context);
                axiom_evaluator->generate_and_apply_axioms_parallel(*unpacked_state, *scratch.axiom_worker_context);
            }
            else
            {
                axiom_evaluator->generate_and_apply_axioms(*unpacked_state);
            }
        }

        auto candidate = ParallelRelaxedBeamSuccessorCandidate {};
        candidate.parent_state = &state;
        candidate.action_schema = m_action;
        candidate.binding_object_indices = binding_object_indices;
        candidate.fluent_atoms = dense_fluent_atoms;
        candidate.derived_atoms = dense_derived_atoms;
        candidate.fluent_numeric_variables = dense_fluent_numeric_variables;
        candidate.action_cost = successor_metric_value - state_metric_value;
        candidate.successor_metric_value = successor_metric_value;
        for (const auto atom_index : dense_fluent_atoms)
        {
            candidate.fluent_atom_indices.push_back(atom_index);
        }
        for (const auto atom_index : dense_derived_atoms)
        {
            candidate.derived_atom_indices.push_back(atom_index);
        }
        return candidate;
    }

    template<typename LookupTablesT>
    void collect_add_effect_fluent_atom_indices(const State& state,
                                                const LookupTablesT& lookup_tables,
                                                const IndexList& binding_object_indices,
                                                StateRepositoryImpl::StagedSuccessorScratch& scratch,
                                                iw::AtomIndexList& out_add_fluent_atom_indices)
    {
        auto& applied_positive_effect_atoms = scratch.applied_positive_effect_atoms;
        applied_positive_effect_atoms.unset_all();
        const auto& unpacked_state = state.get_unpacked_state();

        for (const auto& conditional_effect : m_action->get_conditional_effects())
        {
            if (!condition_holds(unpacked_state, lookup_tables, conditional_effect->get_conjunctive_condition(), binding_object_indices))
            {
                continue;
            }

            for (const auto& literal : conditional_effect->get_conjunctive_effect()->get_literals())
            {
                if (!literal->get_polarity())
                {
                    continue;
                }

                const auto atom_index = lookup_ground_atom_index(lookup_tables.fluent_predicates,
                                                                 literal->get_atom()->get_predicate(),
                                                                 literal->get_atom()->get_terms(),
                                                                 binding_object_indices,
                                                                 m_index_scratch);
                if (atom_index == kMissingIndex)
                {
                    continue;
                }

                applied_positive_effect_atoms.set(atom_index);
            }
        }

        out_add_fluent_atom_indices.clear();
        const auto& state_fluent_atoms = state.get_atoms<FluentTag>();
        for (const auto atom_index : applied_positive_effect_atoms)
        {
            if (!state_fluent_atoms.get(atom_index))
            {
                out_add_fluent_atom_indices.push_back(atom_index);
            }
        }
    }

private:
    template<typename LookupTablesT>
    bool condition_holds(const UnpackedStateImpl& unpacked_state,
                         const LookupTablesT& lookup_tables,
                         ConjunctiveCondition condition,
                         const IndexList& binding_object_indices)
    {
        for (const auto& literal : condition->get_literals<StaticTag>())
        {
            const auto atom_index = lookup_ground_atom_index(lookup_tables.static_predicates,
                                                             literal->get_atom()->get_predicate(),
                                                             literal->get_atom()->get_terms(),
                                                             binding_object_indices,
                                                             m_index_scratch);
            if (atom_index == kMissingIndex || literal->get_polarity() != unpacked_state.get_problem().get_positive_static_initial_atoms_bitset().get(atom_index))
            {
                return false;
            }
        }

        for (const auto& literal : condition->get_literals<FluentTag>())
        {
            const auto atom_index = lookup_ground_atom_index(lookup_tables.fluent_predicates,
                                                             literal->get_atom()->get_predicate(),
                                                             literal->get_atom()->get_terms(),
                                                             binding_object_indices,
                                                             m_index_scratch);
            if (atom_index == kMissingIndex || literal->get_polarity() != unpacked_state.get_atoms<FluentTag>().get(atom_index))
            {
                return false;
            }
        }

        for (const auto& literal : condition->get_literals<DerivedTag>())
        {
            const auto atom_index = lookup_ground_atom_index(lookup_tables.derived_predicates,
                                                             literal->get_atom()->get_predicate(),
                                                             literal->get_atom()->get_terms(),
                                                             binding_object_indices,
                                                             m_index_scratch);
            if (atom_index == kMissingIndex || literal->get_polarity() != unpacked_state.get_atoms<DerivedTag>().get(atom_index))
            {
                return false;
            }
        }

        for (const auto& numeric_constraint : condition->get_numeric_constraints())
        {
            const auto lhs =
                evaluate_function_expression(unpacked_state, lookup_tables, numeric_constraint->get_left_function_expression(), binding_object_indices);
            const auto rhs =
                evaluate_function_expression(unpacked_state, lookup_tables, numeric_constraint->get_right_function_expression(), binding_object_indices);
            if (!evaluate_comparator(numeric_constraint->get_binary_comparator(), lhs, rhs))
            {
                return false;
            }
        }

        return true;
    }

    template<typename LookupTablesT>
    bool effect_holds(const UnpackedStateImpl& unpacked_state,
                      const LookupTablesT& lookup_tables,
                      ConjunctiveEffect effect,
                      const IndexList& binding_object_indices)
    {
        for (const auto& numeric_effect : effect->get_fluent_numeric_effects())
        {
            if (!effect_holds(unpacked_state, lookup_tables, numeric_effect, binding_object_indices))
            {
                return false;
            }
        }

        if (const auto& auxiliary_numeric_effect = effect->get_auxiliary_numeric_effect(); auxiliary_numeric_effect.has_value())
        {
            if (!effect_holds(unpacked_state, lookup_tables, auxiliary_numeric_effect.value(), binding_object_indices))
            {
                return false;
            }
        }

        return true;
    }

    template<typename LookupTablesT>
    void apply_action_effects(const State& state,
                              UnpackedStateImpl& unpacked_state,
                              const LookupTablesT& lookup_tables,
                              const IndexList& binding_object_indices,
                              FlatBitset& ref_dense_fluent_atoms,
                              FlatBitset& ref_negative_applied_effects,
                              FlatBitset& ref_positive_applied_effects,
                              FlatDoubleList& ref_fluent_numeric_variables,
                              ContinuousCost& ref_successor_metric_value)
    {
        const auto& static_numeric_variables = state.get_problem().get_initial_function_to_value<StaticTag>();
        const auto& fluent_numeric_variables = state.get_numeric_variables();

        for (const auto& conditional_effect : m_action->get_conditional_effects())
        {
            if (!condition_holds(unpacked_state, lookup_tables, conditional_effect->get_conjunctive_condition(), binding_object_indices))
            {
                continue;
            }

            apply_conjunctive_effect(state,
                                     lookup_tables,
                                     conditional_effect->get_conjunctive_effect(),
                                     binding_object_indices,
                                     ref_negative_applied_effects,
                                     ref_positive_applied_effects,
                                     static_numeric_variables,
                                     fluent_numeric_variables,
                                     ref_fluent_numeric_variables,
                                     ref_successor_metric_value);
        }

        ref_dense_fluent_atoms -= ref_negative_applied_effects;
        ref_dense_fluent_atoms |= ref_positive_applied_effects;
        unpacked_state.get_atoms<FluentTag>() = ref_dense_fluent_atoms;
        unpacked_state.get_numeric_variables() = ref_fluent_numeric_variables;

        if (!state.get_problem().get_domain()->get_auxiliary_function_skeleton().has_value())
        {
            ref_successor_metric_value =
                state.get_problem().get_optimization_metric().has_value() ?
                    evaluate(state.get_problem().get_optimization_metric().value()->get_function_expression(),
                             static_numeric_variables,
                             ref_fluent_numeric_variables) :
                    ref_successor_metric_value + 1;
        }
    }

    template<typename LookupTablesT>
    void apply_conjunctive_effect(const State& state,
                                  const LookupTablesT& lookup_tables,
                                  ConjunctiveEffect effect,
                                  const IndexList& binding_object_indices,
                                  FlatBitset& ref_negative_applied_effects,
                                  FlatBitset& ref_positive_applied_effects,
                                  const FlatDoubleList& static_numeric_variables,
                                  const FlatDoubleList& fluent_numeric_variables,
                                  FlatDoubleList& ref_fluent_numeric_variables,
                                  ContinuousCost& ref_successor_metric_value)
    {
        for (const auto& literal : effect->get_literals())
        {
            const auto atom_index = lookup_ground_atom_index(lookup_tables.fluent_predicates,
                                                             literal->get_atom()->get_predicate(),
                                                             literal->get_atom()->get_terms(),
                                                             binding_object_indices,
                                                             m_index_scratch);
            if (atom_index == kMissingIndex)
            {
                continue;
            }

            if (literal->get_polarity())
            {
                ref_positive_applied_effects.set(atom_index);
            }
            else
            {
                ref_negative_applied_effects.set(atom_index);
            }
        }

        for (const auto& numeric_effect : effect->get_fluent_numeric_effects())
        {
            const auto function_index = lookup_ground_function_index(lookup_tables.fluent_functions,
                                                                     numeric_effect->get_function()->get_function_skeleton(),
                                                                     numeric_effect->get_function()->get_terms(),
                                                                     binding_object_indices,
                                                                     m_index_scratch);
            if (function_index == kMissingIndex)
            {
                continue;
            }

            if (function_index >= ref_fluent_numeric_variables.size())
            {
                ref_fluent_numeric_variables.resize(function_index + 1, UNDEFINED_CONTINUOUS_COST);
            }

            const auto value = evaluate_function_expression(state.get_unpacked_state(),
                                                            lookup_tables,
                                                            numeric_effect->get_function_expression(),
                                                            binding_object_indices);
            apply_numeric_effect({ numeric_effect->get_assign_operator(), value }, ref_fluent_numeric_variables[function_index]);
        }

        if (const auto& auxiliary_numeric_effect = effect->get_auxiliary_numeric_effect(); auxiliary_numeric_effect.has_value())
        {
            const auto value =
                evaluate_function_expression(state.get_unpacked_state(),
                                             lookup_tables,
                                             auxiliary_numeric_effect.value()->get_function_expression(),
                                             binding_object_indices);
            apply_numeric_effect({ auxiliary_numeric_effect.value()->get_assign_operator(), value }, ref_successor_metric_value);
        }
    }

    static void apply_numeric_effect(const std::pair<loki::AssignOperatorEnum, ContinuousCost>& numeric_effect, ContinuousCost& ref_value)
    {
        const auto [assign_operator, value] = numeric_effect;

        assert(!std::isnan(value));
        assert(assign_operator == loki::AssignOperatorEnum::ASSIGN || !std::isnan(ref_value));

        switch (assign_operator)
        {
            case loki::AssignOperatorEnum::ASSIGN:
                ref_value = value;
                break;
            case loki::AssignOperatorEnum::INCREASE:
                ref_value += value;
                break;
            case loki::AssignOperatorEnum::DECREASE:
                ref_value -= value;
                break;
            case loki::AssignOperatorEnum::SCALE_UP:
                ref_value *= value;
                break;
            case loki::AssignOperatorEnum::SCALE_DOWN:
                assert(value != 0);
                ref_value /= value;
                break;
            default:
                throw std::logic_error("ParallelKPKCActionGrounder::apply_numeric_effect: unexpected AssignOperatorEnum.");
        }
    }

    template<typename LookupTablesT>
    bool effect_holds(const UnpackedStateImpl& unpacked_state,
                      const LookupTablesT& lookup_tables,
                      NumericEffect<FluentTag> effect,
                      const IndexList& binding_object_indices)
    {
        const auto function_index = lookup_ground_function_index(lookup_tables.fluent_functions,
                                                                 effect->get_function()->get_function_skeleton(),
                                                                 effect->get_function()->get_terms(),
                                                                 binding_object_indices,
                                                                 m_index_scratch);
        if (function_index == kMissingIndex)
        {
            return false;
        }

        m_fluent_numeric_changes.resize(function_index + 1, detail::EffectFamily::NONE);
        auto& recorded_effect_family = m_fluent_numeric_changes.at(function_index);
        const auto effect_family = detail::get_effect_family(effect->get_assign_operator());
        if (!detail::is_compatible_effect_family(recorded_effect_family, effect_family))
        {
            return false;
        }
        recorded_effect_family = effect_family;

        const auto value =
            evaluate_function_expression(unpacked_state, lookup_tables, effect->get_function_expression(), binding_object_indices);
        const auto is_assignment_operator = (effect->get_assign_operator() == loki::AssignOperatorEnum::ASSIGN);
        const auto is_undefined_value = (function_index >= unpacked_state.get_numeric_variables().size()
                                         || std::isnan(unpacked_state.get_numeric_variables()[function_index]));

        return !std::isnan(value) && !(is_undefined_value && !is_assignment_operator);
    }

    template<typename LookupTablesT>
    bool effect_holds(const UnpackedStateImpl& unpacked_state,
                      const LookupTablesT& lookup_tables,
                      NumericEffect<AuxiliaryTag> effect,
                      const IndexList& binding_object_indices)
    {
        const auto effect_family = detail::get_effect_family(effect->get_assign_operator());
        if (!detail::is_compatible_effect_family(m_auxiliary_numeric_change, effect_family))
        {
            return false;
        }
        m_auxiliary_numeric_change = effect_family;
        const auto value =
            evaluate_function_expression(unpacked_state, lookup_tables, effect->get_function_expression(), binding_object_indices);
        return !std::isnan(value);
    }

    template<typename LookupTablesT>
    ContinuousCost evaluate_function_expression(const UnpackedStateImpl& unpacked_state,
                                                const LookupTablesT& lookup_tables,
                                                FunctionExpression function_expression,
                                                const IndexList& binding_object_indices)
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
                                           evaluate_function_expression(
                                               unpacked_state, lookup_tables, arg->get_left_function_expression(), binding_object_indices),
                                           evaluate_function_expression(
                                               unpacked_state, lookup_tables, arg->get_right_function_expression(), binding_object_indices));
                }
                else if constexpr (std::is_same_v<T, FunctionExpressionMultiOperator>)
                {
                    if (arg->get_function_expressions().empty())
                    {
                        return UNDEFINED_CONTINUOUS_COST;
                    }

                    auto value =
                        evaluate_function_expression(unpacked_state, lookup_tables, arg->get_function_expressions().front(), binding_object_indices);
                    for (size_t i = 1; i < arg->get_function_expressions().size(); ++i)
                    {
                        value = evaluate_multi(arg->get_multi_operator(),
                                               value,
                                               evaluate_function_expression(
                                                   unpacked_state, lookup_tables, arg->get_function_expressions()[i], binding_object_indices));
                    }
                    return value;
                }
                else if constexpr (std::is_same_v<T, FunctionExpressionMinus>)
                {
                    const auto value =
                        evaluate_function_expression(unpacked_state, lookup_tables, arg->get_function_expression(), binding_object_indices);
                    return std::isnan(value) ? UNDEFINED_CONTINUOUS_COST : -value;
                }
                else if constexpr (std::is_same_v<T, FunctionExpressionFunction<StaticTag>>)
                {
                    const auto function_index = lookup_ground_function_index(lookup_tables.static_functions,
                                                                            arg->get_function()->get_function_skeleton(),
                                                                            arg->get_function()->get_terms(),
                                                                            binding_object_indices,
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
                                                                            binding_object_indices,
                                                                            m_index_scratch);
                    if (function_index == kMissingIndex || function_index >= unpacked_state.get_numeric_variables().size())
                    {
                        return UNDEFINED_CONTINUOUS_COST;
                    }
                    return unpacked_state.get_numeric_variables()[function_index];
                }
                else if constexpr (std::is_same_v<T, FunctionExpressionFunction<AuxiliaryTag>>)
                {
                    throw std::logic_error(
                        "ParallelKPKCActionGrounder::evaluate_function_expression: unexpected FunctionExpressionFunction<AuxiliaryTag>.");
                }
                else
                {
                    static_assert(dependent_false<T>::value, "Missing function expression variant.");
                }
            },
            function_expression->get_variant());
    }

    Action m_action;
    ConjunctiveCondition m_conjunctive_condition;
    std::vector<detail::EffectFamily> m_fluent_numeric_changes;
    detail::EffectFamily m_auxiliary_numeric_change;
    IndexList m_index_scratch;
};

class KPKCParallelApplicableActionWorkerContext final : public IParallelApplicableActionGeneratorWorkerContext
{
public:
    const KPKCLiftedApplicableActionGeneratorImpl* owner;
    ActionSatisficingBindingGeneratorList action_grounding_data;
    std::vector<ParallelKPKCActionGrounder> action_validators;
    StateRepositoryImpl::StagedSuccessorScratch successor_scratch;

    KPKCParallelApplicableActionWorkerContext(const KPKCLiftedApplicableActionGeneratorImpl* owner,
                                             const ActionSatisficingBindingGeneratorList& action_grounding_data) :
        owner(owner),
        action_grounding_data(action_grounding_data),
        action_validators(),
        successor_scratch()
    {
        const auto null_event_handler = satisficing_binding_generator::NullEventHandlerImpl::create();
        for (auto& condition_grounder : this->action_grounding_data)
        {
            condition_grounder.set_event_handler(null_event_handler);
        }

        action_validators.reserve(this->action_grounding_data.size());
        for (const auto& condition_grounder : this->action_grounding_data)
        {
            action_validators.emplace_back(condition_grounder);
        }
    }
};

std::vector<ParallelActionSchemaTask> build_parallel_action_schema_tasks(
    const Problem& problem,
    const SearchContextImpl::LiftedOptions::KPKCOptions& options,
    const State& state,
    const ActionSatisficingBindingGeneratorList& action_grounding_data,
    std::chrono::nanoseconds& symmetry_setup_time)
{
    auto tasks = std::vector<ParallelActionSchemaTask> {};
    tasks.reserve(action_grounding_data.size());

    if (options.pruning == SearchContextImpl::SymmetryPruning::OFF)
    {
        for (size_t schema_index = 0; schema_index < action_grounding_data.size(); ++schema_index)
        {
            const auto& condition_grounder = action_grounding_data[schema_index];
            if (!nullary_conditions_hold(condition_grounder.get_conjunctive_condition(), state.get_unpacked_state()))
            {
                continue;
            }

            tasks.push_back(ParallelActionSchemaTask { schema_index, std::nullopt });
        }

        return tasks;
    }

    const auto symmetry_setup_start = std::chrono::steady_clock::now();
    auto object_graph = datasets::create_object_graph(state, *problem);
    auto vertex_to_orbit = IndexList(object_graph.get_num_vertices());

    if (options.pruning == SearchContextImpl::SymmetryPruning::GI)
    {
        auto nauty_graph = graphs::nauty::SparseGraph(object_graph);
        nauty_graph.canonize();

        for (Index i = 0; i < static_cast<Index>(object_graph.get_num_vertices()); ++i)
        {
            vertex_to_orbit[i] = nauty_graph.get_orbits()[i];
        }
    }
    else if (options.pruning == SearchContextImpl::SymmetryPruning::WL1)
    {
        const auto certificate = graphs::color_refinement::compute_certificate(object_graph);

        auto color_to_index = IndexMap<Index> {};
        for (Index i = 0; i < static_cast<Index>(object_graph.get_num_vertices()); ++i)
        {
            const auto [it, success] = color_to_index.emplace(certificate->get_hash_to_color()[i], color_to_index.size());
            vertex_to_orbit[i] = it->second;
        }
    }

    symmetry_setup_time = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - symmetry_setup_start);

    for (size_t schema_index = 0; schema_index < action_grounding_data.size(); ++schema_index)
    {
        const auto& condition_grounder = action_grounding_data[schema_index];
        if (!nullary_conditions_hold(condition_grounder.get_conjunctive_condition(), state.get_unpacked_state()))
        {
            continue;
        }

        auto touched_orbits = IndexSet {};
        auto count_touched_orbits = IndexList(object_graph.get_num_vertices(), 0);

        for (const auto& objects : condition_grounder.get_static_consistency_graph().get_objects_by_parameter_index())
        {
            touched_orbits.clear();
            for (const auto& object : objects)
            {
                touched_orbits.insert(vertex_to_orbit[object]);
            }
            for (const auto orbit : touched_orbits)
            {
                ++count_touched_orbits[orbit];
            }
        }

        const auto num_objects = problem->get_problem_and_domain_objects().size();
        const auto arity = condition_grounder.get_action()->get_arity();
        auto reduced_objects = boost::dynamic_bitset<>(num_objects * arity);
        auto tmp_count_touched_orbits = IndexList {};

        for (size_t parameter_index = 0; parameter_index < condition_grounder.get_action()->get_arity(); ++parameter_index)
        {
            const auto& objects = condition_grounder.get_static_consistency_graph().get_objects_by_parameter_index()[parameter_index];
            tmp_count_touched_orbits = count_touched_orbits;

            for (const auto& object : objects)
            {
                const auto orbit = vertex_to_orbit[object];
                if (tmp_count_touched_orbits[orbit] > 0)
                {
                    reduced_objects[parameter_index * num_objects + object] = true;
                    --tmp_count_touched_orbits[orbit];
                }
            }
        }

        auto vertex_mask =
            std::optional<boost::dynamic_bitset<>>(boost::dynamic_bitset<>(condition_grounder.get_static_consistency_graph().get_vertices().size(), false));

        for (const auto& vertex : condition_grounder.get_static_consistency_graph().get_vertices())
        {
            const auto parameter = vertex.get_parameter_index();
            const auto object = vertex.get_object_index();

            if (reduced_objects[parameter * num_objects + object])
            {
                vertex_mask->set(vertex.get_index());
            }
        }

        tasks.push_back(ParallelActionSchemaTask { schema_index, std::move(vertex_mask) });
    }

    return tasks;
}
}

struct KPKCLiftedApplicableActionGeneratorImpl::ParallelGroundLookupTables
{
    GroundAtomIndexLookup<StaticTag> static_predicates;
    GroundAtomIndexLookup<FluentTag> fluent_predicates;
    GroundAtomIndexLookup<DerivedTag> derived_predicates;
    GroundFunctionIndexLookup<StaticTag> static_functions;
    GroundFunctionIndexLookup<FluentTag> fluent_functions;
};

/**
 * LiftedApplicableActionGenerator
 */

KPKCLiftedApplicableActionGeneratorImpl::KPKCLiftedApplicableActionGeneratorImpl(Problem problem,
                                                                                 const SearchContextImpl::LiftedOptions::KPKCOptions& options,
                                                                                 EventHandler event_handler,
                                                                                 satisficing_binding_generator::EventHandler binding_event_handler) :
    m_problem(problem),
    m_options(options),
    m_event_handler(event_handler ? event_handler : DefaultEventHandlerImpl::create()),
    m_binding_event_handler(binding_event_handler ? binding_event_handler : satisficing_binding_generator::DefaultEventHandlerImpl::create()),
    m_action_grounding_data(),
    m_dynamic_assignment_sets(*m_problem),
    m_generation_statistics(),
    m_parallel_lookup_tables_mutex(),
    m_parallel_lookup_tables()
{
    /* 2. Initialize the condition grounders for each action schema. */
    const auto& actions = problem->get_domain()->get_actions();
    for (size_t i = 0; i < actions.size(); ++i)
    {
        const auto& action = actions[i];
        assert(action->get_index() == i);
        m_action_grounding_data.push_back(ActionSatisficingBindingGenerator(action, m_problem, m_binding_event_handler));
    }
}

void KPKCLiftedApplicableActionGeneratorImpl::prepare_parallel_applicable_action_generation() const
{
    if (m_parallel_lookup_tables)
    {
        return;
    }

    auto lock = std::scoped_lock(m_parallel_lookup_tables_mutex);
    if (m_parallel_lookup_tables)
    {
        return;
    }

    auto lookup_tables = std::make_shared<ParallelGroundLookupTables>();

    auto static_predicates = std::vector<bool> {};
    auto fluent_predicates = std::vector<bool> {};
    auto derived_predicates = std::vector<bool> {};
    auto static_function_skeletons = std::vector<bool> {};
    auto fluent_function_skeletons = std::vector<bool> {};

    collect_action_lookup_requirements(m_problem,
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
}

KPKCLiftedApplicableActionGenerator KPKCLiftedApplicableActionGeneratorImpl::create(Problem problem,
                                                                                    const SearchContextImpl::LiftedOptions::KPKCOptions& options,
                                                                                    EventHandler event_handler,
                                                                                    satisficing_binding_generator::EventHandler binding_event_handler)
{
    return std::make_shared<KPKCLiftedApplicableActionGeneratorImpl>(problem, options, event_handler, binding_event_handler);
}

bool KPKCLiftedApplicableActionGeneratorImpl::supports_parallel_applicable_action_generation() const { return true; }

bool KPKCLiftedApplicableActionGeneratorImpl::supports_parallel_relaxed_beam_successor_generation() const { return true; }

bool KPKCLiftedApplicableActionGeneratorImpl::supports_partial_binding_completion() const
{
    return m_options.pruning == SearchContextImpl::SymmetryPruning::OFF;
}

std::vector<ParallelApplicableActionGeneratorWorkerContext>&
KPKCLiftedApplicableActionGeneratorImpl::get_parallel_worker_contexts(size_t thread_count)
{
    prepare_parallel_applicable_action_generation();
    while (m_parallel_worker_contexts.size() < thread_count)
    {
        m_parallel_worker_contexts.push_back(create_parallel_worker_context());
    }
    return m_parallel_worker_contexts;
}

ParallelApplicableActionGeneratorWorkerContext KPKCLiftedApplicableActionGeneratorImpl::create_parallel_worker_context() const
{
    assert(m_parallel_lookup_tables);
    return std::make_unique<KPKCParallelApplicableActionWorkerContext>(this, m_action_grounding_data);
}

void KPKCLiftedApplicableActionGeneratorImpl::create_applicable_actions_from_partial_binding(const State& state,
                                                                                             const PartialGroundActionSeed& seed,
                                                                                             std::vector<GroundAction>& out_actions)
{
    if (!supports_partial_binding_completion())
    {
        throw std::logic_error(
            "KPKCLiftedApplicableActionGeneratorImpl::create_applicable_actions_from_partial_binding requires symmetry pruning to be disabled.");
    }

    if (!seed.action_schema)
    {
        throw std::invalid_argument("KPKCLiftedApplicableActionGeneratorImpl::create_applicable_actions_from_partial_binding requires a valid action schema.");
    }

    const auto action_index = seed.action_schema->get_index();
    if (action_index >= m_action_grounding_data.size())
    {
        throw std::invalid_argument(
            "KPKCLiftedApplicableActionGeneratorImpl::create_applicable_actions_from_partial_binding received an unknown action schema.");
    }

    const auto generation_start = std::chrono::steady_clock::now();

    const auto dynamic_assignment_initialization_start = std::chrono::steady_clock::now();
    initialize(state.get_unpacked_state(), m_dynamic_assignment_sets);
    const auto dynamic_assignment_initialization_time =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - dynamic_assignment_initialization_start);
    auto symmetry_setup_time = std::chrono::nanoseconds::zero();
    auto generation_statistics_scope =
        GenerationStatisticsScope { &m_generation_statistics, generation_start, dynamic_assignment_initialization_time, symmetry_setup_time };

    m_event_handler->on_start_generating_applicable_actions();

    auto& condition_grounder = m_action_grounding_data[action_index];
    if (!nullary_conditions_hold(condition_grounder.get_conjunctive_condition(), state.get_unpacked_state()))
    {
        m_event_handler->on_end_generating_applicable_actions();
        return;
    }

    const auto [mask_status, vertex_mask] = create_partial_binding_vertex_mask(condition_grounder, seed);
    if (mask_status == PartialBindingMaskStatus::IMPOSSIBLE)
    {
        m_event_handler->on_end_generating_applicable_actions();
        return;
    }

    const auto& ground_action_repository =
        boost::hana::at_key(state.get_problem().get_repositories().get_hana_repositories(), boost::hana::type<GroundActionImpl> {});

    for (auto&& binding : condition_grounder.create_binding_generator(state, m_dynamic_assignment_sets, vertex_mask))
    {
        const auto num_ground_actions = ground_action_repository.size();

        const auto ground_action = m_problem->ground(condition_grounder.get_action(), std::move(binding));

        assert(is_applicable(ground_action, state));

        m_event_handler->on_ground_action(ground_action);

        (ground_action_repository.size() > num_ground_actions) ? m_event_handler->on_ground_action_cache_miss(ground_action) :
                                                                 m_event_handler->on_ground_action_cache_hit(ground_action);

        out_actions.push_back(ground_action);
    }

    m_event_handler->on_end_generating_applicable_actions();
}

mimir::generator<GroundAction> KPKCLiftedApplicableActionGeneratorImpl::create_applicable_action_generator(const State& state)
{
    const auto generation_start = std::chrono::steady_clock::now();

    const auto dynamic_assignment_initialization_start = std::chrono::steady_clock::now();
    initialize(state.get_unpacked_state(), m_dynamic_assignment_sets);
    const auto dynamic_assignment_initialization_time =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - dynamic_assignment_initialization_start);
    auto symmetry_setup_time = std::chrono::nanoseconds::zero();
    auto generation_statistics_scope =
        GenerationStatisticsScope { &m_generation_statistics, generation_start, dynamic_assignment_initialization_time, symmetry_setup_time };

    /* Generate applicable actions */

    m_event_handler->on_start_generating_applicable_actions();

    const auto& ground_action_repository =
        boost::hana::at_key(state.get_problem().get_repositories().get_hana_repositories(), boost::hana::type<GroundActionImpl> {});

    if (m_options.pruning == SearchContextImpl::SymmetryPruning::OFF)
    {
        for (auto& condition_grounder : m_action_grounding_data)
        {
            // We move this check here to avoid unnecessary creations of mimir::generator.
            if (!nullary_conditions_hold(condition_grounder.get_conjunctive_condition(), state.get_unpacked_state()))
            {
                continue;
            }

            auto vertex_mask = std::optional<boost::dynamic_bitset<>> { std::nullopt };

            for (auto&& binding : condition_grounder.create_binding_generator(state, m_dynamic_assignment_sets, vertex_mask))
            {
                const auto num_ground_actions = ground_action_repository.size();

                const auto ground_action = m_problem->ground(condition_grounder.get_action(), std::move(binding));

                assert(is_applicable(ground_action, state));

                m_event_handler->on_ground_action(ground_action);

                (ground_action_repository.size() > num_ground_actions) ? m_event_handler->on_ground_action_cache_miss(ground_action) :
                                                                         m_event_handler->on_ground_action_cache_hit(ground_action);

                co_yield ground_action;
            }
        }
    }
    else
    {
        // --- Step 1: Create object graph, compute mapping from vertex to orbit where the object with index i corresponds to vertex with index i. ---
        const auto symmetry_setup_start = std::chrono::steady_clock::now();

        auto object_graph = datasets::create_object_graph(state, *m_problem);
        // std::cout << object_graph << std::endl;

        auto vertex_to_orbit = IndexList(object_graph.get_num_vertices());

        if (m_options.pruning == SearchContextImpl::SymmetryPruning::GI)
        {
            auto nauty_graph = graphs::nauty::SparseGraph(object_graph);
            nauty_graph.canonize();
            // std::cout << "orbits: " << to_string(nauty_graph.get_orbits()) << std::endl;

            for (Index i = 0; i < (Index) object_graph.get_num_vertices(); ++i)
            {
                vertex_to_orbit[i] = nauty_graph.get_orbits()[i];
            }
        }
        else if (m_options.pruning == SearchContextImpl::SymmetryPruning::WL1)
        {
            const auto certificate = graphs::color_refinement::compute_certificate(object_graph);
            // std::cout << "orbits: " << to_string(certificate->get_hash_to_color()) << std::endl;

            auto color_to_index = IndexMap<Index> {};
            for (Index i = 0; i < (Index) object_graph.get_num_vertices(); ++i)
            {
                const auto [it, success] = color_to_index.emplace(certificate->get_hash_to_color()[i], color_to_index.size());
                vertex_to_orbit[i] = it->second;
            }
        }

        // std::cout << "vertex_to_orbit: " << to_string(vertex_to_orbit) << std::endl;
        symmetry_setup_time = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - symmetry_setup_start);
        generation_statistics_scope.symmetry_setup_time = symmetry_setup_time;

        for (auto& condition_grounder : m_action_grounding_data)
        {
            // We move this check here to avoid unnecessary creations of mimir::generator.
            if (!nullary_conditions_hold(condition_grounder.get_conjunctive_condition(), state.get_unpacked_state()))
            {
                continue;
            }

            // --- Step 2: Compute number of times each orbits is touched by an action parameter. ---

            auto touched_orbits = IndexSet {};
            auto count_touched_orbits = IndexList(object_graph.get_num_vertices(), 0);

            // std::cout << "get_objects_by_parameter_index: " << to_string(condition_grounder.get_static_consistency_graph().get_objects_by_parameter_index())
            //           << std::endl;

            for (const auto& objects : condition_grounder.get_static_consistency_graph().get_objects_by_parameter_index())
            {
                touched_orbits.clear();
                for (const auto& object : objects)
                {
                    touched_orbits.insert(vertex_to_orbit[object]);  // vertex index
                }
                for (const auto orbit : touched_orbits)
                {
                    ++count_touched_orbits[orbit];
                }
            }

            // std::cout << "action: " << condition_grounder.get_action()->get_name() << std::endl;
            // std::cout << "count_touched_orbits: " << to_string(count_touched_orbits) << std::endl;

            // --- Step 3: Compute a vertex mask where each vertex parameter that touches an orbit with count n sets the mask to 1 if the vertex object is among
            // the n lex smallest objects in the orbit. ---

            const auto num_objects = m_problem->get_problem_and_domain_objects().size();
            const auto arity = condition_grounder.get_action()->get_arity();
            auto reduced_objects = boost::dynamic_bitset<>(num_objects * arity);
            auto tmp_count_touched_orbits = IndexList {};

            for (size_t i = 0; i < condition_grounder.get_action()->get_arity(); ++i)
            {
                const auto& objects = condition_grounder.get_static_consistency_graph().get_objects_by_parameter_index()[i];

                tmp_count_touched_orbits = count_touched_orbits;

                for (const auto& object : objects)
                {
                    const auto orbit = vertex_to_orbit[object];

                    if (tmp_count_touched_orbits[orbit] > 0)
                    {
                        reduced_objects[i * num_objects + object] = true;
                        --tmp_count_touched_orbits[orbit];
                    }
                }
            }

            // std::cout << "objects_by_parameter_index: " << to_string(condition_grounder.get_static_consistency_graph().get_objects_by_parameter_index())
            //           << std::endl;
            // std::cout << "reduced_objects_by_parameter_index: " << to_string(reduced_objects_by_parameter_index) << std::endl << std::endl;

            auto vertex_mask =
                std::optional<boost::dynamic_bitset<>>(boost::dynamic_bitset<>(condition_grounder.get_static_consistency_graph().get_vertices().size(), false));

            for (const auto& vertex : condition_grounder.get_static_consistency_graph().get_vertices())
            {
                const auto parameter = vertex.get_parameter_index();
                const auto object = vertex.get_object_index();

                if (reduced_objects[parameter * num_objects + object])
                {
                    vertex_mask->set(vertex.get_index());
                }
            }

            for (auto&& binding : condition_grounder.create_binding_generator(state, m_dynamic_assignment_sets, vertex_mask))
            {
                const auto num_ground_actions = ground_action_repository.size();

                const auto ground_action = m_problem->ground(condition_grounder.get_action(), std::move(binding));

                assert(is_applicable(ground_action, state));

                m_event_handler->on_ground_action(ground_action);

                (ground_action_repository.size() > num_ground_actions) ? m_event_handler->on_ground_action_cache_miss(ground_action) :
                                                                         m_event_handler->on_ground_action_cache_hit(ground_action);

                co_yield ground_action;
            }
        }
    }

    m_event_handler->on_end_generating_applicable_actions();
}

std::vector<GroundAction> KPKCLiftedApplicableActionGeneratorImpl::create_applicable_action_list_parallel(const State& state,
                                                                                                          BS::thread_pool& thread_pool)
{
    const auto generation_start = std::chrono::steady_clock::now();

    const auto dynamic_assignment_initialization_start = std::chrono::steady_clock::now();
    initialize(state.get_unpacked_state(), m_dynamic_assignment_sets);
    const auto dynamic_assignment_initialization_time =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - dynamic_assignment_initialization_start);
    auto symmetry_setup_time = std::chrono::nanoseconds::zero();
    auto generation_statistics_scope = GenerationStatisticsScope { &m_generation_statistics, generation_start, dynamic_assignment_initialization_time,
                                                                  symmetry_setup_time };

    m_event_handler->on_start_generating_applicable_actions();

    const auto& unpacked_state = state.get_unpacked_state();
    const auto& ground_action_repository =
        boost::hana::at_key(state.get_problem().get_repositories().get_hana_repositories(), boost::hana::type<GroundActionImpl> {});

    const auto thread_count = std::max<size_t>(1, thread_pool.get_thread_count());
    const auto schema_tasks = build_parallel_action_schema_tasks(m_problem, m_options, state, m_action_grounding_data, symmetry_setup_time);
    generation_statistics_scope.symmetry_setup_time = symmetry_setup_time;

    if (schema_tasks.empty())
    {
        m_event_handler->on_end_generating_applicable_actions();
        return {};
    }

    auto& worker_contexts = get_parallel_worker_contexts(thread_count);

    const auto num_partitions = std::min<size_t>(thread_count, std::max<size_t>(1, schema_tasks.size()));
    auto futures = std::vector<std::future<ParallelActionPartitionResult>> {};
    futures.reserve(num_partitions);

    auto begin_index = size_t(0);
    const auto base_partition_size = schema_tasks.size() / num_partitions;
    const auto num_larger_partitions = schema_tasks.size() % num_partitions;

    for (size_t partition_index = 0; partition_index < num_partitions; ++partition_index)
    {
        const auto partition_size = base_partition_size + (partition_index < num_larger_partitions ? 1u : 0u);
        const auto partition_begin = begin_index;
        const auto partition_end = partition_begin + partition_size;
        begin_index = partition_end;

        futures.push_back(thread_pool.submit_task(
            [&unpacked_state,
             &dynamic_assignment_sets = m_dynamic_assignment_sets,
             &schema_tasks,
             &worker_contexts,
             lookup_tables = m_parallel_lookup_tables,
             partition_begin,
             partition_end]()
            {
                const auto worker_index = BS::this_thread::get_index();
                assert(worker_index.has_value());
                auto& typed_worker_context = static_cast<KPKCParallelApplicableActionWorkerContext&>(*worker_contexts[*worker_index]);

                auto result = ParallelActionPartitionResult {};
                result.schema_results.reserve(partition_end - partition_begin);

                for (size_t task_index = partition_begin; task_index < partition_end; ++task_index)
                {
                    const auto& schema_task = schema_tasks[task_index];
                    auto& condition_grounder = typed_worker_context.action_grounding_data[schema_task.schema_index];
                    auto& action_validator = typed_worker_context.action_validators[schema_task.schema_index];
                    const auto binding_arity = condition_grounder.get_action()->get_arity();
                    auto candidate_binding_object_indices = IndexList {};
                    auto num_candidate_bindings = size_t(0);

                    condition_grounder.for_each_candidate_binding_indices(
                        unpacked_state, dynamic_assignment_sets, schema_task.vertex_mask, [&](const IndexList& binding_object_indices)
                    {
                        if (action_validator.test_binding(unpacked_state, *lookup_tables, binding_object_indices))
                        {
                            ++num_candidate_bindings;
                            candidate_binding_object_indices.insert(
                                candidate_binding_object_indices.end(), binding_object_indices.begin(), binding_object_indices.end());
                        }
                    });

                    result.schema_results.push_back(
                        ParallelActionSchemaResult { schema_task.schema_index,
                                                     binding_arity,
                                                     num_candidate_bindings,
                                                     std::move(candidate_binding_object_indices) });
                }

                return result;
            }));
    }

    auto applicable_actions = std::vector<GroundAction> {};
    for (size_t partition_index = 0; partition_index < futures.size(); ++partition_index)
    {
        auto partition_result = futures[partition_index].get();
        for (auto& schema_result : partition_result.schema_results)
        {
            auto& condition_grounder = m_action_grounding_data[schema_result.schema_index];
            auto binding = ObjectList(schema_result.binding_arity);
            const auto& problem_objects = m_problem->get_problem_and_domain_objects();

            for (size_t binding_index = 0; binding_index < schema_result.num_candidate_bindings; ++binding_index)
            {
                if (schema_result.binding_arity > 0)
                {
                    const auto begin = binding_index * schema_result.binding_arity;
                    for (size_t parameter_index = 0; parameter_index < schema_result.binding_arity; ++parameter_index)
                    {
                        binding[parameter_index] = problem_objects[schema_result.candidate_binding_object_indices[begin + parameter_index]];
                    }
                }

                const auto num_ground_actions = ground_action_repository.size();
                const auto ground_action = m_problem->ground(condition_grounder.get_action(), binding);

                assert(is_applicable(ground_action, state));

                m_event_handler->on_ground_action(ground_action);
                (ground_action_repository.size() > num_ground_actions) ? m_event_handler->on_ground_action_cache_miss(ground_action) :
                                                                         m_event_handler->on_ground_action_cache_hit(ground_action);
                applicable_actions.push_back(ground_action);
            }
        }
    }

    m_event_handler->on_end_generating_applicable_actions();
    return applicable_actions;
}

ParallelRelaxedBeamSuccessorGenerationResult KPKCLiftedApplicableActionGeneratorImpl::create_relaxed_parallel_beam_successor_candidates(
    const State& state,
    ContinuousCost state_metric_value,
    DiscreteCost successor_g_value,
    BS::thread_pool& thread_pool,
    StateRepositoryImpl& state_repository,
    const PruningStrategy& pruning_strategy,
    BeamNoveltyMode beam_novelty_mode,
    const LayerOrderingStrategy& layer_ordering_strategy,
    uint32_t beam_width,
    bool iw1_precheck_add_effect_novelty,
    bool randomize_equal_score_ties,
    uint64_t equal_score_tie_seed)
{
    if (beam_novelty_mode != BeamNoveltyMode::SURVIVORS_ONLY)
    {
        throw std::invalid_argument(
            "KPKCLiftedApplicableActionGeneratorImpl::create_relaxed_parallel_beam_successor_candidates requires BeamNoveltyMode::SURVIVORS_ONLY.");
    }

    /* This path derives add effects only. A strategy whose precheck also needs the delete effects
       -- landmark-restricted novelty, whose coordinates can disappear as well as appear -- would be
       handed an incomplete transition and could prune an admissible action, so it simply does not
       get the precheck here. Skipping a precheck costs speed; running it on a partial transition
       would cost correctness. */
    if (pruning_strategy->precheck_requires_delete_effects())
    {
        iw1_precheck_add_effect_novelty = false;
    }

    const auto generation_start = std::chrono::steady_clock::now();

    const auto dynamic_assignment_initialization_start = std::chrono::steady_clock::now();
    initialize(state.get_unpacked_state(), m_dynamic_assignment_sets);
    const auto dynamic_assignment_initialization_time =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - dynamic_assignment_initialization_start);
    auto symmetry_setup_time = std::chrono::nanoseconds::zero();
    auto generation_statistics_scope = GenerationStatisticsScope { &m_generation_statistics, generation_start, dynamic_assignment_initialization_time,
                                                                  symmetry_setup_time };

    m_event_handler->on_start_generating_applicable_actions();

    const auto thread_count = std::max<size_t>(1, thread_pool.get_thread_count());
    const auto schema_tasks = build_parallel_action_schema_tasks(m_problem, m_options, state, m_action_grounding_data, symmetry_setup_time);
    generation_statistics_scope.symmetry_setup_time = symmetry_setup_time;

    if (schema_tasks.empty())
    {
        m_event_handler->on_end_generating_applicable_actions();
        return ParallelRelaxedBeamSuccessorGenerationResult {};
    }

    auto& worker_contexts = get_parallel_worker_contexts(thread_count);

    const auto use_small_beam = beam_width <= 64;
    const auto ranking = LocalBeamRanking<ParallelRelaxedBeamSuccessorCandidate> { layer_ordering_strategy->prefer_higher_scores() };
    const auto heap_compare = LocalBeamHeapCompare<ParallelRelaxedBeamSuccessorCandidate> { ranking };
    const auto num_partitions = std::min<size_t>(thread_count, std::max<size_t>(1, schema_tasks.size()));
    auto futures = std::vector<std::future<ParallelRelaxedBeamSuccessorGenerationResult>> {};
    futures.reserve(num_partitions);

    auto begin_index = size_t(0);
    const auto base_partition_size = schema_tasks.size() / num_partitions;
    const auto num_larger_partitions = schema_tasks.size() % num_partitions;

    for (size_t partition_index = 0; partition_index < num_partitions; ++partition_index)
    {
        const auto partition_size = base_partition_size + (partition_index < num_larger_partitions ? 1u : 0u);
        const auto partition_begin = begin_index;
        const auto partition_end = partition_begin + partition_size;
        begin_index = partition_end;

        futures.push_back(thread_pool.submit_task([&state,
                                                   &state_repository,
                                                   &pruning_strategy,
                                                   &layer_ordering_strategy,
                                                   &dynamic_assignment_sets = m_dynamic_assignment_sets,
                                                   &schema_tasks,
                                                   &worker_contexts,
                                                   lookup_tables = m_parallel_lookup_tables,
                                                   state_metric_value,
                                                   successor_g_value,
                                                   beam_width,
                                                   beam_novelty_mode,
                                                   use_small_beam,
                                                   ranking,
                                                   heap_compare,
                                                   iw1_precheck_add_effect_novelty,
                                                   randomize_equal_score_ties,
                                                   equal_score_tie_seed,
                                                   partition_begin,
                                                   partition_end]()
                                                  {
                                                      auto result = ParallelRelaxedBeamSuccessorGenerationResult {};
                                                      result.candidates.reserve(std::min<size_t>(beam_width, partition_end - partition_begin));

                                                      const auto worker_index = BS::this_thread::get_index();
                                                      assert(worker_index.has_value());
                                                      auto& typed_worker_context =
                                                          static_cast<KPKCParallelApplicableActionWorkerContext&>(*worker_contexts[*worker_index]);
                                                      const auto& axiom_evaluator = state_repository.get_axiom_evaluator();

                                                      const auto worker_compute_start = std::chrono::steady_clock::now();
                                                      for (size_t task_order = partition_begin; task_order < partition_end; ++task_order)
                                                      {
                                                          const auto& schema_task = schema_tasks[task_order];
                                                          auto& condition_grounder =
                                                              typed_worker_context.action_grounding_data[schema_task.schema_index];
                                                          auto& action_validator = typed_worker_context.action_validators[schema_task.schema_index];
                                                          auto local_binding_index = uint32_t(0);
                                                          auto add_effect_atom_indices = iw::AtomIndexList {};
                                                          /* Never populated: strategies that read
                                                             it are excluded from this path above. */
                                                          const auto no_del_effect_atom_indices = iw::AtomIndexList {};

                                                          condition_grounder.for_each_candidate_binding_indices(state.get_unpacked_state(),
                                                                                                                dynamic_assignment_sets,
                                                                                                                schema_task.vertex_mask,
                                                                                                                [&](const IndexList& binding_object_indices)
                                                          {
                                                              if (!action_validator.test_binding(
                                                                      state.get_unpacked_state(), *lookup_tables, binding_object_indices))
                                                              {
                                                                  return;
                                                              }

                                                              if (iw1_precheck_add_effect_novelty
                                                                  && !pruning_strategy->should_bypass_action_add_effect_precheck(state))
                                                              {
                                                                  action_validator.collect_add_effect_fluent_atom_indices(state,
                                                                                                                          *lookup_tables,
                                                                                                                          binding_object_indices,
                                                                                                                          typed_worker_context.successor_scratch,
                                                                                                                          add_effect_atom_indices);
                                                                  if (!pruning_strategy->test_transition_novelty_from_add_effects(
                                                                          state, add_effect_atom_indices, no_del_effect_atom_indices))
                                                                  {
                                                                      ++local_binding_index;
                                                                      return;
                                                                  }
                                                              }

                                                              ++result.num_scored_candidates;
                                                              auto candidate = action_validator.compute_staged_successor_candidate(state,
                                                                                                                                   *lookup_tables,
                                                                                                                                   binding_object_indices,
                                                                                                                                   state_metric_value,
                                                                                                                                   axiom_evaluator,
                                                                                                                                   typed_worker_context.successor_scratch);
                                                              if (pruning_strategy->test_prune_staged_successor_state_for_relaxed_beam_selection(
                                                                      state,
                                                                      candidate.fluent_atoms,
                                                                      candidate.derived_atoms,
                                                                      candidate.fluent_numeric_variables,
                                                                      candidate.fluent_atom_indices,
                                                                      beam_novelty_mode))
                                                              {
                                                                  ++local_binding_index;
                                                                  return;
                                                              }

                                                              candidate.score = layer_ordering_strategy->score_staged_state(candidate.fluent_atoms,
                                                                                                                            candidate.derived_atoms,
                                                                                                                            candidate.fluent_numeric_variables,
                                                                                                                            successor_g_value);
                                                              candidate.successor_g_value = successor_g_value;
                                                              candidate.generation_sequence =
                                                                  make_generation_sequence(static_cast<uint32_t>(task_order), local_binding_index++);
                                                              candidate.tie_token = randomize_equal_score_ties ?
                                                                                        splitmix64(equal_score_tie_seed ^ candidate.generation_sequence) :
                                                                                        uint64_t(0);

                                                              const auto reject_candidate = [](const auto&) {};
                                                              if (use_small_beam)
                                                              {
                                                                  add_candidate_to_small_local_beam(
                                                                      result.candidates,
                                                                      std::move(candidate),
                                                                      beam_width,
                                                                      ranking,
                                                                      reject_candidate);
                                                              }
                                                              else
                                                              {
                                                                  add_candidate_to_heap_local_beam(result.candidates,
                                                                                                   std::move(candidate),
                                                                                                   beam_width,
                                                                                                   ranking,
                                                                                                   heap_compare,
                                                                                                   reject_candidate);
                                                              }
                                                          });
                                                      }

                                                      if (!use_small_beam)
                                                      {
                                                          std::sort(result.candidates.begin(),
                                                                    result.candidates.end(),
                                                                    [&ranking](const auto& lhs, const auto& rhs) { return ranking.better(lhs, rhs); });
                                                      }

                                                      result.worker_compute_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                          std::chrono::steady_clock::now() - worker_compute_start);
                                                      return result;
                                                  }));
    }

    auto merged_result = ParallelRelaxedBeamSuccessorGenerationResult {};
    merged_result.candidates.reserve(std::min<size_t>(beam_width * num_partitions, schema_tasks.size() * beam_width));

    for (auto& future : futures)
    {
        auto partition_result = future.get();
        merged_result.worker_compute_time += partition_result.worker_compute_time;
        merged_result.num_scored_candidates += partition_result.num_scored_candidates;
        for (auto& candidate : partition_result.candidates)
        {
            merged_result.candidates.push_back(std::move(candidate));
        }
    }

    std::sort(merged_result.candidates.begin(),
              merged_result.candidates.end(),
              [&ranking](const auto& lhs, const auto& rhs) { return ranking.better(lhs, rhs); });

    m_event_handler->on_end_generating_applicable_actions();
    return merged_result;
}

const Problem& KPKCLiftedApplicableActionGeneratorImpl::get_problem() const { return m_problem; }
const KPKCLiftedApplicableActionGeneratorImpl::GenerationStatistics& KPKCLiftedApplicableActionGeneratorImpl::get_generation_statistics() const
{
    return m_generation_statistics;
}

void KPKCLiftedApplicableActionGeneratorImpl::on_finish_search_layer()
{
    m_event_handler->on_finish_search_layer();
    m_binding_event_handler->on_finish_search_layer();
}

void KPKCLiftedApplicableActionGeneratorImpl::on_end_search()
{
    m_event_handler->on_end_search();
    m_binding_event_handler->on_end_search();
}

void KPKCLiftedApplicableActionGeneratorImpl::release_parallel_memory(bool clear_shared_caches)
{
    m_parallel_worker_contexts.clear();
    m_parallel_worker_contexts.shrink_to_fit();
    if (clear_shared_caches)
    {
        auto lock = std::scoped_lock(m_parallel_lookup_tables_mutex);
        m_parallel_lookup_tables.reset();
    }
}
}
