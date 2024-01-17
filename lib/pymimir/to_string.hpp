
#ifndef PYMIMIR_TO_STRING_HPP
#define PYMIMIR_TO_STRING_HPP

#include "mimir/formalism/atom.hpp"
#include "mimir/formalism/action.hpp"

inline std::string to_string(const mimir::formalism::AtomImpl& state)
{
    std::string repr = state.predicate->name + "(";

    for (std::size_t index = 0; index < state.arguments.size(); ++index)
    {
        repr += state.arguments[index]->name;

        if ((index + 1) < state.arguments.size())
        {
            repr += ", ";
        }
    }

    return repr + ")";
}

inline std::string to_string(const mimir::formalism::ActionImpl& action)
{
    std::string repr = action.schema->name + "(";
    const auto& action_arguments = action.get_arguments();

    for (std::size_t index = 0; index < action_arguments.size(); ++index)
    {
        repr += action_arguments[index]->name;

        if ((index + 1) < action_arguments.size())
        {
            repr += ", ";
        }
    }

    return repr + ")";
}


#endif //PYMIMIR_TO_STRING_HPP
