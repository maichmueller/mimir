
#pragma once

#include "mimir/datasets/state_space.hpp"
#include "mimir/formalism/atom.hpp"
#include "mimir/formalism/declarations.hpp"
#include "mimir/formalism/factories.hpp"
#include "mimir/formalism/ground_atom.hpp"
#include "mimir/formalism/object.hpp"
#include "mimir/formalism/predicate.hpp"
#include "mimir/formalism/predicate_category.hpp"
#include "mimir/formalism/requirements.hpp"
#include "mimir/formalism/term.hpp"
#include "mimir/formalism/variable.hpp"
#include "mimir/utils/utils.hpp"
#include "opaque_types.hpp"

#include <any>
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <fmt/ranges.h>
#include <pybind11/detail/common.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>  // Necessary for automatic conversion of e.g. std::vectors
#include <pybind11/stl_bind.h>
#define STRINGIFY(x) #x
#define MACRO_STRINGIFY(x) STRINGIFY(x)

#ifndef CONST_OVERLOAD
#define CONST_OVERLOAD(func, ...) py::overload_cast<__VA_ARGS__>(&func, py::const_)
#endif

#include "mimir/mimir.hpp"

//
// utilities
//

struct default_elem_repr
{
    template<typename T>
    std::string operator()(const T& elem) const
    {
        return mimir::deref(elem).str();
    }
};

template<typename BoundVectorType, typename ReprFunctor = default_elem_repr>
inline auto& def_opaque_vector_repr(auto& cls, const std::string& class_name, ReprFunctor elem_repr = ReprFunctor {})
{
    return cls;
}

template<typename BoundVectorType, typename ReprFunctor = default_elem_repr>
    requires requires(typename BoundVectorType::value_type elem) {
        { mimir::deref(elem).str() } -> std::convertible_to<std::string>;
    }
inline auto& def_opaque_vector_repr(auto& cls, const std::string& class_name, ReprFunctor elem_repr = ReprFunctor {})
{
    return cls.def("__str__",
                   [=](const BoundVectorType& self) { return fmt::format("{}[{}]", class_name, fmt::join(std::views::transform(self, elem_repr), ",")); });
}

template<typename Class_, typename... Options>
py::class_<Class_, Options...> class_(pybind11::module_& m, const char* cls_name)
{
    if (py::hasattr(m, cls_name))
    {
        return py::class_<Class_, Options...>(m.attr(cls_name));
    }
    return py::class_<Class_, Options...>(m, cls_name);
}


//
// init - declarations:
//
#ifndef DECLARE_INIT_FUNC
#define DECLARE_INIT_FUNC(name) void init_##name(pybind11::module_& m)
#endif

DECLARE_INIT_FUNC(pymimir);
DECLARE_INIT_FUNC(enums);
DECLARE_INIT_FUNC(aag);
DECLARE_INIT_FUNC(atoms);
DECLARE_INIT_FUNC(ground_atoms);
DECLARE_INIT_FUNC(nauty_wrappers);
DECLARE_INIT_FUNC(conditional_effect);
DECLARE_INIT_FUNC(flatbitset);
DECLARE_INIT_FUNC(algorithms);
DECLARE_INIT_FUNC(literals);
DECLARE_INIT_FUNC(axiom);
DECLARE_INIT_FUNC(predicates);
DECLARE_INIT_FUNC(numeric_fluent);
DECLARE_INIT_FUNC(object);
DECLARE_INIT_FUNC(variable);
DECLARE_INIT_FUNC(termvariable);
DECLARE_INIT_FUNC(termvariant);
DECLARE_INIT_FUNC(termobject);
DECLARE_INIT_FUNC(requirements);
DECLARE_INIT_FUNC(function);
DECLARE_INIT_FUNC(function_expression);
DECLARE_INIT_FUNC(ground_function_expression);
DECLARE_INIT_FUNC(optimization_metric);
DECLARE_INIT_FUNC(effects);
DECLARE_INIT_FUNC(actions);
DECLARE_INIT_FUNC(domain);
DECLARE_INIT_FUNC(problem);
DECLARE_INIT_FUNC(pddl_factories);
DECLARE_INIT_FUNC(state);
DECLARE_INIT_FUNC(state_space);
DECLARE_INIT_FUNC(strips_action_precondition);
DECLARE_INIT_FUNC(abstraction);
DECLARE_INIT_FUNC(tuple_graph);
DECLARE_INIT_FUNC(static_digraph);
DECLARE_INIT_FUNC(static_vertexcolored_graph);
DECLARE_INIT_FUNC(heuristics);
