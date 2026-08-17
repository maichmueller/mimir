#include "../init_declarations.hpp"
#include "mimir/search/heuristics/h2.hpp"

#include <nanobind/nanobind.h>
#include <nanobind/trampoline.h>

using namespace mimir::formalism;

namespace mimir::search
{

namespace
{
std::vector<size_t> compute_transition_novel_fluent_atom_indices_read_only(const IPruningStrategy& pruning_strategy,
                                                                            const State& state,
                                                                            const State& successor_state)
{
    auto novel_atom_indices = iw::AtomIndexList {};
    if (!pruning_strategy.supports_transition_novel_witness_query())
    {
        return {};
    }
    pruning_strategy.compute_transition_novel_fluent_atom_indices_read_only(state, successor_state, novel_atom_indices);
    return std::vector<size_t>(novel_atom_indices.begin(), novel_atom_indices.end());
}

/// @brief Throw unless `strategy` is one of the native goal strategies, or absent.
///
/// The entry points below release the GIL for the whole search, and the parallel one runs the search
/// on several threads. Calling back into a Python-subclassed strategy from there would be a crash
/// rather than an exception, so a trampoline has to be refused here, on the calling thread, while
/// the GIL is still held. Recognizing the native implementations by type is the check: anything else
/// -- including a pure-C++ strategy an embedder added -- is refused rather than assumed safe.
void reject_python_strategy(const GoalStrategy& strategy, const char* option_name)
{
    if (!strategy || std::dynamic_pointer_cast<ProblemGoalStrategyImpl>(strategy) || std::dynamic_pointer_cast<ProblemMultiGoalStrategyImpl>(strategy))
    {
        return;
    }

    throw std::invalid_argument(std::string(option_name)
                                + " must be a native goal strategy (ProblemGoalStrategy or ProblemMultiGoalStrategy). This entry point releases the GIL "
                                  "for the whole search and runs it on worker threads, so it cannot call back into Python.");
}
}  // namespace

class IPyGoalStrategy : public IGoalStrategy
{
public:
    NB_TRAMPOLINE(IGoalStrategy, 2);

    /* Trampoline (need one for each virtual function) */
    bool test_static_goal() override { NB_OVERRIDE_PURE(test_static_goal); }

    bool test_dynamic_goal(const State& state) override { NB_OVERRIDE_PURE(test_dynamic_goal, state); }
};

class IPyPruningStrategy : public IPruningStrategy
{
public:
    NB_TRAMPOLINE(IPruningStrategy, 2);

    /* Trampoline (need one for each virtual function) */
    bool test_prune_initial_state(const State& state) override { NB_OVERRIDE_PURE(test_prune_initial_state, state); }

    bool test_prune_successor_state(const State& state, const State& succ_state, bool is_new_succ) override
    {
        NB_OVERRIDE_PURE(test_prune_successor_state, state, succ_state, is_new_succ);
    }
};

class IPyLayerOrderingStrategy : public ILayerOrderingStrategy
{
public:
    NB_TRAMPOLINE(ILayerOrderingStrategy, 4);

    bool supports_eager_scoring() const override { NB_OVERRIDE(supports_eager_scoring); }

    ContinuousCost score_state(const State& state, DiscreteCost g_value) const override { NB_OVERRIDE(score_state, state, g_value); }

    bool prefer_higher_scores() const override { NB_OVERRIDE(prefer_higher_scores); }

    void order_layer(StateList& states, DiscreteCost g_value) override { NB_OVERRIDE_PURE(order_layer, states, g_value); }
};

class IPyExplorationStrategy : public IExplorationStrategy
{
public:
    NB_TRAMPOLINE(IExplorationStrategy, 1);

    /* Trampoline (need one for each virtual function) */
    bool on_generate_state(const State& state, GroundAction action, const State& succ_state) override
    {
        NB_OVERRIDE_PURE(on_generate_state, state, action, succ_state);
    }
};

class IPyHeuristic : public IHeuristic
{
public:
    NB_TRAMPOLINE(IHeuristic, 2);

    /* Trampoline (need one for each virtual function) */
    ContinuousCost compute_heuristic(const State& state, formalism::GroundConjunctiveCondition goal = nullptr) override
    {
        NB_OVERRIDE_PURE(compute_heuristic, state, goal);
    }

    const PreferredActions& get_preferred_actions() const override { NB_OVERRIDE(get_preferred_actions); }
};

class IPyAStarEagerEventHandler : public astar_eager::IEventHandler
{
public:
    NB_TRAMPOLINE(astar_eager::IEventHandler, 14);

    /* Trampoline (need one for each virtual function) */
    void on_expand_state(const State& state) override { NB_OVERRIDE_PURE(on_expand_state, state); }

    void on_expand_goal_state(const State& state) override { NB_OVERRIDE_PURE(on_expand_goal_state, state); }

    void on_generate_state(const State& state, GroundAction action, ContinuousCost action_cost, const State& successor_state) override
    {
        NB_OVERRIDE_PURE(on_generate_state, state, action, action_cost, successor_state);
    }
    void on_generate_state_relaxed(const State& state, GroundAction action, ContinuousCost action_cost, const State& successor_state) override
    {
        NB_OVERRIDE_PURE(on_generate_state_relaxed, state, action, action_cost, successor_state);
    }
    void on_generate_state_not_relaxed(const State& state, GroundAction action, ContinuousCost action_cost, const State& successor_state) override
    {
        NB_OVERRIDE_PURE(on_generate_state_not_relaxed, state, action, action_cost, successor_state);
    }
    void on_close_state(const State& state) override { NB_OVERRIDE_PURE(on_close_state, state); }
    void on_finish_f_layer(ContinuousCost f_value) override { NB_OVERRIDE_PURE(on_finish_f_layer, f_value); }
    void on_prune_state(const State& state) override { NB_OVERRIDE_PURE(on_prune_state, state); }
    void on_start_search(const State& start_state, ContinuousCost g_value, ContinuousCost h_value) override
    {
        NB_OVERRIDE_PURE(on_start_search, start_state, g_value, h_value);
    }
    void on_end_search(uint64_t num_reached_fluent_atoms,
                       uint64_t num_reached_derived_atoms,
                       uint64_t num_states,
                       uint64_t num_nodes,
                       uint64_t num_actions,
                       uint64_t num_axioms) override
    {
        NB_OVERRIDE_PURE(on_end_search, num_reached_fluent_atoms, num_reached_derived_atoms, num_states, num_nodes, num_actions, num_axioms);
    }
    void on_solved(const Plan& plan) override { NB_OVERRIDE_PURE(on_solved, plan); }
    void on_unsolvable() override { NB_OVERRIDE_PURE(on_unsolvable); }
    void on_exhausted() override { NB_OVERRIDE_PURE(on_exhausted); }
    const astar_eager::Statistics& get_statistics() const override { NB_OVERRIDE_PURE(get_statistics); }
};

class IPyAStarIWEventHandler : public astar_iw::IEventHandler
{
public:
    NB_TRAMPOLINE(astar_iw::IEventHandler, 14);

    void on_start_search(const State& state, ContinuousCost g_value, ContinuousCost f_value) override
    {
        NB_OVERRIDE_PURE(on_start_search, state, g_value, f_value);
    }
    void on_generate_state(const State& state, GroundAction action, ContinuousCost action_cost, const State& successor_state) override
    {
        NB_OVERRIDE_PURE(on_generate_state, state, action, action_cost, successor_state);
    }
    void on_expand_state(const State& state) override { NB_OVERRIDE_PURE(on_expand_state, state); }
    void on_expand_goal_state(const State& state) override { NB_OVERRIDE_PURE(on_expand_goal_state, state); }
    void on_reopen_state(const State& state) override { NB_OVERRIDE_PURE(on_reopen_state, state); }
    void on_deadend_state(const State& state) override { NB_OVERRIDE_PURE(on_deadend_state, state); }
    void on_reject_state_novelty(const State& state) override { NB_OVERRIDE_PURE(on_reject_state_novelty, state); }
    void on_discard_stale_g(const State& state) override { NB_OVERRIDE_PURE(on_discard_stale_g, state); }
    void on_discard_stale_novelty(const State& state) override { NB_OVERRIDE_PURE(on_discard_stale_novelty, state); }
    void on_end_search(uint64_t num_states, uint64_t num_nodes) override { NB_OVERRIDE_PURE(on_end_search, num_states, num_nodes); }
    void on_solved(const Plan& plan) override { NB_OVERRIDE_PURE(on_solved, plan); }
    void on_unsolvable() override { NB_OVERRIDE_PURE(on_unsolvable); }
    void on_exhausted() override { NB_OVERRIDE_PURE(on_exhausted); }
    const astar_iw::Statistics& get_statistics() const override { NB_OVERRIDE_PURE(get_statistics); }
};

class IPyAStarLazyEventHandler : public astar_lazy::IEventHandler
{
public:
    NB_TRAMPOLINE(astar_lazy::IEventHandler, 14);

    /* Trampoline (need one for each virtual function) */
    void on_expand_state(const State& state) override { NB_OVERRIDE_PURE(on_expand_state, state); }

    void on_expand_goal_state(const State& state) override { NB_OVERRIDE_PURE(on_expand_goal_state, state); }

    void on_generate_state(const State& state, GroundAction action, ContinuousCost action_cost, const State& successor_state) override
    {
        NB_OVERRIDE_PURE(on_generate_state, state, action, action_cost, successor_state);
    }
    void on_generate_state_relaxed(const State& state, GroundAction action, ContinuousCost action_cost, const State& successor_state) override
    {
        NB_OVERRIDE_PURE(on_generate_state_relaxed, state, action, action_cost, successor_state);
    }
    void on_generate_state_not_relaxed(const State& state, GroundAction action, ContinuousCost action_cost, const State& successor_state) override
    {
        NB_OVERRIDE_PURE(on_generate_state_not_relaxed, state, action, action_cost, successor_state);
    }
    void on_close_state(const State& state) override { NB_OVERRIDE_PURE(on_close_state, state); }
    void on_finish_f_layer(ContinuousCost f_value) override { NB_OVERRIDE_PURE(on_finish_f_layer, f_value); }
    void on_prune_state(const State& state) override { NB_OVERRIDE_PURE(on_prune_state, state); }
    void on_start_search(const State& start_state, ContinuousCost g_value, ContinuousCost h_value) override
    {
        NB_OVERRIDE_PURE(on_start_search, start_state, g_value, h_value);
    }
    void on_end_search(uint64_t num_reached_fluent_atoms,
                       uint64_t num_reached_derived_atoms,
                       uint64_t num_states,
                       uint64_t num_nodes,
                       uint64_t num_actions,
                       uint64_t num_axioms) override
    {
        NB_OVERRIDE_PURE(on_end_search, num_reached_fluent_atoms, num_reached_derived_atoms, num_states, num_nodes, num_actions, num_axioms);
    }
    void on_solved(const Plan& plan) override { NB_OVERRIDE_PURE(on_solved, plan); }
    void on_unsolvable() override { NB_OVERRIDE_PURE(on_unsolvable); }
    void on_exhausted() override { NB_OVERRIDE_PURE(on_exhausted); }
    const astar_lazy::Statistics& get_statistics() const override { NB_OVERRIDE_PURE(get_statistics); }
};

class IPyBrFSEventHandler : public brfs::IEventHandler
{
public:
    NB_TRAMPOLINE(brfs::IEventHandler, 14);

    /* Trampoline (need one for each virtual function) */
    void on_expand_state(const State& state) override { NB_OVERRIDE_PURE(on_expand_state, state); }

    void on_expand_goal_state(const State& state) override { NB_OVERRIDE_PURE(on_expand_goal_state, state); }

    void on_generate_state(const State& state, GroundAction action, ContinuousCost action_cost, const State& successor_state) override
    {
        NB_OVERRIDE_PURE(on_generate_state, state, action, action_cost, successor_state);
    }
    bool supports_novel_witness_events() const override { NB_OVERRIDE(supports_novel_witness_events); }
    void on_generate_state_with_novel_witness(const State& state,
                                              GroundAction action,
                                              ContinuousCost action_cost,
                                              const State& successor_state,
                                              const iw::AtomIndexList& novel_fluent_atom_indices) override
    {
        NB_OVERRIDE(on_generate_state_with_novel_witness, state, action, action_cost, successor_state, novel_fluent_atom_indices);
    }
    void on_generate_state_in_search_tree(const State& state, GroundAction action, ContinuousCost action_cost, const State& successor_state) override
    {
        NB_OVERRIDE_PURE(on_generate_state_in_search_tree, state, action, action_cost, successor_state);
    }
    void on_generate_state_not_in_search_tree(const State& state, GroundAction action, ContinuousCost action_cost, const State& successor_state) override
    {
        NB_OVERRIDE_PURE(on_generate_state_not_in_search_tree, state, action, action_cost, successor_state);
    }

    void on_finish_g_layer(DiscreteCost g_value) override { NB_OVERRIDE_PURE(on_finish_g_layer, g_value); }
    void on_start_search(const State& start_state) override { NB_OVERRIDE_PURE(on_start_search, start_state); }
    void on_end_search(uint64_t num_reached_fluent_atoms,
                       uint64_t num_reached_derived_atoms,
                       uint64_t num_states,
                       uint64_t num_nodes,
                       uint64_t num_actions,
                       uint64_t num_axioms) override
    {
        NB_OVERRIDE_PURE(on_end_search, num_reached_fluent_atoms, num_reached_derived_atoms, num_states, num_nodes, num_actions, num_axioms);
    }
    void on_solved(const Plan& plan) override { NB_OVERRIDE_PURE(on_solved, plan); }
    void on_unsolvable() override { NB_OVERRIDE_PURE(on_unsolvable); }
    void on_exhausted() override { NB_OVERRIDE_PURE(on_exhausted); }
    const brfs::Statistics& get_statistics() const override { NB_OVERRIDE_PURE(get_statistics); }
};

class IPyGBFSEagerEventHandler : public gbfs_eager::IEventHandler
{
public:
    NB_TRAMPOLINE(gbfs_eager::IEventHandler, 11);

    /* Trampoline (need one for each virtual function) */
    void on_expand_state(const State& state) override { NB_OVERRIDE_PURE(on_expand_state, state); }

    void on_expand_goal_state(const State& state) override { NB_OVERRIDE_PURE(on_expand_goal_state, state); }

    void on_generate_state(const State& state, GroundAction action, ContinuousCost action_cost, const State& successor_state) override
    {
        NB_OVERRIDE_PURE(on_generate_state, state, action, action_cost, successor_state);
    }
    void on_prune_state(const State& state) override { NB_OVERRIDE_PURE(on_prune_state, state); }
    void on_start_search(const State& start_state, ContinuousCost g_value, ContinuousCost h_value) override
    {
        NB_OVERRIDE_PURE(on_start_search, start_state, g_value, h_value);
    }
    void on_new_best_h_value(ContinuousCost h_value) override { NB_OVERRIDE_PURE(on_new_best_h_value, h_value); }
    void on_end_search(uint64_t num_reached_fluent_atoms,
                       uint64_t num_reached_derived_atoms,
                       uint64_t num_states,
                       uint64_t num_nodes,
                       uint64_t num_actions,
                       uint64_t num_axioms) override
    {
        NB_OVERRIDE_PURE(on_end_search, num_reached_fluent_atoms, num_reached_derived_atoms, num_states, num_nodes, num_actions, num_axioms);
    }
    void on_solved(const Plan& plan) override { NB_OVERRIDE_PURE(on_solved, plan); }
    void on_unsolvable() override { NB_OVERRIDE_PURE(on_unsolvable); }
    void on_exhausted() override { NB_OVERRIDE_PURE(on_exhausted); }
    const gbfs_eager::Statistics& get_statistics() const override { NB_OVERRIDE_PURE(get_statistics); }
};

class IPyGBFSLazyEventHandler : public gbfs_lazy::IEventHandler
{
public:
    NB_TRAMPOLINE(gbfs_lazy::IEventHandler, 11);

    /* Trampoline (need one for each virtual function) */
    void on_expand_state(const State& state) override { NB_OVERRIDE_PURE(on_expand_state, state); }

    void on_expand_goal_state(const State& state) override { NB_OVERRIDE_PURE(on_expand_goal_state, state); }

