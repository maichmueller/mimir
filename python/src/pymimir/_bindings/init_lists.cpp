
#include "init_declarations.hpp"
#include "mimir/common/itertools.hpp"
#include "opaque_types.hpp"
#include "utils.hpp"

#include <pybind11/pybind11.h>

namespace py = pybind11;

using namespace pymimir;

void init_lists(py::module& m)
{
    def_opaque_vector_repr<GroundActionList>(m, "GroundActionList");
    bind_const_span<std::span<const GroundAction>>(m, "GroundActionSpan");
    static_assert(!py::detail::vector_needs_copy<GroundActionList>::value);  // Ensure return by reference + keep alive

    for_each_tag(
        [&]<typename Tag>(Tag)
        {
            static_assert(!py::detail::vector_needs_copy<AtomList<Tag>>::value);
            std::string class_name = tag_name<Tag>() + "AtomList";
            def_opaque_vector_repr<AtomList<Tag>>(m, class_name);
        });

    static_assert(!py::detail::vector_needs_copy<EffectComplexList>::value);  // Ensure return by reference + keep alive
    def_opaque_vector_repr<EffectComplexList>(m, "EffectComplexList");

    static_assert(!py::detail::vector_needs_copy<GroundFunctionExpressionList>::value);  // Ensure return by reference + keep alive
    def_opaque_vector_repr<GroundFunctionExpressionList>(m, "GroundFunctionExpressionList");

    static_assert(!py::detail::vector_needs_copy<StateList>::value);  // Ensure return by reference + keep alive
    def_opaque_vector_repr<StateList>(m, "StateList");
    bind_const_span<std::span<const State>>(m, "StateSpan");
    bind_const_index_grouped_vector<IndexGroupedVector<const State>>(m, "IndexGroupedVector");

    static_assert(!py::detail::vector_needs_copy<ActionList>::value);  // Ensure return by reference + keep alive
    def_opaque_vector_repr<ActionList>(m, "ActionList");

    static_assert(!py::detail::vector_needs_copy<ProblemList>::value);  // Ensure return by reference + keep alive
    def_opaque_vector_repr<ProblemList>(m, "ProblemList");

    static_assert(!py::detail::vector_needs_copy<AxiomList>::value);  // Ensure return by reference + keep alive
    def_opaque_vector_repr<AxiomList>(m, "AxiomList");

    static_assert(!py::detail::vector_needs_copy<VariableList>::value);  // Ensure return by reference + keep alive
    def_opaque_vector_repr<VariableList>(m, "VariableList");

    static_assert(!py::detail::vector_needs_copy<DomainList>::value);  // Ensure return by reference + keep alive
    def_opaque_vector_repr<DomainList>(m, "DomainList");

    static_assert(!py::detail::vector_needs_copy<FunctionExpressionList>::value);  // Ensure return by reference + keep alive
    def_opaque_vector_repr<FunctionExpressionList>(m, "FunctionExpressionList");

    static_assert(!py::detail::vector_needs_copy<FunctionSkeletonList>::value);  // Ensure return by reference + keep alive
    def_opaque_vector_repr<FunctionSkeletonList>(m, "FunctionSkeletonList");

    static_assert(!py::detail::vector_needs_copy<FunctionList>::value);  // Ensure return by reference + keep alive
    def_opaque_vector_repr<FunctionList>(m, "FunctionList");

    static_assert(!py::detail::vector_needs_copy<GroundFunctionList>::value);  // Ensure return by reference + keep alive
    def_opaque_vector_repr<GroundFunctionList>(m, "GroundFunctionList");

    static_assert(!py::detail::vector_needs_copy<NumericFluentList>::value);  // Ensure return by reference + keep alive
    def_opaque_vector_repr<NumericFluentList>(m, "NumericFluentList");

    // PredicateList
    for_each_tag(
        [&]<typename Tag>(Tag)
        {
            static_assert(!py::detail::vector_needs_copy<PredicateList<Tag>>::value);
            std::string class_name = tag_name<Tag>() + "PredicateList";
            def_opaque_vector_repr<PredicateList<Tag>>(m, class_name);
            ;
        });

    {
        static_assert(!py::detail::vector_needs_copy<ObjectList>::value);  // Ensure return by reference + keep alive
        def_opaque_vector_repr<ObjectList>(m, "ObjectList");
    }

    {
        static_assert(!py::detail::vector_needs_copy<TermList>::value);  // Ensure return by reference + keep alive
        def_opaque_vector_repr<TermList>(m, "TermList");
    }

    // LiteralList
    for_each_tag(
        [&]<typename Tag>(Tag)
        {
            static_assert(!py::detail::vector_needs_copy<LiteralList<Tag>>::value);
            std::string class_name = tag_name<Tag>() + "LiteralList";
            def_opaque_vector_repr<LiteralList<Tag>>(m, class_name);
        });

    // GroundLiteralList
    for_each_tag(
        [&]<typename Tag>(Tag)
        {
            static_assert(!py::detail::vector_needs_copy<GroundLiteralList<Tag>>::value);
            std::string class_name = tag_name<Tag>() + "GroundLiteralList";
            auto list_class = class_<GroundLiteralList<Tag>>(m, class_name.c_str())
                                  .def(
                                      "lift",
                                      [](const GroundLiteralList<Tag>& ground_literals, PDDLRepositories& pddl_repositories)
                                      { return lift(ground_literals, pddl_repositories); },
                                      py::arg("pddl_repositories"));
            def_opaque_vector_repr<GroundLiteralList<Tag>>(list_class, class_name);
        });

    // GroundAtomList
    for_each_tag(
        [&]<typename Tag>(Tag)
        {
            static_assert(!py::detail::vector_needs_copy<GroundAtomList<Tag>>::value);
            std::string class_name = tag_name<Tag>() + "GroundAtomList";
            auto list_class =
                class_<GroundAtomList<Tag>>(m, class_name.c_str())
                    .def(
                        "lift",
                        [](const GroundAtomList<Tag>& ground_atoms, PDDLRepositories& pddl_repositories) { return lift(ground_atoms, pddl_repositories); },
                        py::arg("pddl_repositories"));
            def_opaque_vector_repr<GroundAtomList<Tag>>(list_class, class_name);
        });
}