    void on_generate_state(const State& state, GroundAction action, ContinuousCost action_cost, const State& successor_state) override
    {
        NB_OVERRIDE_PURE(on_generate_state, state, action, action_cost, successor_state);
    }
    void on_prune_state(const State& state) override { NB_OVERRIDE_PURE(on_prune_state, state); }
    void on_start_search(const State& start_state, ContinuousCost g_value, ContinuousCost h_value) override
    {
        NB_OVERRIDE_PURE(on_start_search, start_state, g_value, h_value);
    }
    void on_new_best_h_value(ContinuousCost h_value) override { NB_OVERRIDE_PURE(on_new_best_h_value, h_value); }
    void on_end_search(uint64_t num_reached_fluent_atoms,
                       uint64_t num_reached_derived_atoms,
                       uint64_t num_states,
                       uint64_t num_nodes,
                       uint64_t num_actions,
                       uint64_t num_axioms) override
    {
        NB_OVERRIDE_PURE(on_end_search, num_reached_fluent_atoms, num_reached_derived_atoms, num_states, num_nodes, num_actions, num_axioms);
    }
    void on_solved(const Plan& plan) override { NB_OVERRIDE_PURE(on_solved, plan); }
    void on_unsolvable() override { NB_OVERRIDE_PURE(on_unsolvable); }
    void on_exhausted() override { NB_OVERRIDE_PURE(on_exhausted); }
    const gbfs_lazy::Statistics& get_statistics() const override { NB_OVERRIDE_PURE(get_statistics); }
};

/// @brief Co-occurrence rows as one index list per atom, in atom-index order.
///
/// Empty rows are kept so the result stays indexable by atom, the same convention as
/// `landing_state_by_atom`.
std::vector<IndexList> co_occurrence_as_index_lists(const std::vector<FlatBitset>& rows)
{
    auto result = std::vector<IndexList> {};
    result.reserve(rows.size());
    for (const auto& row : rows)
    {
        auto indices = IndexList {};
        for (const auto index : row)
        {
            indices.push_back(index);
        }
        result.push_back(std::move(indices));
    }
    return result;
}

void bind_module_definitions(nb::module_& m)
{
    /* Enums */
    nb::enum_<SearchContextImpl::SymmetryPruning>(m, "SymmetryPruning")
        .value("OFF", SearchContextImpl::SymmetryPruning::OFF)
        .value("GI", SearchContextImpl::SymmetryPruning::GI)
        .value("WL1", SearchContextImpl::SymmetryPruning::WL1)
        .export_values();

    nb::enum_<SearchNodeStatus>(m, "SearchNodeStatus")
        .value("NEW", SearchNodeStatus::NEW)
        .value("OPEN", SearchNodeStatus::OPEN)
        .value("CLOSED", SearchNodeStatus::CLOSED)
        .value("DEAD_END", SearchNodeStatus::DEAD_END)
        .value("GOAL", SearchNodeStatus::GOAL)
        .export_values();

    nb::enum_<SearchStatus>(m, "SearchStatus")
        .value("IN_PROGRESS", SearchStatus::IN_PROGRESS)
        .value("OUT_OF_TIME", SearchStatus::OUT_OF_TIME)
        .value("OUT_OF_MEMORY", SearchStatus::OUT_OF_MEMORY)
        .value("OUT_OF_STATES", SearchStatus::OUT_OF_STATES)
        .value("FAILED", SearchStatus::FAILED)
        .value("EXHAUSTED", SearchStatus::EXHAUSTED)
        .value("SOLVED", SearchStatus::SOLVED)
        .value("UNSOLVABLE", SearchStatus::UNSOLVABLE)
        .value("CANCELED", SearchStatus::CANCELED)
        .export_values();

    nb::enum_<BeamNoveltyMode>(m, "BeamNoveltyMode")
        .value("ALL_TESTED", BeamNoveltyMode::ALL_TESTED)
        .value("SURVIVORS_ONLY", BeamNoveltyMode::SURVIVORS_ONLY)
        .export_values();

    nb::enum_<astar_iw::NoveltyFeatureMode>(m, "AStarIWNoveltyFeatureMode")
        .value("CLASSICAL", astar_iw::NoveltyFeatureMode::CLASSICAL)
        .value("ABSTRACTED", astar_iw::NoveltyFeatureMode::ABSTRACTED)
        .value("BASE_ABSTRACTED", astar_iw::NoveltyFeatureMode::BASE_ABSTRACTED);

    nb::enum_<match_tree::SplitMetricEnum>(m, "MatchTreeSplitMetric")
        .value("FREQUENCY", match_tree::SplitMetricEnum::FREQUENCY)
        .value("GINI", match_tree::SplitMetricEnum::GINI);

    nb::enum_<match_tree::SplitStrategyEnum>(m, "MatchTreeSplitStrategy")  //
        .value("DYNAMIC", match_tree::SplitStrategyEnum::DYNAMIC);

    nb::enum_<match_tree::OptimizationDirectionEnum>(m, "MatchTreeOptimizationDirection")
        .value("MINIMIZE", match_tree::OptimizationDirectionEnum::MINIMIZE)
        .value("MAXIMIZE", match_tree::OptimizationDirectionEnum::MAXIMIZE);

    /* SearchContext */

    nb::class_<SearchContextImpl::GroundedOptions>(m, "GroundedOptions")  //
        .def(nb::init<>());

    nb::class_<SearchContextImpl::LiftedOptions::ExhaustiveOptions>(m, "LiftedExhaustiveOptions")  //
        .def(nb::init<>());

    nb::class_<SearchContextImpl::LiftedOptions::KPKCOptions>(m, "LiftedKPKCOptions")  //
        .def(nb::init<>())
        .def(nb::init<SearchContextImpl::SymmetryPruning>(), "symmetry_pruning"_a);

    nb::class_<SearchContextImpl::LiftedOptions>(m, "LiftedOptions")  //
        .def(nb::init<>())
        .def(nb::init<SearchContextImpl::LiftedOptions::VariantOption>(), "variant_options"_a);

    nb::class_<SearchContextImpl::Options>(m, "SearchContextOptions")
        .def(nb::init<>())
        .def(nb::init<SearchContextImpl::SearchMode>(), "mode"_a)
        .def_rw("mode", &SearchContextImpl::Options::mode);

    nb::class_<SearchContextImpl>(m, "SearchContext")
        .def_static(
            "create",
            [](const fs::path& domain_filepath, const fs::path& problem_filepath, const SearchContextImpl::Options& options) -> SearchContext
            { return SearchContextImpl::create(domain_filepath, problem_filepath, options); },
            "domain_filepath"_a,
            "problem_filepath"_a,
            "options"_a)
        .def_static("create", nb::overload_cast<Problem, const SearchContextImpl::Options&>(&SearchContextImpl::create), "problem"_a, "options"_a)
        .def_static("create",
                    nb::overload_cast<Problem, ApplicableActionGenerator, StateRepository>(&SearchContextImpl::create),
                    "problem"_a,
                    "applicable_action_generator"_a,
                    "state_repository"_a)
        .def("get_problem", &SearchContextImpl::get_problem)
        .def("get_applicable_action_generator", &SearchContextImpl::get_applicable_action_generator)
        .def("get_state_repository", &SearchContextImpl::get_state_repository)
        .def("release_parallel_memory", &SearchContextImpl::release_parallel_memory, "clear_shared_caches"_a = false);

    /* GeneralizedSearchContext */
    nb::class_<GeneralizedSearchContextImpl>(m, "GeneralizedSearchContext")
        .def_static(
            "create",
            [](const fs::path& domain_filepath, const std::vector<fs::path>& problem_filepaths, const SearchContextImpl::Options& options)
                -> GeneralizedSearchContext { return GeneralizedSearchContextImpl::create(domain_filepath, problem_filepaths, options); },
            "domain_filepath"_a,
            "problem_filepaths"_a,
            "options"_a)
        .def_static(
            "create",
            [](const fs::path& domain_filepath, const fs::path& problems_directory, const SearchContextImpl::Options& options) -> GeneralizedSearchContext
            { return GeneralizedSearchContextImpl::create(domain_filepath, problems_directory, options); },
            "domain_filepath"_a,
            "problems_directory"_a,
            "options"_a)
        .def_static("create",
                    nb::overload_cast<GeneralizedProblem, const SearchContextImpl::Options&>(&GeneralizedSearchContextImpl::create),
                    "generalized_problem"_a,
                    "options"_a)
        .def_static("create",
                    nb::overload_cast<GeneralizedProblem, SearchContextList>(&GeneralizedSearchContextImpl::create),
                    "generalized_problem"_a,
                    "search_contexts"_a)
        .def("get_generalized_problem", &GeneralizedSearchContextImpl::get_generalized_problem)
        .def("get_search_contexts", &GeneralizedSearchContextImpl::get_search_contexts);

    /* PackedState */
    nb::class_<PackedStateImpl>(m, "PackedState")
        .def("__hash__", [](const PackedStateImpl& self) { return loki::Hash<PackedStateImpl> {}(self); })
        .def("__eq__", [](const PackedStateImpl& lhs, const PackedStateImpl& rhs) { return loki::EqualTo<PackedStateImpl> {}(lhs, rhs); });

    /* State */
    nb::class_<State>(m, "State")  //
        .def("__str__", [](const State& self) { return to_string(self); })
        .def("__hash__", [](const State& self) { return loki::Hash<State> {}(self); })
        .def("__eq__", [](const State& lhs, const State& rhs) { return loki::EqualTo<State> {}(lhs, rhs); })
        .def(
            "get_fluent_atoms",
            [](const State& self)
            {
                return nb::make_iterator(nb::type<nb::iterator>(),
                                         "Iterator over fluent state atom indices.",
                                         self.get_atoms<FluentTag>().begin(),
                                         self.get_atoms<FluentTag>().end());
            },
            nb::keep_alive<0, 1>())
        .def(
            "get_derived_atoms",
            [](const State& self)
            {
                return nb::make_iterator(nb::type<nb::iterator>(),
                                         "Iterator over derived state atom indices.",
                                         self.get_atoms<DerivedTag>().begin(),
                                         self.get_atoms<DerivedTag>().end());
            },
            nb::keep_alive<0, 1>())
        .def("get_numeric_variables",
             [](const State& self) { return std::vector<double>(self.get_numeric_variables().begin(), self.get_numeric_variables().end()); })
        .def("literal_holds", &State::literal_holds<FluentTag>, nb::rv_policy::copy, "literal"_a)
        .def("literal_holds", &State::literal_holds<DerivedTag>, nb::rv_policy::copy, "literal"_a)
        .def("literal_holds", &State::literals_hold<FluentTag>, nb::rv_policy::copy, "literals"_a)
        .def("literal_holds", &State::literals_hold<DerivedTag>, nb::rv_policy::copy, "literals"_a)
        .def("get_index", &State::get_index);
    nb::bind_vector<StateList>(m, "StateList");

    /* Plan */
    nb::class_<Plan>(m, "Plan")  //
        .def(nb::init<SearchContext, StateList, GroundActionList, ContinuousCost>(), "search_context"_a, "states"_a, "actions"_a, "cost"_a)
        .def("__str__", [](const Plan& self) { return to_string(self); })
        .def("__len__", &Plan::get_length)
        .def("get_search_context", &Plan::get_search_context)
        .def("get_states", &Plan::get_states, nb::rv_policy::copy)
        .def("get_actions", &Plan::get_actions, nb::rv_policy::copy)
        .def("get_cost", &Plan::get_cost);

    /* PartiallyOrderedPlan*/
    nb::class_<PartiallyOrderedPlan>(m, "PartiallyOrderedPlan")  //
        .def(nb::init<Plan>(), "total_ordered_plan"_a)
        .def("__str__", [](const PartiallyOrderedPlan& self) { return to_string(self); })
        .def("compute_totally_ordered_plan_with_maximal_makespan", &PartiallyOrderedPlan::compute_t_o_plan_with_maximal_makespan)
        .def("get_totally_ordered_plan", &PartiallyOrderedPlan::get_t_o_plan, nb::rv_policy::reference_internal)  // Plan is immutable
        .def("get_graph", [](const PartiallyOrderedPlan& self) { return PyImmutable<graphs::DynamicDigraph>(self.get_graph()); });

    /* SatisficingBindingGenerator */
    nb::class_<satisficing_binding_generator::IEventHandler>(m,
                                                             "ISatisficingBindingGeneratorEventHandler");  //
    nb::class_<satisficing_binding_generator::DefaultEventHandlerImpl, satisficing_binding_generator::IEventHandler>(
        m,
        "DefaultSatisficingBindingGeneratorEventHandler")
        .def_static("create", &satisficing_binding_generator::DefaultEventHandlerImpl::create, "quiet"_a = true);

    /* ConjunctiveConditionSatisficingBindingGenerator */
    nb::class_<ConjunctiveConditionSatisficingBindingGenerator>(m, "ConjunctiveConditionSatisficingBindingGenerator")  //
        .def(nb::init<ConjunctiveCondition, Problem>(), "conjunctive_condition"_a, "problem"_a)
        .def(
            "generate_ground_conjunctions",
            [](ConjunctiveConditionSatisficingBindingGenerator& self, const State& state, size_t max_num_groundings)
            {
                auto result = std::vector<
                    std::pair<ObjectList, std::tuple<GroundLiteralList<StaticTag>, GroundLiteralList<FluentTag>, GroundLiteralList<DerivedTag>>>> {};

                auto count = size_t(0);
                for (const auto& ground_conjunction : self.create_ground_conjunction_generator(state))
                {
                    if (count >= max_num_groundings)
                    {
                        break;
                    }
                    result.push_back(ground_conjunction);
                    ++count;
                }
                return result;
            },
            "state"_a,
            "max_num_groundings"_a);

    nb::class_<ActionSatisficingBindingGenerator>(m, "ActionSatisficingBindingGenerator")  //
        .def(nb::init<Action, Problem>(), "action"_a, "problem"_a)
        .def(
            "generate_ground_conjunctions",
            [](ConjunctiveConditionSatisficingBindingGenerator& self, const State& state, size_t max_num_groundings)
            {
                auto result = std::vector<
                    std::pair<ObjectList, std::tuple<GroundLiteralList<StaticTag>, GroundLiteralList<FluentTag>, GroundLiteralList<DerivedTag>>>> {};

                auto count = size_t(0);
                for (const auto& ground_conjunction : self.create_ground_conjunction_generator(state))
                {
                    if (count >= max_num_groundings)
                    {
                        break;
                    }
                    result.push_back(ground_conjunction);
                    ++count;
                }
                return result;
            },
            "state"_a,
            "max_num_groundings"_a);

    nb::class_<AxiomSatisficingBindingGenerator>(m, "AxiomSatisficingBindingGenerator")  //
        .def(nb::init<Axiom, Problem>(), "axiom"_a, "problem"_a)
        .def(
            "generate_ground_conjunctions",
            [](ConjunctiveConditionSatisficingBindingGenerator& self, const State& state, size_t max_num_groundings)
            {
                auto result = std::vector<
                    std::pair<ObjectList, std::tuple<GroundLiteralList<StaticTag>, GroundLiteralList<FluentTag>, GroundLiteralList<DerivedTag>>>> {};

                auto count = size_t(0);
                for (const auto& ground_conjunction : self.create_ground_conjunction_generator(state))
                {
                    if (count >= max_num_groundings)
                    {
                        break;
                    }
                    result.push_back(ground_conjunction);
                    ++count;
                }
                return result;
            },
            "state"_a,
            "max_num_groundings"_a);

    /* ApplicableActionGenerators */
    m.def("is_applicable", nb::overload_cast<GroundAction, const State&>(&search::is_applicable), "action"_a, "state"_a);
    m.def("compute_transition_novel_fluent_atom_indices_read_only",
          &compute_transition_novel_fluent_atom_indices_read_only,
          "pruning_strategy"_a,
          "state"_a,
          "successor_state"_a);

    nb::class_<IApplicableActionGenerator>(m, "IApplicableActionGenerator")
        .def("get_problem", &IApplicableActionGenerator::get_problem)
        .def(
            "generate_applicable_actions",
            [](IApplicableActionGenerator& self, const State& state)
            {
                auto actions = GroundActionList();
                for (const auto& action : self.create_applicable_action_generator(state))
                {
                    actions.push_back(action);
                }
                return actions;
            },
            nb::keep_alive<0, 1>(),
            "state"_a);

    // Lifted Exhaustive
    nb::class_<ExhaustiveLiftedApplicableActionGeneratorImpl::IEventHandler>(m,
                                                                             "IExhaustiveLiftedApplicableActionGeneratorEventHandler");  //
    nb::class_<ExhaustiveLiftedApplicableActionGeneratorImpl::DefaultEventHandlerImpl, ExhaustiveLiftedApplicableActionGeneratorImpl::IEventHandler>(
        m,
        "DefaultExhaustiveLiftedApplicableActionGeneratorEventHandler")
        .def_static("create", &ExhaustiveLiftedApplicableActionGeneratorImpl::DefaultEventHandlerImpl::create, "quiet"_a = true);
    nb::class_<ExhaustiveLiftedApplicableActionGeneratorImpl::DebugEventHandlerImpl, ExhaustiveLiftedApplicableActionGeneratorImpl::IEventHandler>(
        m,
        "DebugExhaustiveLiftedApplicableActionGeneratorEventHandler")  //
        .def_static("create", &ExhaustiveLiftedApplicableActionGeneratorImpl::DebugEventHandlerImpl::create, "quiet"_a = true);
    nb::class_<ExhaustiveLiftedApplicableActionGeneratorImpl, IApplicableActionGenerator>(m,
                                                                                          "ExhaustiveLiftedApplicableActionGenerator")  //
        .def_static("create",
                    &ExhaustiveLiftedApplicableActionGeneratorImpl::create,
                    "problem"_a,
                    "event_handler"_a = nullptr,
                    "binding_event_handler"_a = nullptr);

    // Lifted KPKC
    nb::class_<KPKCLiftedApplicableActionGeneratorImpl::IEventHandler>(m,
                                                                       "IKPKCLiftedApplicableActionGeneratorEventHandler");  //
    nb::class_<KPKCLiftedApplicableActionGeneratorImpl::DefaultEventHandlerImpl, KPKCLiftedApplicableActionGeneratorImpl::IEventHandler>(
        m,
        "DefaultKPKCLiftedApplicableActionGeneratorEventHandler")
        .def_static("create", &KPKCLiftedApplicableActionGeneratorImpl::DefaultEventHandlerImpl::create, "quiet"_a = true);
    nb::class_<KPKCLiftedApplicableActionGeneratorImpl::DebugEventHandlerImpl, KPKCLiftedApplicableActionGeneratorImpl::IEventHandler>(
        m,
        "DebugKPKCLiftedApplicableActionGeneratorEventHandler")  //
        .def_static("create", &KPKCLiftedApplicableActionGeneratorImpl::DebugEventHandlerImpl::create, "quiet"_a = true);
    nb::class_<KPKCLiftedApplicableActionGeneratorImpl, IApplicableActionGenerator>(m,
                                                                                    "KPKCLiftedApplicableActionGenerator")  //
        .def_static("create",
                    &KPKCLiftedApplicableActionGeneratorImpl::create,
                    "problem"_a,
                    "options"_a,
                    "event_handler"_a = nullptr,
                    "binding_event_handler"_a = nullptr);

    // Grounded
    nb::class_<GroundedApplicableActionGeneratorImpl::IEventHandler>(m,
                                                                     "IGroundedApplicableActionGeneratorEventHandler");  //
    nb::class_<GroundedApplicableActionGeneratorImpl::DefaultEventHandlerImpl, GroundedApplicableActionGeneratorImpl::IEventHandler>(
        m,
        "DefaultGroundedApplicableActionGeneratorEventHandler")  //
        .def_static("create", &GroundedApplicableActionGeneratorImpl::DefaultEventHandlerImpl::create, "quiet"_a = true);
    nb::class_<GroundedApplicableActionGeneratorImpl::DebugEventHandlerImpl, GroundedApplicableActionGeneratorImpl::IEventHandler>(
        m,
        "DebugGroundedApplicableActionGeneratorEventHandler")  //
        .def_static("create", &GroundedApplicableActionGeneratorImpl::DebugEventHandlerImpl::create, "quiet"_a = true);
    nb::class_<GroundedApplicableActionGeneratorImpl, IApplicableActionGenerator>(m, "GroundedApplicableActionGenerator");

    /* IAxiomEvaluator */
    nb::class_<IAxiomEvaluator>(m, "IAxiomEvaluator")  //
        .def("get_problem", &IAxiomEvaluator::get_problem);

    // Lifted: Exhaustive
    nb::class_<ExhaustiveLiftedAxiomEvaluatorImpl::IEventHandler>(m, "IExhaustiveLiftedAxiomEvaluatorEventHandler");  //
    nb::class_<ExhaustiveLiftedAxiomEvaluatorImpl::DefaultEventHandlerImpl, ExhaustiveLiftedAxiomEvaluatorImpl::IEventHandler>(
        m,
        "DefaultExhaustiveLiftedAxiomEvaluatorEventHandler")  //
        .def_static("create", &ExhaustiveLiftedAxiomEvaluatorImpl::DefaultEventHandlerImpl::create, "quiet"_a = true);
    nb::class_<ExhaustiveLiftedAxiomEvaluatorImpl::DebugEventHandlerImpl, ExhaustiveLiftedAxiomEvaluatorImpl::IEventHandler>(
        m,
        "DebugExhaustiveLiftedAxiomEvaluatorEventHandler")  //
        .def_static("create", &ExhaustiveLiftedAxiomEvaluatorImpl::DebugEventHandlerImpl::create, "quiet"_a = true);
    nb::class_<ExhaustiveLiftedAxiomEvaluatorImpl, IAxiomEvaluator>(m, "ExhaustiveLiftedAxiomEvaluator")  //
        .def_static("create", &ExhaustiveLiftedAxiomEvaluatorImpl::create, "problem"_a, "event_handler"_a = nullptr, "binding_event_handler"_a = nullptr);

    // Lifted: KPKC
    nb::class_<KPKCLiftedAxiomEvaluatorImpl::IEventHandler>(m, "IKPKCLiftedAxiomEvaluatorEventHandler");  //
    nb::class_<KPKCLiftedAxiomEvaluatorImpl::DefaultEventHandlerImpl, KPKCLiftedAxiomEvaluatorImpl::IEventHandler>(
        m,
        "DefaultKPKCLiftedAxiomEvaluatorEventHandler")  //
        .def_static("create", &KPKCLiftedAxiomEvaluatorImpl::DefaultEventHandlerImpl::create, "quiet"_a = true);
    nb::class_<KPKCLiftedAxiomEvaluatorImpl::DebugEventHandlerImpl, KPKCLiftedAxiomEvaluatorImpl::IEventHandler>(
        m,
        "DebugKPKCLiftedAxiomEvaluatorEventHandler")  //
        .def_static("create", &KPKCLiftedAxiomEvaluatorImpl::DebugEventHandlerImpl::create, "quiet"_a = true);
    nb::class_<KPKCLiftedAxiomEvaluatorImpl, IAxiomEvaluator>(m, "KPKCLiftedAxiomEvaluator")  //
        .def_static("create", &KPKCLiftedAxiomEvaluatorImpl::create, "problem"_a, "event_handler"_a = nullptr, "binding_event_handler"_a = nullptr);

    // Grounded
    nb::class_<GroundedAxiomEvaluatorImpl::IEventHandler>(m, "IGroundedAxiomEvaluatorEventHandler");  //
    nb::class_<GroundedAxiomEvaluatorImpl::DefaultEventHandlerImpl, GroundedAxiomEvaluatorImpl::IEventHandler>(m,
                                                                                                               "DefaultGroundedAxiomEvaluatorEventHandler")  //
        .def_static("create", &GroundedAxiomEvaluatorImpl::DefaultEventHandlerImpl::create, "quiet"_a = true);
    nb::class_<GroundedAxiomEvaluatorImpl::DebugEventHandlerImpl, GroundedAxiomEvaluatorImpl::IEventHandler>(m,
                                                                                                             "DebugGroundedAxiomEvaluatorEventHandler")  //
        .def_static("create", &GroundedAxiomEvaluatorImpl::DefaultEventHandlerImpl::create, "quiet"_a = true);
    nb::class_<GroundedAxiomEvaluatorImpl, IAxiomEvaluator>(m, "GroundedAxiomEvaluator");

    /* StateRepositoryImpl */
    m.def("compute_state_metric_value", &compute_state_metric_value, "state"_a);

    nb::class_<StateRepositoryImpl>(m, "StateRepository")
        // `create` is overloaded (the other overload requests private interning tables, which
        // is an internal detail of the batched parallel rollout path), so disambiguate.
        .def_static("create", static_cast<StateRepository (*)(AxiomEvaluator)>(&StateRepositoryImpl::create), "axiom_evaluator"_a)
        .def("get_or_create_initial_state", &StateRepositoryImpl::get_or_create_initial_state, nb::rv_policy::copy)
        .def(
            "get_or_create_state",
            [](StateRepositoryImpl& self, const GroundAtomList<FluentTag>& fluent_atoms, const ContinuousCostList& numeric_variables)
            { return self.get_or_create_state(fluent_atoms, FlatDoubleList(numeric_variables.begin(), numeric_variables.end())); },
            "fluent_atoms"_a,
            "numeric_variables"_a,
            nb::rv_policy::copy)
        .def("get_or_create_successor_state",
             nb::overload_cast<const State&, GroundAction, ContinuousCost>(&StateRepositoryImpl::get_or_create_successor_state),
             "state"_a,
             "action"_a,
             "state_metric_value"_a,
             nb::rv_policy::copy)
        .def("get_state", &StateRepositoryImpl::get_state, nb::rv_policy::copy, "packed_state"_a)
        .def("get_state_index", &StateRepositoryImpl::get_state_index, nb::rv_policy::copy, "packed_state"_a)
        // The inverse of `get_state_index`, so a caller holding an index -- from a search's own
        // event handler, say -- can get back to the state without having retained it. Returned
        // by reference and kept alive by the repository: `PackedState` points into the
        // repository's own (node-stable) map, and copying it would detach it from that owner.
        .def(
            "get_packed_state",
            [](const StateRepositoryImpl& self, Index state_index)
            {
                if (state_index >= self.get_state_count())
                {
                    throw nb::index_error("state index out of range");
                }
                return self.get_packed_state(state_index);
            },
            nb::rv_policy::reference_internal,
            "state_index"_a)
        .def("get_state_count", &StateRepositoryImpl::get_state_count, nb::rv_policy::copy)
        .def("get_reached_fluent_ground_atoms_bitset", &StateRepositoryImpl::get_reached_fluent_ground_atoms_bitset, nb::rv_policy::copy)
        .def("get_reached_derived_ground_atoms_bitset", &StateRepositoryImpl::get_reached_derived_ground_atoms_bitset, nb::rv_policy::copy)
        // Exposed so a test can assert that an action-level novelty precheck decides without
        // building successors. Counts every construction entry point, staged ones included.
        .def("get_num_successor_state_constructions", &StateRepositoryImpl::get_num_successor_state_constructions, nb::rv_policy::copy)
        .def(
            "collect_action_change_effect_fluent_atom_indices",
            [](StateRepositoryImpl& self, const State& state, GroundAction action)
            {
                auto add_fluent_atom_indices = iw::AtomIndexList {};
                auto del_fluent_atom_indices = iw::AtomIndexList {};
                self.collect_action_change_effect_fluent_atom_indices(state, action, add_fluent_atom_indices, del_fluent_atom_indices);
                return std::make_pair(add_fluent_atom_indices, del_fluent_atom_indices);
            },
            "state"_a,
            "action"_a);

    /* Grounder */

    nb::class_<match_tree::Options>(m, "MatchTreeOptions")
        .def(nb::init<>())
        .def_rw("enable_dump_dot_file", &match_tree::Options::enable_dump_dot_file)
        .def_rw("output_dot_file", &match_tree::Options::output_dot_file)
        .def_rw("max_num_nodes", &match_tree::Options::max_num_nodes)
        .def_rw("split_strategy", &match_tree::Options::split_strategy)
        .def_rw("split_metric", &match_tree::Options::split_metric)
        .def_rw("optimization_direction", &match_tree::Options::optimization_direction);

    nb::class_<IGrounder>(m, "IGrounder")  //
        .def("create_ground_actions", &IGrounder::create_ground_actions)
        .def("create_ground_axioms", &IGrounder::create_ground_axioms)
        .def("create_grounded_axiom_evaluator",
             &IGrounder::create_grounded_axiom_evaluator,
             "match_tree_options"_a,
             "axiom_evaluator_event_handler"_a = nullptr)
        .def("create_grounded_applicable_action_generator",
             &IGrounder::create_grounded_applicable_action_generator,
             "match_tree_options"_a,
             "axiom_evaluator_event_handler"_a = nullptr);

    nb::class_<LiftedGrounder, IGrounder>(m, "LiftedGrounder")  //
        .def(nb::init<Problem>(), "problem"_a);

    /* Heuristics */
    nb::class_<PreferredActions>(m, "PreferredActions")  //
        .def_rw("data", &PreferredActions::data);

    nb::class_<IHeuristic, IPyHeuristic>(m, "IHeuristic")  //
        .def(nb::init<>())
        .def("compute_heuristic", &IHeuristic::compute_heuristic, "state"_a, "goal"_a = nullptr)
        .def("get_preferred_actions", &IHeuristic::get_preferred_actions, nb::rv_policy::reference_internal);

    nb::class_<BlindHeuristicImpl, IHeuristic>(m, "BlindHeuristic")  //
        .def_static("create", &BlindHeuristicImpl::create, "problem"_a);

    nb::class_<PerfectHeuristicImpl, IHeuristic>(m, "PerfectHeuristic")  //
        .def_static("create", &PerfectHeuristicImpl::create, "search_context"_a);

    nb::class_<MaxHeuristicImpl, IHeuristic>(m, "MaxHeuristic")  //
        .def_static("create", &MaxHeuristicImpl::create, "delete_relaxed_problem_explorator"_a);

    nb::class_<AddHeuristicImpl, IHeuristic>(m, "AddHeuristic")  //
        .def_static("create", &AddHeuristicImpl::create, "delete_relaxed_problem_explorator"_a);

    nb::class_<SetAddHeuristicImpl, IHeuristic>(m, "SetAddHeuristic")  //
        .def_static("create", &SetAddHeuristicImpl::create, "delete_relaxed_problem_explorator"_a);

    nb::class_<FFHeuristicImpl, IHeuristic>(m, "FFHeuristic")  //
        .def_static("create", &FFHeuristicImpl::create, "delete_relaxed_problem_explorator"_a);

    nb::class_<H2HeuristicImpl, IHeuristic>(m, "H2Heuristic")  //
        .def_static("create", &H2HeuristicImpl::create, "delete_relaxed_problem_explorator"_a);

    /* Landmarks */

    nb::class_<landmarks::FactLandmarkGeneratorOptions>(m, "FactLandmarkGeneratorOptions")  //
        .def(nb::init<>())
        .def_rw("include_positive_goal_facts", &landmarks::FactLandmarkGeneratorOptions::include_positive_goal_facts)
        .def_rw("compute_greedy_necessary_orderings", &landmarks::FactLandmarkGeneratorOptions::compute_greedy_necessary_orderings)
        .def_rw("max_disjunctive_landmark_size", &landmarks::FactLandmarkGeneratorOptions::max_disjunctive_landmark_size)
        .def_rw("max_disjunctive_landmark_depth", &landmarks::FactLandmarkGeneratorOptions::max_disjunctive_landmark_depth);

    nb::class_<landmarks::FactLandmarkGraphImpl>(m, "FactLandmarkGraph")  //
        .def("get_problem", &landmarks::FactLandmarkGraphImpl::get_problem)
        .def("get_landmark_atom_indices", &landmarks::FactLandmarkGraphImpl::get_landmark_atom_indices)
        .def("get_landmark_atoms", &landmarks::FactLandmarkGraphImpl::get_landmark_atoms)
        .def("get_disjunctive_landmarks", &landmarks::FactLandmarkGraphImpl::get_disjunctive_landmarks)
        .def("get_disjunctive_landmark_atom_indices", &landmarks::FactLandmarkGraphImpl::get_disjunctive_landmark_atom_indices)
        .def("is_landmark",
             static_cast<bool (landmarks::FactLandmarkGraphImpl::*)(Index) const>(&landmarks::FactLandmarkGraphImpl::is_landmark),
             "atom_index"_a)
        .def("is_landmark",
             static_cast<bool (landmarks::FactLandmarkGraphImpl::*)(GroundAtom<FluentTag>) const>(&landmarks::FactLandmarkGraphImpl::is_landmark),
             "atom"_a)
        .def("get_achieved_landmark_atom_indices", &landmarks::FactLandmarkGraphImpl::get_achieved_landmark_atom_indices, "state"_a)
        .def("get_unachieved_landmark_atom_indices", &landmarks::FactLandmarkGraphImpl::get_unachieved_landmark_atom_indices, "state"_a)
        .def("get_achiever_action_indices", &landmarks::FactLandmarkGraphImpl::get_achiever_action_indices, "landmark_atom_index"_a)
        .def("get_achievers", &landmarks::FactLandmarkGraphImpl::get_achievers, "landmark_atom_index"_a)
        .def("get_first_achiever_action_indices", &landmarks::FactLandmarkGraphImpl::get_first_achiever_action_indices, "landmark_atom_index"_a)
        .def("get_first_achievers", &landmarks::FactLandmarkGraphImpl::get_first_achievers, "landmark_atom_index"_a)
        .def("get_unique_achiever_action_index", &landmarks::FactLandmarkGraphImpl::get_unique_achiever_action_index, "landmark_atom_index"_a)
        // `get_unique_achiever` returns `std::optional<GroundAction>` where `GroundAction` is an interned
        // `const GroundActionImpl*` owned by the problem's repository. Without an explicit policy nanobind's
        // default `automatic` treats the returned pointer as `take_ownership` and `delete`s it when the Python
        // wrapper is collected -- a double-free of repository-owned memory. `reference` (the same policy the
        // datasets bindings use for single `GroundAction` returns) wraps it without taking ownership; the
        // `std::optional` caster forwards this policy to the inner pointer.
        .def("get_unique_achiever", &landmarks::FactLandmarkGraphImpl::get_unique_achiever, nb::rv_policy::reference, "landmark_atom_index"_a)
        .def("is_landmark_achiever", &landmarks::FactLandmarkGraphImpl::is_landmark_achiever, "action"_a)
        .def("is_first_landmark_achiever", &landmarks::FactLandmarkGraphImpl::is_first_landmark_achiever, "action"_a)
        .def("is_unique_landmark_achiever", &landmarks::FactLandmarkGraphImpl::is_unique_landmark_achiever, "action"_a)
        .def("get_landmarks_achieved_by_action", &landmarks::FactLandmarkGraphImpl::get_landmarks_achieved_by_action, "action"_a)
        .def("get_landmarks_uniquely_achieved_by_action", &landmarks::FactLandmarkGraphImpl::get_landmarks_uniquely_achieved_by_action, "action"_a)
        .def("get_predecessors", &landmarks::FactLandmarkGraphImpl::get_predecessors, "landmark_atom_index"_a)
        .def("get_successors", &landmarks::FactLandmarkGraphImpl::get_successors, "landmark_atom_index"_a);

    nb::class_<landmarks::ApproximateFactLandmarkGenerator>(m, "ApproximateFactLandmarkGenerator")  //
        .def_static("create",
                    &landmarks::ApproximateFactLandmarkGenerator::create,
                    "grounder"_a,
                    "options"_a = landmarks::FactLandmarkGeneratorOptions());

    /* Algorithms */

    // SearchResult
    nb::class_<SearchResult>(m, "SearchResult")
        .def(nb::init<>())
        .def_rw("status", &SearchResult::status)
        .def_rw("plan", &SearchResult::plan)
        .def_rw("goal_state", &SearchResult::goal_state);

    // GoalStrategy
    nb::class_<IGoalStrategy, IPyGoalStrategy>(m, "IGoalStrategy")
        .def(nb::init<>())
        .def("test_static_goal", &IGoalStrategy::test_static_goal)
        .def("test_dynamic_goal", &IGoalStrategy::test_dynamic_goal, "state"_a);

    nb::class_<ProblemGoalStrategyImpl, IGoalStrategy>(m, "ProblemGoalStrategy")  //
        .def_static("create", &ProblemGoalStrategyImpl::create, "problem"_a, "goal_condition"_a = std::nullopt);

    nb::class_<ProblemMultiGoalStrategyImpl, IGoalStrategy>(m, "ProblemMultiGoalStrategy")  //
        .def_static("create",
                    static_cast<ProblemMultiGoalStrategy (*)(Problem, std::vector<GroundConjunctiveCondition>)>(&ProblemMultiGoalStrategyImpl::create),
                    "problem"_a,
                    "goal_conditions"_a);

    nb::class_<ILayerOrderingStrategy, IPyLayerOrderingStrategy>(m, "ILayerOrderingStrategy")
        .def(nb::init<>())
        .def("supports_eager_scoring", &ILayerOrderingStrategy::supports_eager_scoring)
        .def("score_state", &ILayerOrderingStrategy::score_state, "state"_a, "g_value"_a)
        .def("prefer_higher_scores", &ILayerOrderingStrategy::prefer_higher_scores)
        .def("order_layer", &ILayerOrderingStrategy::order_layer, "states"_a, "g_value"_a);

    nb::class_<InOrderLayerOrderingStrategyImpl, ILayerOrderingStrategy>(m, "InOrderLayerOrderingStrategy")
        .def(nb::init<>())
        .def_static("create", &InOrderLayerOrderingStrategyImpl::create);

    nb::class_<ReverseOrderLayerOrderingStrategyImpl, ILayerOrderingStrategy>(m, "ReverseOrderLayerOrderingStrategy")
        .def(nb::init<>())
        .def_static("create", &ReverseOrderLayerOrderingStrategyImpl::create);

    nb::class_<RandomizedLayerOrderingStrategyImpl, ILayerOrderingStrategy>(m, "RandomizedLayerOrderingStrategy")
        .def(nb::init<std::optional<uint64_t>>(), "seed"_a = std::nullopt)
        .def_static("create", &RandomizedLayerOrderingStrategyImpl::create, "seed"_a = std::nullopt);

    nb::class_<GoalCountLayerOrderingStrategyImpl, ILayerOrderingStrategy>(m, "GoalCountLayerOrderingStrategy")
        .def(nb::init<Problem, bool>(), "problem"_a, "prefer_more_satisfied_goals"_a = true)
        .def_static("create", &GoalCountLayerOrderingStrategyImpl::create, "problem"_a, "prefer_more_satisfied_goals"_a = true);

    // Declared before the pruning strategies because `LandmarkNoveltyPruningStrategy` uses a
    // default-constructed instance as a default argument, which nanobind converts eagerly.
    nb::enum_<iw::LandmarkDenseLayout>(m, "LandmarkDenseLayout")  //
        .value("RANK_MAJOR", iw::LandmarkDenseLayout::RANK_MAJOR)
        .value("TUPLE_MAJOR", iw::LandmarkDenseLayout::TUPLE_MAJOR);

    nb::class_<iw::LandmarkGrouping>(m, "LandmarkGrouping")  //
        .def(nb::init<>())
        .def_rw("disjunctive_landmarks", &iw::LandmarkGrouping::disjunctive_landmarks)
        .def_rw("unshared_atom_indices", &iw::LandmarkGrouping::unshared_atom_indices)
        .def("is_trivial", &iw::LandmarkGrouping::is_trivial);

    nb::class_<iw::LandmarkNoveltyTableOptions>(m, "LandmarkNoveltyTableOptions")  //
        .def(nb::init<>())
        .def_rw("max_dense_table_bytes", &iw::LandmarkNoveltyTableOptions::max_dense_table_bytes)
        .def_rw("force_dense", &iw::LandmarkNoveltyTableOptions::force_dense)
        .def_rw("dense_layout", &iw::LandmarkNoveltyTableOptions::dense_layout);

    // PruningStrategy
    nb::class_<IPruningStrategy, IPyPruningStrategy>(m, "IPruningStrategy")
        .def(nb::init<>())
        .def("test_prune_initial_state", &IPruningStrategy::test_prune_initial_state, "initial_state"_a)
        .def("test_prune_successor_state", &IPruningStrategy::test_prune_successor_state, "state"_a, "successor_state"_a, "is_new_successor"_a)
        .def("supports_action_add_effect_precheck", &IPruningStrategy::supports_action_add_effect_precheck)
        .def("precheck_requires_delete_effects", &IPruningStrategy::precheck_requires_delete_effects)
        .def("test_transition_novelty_from_add_effects",
             &IPruningStrategy::test_transition_novelty_from_add_effects,
             "state"_a,
             "add_fluent_atom_indices"_a,
             "del_fluent_atom_indices"_a = iw::AtomIndexList {})
        .def("supports_atom_novelty_query", &IPruningStrategy::supports_atom_novelty_query)
        .def("supports_transition_novel_witness_query", &IPruningStrategy::supports_transition_novel_witness_query)
        .def("compute_transition_novel_fluent_atom_indices_read_only",
             [](const IPruningStrategy& self, const State& state, const State& successor_state)
             {
                 auto out_novel_fluent_atom_indices = iw::AtomIndexList {};
                 self.compute_transition_novel_fluent_atom_indices_read_only(state, successor_state, out_novel_fluent_atom_indices);
                 return out_novel_fluent_atom_indices;
             },
             "state"_a,
             "successor_state"_a);

    nb::class_<NoPruningStrategyImpl, IPruningStrategy>(m, "NoPruningStrategy")  //
        .def(nb::init<>())
        .def_static("create", &NoPruningStrategyImpl::create);

    nb::class_<DuplicatePruningStrategyImpl, IPruningStrategy>(m, "DuplicatePruningStrategy")  //
        .def(nb::init<>())
        .def_static("create", &DuplicatePruningStrategyImpl::create);

    nb::class_<iw::ArityZeroNoveltyPruningStrategyImpl, IPruningStrategy>(m, "ArityZeroNoveltyPruningStrategy")  //
        .def(nb::init<State>(), "initial_state"_a)
        .def_static("create", &iw::ArityZeroNoveltyPruningStrategyImpl::create, "initial_state"_a);

    nb::class_<iw::ArityKNoveltyPruningStrategyImpl, IPruningStrategy>(m, "ArityKNoveltyPruningStrategy")  //
        .def(nb::init<size_t, size_t>(), "arity"_a, "num_atoms"_a)
        .def_static("create", &iw::ArityKNoveltyPruningStrategyImpl::create, "arity"_a, "num_atoms"_a, "optimize_root_depth_one_continuation"_a = false);

    // LIW(k): novelty over `(landmark coordinate, free tuple of size <= arity)` pairs. Exposed for
    // callers that drive `find_solution_brfs` with an explicit pruning strategy; `IWOptions`'
    // `landmark_novelty_graph` is the equivalent for the `find_solution_iw` ladder.
    nb::class_<iw::LandmarkNoveltyPruningStrategyImpl, IPruningStrategy>(m, "LandmarkNoveltyPruningStrategy")  //
        .def(nb::init<const landmarks::FactLandmarkGraph&, size_t, size_t, iw::LandmarkNoveltyTableOptions>(),
             "landmarks"_a,
             "arity"_a,
             "num_atoms"_a,
             "table_options"_a = iw::LandmarkNoveltyTableOptions())
        .def_static("create",
                    &iw::LandmarkNoveltyPruningStrategyImpl::create,
                    "landmarks"_a,
                    "arity"_a,
                    "num_atoms"_a,
                    "table_options"_a = iw::LandmarkNoveltyTableOptions(),
                    "grouping"_a = iw::LandmarkGrouping())
        .def_static("make_grouping",
                    &iw::LandmarkNoveltyPruningStrategyImpl::make_grouping,
                    "landmarks"_a,
                    "disjunctive"_a,
                    "unshared_atom_indices"_a = IndexSet())
        // The table's own geometry, so a layout's footprint can be measured rather than inferred.
        .def_prop_ro("table_is_dense", [](const iw::LandmarkNoveltyPruningStrategyImpl& self) { return self.get_novelty_table().is_dense(); })
        .def_prop_ro("table_num_cells", [](const iw::LandmarkNoveltyPruningStrategyImpl& self) { return self.get_novelty_table().get_table_size(); })
        .def_prop_ro("table_num_bytes", [](const iw::LandmarkNoveltyPruningStrategyImpl& self) { return self.get_novelty_table().get_table_bytes(); });


    nb::class_<iw::AbstractedNoveltyPruningStrategyImpl, IPruningStrategy>(m, "AbstractedNoveltyPruningStrategy")  //
        .def(nb::init<Problem, size_t, bool, bool, bool>(),
             "problem"_a,
             "width"_a = 1,
             "base_abstracted"_a = false,
             "preserve_goal_atoms"_a = true,
             "keep_depth_one_novel"_a = false)
        .def_static("create",
                    &iw::AbstractedNoveltyPruningStrategyImpl::create,
                    "problem"_a,
                    "width"_a = 1,
                    "base_abstracted"_a = false,
                    "preserve_goal_atoms"_a = true,
                    "keep_depth_one_novel"_a = false);

    // TransitionOrderingStrategy
    //
    // Only the compiled concrete `LandmarkTransitionOrderingStrategy` is exposed. The CRTP base and the
    // `TransitionOrderingStrategy` concept are compile-time constructs, and a per-transition Python
    // callback would defeat the purpose of the feature, so there is deliberately no subclassable
    // interface / trampoline here.
    nb::class_<LandmarkTransitionOrderingOptions>(m, "LandmarkTransitionOrderingOptions")  //
        .def(nb::init<>())
        .def_rw("prefer_new_landmarks", &LandmarkTransitionOrderingOptions::prefer_new_landmarks)
        .def_rw("prefer_unique_achievers", &LandmarkTransitionOrderingOptions::prefer_unique_achievers)
        .def_rw("prefer_landmark_actions_when_deleting", &LandmarkTransitionOrderingOptions::prefer_landmark_actions_when_deleting)
        .def_rw("prefer_fewer_deleted_landmarks", &LandmarkTransitionOrderingOptions::prefer_fewer_deleted_landmarks);

    nb::class_<LandmarkTransitionOrderingStrategy>(m, "LandmarkTransitionOrderingStrategy")  //
        .def(nb::init<landmarks::FactLandmarkGraph, LandmarkTransitionOrderingOptions>(),
             "landmarks"_a,
             "options"_a = LandmarkTransitionOrderingOptions())
        .def("get_landmarks", &LandmarkTransitionOrderingStrategy::get_landmarks)
        .def("get_options", &LandmarkTransitionOrderingStrategy::get_options, nb::rv_policy::reference_internal);

    // ExplorationStrategy
    nb::class_<IExplorationStrategy, IPyExplorationStrategy>(m, "IExplorationStrategy")
        .def(nb::init<>())
        .def("on_generate_state", &IExplorationStrategy::on_generate_state, "state"_a, "action"_a, "succ_state"_a);

    // AStar_EAGER
    nb::class_<astar_eager::Statistics>(m, "AStarEagerStatistics")  //
        .def(nb::init<>())
        .def("__str__", [](const astar_eager::Statistics& self) { return to_string(self); })
        .def("get_num_generated", &astar_eager::Statistics::get_num_generated)
        .def("get_num_expanded", &astar_eager::Statistics::get_num_expanded)
        .def("get_num_deadends", &astar_eager::Statistics::get_num_deadends)
        .def("get_num_pruned", &astar_eager::Statistics::get_num_pruned)
        .def("get_num_generated_until_f_value", &astar_eager::Statistics::get_num_generated_until_f_value)
        .def("get_num_expanded_until_f_value", &astar_eager::Statistics::get_num_expanded_until_f_value)
        .def("get_num_deadends_until_f_value", &astar_eager::Statistics::get_num_deadends_until_f_value)
        .def("get_num_pruned_until_f_value", &astar_eager::Statistics::get_num_pruned_until_f_value)
        .def("get_search_time_ms", &astar_eager::Statistics::get_search_time_ms);

    nb::class_<astar_eager::IEventHandler, IPyAStarEagerEventHandler>(m,
                                                                      "IAStarEagerEventHandler")  //
        .def(nb::init<>())
        .def("on_expand_state", &astar_eager::IEventHandler::on_expand_state)
        .def("on_expand_goal_state", &astar_eager::IEventHandler::on_expand_goal_state)
        .def("on_generate_state", &astar_eager::IEventHandler::on_generate_state)
        .def("on_generate_state_relaxed", &astar_eager::IEventHandler::on_generate_state_relaxed)
        .def("on_generate_state_not_relaxed", &astar_eager::IEventHandler::on_generate_state_not_relaxed)
        .def("on_close_state", &astar_eager::IEventHandler::on_close_state)
        .def("on_finish_f_layer", &astar_eager::IEventHandler::on_finish_f_layer)
        .def("on_prune_state", &astar_eager::IEventHandler::on_prune_state)
        .def("on_start_search", &astar_eager::IEventHandler::on_start_search)
        .def("on_end_search", &astar_eager::IEventHandler::on_end_search)
        .def("on_solved", &astar_eager::IEventHandler::on_solved)
        .def("on_unsolvable", &astar_eager::IEventHandler::on_unsolvable)
        .def("on_exhausted", &astar_eager::IEventHandler::on_exhausted)
        .def("get_statistics", &astar_eager::IEventHandler::get_statistics);

    nb::class_<astar_eager::DefaultEventHandlerImpl, astar_eager::IEventHandler>(m,
                                                                                 "DefaultAStarEagerEventHandler")  //
        .def(nb::init<Problem, bool>(), "problem"_a, "quiet"_a = true);
    nb::class_<astar_eager::DebugEventHandlerImpl, astar_eager::IEventHandler>(m,
                                                                               "DebugAStarEagerEventHandler")  //
        .def(nb::init<Problem, bool>(), "problem"_a, "quiet"_a = true);

    nb::class_<astar_eager::Options>(m, "AStarEagerOptions")  //
        .def(nb::init<>())
        .def_rw("start_state", &astar_eager::Options::start_state)
        .def_rw("event_handler", &astar_eager::Options::event_handler)
        .def_rw("goal_strategy", &astar_eager::Options::goal_strategy)
        .def_rw("pruning_strategy", &astar_eager::Options::pruning_strategy)
        .def_rw("max_num_states", &astar_eager::Options::max_num_states)
        .def_rw("max_time_in_ms", &astar_eager::Options::max_time_in_ms);

    m.def("find_solution_astar_eager", &astar_eager::find_solution, "search_context"_a, "heuristic"_a, "options"_a);

    // AStarIW
    nb::class_<astar_iw::Statistics>(m, "AStarIWStatistics")
        .def("get_num_generated", &astar_iw::Statistics::get_num_generated)
        .def("get_num_expanded", &astar_iw::Statistics::get_num_expanded)
        .def("get_num_reopened", &astar_iw::Statistics::get_num_reopened)
        .def("get_num_deadends", &astar_iw::Statistics::get_num_deadends)
        .def("get_num_novelty_rejected", &astar_iw::Statistics::get_num_novelty_rejected)
        .def("get_num_stale_g_discarded", &astar_iw::Statistics::get_num_stale_g_discarded)
        .def("get_num_stale_novelty_discarded", &astar_iw::Statistics::get_num_stale_novelty_discarded)
        .def("get_num_states", &astar_iw::Statistics::get_num_states)
        .def("get_num_nodes", &astar_iw::Statistics::get_num_nodes)
        .def("get_search_time_ms", &astar_iw::Statistics::get_search_time_ms);

    nb::class_<astar_iw::IEventHandler, IPyAStarIWEventHandler>(m, "IAStarIWEventHandler")
        .def(nb::init<>())
        .def("on_start_search", &astar_iw::IEventHandler::on_start_search)
        .def("on_generate_state", &astar_iw::IEventHandler::on_generate_state)
        .def("on_expand_state", &astar_iw::IEventHandler::on_expand_state)
        .def("on_expand_goal_state", &astar_iw::IEventHandler::on_expand_goal_state)
        .def("on_reopen_state", &astar_iw::IEventHandler::on_reopen_state)
        .def("on_deadend_state", &astar_iw::IEventHandler::on_deadend_state)
        .def("on_reject_state_novelty", &astar_iw::IEventHandler::on_reject_state_novelty)
        .def("on_discard_stale_g", &astar_iw::IEventHandler::on_discard_stale_g)
        .def("on_discard_stale_novelty", &astar_iw::IEventHandler::on_discard_stale_novelty)
        .def("on_end_search", &astar_iw::IEventHandler::on_end_search)
        .def("on_solved", &astar_iw::IEventHandler::on_solved)
        .def("on_unsolvable", &astar_iw::IEventHandler::on_unsolvable)
        .def("on_exhausted", &astar_iw::IEventHandler::on_exhausted)
        .def("get_statistics", &astar_iw::IEventHandler::get_statistics, nb::rv_policy::reference_internal);

    nb::class_<astar_iw::DefaultEventHandlerImpl, astar_iw::IEventHandler>(m, "DefaultAStarIWEventHandler")
        .def(nb::init<Problem, bool>(), "problem"_a, "quiet"_a = true)
        .def_static("create", &astar_iw::DefaultEventHandlerImpl::create, "problem"_a, "quiet"_a = true);

    nb::class_<astar_iw::Options>(m, "AStarIWOptions")
        .def(nb::init<>())
        .def_rw("start_state", &astar_iw::Options::start_state)
        .def_rw("event_handler", &astar_iw::Options::event_handler)
        .def_rw("goal_strategy", &astar_iw::Options::goal_strategy)
        .def_rw("width", &astar_iw::Options::width)
        .def_rw("novelty_feature_mode", &astar_iw::Options::novelty_feature_mode)
        // Restricts novelty to (landmark atom, free tuple) pairs; CLASSICAL mode only.
        .def_rw("landmark_novelty_graph", &astar_iw::Options::landmark_novelty_graph)
        .def_rw("preserve_goal_atoms", &astar_iw::Options::preserve_goal_atoms)
        .def_rw("heuristic_weight", &astar_iw::Options::heuristic_weight)
        .def_rw("allow_non_novel_root_goal", &astar_iw::Options::allow_non_novel_root_goal)
        // Skips the heuristic on successors novelty would reject; off is only worth it when the
        // heuristic is free, since the probe is then the only thing it adds.
        .def_rw("probe_novelty_before_heuristic", &astar_iw::Options::probe_novelty_before_heuristic)
        .def_rw("max_num_states", &astar_iw::Options::max_num_states)
        .def_rw("max_time_in_ms", &astar_iw::Options::max_time_in_ms);

    m.def("find_solution_astar_iw", &astar_iw::find_solution, "search_context"_a, "heuristic"_a, "options"_a = astar_iw::Options {});

    // AStar_LAZY
    nb::class_<astar_lazy::Statistics>(m, "AStarLazyStatistics")  //
        .def(nb::init<>())
        .def("__str__", [](const astar_lazy::Statistics& self) { return to_string(self); })
        .def("get_num_generated", &astar_lazy::Statistics::get_num_generated)
        .def("get_num_expanded", &astar_lazy::Statistics::get_num_expanded)
        .def("get_num_deadends", &astar_lazy::Statistics::get_num_deadends)
        .def("get_num_pruned", &astar_lazy::Statistics::get_num_pruned)
        .def("get_num_generated_until_f_value", &astar_lazy::Statistics::get_num_generated_until_f_value)
        .def("get_num_expanded_until_f_value", &astar_lazy::Statistics::get_num_expanded_until_f_value)
        .def("get_num_deadends_until_f_value", &astar_lazy::Statistics::get_num_deadends_until_f_value)
        .def("get_num_pruned_until_f_value", &astar_lazy::Statistics::get_num_pruned_until_f_value)
        .def("get_search_time_ms", &astar_lazy::Statistics::get_search_time_ms);

    nb::class_<astar_lazy::IEventHandler, IPyAStarLazyEventHandler>(m,
                                                                    "IAStarLazyEventHandler")  //
        .def(nb::init<>())
        .def("on_expand_state", &astar_lazy::IEventHandler::on_expand_state)
        .def("on_expand_goal_state", &astar_lazy::IEventHandler::on_expand_goal_state)
        .def("on_generate_state", &astar_lazy::IEventHandler::on_generate_state)
        .def("on_generate_state_relaxed", &astar_lazy::IEventHandler::on_generate_state_relaxed)
        .def("on_generate_state_not_relaxed", &astar_lazy::IEventHandler::on_generate_state_not_relaxed)
        .def("on_close_state", &astar_lazy::IEventHandler::on_close_state)
        .def("on_finish_f_layer", &astar_lazy::IEventHandler::on_finish_f_layer)
        .def("on_prune_state", &astar_lazy::IEventHandler::on_prune_state)
        .def("on_start_search", &astar_lazy::IEventHandler::on_start_search)
        .def("on_end_search", &astar_lazy::IEventHandler::on_end_search)
        .def("on_solved", &astar_lazy::IEventHandler::on_solved)
        .def("on_unsolvable", &astar_lazy::IEventHandler::on_unsolvable)
        .def("on_exhausted", &astar_lazy::IEventHandler::on_exhausted)
        .def("get_statistics", &astar_lazy::IEventHandler::get_statistics);

    nb::class_<astar_lazy::DefaultEventHandlerImpl, astar_lazy::IEventHandler>(m,
                                                                               "DefaultAStarLazyEventHandler")  //
        .def(nb::init<Problem, bool>(), "problem"_a, "quiet"_a = true);
    nb::class_<astar_lazy::DebugEventHandlerImpl, astar_lazy::IEventHandler>(m,
                                                                             "DebugAStarLazyEventHandler")  //
        .def(nb::init<Problem, bool>(), "problem"_a, "quiet"_a = true);

    nb::class_<astar_lazy::Options>(m, "AStarLazyOptions")  //
        .def(nb::init<>())
        .def_rw("start_state", &astar_lazy::Options::start_state)
        .def_rw("event_handler", &astar_lazy::Options::event_handler)
        .def_rw("goal_strategy", &astar_lazy::Options::goal_strategy)
        .def_rw("pruning_strategy", &astar_lazy::Options::pruning_strategy)
        .def_rw("max_num_states", &astar_lazy::Options::max_num_states)
        .def_rw("max_time_in_ms", &astar_lazy::Options::max_time_in_ms)
        .def_rw("openlist_weights", &astar_lazy::Options::openlist_weights);

    m.def("find_solution_astar_lazy", &astar_lazy::find_solution, "search_context"_a, "heuristic"_a, "options"_a);

    // BrFS
    nb::class_<brfs::IW1IncrementalFirstApplicabilityStatistics>(m, "IW1IncrementalFirstApplicabilityStatistics")  //
        .def(nb::init<>())
        .def("get_num_root_actions_fully_enumerated", &brfs::IW1IncrementalFirstApplicabilityStatistics::get_num_root_actions_fully_enumerated)
        .def("get_num_non_root_states_using_incremental_path",
             &brfs::IW1IncrementalFirstApplicabilityStatistics::get_num_non_root_states_using_incremental_path)
        .def("get_num_changed_atoms_processed", &brfs::IW1IncrementalFirstApplicabilityStatistics::get_num_changed_atoms_processed)
        .def("get_num_trigger_records_visited", &brfs::IW1IncrementalFirstApplicabilityStatistics::get_num_trigger_records_visited)
        .def("get_num_partial_seeds_created", &brfs::IW1IncrementalFirstApplicabilityStatistics::get_num_partial_seeds_created)
        .def("get_num_ground_actions_returned_by_partial_completion",
             &brfs::IW1IncrementalFirstApplicabilityStatistics::get_num_ground_actions_returned_by_partial_completion)
        .def("get_num_local_duplicate_candidates_removed",
             &brfs::IW1IncrementalFirstApplicabilityStatistics::get_num_local_duplicate_candidates_removed)
        .def("get_num_already_tested_actions_skipped", &brfs::IW1IncrementalFirstApplicabilityStatistics::get_num_already_tested_actions_skipped)
        .def("get_num_non_root_states_with_zero_returned_actions",
             &brfs::IW1IncrementalFirstApplicabilityStatistics::get_num_non_root_states_with_zero_returned_actions)
        .def("get_trigger_lookup_time_ms", &brfs::IW1IncrementalFirstApplicabilityStatistics::get_trigger_lookup_time_ms)
        .def("get_partial_completion_time_ms", &brfs::IW1IncrementalFirstApplicabilityStatistics::get_partial_completion_time_ms)
        .def("get_debug_crosscheck_time_ms", &brfs::IW1IncrementalFirstApplicabilityStatistics::get_debug_crosscheck_time_ms);

    nb::class_<brfs::Statistics>(m, "BrFSStatistics")  //
        .def(nb::init<>())
        .def("__str__", [](const brfs::Statistics& self) { return to_string(self); })
        .def("get_num_generated", &brfs::Statistics::get_num_generated)
        .def("get_num_generated_in_search_tree", &brfs::Statistics::get_num_generated_in_search_tree)
        .def("get_num_generated_not_in_search_tree", &brfs::Statistics::get_num_generated_not_in_search_tree)
        .def("get_num_expanded", &brfs::Statistics::get_num_expanded)
        .def("get_num_expanded_goal_states", &brfs::Statistics::get_num_expanded_goal_states)
        .def("get_num_deadends", &brfs::Statistics::get_num_deadends)
        .def("get_num_pruned", &brfs::Statistics::get_num_pruned)
        .def("get_num_reached_fluent_atoms", &brfs::Statistics::get_num_reached_fluent_atoms)
        .def("get_num_reached_derived_atoms", &brfs::Statistics::get_num_reached_derived_atoms)
        .def("get_num_states", &brfs::Statistics::get_num_states)
        .def("get_num_nodes", &brfs::Statistics::get_num_nodes)
        .def("get_num_actions", &brfs::Statistics::get_num_actions)
        .def("get_num_axioms", &brfs::Statistics::get_num_axioms)
        .def("get_iw1_incremental_first_applicability_statistics", &brfs::Statistics::get_iw1_incremental_first_applicability_statistics)
        .def("get_num_parallel_beam_chunk_flushes", &brfs::Statistics::get_num_parallel_beam_chunk_flushes)
        .def("get_num_parallel_beam_chunk_tasks_total", &brfs::Statistics::get_num_parallel_beam_chunk_tasks_total)
        .def("get_average_parallel_beam_chunk_size", &brfs::Statistics::get_average_parallel_beam_chunk_size)
        .def("get_max_parallel_beam_chunk_size", &brfs::Statistics::get_max_parallel_beam_chunk_size)
        .def("get_parallel_beam_worker_compute_time_ms", &brfs::Statistics::get_parallel_beam_worker_compute_time_ms)
        .def("get_parallel_beam_main_thread_merge_time_ms", &brfs::Statistics::get_parallel_beam_main_thread_merge_time_ms)
        .def("get_parallel_beam_main_thread_intern_time_ms", &brfs::Statistics::get_parallel_beam_main_thread_intern_time_ms)
        .def("get_parallel_beam_fluent_slot_time_ms", &brfs::Statistics::get_parallel_beam_fluent_slot_time_ms)
        .def("get_parallel_beam_numeric_slot_time_ms", &brfs::Statistics::get_parallel_beam_numeric_slot_time_ms)
        .def("get_parallel_beam_derived_slot_time_ms", &brfs::Statistics::get_parallel_beam_derived_slot_time_ms)
        .def("get_parallel_beam_state_lookup_time_ms", &brfs::Statistics::get_parallel_beam_state_lookup_time_ms)
        .def("get_parallel_beam_reached_atom_update_time_ms", &brfs::Statistics::get_parallel_beam_reached_atom_update_time_ms)
        .def("get_parallel_beam_ready_queue_high_water", &brfs::Statistics::get_parallel_beam_ready_queue_high_water)
        .def("get_parallel_beam_in_flight_chunks_high_water", &brfs::Statistics::get_parallel_beam_in_flight_chunks_high_water)
        .def("get_parallel_beam_consumer_stall_time_ms", &brfs::Statistics::get_parallel_beam_consumer_stall_time_ms)
        .def("get_parallel_beam_producer_stall_time_ms", &brfs::Statistics::get_parallel_beam_producer_stall_time_ms)
        .def("get_num_generated_until_g_value", &brfs::Statistics::get_num_generated_until_g_value)
        .def("get_num_generated_in_search_tree_until_g_value", &brfs::Statistics::get_num_generated_in_search_tree_until_g_value)
        .def("get_num_generated_not_in_search_tree_until_g_value", &brfs::Statistics::get_num_generated_not_in_search_tree_until_g_value)
        .def("get_num_expanded_until_g_value", &brfs::Statistics::get_num_expanded_until_g_value)
        .def("get_num_expanded_goal_states_until_g_value", &brfs::Statistics::get_num_expanded_goal_states_until_g_value)
        .def("get_num_deadends_until_g_value", &brfs::Statistics::get_num_deadends_until_g_value)
        .def("get_num_pruned_until_g_value", &brfs::Statistics::get_num_pruned_until_g_value)
        .def("get_finished_g_values", &brfs::Statistics::get_finished_g_values)
        .def("get_search_time_ms", &brfs::Statistics::get_search_time_ms);

    nb::class_<brfs::IEventHandler, IPyBrFSEventHandler>(m, "IBrFSEventHandler")  //
        .def(nb::init<>())
        .def("on_expand_state", &brfs::IEventHandler::on_expand_state)
        .def("on_expand_goal_state", &brfs::IEventHandler::on_expand_goal_state)
        .def("on_generate_state", &brfs::IEventHandler::on_generate_state)
        .def("supports_novel_witness_events", &brfs::IEventHandler::supports_novel_witness_events)
        .def("on_generate_state_with_novel_witness", &brfs::IEventHandler::on_generate_state_with_novel_witness)
        .def("on_generate_state_in_search_tree", &brfs::IEventHandler::on_generate_state_in_search_tree)
        .def("on_generate_state_not_in_search_tree", &brfs::IEventHandler::on_generate_state_not_in_search_tree)
        .def("on_finish_g_layer", &brfs::IEventHandler::on_finish_g_layer)
        .def("on_start_search", &brfs::IEventHandler::on_start_search)
        .def("on_end_search", &brfs::IEventHandler::on_end_search)
        .def("on_finish_parallel_beam_chunk", &brfs::IEventHandler::on_finish_parallel_beam_chunk)
        .def("on_finish_parallel_beam_pipeline", &brfs::IEventHandler::on_finish_parallel_beam_pipeline)
        .def("on_solved", &brfs::IEventHandler::on_solved)
        .def("on_unsolvable", &brfs::IEventHandler::on_unsolvable)
        .def("on_exhausted", &brfs::IEventHandler::on_exhausted)
        .def("get_statistics", &brfs::IEventHandler::get_statistics);

    nb::class_<brfs::DefaultEventHandlerImpl, brfs::IEventHandler>(m,
                                                                   "DefaultBrFSEventHandler")  //
        .def(nb::init<Problem, bool>(), "problem"_a, "quiet"_a = true);
    nb::class_<brfs::DebugEventHandlerImpl, brfs::IEventHandler>(m,
                                                                 "DebugBrFSEventHandler")  //
        .def(nb::init<Problem, bool>(), "problem"_a, "quiet"_a = true);

    nb::class_<brfs::SearchTreeNode>(m, "BrFSSearchTreeNode")  //
        .def_ro("state_index", &brfs::SearchTreeNode::state)
        .def_prop_ro("parent_index", [](const brfs::SearchTreeNode& self) { return self.parent_node; })
        .def_prop_ro("incoming_action_index",
                     [](const brfs::SearchTreeNode& self) -> std::optional<Index>
                     {
                         // The root has no incoming action; report that as None rather than as a
                         // sentinel index a caller could mistake for a real action.
                         return self.parent_node.has_value() ? std::optional<Index>(self.incoming_action) : std::nullopt;
                     })
        .def_ro("depth", &brfs::SearchTreeNode::depth)
        .def("__repr__",
             [](const brfs::SearchTreeNode& self)
             {
                 return "BrFSSearchTreeNode(state_index=" + std::to_string(self.state) + ", parent_index="
                        + (self.parent_node.has_value() ? std::to_string(*self.parent_node) : std::string("None")) + ", incoming_action_index="
                        + (self.parent_node.has_value() ? std::to_string(self.incoming_action) : std::string("None"))
                        + ", depth=" + std::to_string(self.depth) + ")";
             });

    nb::class_<brfs::SearchTree>(m, "BrFSSearchTree")  //
        .def_prop_ro("nodes", &brfs::SearchTree::get_nodes, nb::rv_policy::copy)
        .def("__len__", &brfs::SearchTree::get_num_nodes)
        .def("get_nodes", &brfs::SearchTree::get_nodes, nb::rv_policy::copy)
        .def("get_num_nodes", &brfs::SearchTree::get_num_nodes)
        .def("find_node_by_state", &brfs::SearchTree::find_node_by_state, "state_index"_a)
        .def("find_depth_by_state", &brfs::SearchTree::find_depth_by_state, "state_index"_a)
        // Walked from the parent links on call, so a caller pays for the paths it actually asks for
        // rather than for a stored path on every node.
        .def("get_action_indices", &brfs::SearchTree::get_action_indices, "node_index"_a)
        .def("get_state_indices", &brfs::SearchTree::get_state_indices, "node_index"_a);

    nb::enum_<brfs::TransitionDisposition>(m, "BrFSTransitionDisposition")  //
        .value("ADMITTED", brfs::TransitionDisposition::ADMITTED)
        .value("REJECTED", brfs::TransitionDisposition::REJECTED);

    nb::class_<brfs::GroundActionEffectSummary>(m, "GroundActionEffectSummary")  //
        .def_ro("num_add_effects", &brfs::GroundActionEffectSummary::num_add_effects)
        .def_ro("num_delete_effects", &brfs::GroundActionEffectSummary::num_delete_effects)
        .def_ro("num_fluent_numeric_effects", &brfs::GroundActionEffectSummary::num_fluent_numeric_effects)
        .def_ro("has_auxiliary_numeric_effect", &brfs::GroundActionEffectSummary::has_auxiliary_numeric_effect)
        .def("__repr__",
             [](const brfs::GroundActionEffectSummary& self)
             {
                 return "GroundActionEffectSummary(num_add_effects=" + std::to_string(self.num_add_effects)
                        + ", num_delete_effects=" + std::to_string(self.num_delete_effects)
                        + ", num_fluent_numeric_effects=" + std::to_string(self.num_fluent_numeric_effects)
                        + ", has_auxiliary_numeric_effect=" + (self.has_auxiliary_numeric_effect ? "True" : "False") + ")";
             });

    nb::class_<brfs::TransitionObservation>(m, "BrFSTransitionObservation")  //
        .def_ro("parent_state_index", &brfs::TransitionObservation::parent_state)
        .def_ro("action_index", &brfs::TransitionObservation::action)
        .def_ro("successor_state_index", &brfs::TransitionObservation::successor_state)
        .def_ro("parent_depth", &brfs::TransitionObservation::parent_depth)
        .def_ro("successor_depth", &brfs::TransitionObservation::successor_depth)
        .def_ro("action_cost", &brfs::TransitionObservation::action_cost)
        .def_ro("disposition", &brfs::TransitionObservation::disposition)
        .def_prop_ro("is_admitted",
                     [](const brfs::TransitionObservation& self) { return self.disposition == brfs::TransitionDisposition::ADMITTED; })
        .def_ro("novel_fluent_atom_indices", &brfs::TransitionObservation::novel_fluent_atom_indices)
        // None when no witness was available at all, which is a different statement from a witness
        // that ran and found nothing.
        .def_prop_ro("novelty_witness_count",
                     [](const brfs::TransitionObservation& self) -> std::optional<size_t>
                     {
                         return self.novel_fluent_atom_indices.has_value() ? std::optional<size_t>(self.novel_fluent_atom_indices->size()) :
                                                                             std::nullopt;
                     })
        .def_ro("realized_added_fluent_atom_indices", &brfs::TransitionObservation::realized_added_fluent_atom_indices)
        .def_ro("realized_deleted_fluent_atom_indices", &brfs::TransitionObservation::realized_deleted_fluent_atom_indices)
        // Returned by value: the summary is 16 bytes, and a copy cannot outlive the observation it
        // was cached in.
        .def_prop_ro("action_effect_summary",
                     [](const brfs::TransitionObservation& self) -> std::optional<brfs::GroundActionEffectSummary>
                     {
                         return self.action_effect_summary ? std::optional<brfs::GroundActionEffectSummary>(*self.action_effect_summary) : std::nullopt;
                     })
        .def("__repr__",
             [](const brfs::TransitionObservation& self)
             {
                 return "BrFSTransitionObservation(parent_state_index=" + std::to_string(self.parent_state) + ", action_index="
                        + std::to_string(self.action) + ", successor_state_index=" + std::to_string(self.successor_state) + ", successor_depth="
                        + std::to_string(self.successor_depth)
                        + ", disposition=" + (self.disposition == brfs::TransitionDisposition::ADMITTED ? "ADMITTED" : "REJECTED") + ")";
             });

    // Bound as an opaque sequence rather than converted to a Python list: a rejected-transition log
    // can hold millions of records, and converting would copy every one of them on each access.
    nb::bind_vector<brfs::TransitionObservationList>(m, "BrFSTransitionObservationList");

    nb::class_<brfs::TransitionAggregates>(m, "BrFSTransitionAggregates")  //
        .def_ro("num_transitions", &brfs::TransitionAggregates::num_transitions)
        .def_ro("num_admitted", &brfs::TransitionAggregates::num_admitted)
        .def_ro("num_rejected", &brfs::TransitionAggregates::num_rejected)
        .def_ro("num_transitions_with_witness", &brfs::TransitionAggregates::num_transitions_with_witness)
        .def_ro("total_witness_size", &brfs::TransitionAggregates::total_witness_size)
        .def_ro("max_witness_size", &brfs::TransitionAggregates::max_witness_size)
        .def_ro("num_admitted_with_effect_summary", &brfs::TransitionAggregates::num_admitted_with_effect_summary)
        .def_ro("total_add_effects", &brfs::TransitionAggregates::total_add_effects)
        .def_ro("max_add_effects", &brfs::TransitionAggregates::max_add_effects)
        .def_ro("total_delete_effects", &brfs::TransitionAggregates::total_delete_effects)
        .def_ro("max_delete_effects", &brfs::TransitionAggregates::max_delete_effects)
        .def_ro("total_realized_added", &brfs::TransitionAggregates::total_realized_added)
        .def_ro("total_realized_deleted", &brfs::TransitionAggregates::total_realized_deleted)
        .def_ro("num_admitted_by_depth", &brfs::TransitionAggregates::num_admitted_by_depth)
        .def_ro("num_rejected_by_depth", &brfs::TransitionAggregates::num_rejected_by_depth)
        .def_ro("total_witness_size_by_depth", &brfs::TransitionAggregates::total_witness_size_by_depth)
        .def_ro("num_transitions_with_witness_by_depth", &brfs::TransitionAggregates::num_transitions_with_witness_by_depth)
        .def_prop_ro("average_witness_size", &brfs::TransitionAggregates::get_average_witness_size)
        .def_prop_ro("average_add_effects", &brfs::TransitionAggregates::get_average_add_effects)
        .def_prop_ro("average_delete_effects", &brfs::TransitionAggregates::get_average_delete_effects);

    m.def("compute_brfs_transition_aggregates", &brfs::compute_transition_aggregates, "transitions"_a);

    nb::class_<brfs::ObservationOptions>(m, "BrFSObservationOptions")  //
        .def(nb::init<>())
        .def_rw("capture_search_tree", &brfs::ObservationOptions::capture_search_tree)
        .def_rw("capture_admitted_transitions", &brfs::ObservationOptions::capture_admitted_transitions)
        .def_rw("capture_rejected_transitions", &brfs::ObservationOptions::capture_rejected_transitions)
        .def_rw("capture_novel_witnesses", &brfs::ObservationOptions::capture_novel_witnesses)
        .def_rw("capture_action_effect_summaries", &brfs::ObservationOptions::capture_action_effect_summaries)
        .def_rw("capture_realized_effects", &brfs::ObservationOptions::capture_realized_effects);

    /* `Observation` is handed out by reference and never copied: its transition records point into
       its own effect-summary cache, so a copy would leave those pointers aimed at the original. The
       handler that owns it is kept alive by `reference_internal`. */
    nb::class_<brfs::Observation>(m, "BrFSObservation")  //
        .def_prop_ro("search_tree", &brfs::Observation::get_search_tree, nb::rv_policy::reference_internal)
        .def_prop_ro("transitions", &brfs::Observation::get_transitions, nb::rv_policy::reference_internal)
        .def("get_search_tree", &brfs::Observation::get_search_tree, nb::rv_policy::reference_internal)
        .def("get_transitions", &brfs::Observation::get_transitions, nb::rv_policy::reference_internal)
        .def("get_num_action_effect_summaries", &brfs::Observation::get_num_action_effect_summaries)
        .def(
            "get_action_effect_summary",
            [](const brfs::Observation& self, Index action_index) -> std::optional<brfs::GroundActionEffectSummary>
            {
                const auto* summary = self.find_action_effect_summary(action_index);
                return summary ? std::optional<brfs::GroundActionEffectSummary>(*summary) : std::nullopt;
            },
            "action_index"_a)
        .def("compute_transition_aggregates",
             [](const brfs::Observation& self) { return brfs::compute_transition_aggregates(self.get_transitions()); });

    nb::class_<brfs::ObservationEventHandlerImpl, brfs::IEventHandler>(m, "ObservationBrFSEventHandler")  //
        .def(nb::init<Problem, brfs::ObservationOptions>(), "problem"_a, "options"_a)
        .def_static("create", &brfs::ObservationEventHandlerImpl::create, "problem"_a, "options"_a)
        .def("get_options", &brfs::ObservationEventHandlerImpl::get_options, nb::rv_policy::copy)
        .def("get_observation", &brfs::ObservationEventHandlerImpl::get_observation, nb::rv_policy::reference_internal)
        .def_prop_ro("observation", &brfs::ObservationEventHandlerImpl::get_observation, nb::rv_policy::reference_internal);

    nb::class_<brfs::CompositeEventHandlerImpl, brfs::IEventHandler>(m, "CompositeBrFSEventHandler")  //
        .def(nb::init<std::vector<brfs::EventHandler>, size_t>(), "handlers"_a, "statistics_source"_a = 0)
        .def_static("create", &brfs::CompositeEventHandlerImpl::create, "handlers"_a, "statistics_source"_a = 0)
        .def("get_handlers", &brfs::CompositeEventHandlerImpl::get_handlers, nb::rv_policy::copy);

    nb::class_<brfs::Options>(m, "BrFSOptions")  //
        .def(nb::init<>())
        .def_rw("start_state", &brfs::Options::start_state)
        .def_rw("event_handler", &brfs::Options::event_handler)
        .def_rw("goal_strategy", &brfs::Options::goal_strategy)
        .def_rw("pruning_strategy", &brfs::Options::pruning_strategy)
        .def_rw("layer_ordering_strategy", &brfs::Options::layer_ordering_strategy)
        .def_rw("max_next_layer_states", &brfs::Options::max_next_layer_states)
        .def_rw("beam_width", &brfs::Options::beam_width)
        .def_rw("beam_novelty_mode", &brfs::Options::beam_novelty_mode)
        .def_rw("relaxed_survivors_only_beam", &brfs::Options::relaxed_survivors_only_beam)
        .def_rw("randomize_equal_score_ties", &brfs::Options::randomize_equal_score_ties)
        .def_rw("equal_score_tie_seed", &brfs::Options::equal_score_tie_seed)
        .def_rw("parallel_beam_num_threads", &brfs::Options::parallel_beam_num_threads)
        .def_rw("parallel_beam_chunk_size", &brfs::Options::parallel_beam_chunk_size)
        // Set of state indices in the search context's OWN state repository; see the C++ option
        // for the repository caveat. Owned by the options object, so the Python caller may drop
        // its own reference.
        .def_rw("blocked_states", &brfs::Options::blocked_states)
        .def_rw("iw1_precheck_add_effect_novelty", &brfs::Options::iw1_precheck_add_effect_novelty)
        .def_rw("iw1_atom_first_mode", &brfs::Options::iw1_atom_first_mode)
        .def_rw("iw1_atom_first_ratio", &brfs::Options::iw1_atom_first_ratio)
        .def_rw("iw1_incremental_first_applicability", &brfs::Options::iw1_incremental_first_applicability)
        .def_rw("iw1_incremental_first_applicability_debug_crosscheck",
                &brfs::Options::iw1_incremental_first_applicability_debug_crosscheck)
        .def_rw("stop_if_goal", &brfs::Options::stop_if_goal)
        .def_rw("max_depth", &brfs::Options::max_depth)
        .def_rw("max_num_states", &brfs::Options::max_num_states)
        .def_rw("max_time_in_ms", &brfs::Options::max_time_in_ms);

    // `brfs::find_solution` is overloaded, so the function address must be disambiguated explicitly.
    m.def("find_solution_brfs",
          static_cast<SearchResult (*)(const SearchContext&, const brfs::Options&)>(&brfs::find_solution),
          "search_context"_a,
          "options"_a);
    m.def("find_solution_brfs",
          static_cast<SearchResult (*)(const SearchContext&, const brfs::Options&, const LandmarkTransitionOrderingStrategy&)>(&brfs::find_solution),
          "search_context"_a,
          "options"_a,
          "transition_ordering_strategy"_a);

    // GBFS_EAGER
    nb::class_<gbfs_eager::Statistics>(m, "GBFSEagerStatistics")  //
        .def(nb::init<>())
        .def("__str__", [](const gbfs_eager::Statistics& self) { return to_string(self); })
        .def("get_num_generated", &gbfs_eager::Statistics::get_num_generated)
        .def("get_num_expanded", &gbfs_eager::Statistics::get_num_expanded)
        .def("get_num_deadends", &gbfs_eager::Statistics::get_num_deadends)
        .def("get_num_pruned", &gbfs_eager::Statistics::get_num_pruned)
        .def("get_search_time_ms", &gbfs_eager::Statistics::get_search_time_ms);

    nb::class_<gbfs_eager::IEventHandler, IPyGBFSEagerEventHandler>(m, "IGBFSEagerEventHandler")  //
        .def(nb::init<>())
        .def("on_expand_state", &gbfs_eager::IEventHandler::on_expand_state)
        .def("on_expand_goal_state", &gbfs_eager::IEventHandler::on_expand_goal_state)
        .def("on_generate_state", &gbfs_eager::IEventHandler::on_generate_state)
        .def("on_prune_state", &gbfs_eager::IEventHandler::on_prune_state)
        .def("on_start_search", &gbfs_eager::IEventHandler::on_start_search)
        .def("on_new_best_h_value", &gbfs_eager::IEventHandler::on_new_best_h_value)
        .def("on_end_search", &gbfs_eager::IEventHandler::on_end_search)
        .def("on_solved", &gbfs_eager::IEventHandler::on_solved)
        .def("on_unsolvable", &gbfs_eager::IEventHandler::on_unsolvable)
        .def("on_exhausted", &gbfs_eager::IEventHandler::on_exhausted)
        .def("get_statistics", &gbfs_eager::IEventHandler::get_statistics);

    nb::class_<gbfs_eager::DefaultEventHandlerImpl, gbfs_eager::IEventHandler>(m,
                                                                               "DefaultGBFSEagerEventHandler")  //
        .def(nb::init<Problem, bool>(), "problem"_a, "quiet"_a = true);
    nb::class_<gbfs_eager::DebugEventHandlerImpl, gbfs_eager::IEventHandler>(m,
                                                                             "DebugGBFSEagerEventHandler")  //
        .def(nb::init<Problem, bool>(), "problem"_a, "quiet"_a = true);

    nb::class_<gbfs_eager::Options>(m, "GBFSEagerOptions")  //
        .def(nb::init<>())
        .def_rw("start_state", &gbfs_eager::Options::start_state)
        .def_rw("event_handler", &gbfs_eager::Options::event_handler)
        .def_rw("goal_strategy", &gbfs_eager::Options::goal_strategy)
        .def_rw("pruning_strategy", &gbfs_eager::Options::pruning_strategy)
        .def_rw("max_num_states", &gbfs_eager::Options::max_num_states)
        .def_rw("max_time_in_ms", &gbfs_eager::Options::max_time_in_ms);

    m.def("find_solution_gbfs_eager", &gbfs_eager::find_solution, "search_context"_a, "heuristic"_a, "options"_a);

    // GBFS_LAZY
    nb::class_<gbfs_lazy::Statistics>(m, "GBFSLazyStatistics")  //
        .def(nb::init<>())
        .def("__str__", [](const gbfs_lazy::Statistics& self) { return to_string(self); })
        .def("get_num_generated", &gbfs_lazy::Statistics::get_num_generated)
        .def("get_num_expanded", &gbfs_lazy::Statistics::get_num_expanded)
        .def("get_num_deadends", &gbfs_lazy::Statistics::get_num_deadends)
        .def("get_num_pruned", &gbfs_lazy::Statistics::get_num_pruned)
        .def("get_search_time_ms", &gbfs_lazy::Statistics::get_search_time_ms);

    nb::class_<gbfs_lazy::IEventHandler, IPyGBFSLazyEventHandler>(m, "IGBFSLazyEventHandler")  //
        .def(nb::init<>())
        .def("on_expand_state", &gbfs_lazy::IEventHandler::on_expand_state)
        .def("on_expand_goal_state", &gbfs_lazy::IEventHandler::on_expand_goal_state)
        .def("on_generate_state", &gbfs_lazy::IEventHandler::on_generate_state)
        .def("on_prune_state", &gbfs_lazy::IEventHandler::on_prune_state)
        .def("on_start_search", &gbfs_lazy::IEventHandler::on_start_search)
        .def("on_new_best_h_value", &gbfs_lazy::IEventHandler::on_new_best_h_value)
        .def("on_end_search", &gbfs_lazy::IEventHandler::on_end_search)
        .def("on_solved", &gbfs_lazy::IEventHandler::on_solved)
        .def("on_unsolvable", &gbfs_lazy::IEventHandler::on_unsolvable)
        .def("on_exhausted", &gbfs_lazy::IEventHandler::on_exhausted)
        .def("get_statistics", &gbfs_lazy::IEventHandler::get_statistics);

    nb::class_<gbfs_lazy::DefaultEventHandlerImpl, gbfs_lazy::IEventHandler>(m,
                                                                             "DefaultGBFSLazyEventHandler")  //
        .def(nb::init<Problem, bool>(), "problem"_a, "quiet"_a = true);
    nb::class_<gbfs_lazy::DebugEventHandlerImpl, gbfs_lazy::IEventHandler>(m,
                                                                           "DebugGBFSLazyEventHandler")  //
        .def(nb::init<Problem, bool>(), "problem"_a, "quiet"_a = true);

    nb::class_<gbfs_lazy::Options>(m, "GBFSLazyOptions")  //
        .def(nb::init<>())
        .def_rw("start_state", &gbfs_lazy::Options::start_state)
        .def_rw("event_handler", &gbfs_lazy::Options::event_handler)
        .def_rw("goal_strategy", &gbfs_lazy::Options::goal_strategy)
        .def_rw("pruning_strategy", &gbfs_lazy::Options::pruning_strategy)
        .def_rw("exploration_strategy", &gbfs_lazy::Options::exploration_strategy)
        .def_rw("max_num_states", &gbfs_lazy::Options::max_num_states)
        .def_rw("max_time_in_ms", &gbfs_lazy::Options::max_time_in_ms)
        .def_rw("openlist_weights", &gbfs_lazy::Options::openlist_weights);

    m.def("find_solution_gbfs_lazy", &gbfs_lazy::find_solution, "search_context"_a, "heuristic"_a, "options"_a);

    // IW
    nb::class_<iw::TupleIndexMapper>(m, "TupleIndexMapper")  //
        .def(nb::init<size_t, size_t>(), "arity"_a, "num_atoms"_a)
        .def("to_tuple_index", &iw::TupleIndexMapper::to_tuple_index, "atom_indices"_a)
        .def(
            "to_atom_indices",
            [](const iw::TupleIndexMapper& self, const iw::TupleIndex tuple_index)
            {
                auto atom_indices = iw::AtomIndexList {};
                self.to_atom_indices(tuple_index, atom_indices);
                return atom_indices;
            },
            "tuple_index"_a)
        .def("initialize", &iw::TupleIndexMapper::initialize, "arity"_a, "num_atoms"_a)
        .def("tuple_index_to_string", &iw::TupleIndexMapper::tuple_index_to_string, "tuple_index"_a)
        .def("get_num_atoms", &iw::TupleIndexMapper::get_num_atoms)
        .def("get_arity", &iw::TupleIndexMapper::get_arity)
        .def("get_factors", &iw::TupleIndexMapper::get_factors, nb::rv_policy::reference_internal)
        .def("get_max_tuple_index", &iw::TupleIndexMapper::get_max_tuple_index)
        .def("get_empty_tuple_index", &iw::TupleIndexMapper::get_empty_tuple_index);

    nb::class_<iw::DynamicNoveltyTable>(m, "DynamicNoveltyTable")
        .def(nb::init<size_t>(), "arity"_a)
        .def(nb::init<size_t, size_t>(), "arity"_a, "num_atoms"_a)
        .def(
            "compute_novel_tuples",
            [](iw::DynamicNoveltyTable& self, const State& state)
            {
                std::vector<iw::AtomIndexList> out;
                self.compute_novel_tuples(state, out);
                return out;
            },
            "state"_a)
        .def("insert_tuples", &iw::DynamicNoveltyTable::insert_tuples, "tuples"_a)
        .def("test_novelty_and_update_table", nb::overload_cast<const State&>(&iw::DynamicNoveltyTable::test_novelty_and_update_table), "state"_a)
        .def("test_novelty_and_update_table",
             nb::overload_cast<const State&, const State&>(&iw::DynamicNoveltyTable::test_novelty_and_update_table),
             "state"_a,
             "succ_state"_a)
        .def("reset", &iw::DynamicNoveltyTable::reset)
        .def("get_tuple_index_mapper", &iw::DynamicNoveltyTable::get_tuple_index_mapper, nb::rv_policy::reference_internal);

    nb::class_<iw::StateTupleIndexGenerator>(m, "StateTupleIndexGenerator")  //
        .def(nb::init<const iw::TupleIndexMapper*>(), "tuple_index_mapper"_a)
        .def(
            "__iter__",
            [](iw::StateTupleIndexGenerator& self, const State& state)
            {
                return nb::make_iterator(nb::type<iw::StateTupleIndexGenerator>(),
                                         "Iterator over atom tuples of size at most the arity as specified in the tuple index mapper.",
                                         self.begin(state),
                                         self.end());
            },
            nb::keep_alive<0, 1>());

    nb::class_<iw::StatePairTupleIndexGenerator>(m, "StatePairTupleIndexGenerator")  //
        .def(nb::init<const iw::TupleIndexMapper*>(), "tuple_index_mapper"_a)
        .def(
            "__iter__",
            [](iw::StatePairTupleIndexGenerator& self, const State& state, const State& succ_state)
            {
                return nb::make_iterator(nb::type<iw::StatePairTupleIndexGenerator>(),
                                         "Iterator over atom tuples of size at most the arity as specified in the tuple index mapper.",
                                         self.begin(state, succ_state),
                                         self.end());
            },
            nb::keep_alive<0, 1>());

    nb::class_<iw::Statistics>(m, "IWStatistics")  //
        .def(nb::init<>())
        .def("__str__", [](const iw::Statistics& self) { return to_string(self); })
        .def("get_effective_width", &iw::Statistics::get_effective_width)
        .def("get_brfs_statistics_by_arity", &iw::Statistics::get_brfs_statistics_by_arity)
        .def("get_search_time_ms", &iw::Statistics::get_search_time_ms);

    nb::class_<iw::IEventHandler>(m, "IIWEventHandler")  //
        .def("get_statistics", &iw::IEventHandler::get_statistics)
        .def("is_quiet", &iw::IEventHandler::is_quiet);

    nb::class_<iw::DefaultEventHandlerImpl, iw::IEventHandler>(m, "DefaultIWEventHandler")
        .def(nb::init<Problem, bool>(), "problem"_a, "quiet"_a = true)
        .def_static("create", &iw::DefaultEventHandlerImpl::create, "problem"_a, "quiet"_a = true);

    /* Per-arity IW observation. Each pass has its own tree, so entries are never merged; a pass that
       ran no search -- the width-0 placeholder of optimized IW(1) -- appears with empty data so an
       entry's position keeps matching the width it stands for. */
    nb::class_<iw::ArityObservation>(m, "IWArityObservation")  //
        .def_ro("arity", &iw::ArityObservation::arity)
        .def_prop_ro("statistics", [](const iw::ArityObservation& self) { return self.statistics; })
        .def_prop_ro(
            "observation",
            [](const iw::ArityObservation& self) -> const brfs::Observation& { return self.observation; },
            nb::rv_policy::reference_internal)
        .def_prop_ro(
            "search_tree",
            [](const iw::ArityObservation& self) -> const brfs::SearchTree& { return self.observation.get_search_tree(); },
            nb::rv_policy::reference_internal)
        .def_prop_ro(
            "transitions",
            [](const iw::ArityObservation& self) -> const brfs::TransitionObservationList& { return self.observation.get_transitions(); },
            nb::rv_policy::reference_internal);

    nb::class_<iw::Observation>(m, "IWObservation")  //
        .def("__len__", &iw::Observation::get_num_arities)
        .def("get_num_arities", &iw::Observation::get_num_arities)
        .def(
            "get_arity_observation",
            [](const iw::Observation& self, size_t index) -> const iw::ArityObservation&
            {
                if (index >= self.get_num_arities())
                {
                    throw nb::index_error("arity observation index out of range");
                }
                return self.get_by_arity()[index];
            },
            "index"_a,
            nb::rv_policy::reference_internal);

    nb::class_<iw::ObservationEventHandlerImpl, iw::IEventHandler>(m, "ObservationIWEventHandler")  //
        .def(nb::init<Problem, brfs::ObservationEventHandler>(), "problem"_a, "brfs_observation_handler"_a)
        .def_static("create", &iw::ObservationEventHandlerImpl::create, "problem"_a, "brfs_observation_handler"_a)
        .def("get_observation", &iw::ObservationEventHandlerImpl::get_observation, nb::rv_policy::reference_internal)
        .def_prop_ro("observation", &iw::ObservationEventHandlerImpl::get_observation, nb::rv_policy::reference_internal);

    nb::class_<iw::Options>(m, "IWOptions")  //
        .def(nb::init<>())
        .def_rw("start_state", &iw::Options::start_state)
        .def_rw("iw_event_handler", &iw::Options::iw_event_handler)
        .def_rw("brfs_event_handler", &iw::Options::brfs_event_handler)
        .def_rw("goal_strategy", &iw::Options::goal_strategy)
        .def_rw("layer_ordering_strategy", &iw::Options::layer_ordering_strategy)
        .def_rw("max_next_layer_states", &iw::Options::max_next_layer_states)
        .def_rw("beam_width", &iw::Options::beam_width)
        .def_rw("beam_novelty_mode", &iw::Options::beam_novelty_mode)
        .def_rw("relaxed_survivors_only_beam", &iw::Options::relaxed_survivors_only_beam)
        .def_rw("randomize_equal_score_ties", &iw::Options::randomize_equal_score_ties)
        .def_rw("equal_score_tie_seed", &iw::Options::equal_score_tie_seed)
        .def_rw("parallel_beam_num_threads", &iw::Options::parallel_beam_num_threads)
        .def_rw("parallel_beam_chunk_size", &iw::Options::parallel_beam_chunk_size)
        .def_rw("iw1_precheck_add_effect_novelty", &iw::Options::iw1_precheck_add_effect_novelty)
        .def_rw("iw1_atom_first_mode", &iw::Options::iw1_atom_first_mode)
        .def_rw("iw1_atom_first_ratio", &iw::Options::iw1_atom_first_ratio)
        .def_rw("iw1_incremental_first_applicability", &iw::Options::iw1_incremental_first_applicability)
        .def_rw("iw1_incremental_first_applicability_debug_crosscheck",
                &iw::Options::iw1_incremental_first_applicability_debug_crosscheck)
        .def_rw("max_depth", &iw::Options::max_depth)
        .def_rw("max_arity", &iw::Options::max_arity)
        // Setting this turns the search into LIW: the ladder becomes 0, LIW(1), LIW(2), ..., where
        // LIW(k) tests novelty over (landmark atom, free tuple of size <= k) pairs.
        .def_rw("landmark_novelty_graph", &iw::Options::landmark_novelty_graph)
        .def_rw("landmark_novelty_table_options", &iw::Options::landmark_novelty_table_options)
        .def_rw("landmark_novelty_disjunctive", &iw::Options::landmark_novelty_disjunctive)
        .def_rw("landmark_novelty_unshared_atoms", &iw::Options::landmark_novelty_unshared_atoms)
        // `control` is deliberately not exposed: it is a raw pointer to state shared with other
        // native searches, which has no meaning from Python.
        .def_rw("max_time_in_ms", &iw::Options::max_time_in_ms)
        // Set of state indices in the search context's OWN state repository, applied to every
        // arity pass; see the C++ option. With a non-empty set an exhausted ladder proves only
        // that the goal is unreachable *without re-entering a blocked state*.
        .def_rw("blocked_states", &iw::Options::blocked_states)
        .def_rw("max_num_states", &iw::Options::max_num_states);

    // `iw::find_solution` is overloaded, so the function address must be disambiguated explicitly.
    m.def("find_solution_iw",
          static_cast<SearchResult (*)(const SearchContext&, const iw::Options&)>(&iw::find_solution),
          "search_context"_a,
          "options"_a);
    m.def("find_solution_iw",
          static_cast<SearchResult (*)(const SearchContext&, const iw::Options&, const LandmarkTransitionOrderingStrategy&)>(&iw::find_solution),
          "search_context"_a,
          "options"_a,
          "transition_ordering_strategy"_a);

    /* Batched parallel IW rollouts. See docs/PARALLEL_IW_ROLLOUTS.md. */

    nb::class_<iw::ParallelRolloutOptions>(m, "IWParallelRolloutOptions")  //
        .def(nb::init<>())
        .def_rw("seeds", &iw::ParallelRolloutOptions::seeds)
        .def_rw("num_threads", &iw::ParallelRolloutOptions::num_threads)
        .def_rw("options", &iw::ParallelRolloutOptions::options)
        .def_rw("report_landing_states", &iw::ParallelRolloutOptions::report_landing_states)
        .def_rw("report_co_occurrence", &iw::ParallelRolloutOptions::report_co_occurrence);

    nb::class_<iw::LandingState>(m, "IWLandingState")  //
        .def_ro("is_direct_dead_end", &iw::LandingState::is_direct_dead_end)
        .def_prop_ro("fluent_atoms",
                     [](const iw::LandingState& self)
                     {
                         auto indices = std::vector<Index> {};
                         for (const auto index : self.fluent_atoms)
                         {
                             indices.push_back(index);
                         }
                         return indices;
                     });

    nb::class_<iw::RolloutResult>(m, "IWRolloutResult")  //
        .def_ro("status", &iw::RolloutResult::status)
        .def_ro("num_states", &iw::RolloutResult::num_states)
        .def_ro("landing_states", &iw::RolloutResult::landing_states)
        .def_ro("landing_state_by_atom", &iw::RolloutResult::landing_state_by_atom)
        .def_prop_ro("reached_fluent_atoms",
                     [](const iw::RolloutResult& self)
                     {
                         auto indices = std::vector<Index> {};
                         for (const auto index : self.reached_fluent_atoms)
                         {
                             indices.push_back(index);
                         }
                         return indices;
                     })
        .def_prop_ro("reached_derived_atoms",
                     [](const iw::RolloutResult& self)
                     {
                         auto indices = std::vector<Index> {};
                         for (const auto index : self.reached_derived_atoms)
                         {
                             indices.push_back(index);
                         }
                         return indices;
                     })
        .def_prop_ro("co_occurrence_by_atom", [](const iw::RolloutResult& self) { return co_occurrence_as_index_lists(self.co_occurrence_by_atom); });

    /* Folds the batch without materializing pairs; see `iw::intersect_co_occurrence`. Callers
       that then want the pairs themselves should work from the rows rather than from this list
       of lists -- the row form is quadratically smaller, and building the Python list here is
       already the expensive part of the fold. */
    m.def(
        "intersect_iw_rollout_co_occurrence",
        [](const std::vector<iw::RolloutResult>& results) { return co_occurrence_as_index_lists(iw::intersect_co_occurrence(results)); },
        "results"_a);

    // The GIL is released for the whole batch. This is only sound because the batch takes no
    // Python callbacks -- unlike `find_solution_iw`, whose event handlers may be Python
    // objects and which therefore has to keep holding the GIL.
    m.def(
        "find_rollouts_iw_parallel",
        [](const SearchContext& search_context, const iw::ParallelRolloutOptions& options)
        {
            nb::gil_scoped_release release;
            return iw::find_rollouts_parallel(search_context, options);
        },
        "search_context"_a,
        "options"_a);

    // Must run on this thread, after the batch has joined: it mutates `target` and the states
    // it creates there carry pooled handles with non-atomic refcounts.
    m.def(
        "migrate_iw_rollout_landing_states",
        [](const std::vector<iw::RolloutResult>& results, StateRepository& target) { return iw::migrate_landing_states(results, target); },
        "results"_a,
        "target"_a);

    /* True Rollout IW(1) (Bandres, Bonet, Geffner) and the atomic-goal portfolio built on it.
       See docs/ROLLOUT_IW_IMPLEMENTATION_PLAN.md. Distinct from `find_rollouts_iw_parallel` above,
       which runs K ordinary IW searches with randomized layer orderings. */

    nb::enum_<rollout_iw::ActionOrderingKind>(m, "RolloutIWActionOrderingKind")
        .value("IN_ORDER", rollout_iw::ActionOrderingKind::IN_ORDER)
        .value("RANDOMIZED", rollout_iw::ActionOrderingKind::RANDOMIZED)
        .value("DIRECT_GOAL_ACHIEVER_FIRST", rollout_iw::ActionOrderingKind::DIRECT_GOAL_ACHIEVER_FIRST)
        .value("GOAL_REGRESSION_RELEVANCE", rollout_iw::ActionOrderingKind::GOAL_REGRESSION_RELEVANCE)
        .value("MIXED_REGRESSION_RANDOM", rollout_iw::ActionOrderingKind::MIXED_REGRESSION_RANDOM);

    nb::class_<rollout_iw::ActionOrderingConfiguration>(m, "RolloutIWActionOrderingConfiguration")  //
        .def(nb::init<>())
        .def(nb::init<rollout_iw::ActionOrderingKind, uint64_t>(), "kind"_a, "seed"_a = 0)
        .def_rw("kind", &rollout_iw::ActionOrderingConfiguration::kind)
        .def_rw("seed", &rollout_iw::ActionOrderingConfiguration::seed);

    nb::class_<rollout_iw::PlanStep>(m, "RolloutIWPlanStep")  //
        .def_ro("schema", &rollout_iw::PlanStep::schema)
        .def_ro("binding", &rollout_iw::PlanStep::binding);

    nb::class_<rollout_iw::Options>(m, "RolloutIWOptions")  //
        .def(nb::init<>())
        .def_rw("start_state", &rollout_iw::Options::start_state)
        .def_rw("goal_condition", &rollout_iw::Options::goal_condition)
        .def_rw("goal_strategy", &rollout_iw::Options::goal_strategy)
        .def_rw("action_ordering_configuration", &rollout_iw::Options::action_ordering_configuration)
        .def_rw("seed", &rollout_iw::Options::seed)
        .def_rw("max_depth", &rollout_iw::Options::max_depth)
        .def_rw("max_rollouts", &rollout_iw::Options::max_rollouts)
        .def_rw("max_num_states", &rollout_iw::Options::max_num_states)
        .def_rw("max_time_in_ms", &rollout_iw::Options::max_time_in_ms)
        .def_rw("incumbent_bound", &rollout_iw::Options::incumbent_bound);

    nb::class_<rollout_iw::Statistics>(m, "RolloutIWStatistics")  //
        .def(nb::init<>())
        .def_ro("num_rollouts", &rollout_iw::Statistics::num_rollouts)
        .def_ro("num_generated_states", &rollout_iw::Statistics::num_generated_states)
        .def_ro("num_expanded_nodes", &rollout_iw::Statistics::num_expanded_nodes)
        .def_ro("num_feature_depth_improvements", &rollout_iw::Statistics::num_feature_depth_improvements)
        .def_ro("num_case_1", &rollout_iw::Statistics::num_case_1)
        .def_ro("num_case_2", &rollout_iw::Statistics::num_case_2)
        .def_ro("num_case_3", &rollout_iw::Statistics::num_case_3)
        .def_ro("num_case_4", &rollout_iw::Statistics::num_case_4)
        .def_ro("num_solved_propagations", &rollout_iw::Statistics::num_solved_propagations)
        .def_ro("num_dead_ends", &rollout_iw::Statistics::num_dead_ends)
        .def_ro("num_depth_bound_prunings", &rollout_iw::Statistics::num_depth_bound_prunings)
        .def_ro("num_incumbent_bound_prunings", &rollout_iw::Statistics::num_incumbent_bound_prunings)
        .def_ro("max_rollout_depth", &rollout_iw::Statistics::max_rollout_depth)
        .def_ro("num_tree_nodes", &rollout_iw::Statistics::num_tree_nodes);

    nb::class_<rollout_iw::Result>(m, "RolloutIWResult")  //
        .def_ro("search_result", &rollout_iw::Result::search_result)
        .def_ro("statistics", &rollout_iw::Result::statistics)
        .def_ro("root_solved", &rollout_iw::Result::root_solved)
        .def_ro("plan_steps", &rollout_iw::Result::plan_steps)
        .def_ro("plan_length", &rollout_iw::Result::plan_length)
        .def_ro("stop_reason", &rollout_iw::Result::stop_reason);

    // The GIL is released for the whole search, which is only sound because nothing in it can call
    // back into Python: `reject_python_strategy` above turns a Python-subclassed goal strategy into
    // an error here, on this thread, rather than a deadlock or a crash inside the search.
    m.def(
        "find_solution_rollout_iw",
        [](const SearchContext& search_context, const rollout_iw::Options& options)
        {
            reject_python_strategy(options.goal_strategy, "RolloutIWOptions.goal_strategy");

            nb::gil_scoped_release release;
            return rollout_iw::find_solution(search_context, options);
        },
        "search_context"_a,
        "options"_a = rollout_iw::Options());

    nb::enum_<AtomicGoalPortfolioSearchMode>(m, "AtomicGoalPortfolioSearchMode")
        .value("GROUNDED", AtomicGoalPortfolioSearchMode::GROUNDED)
        .value("LIFTED_KPKC", AtomicGoalPortfolioSearchMode::LIFTED_KPKC)
        .value("INHERIT_CONTEXT", AtomicGoalPortfolioSearchMode::INHERIT_CONTEXT);

    nb::class_<iw::AtomicGoalPortfolioOptions>(m, "AtomicGoalIWPortfolioOptions")  //
        .def(nb::init<>())
        .def_rw("start_state", &iw::AtomicGoalPortfolioOptions::start_state)
        .def_rw("atomic_goal", &iw::AtomicGoalPortfolioOptions::atomic_goal)
        .def_rw("atomic_goal_atoms", &iw::AtomicGoalPortfolioOptions::atomic_goal_atoms)
        .def_rw("search_mode", &iw::AtomicGoalPortfolioOptions::search_mode)
        .def_rw("num_rollout_workers", &iw::AtomicGoalPortfolioOptions::num_rollout_workers)
        .def_rw("num_threads", &iw::AtomicGoalPortfolioOptions::num_threads)
        .def_rw("base_seed", &iw::AtomicGoalPortfolioOptions::base_seed)
        .def_rw("max_time_in_ms", &iw::AtomicGoalPortfolioOptions::max_time_in_ms)
        .def_rw("max_total_expansions", &iw::AtomicGoalPortfolioOptions::max_total_expansions)
        .def_rw("max_depth", &iw::AtomicGoalPortfolioOptions::max_depth)
        .def_rw("rollout_orderings", &iw::AtomicGoalPortfolioOptions::rollout_orderings);

    nb::class_<iw::AtomicGoalPortfolioResult>(m, "AtomicGoalIWPortfolioResult")  //
        .def_ro("status", &iw::AtomicGoalPortfolioResult::status)
        .def_ro("plan", &iw::AtomicGoalPortfolioResult::plan)
        .def_ro("plan_steps", &iw::AtomicGoalPortfolioResult::plan_steps)
        .def_ro("plan_length", &iw::AtomicGoalPortfolioResult::plan_length)
        .def_ro("certified_optimal", &iw::AtomicGoalPortfolioResult::certified_optimal)
        .def_ro("iw_lower_bound", &iw::AtomicGoalPortfolioResult::iw_lower_bound)
        .def_ro("iw_completed_depth", &iw::AtomicGoalPortfolioResult::iw_completed_depth)
        .def_ro("winning_worker", &iw::AtomicGoalPortfolioResult::winning_worker)
        .def_ro("executed_mode", &iw::AtomicGoalPortfolioResult::executed_mode)
        .def_ro("stop_reason", &iw::AtomicGoalPortfolioResult::stop_reason)
        .def_ro("certifier_status", &iw::AtomicGoalPortfolioResult::certifier_status)
        .def_ro("iw_statistics", &iw::AtomicGoalPortfolioResult::iw_statistics)
        .def_ro("rollout_statistics", &iw::AtomicGoalPortfolioResult::rollout_statistics)
        .def_ro("rollout_statuses", &iw::AtomicGoalPortfolioResult::rollout_statuses)
        .def_ro("total_expansions", &iw::AtomicGoalPortfolioResult::total_expansions);

    // Releases the GIL for the whole run, across all K+1 worker threads. Nothing in the native path
    // touches Python, and it must stay that way: a Python object reached from a worker thread while
    // the GIL is released is a crash, not an exception.
    m.def(
        "find_solution_atomic_goal_iw_portfolio",
        [](const SearchContext& search_context, const iw::AtomicGoalPortfolioOptions& options)
        {
            nb::gil_scoped_release release;
            return iw::find_solution_atomic_goal_portfolio(search_context, options);
        },
        "search_context"_a,
        "options"_a = iw::AtomicGoalPortfolioOptions());

    // SIW
    nb::class_<siw::Statistics>(m, "SIWStatistics")  //
        .def(nb::init<>())
        .def("__str__", [](const siw::Statistics& self) { return to_string(self); })
        .def("get_maximum_effective_width", &siw::Statistics::get_maximum_effective_width)
        .def("get_average_effective_width", &siw::Statistics::get_average_effective_width)
        .def("get_iw_statistics_by_subproblem", &siw::Statistics::get_iw_statistics_by_subproblem)
        .def("get_search_time_ms", &siw::Statistics::get_search_time_ms);

    nb::class_<siw::IEventHandler>(m, "ISIWEventHandler")  //
        .def("get_statistics", &siw::IEventHandler::get_statistics);
    nb::class_<siw::DefaultEventHandlerImpl, siw::IEventHandler>(m, "DefaultSIWEventHandler").def(nb::init<Problem, bool>(), "problem"_a, "quiet"_a = true);

    nb::class_<siw::Options>(m, "SIWOptions")  //
        .def(nb::init<>())
        .def_rw("start_state", &siw::Options::start_state)
        .def_rw("siw_event_handler", &siw::Options::siw_event_handler)
        .def_rw("iw_event_handler", &siw::Options::iw_event_handler)
        .def_rw("brfs_event_handler", &siw::Options::brfs_event_handler)
        .def_rw("goal_strategy", &siw::Options::goal_strategy)
        .def_rw("max_arity", &siw::Options::max_arity);

    m.def("find_solution_siw", &siw::find_solution, "search_context"_a, "options"_a);
}

}
