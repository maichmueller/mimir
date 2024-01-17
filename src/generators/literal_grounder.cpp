

#include "mimir/generators/literal_grounder.hpp"

namespace mimir::planners
{

    LiteralGrounder::LiteralGrounder(const mimir::formalism::ProblemDescription& problem, const mimir::formalism::AtomList& atom_list) :
        action_schema_(nullptr),
        generator_(nullptr)
    {
        std::map<std::string, mimir::formalism::Parameter> parameter_map;
        mimir::formalism::ParameterList parameters;
        mimir::formalism::LiteralList precondition;

        // Create new parameters

        for (const auto& atom : atom_list)
        {
            for (const auto& term : atom->arguments)
            {
                if (term->is_free_variable() && !parameter_map.count(term->name))
                {
                    const auto id = static_cast<uint32_t>(parameter_map.size());
                    const auto new_parameter = mimir::formalism::create_object(id, term->name, term->type);
                    parameter_map.emplace(term->name, new_parameter);
                    parameters.emplace_back(new_parameter);
                }
            }
        }

        // Create new atoms

        for (const auto& atom : atom_list)
        {
            mimir::formalism::ParameterList new_terms;

            for (const auto& term : atom->arguments)
            {
                if (term->is_free_variable())
                {
                    new_terms.emplace_back(parameter_map.at(term->name));
                }
                else
                {
                    new_terms.emplace_back(term);
                }
            }

            const auto new_atom = mimir::formalism::create_atom(atom->predicate, new_terms);
            const auto new_literal = mimir::formalism::create_literal(new_atom, false);
            precondition.emplace_back(new_literal);
        }

        // Create action schema

        const auto unit_cost = mimir::formalism::create_unit_cost_function(problem->domain);
        action_schema_ = mimir::formalism::create_action_schema("dummy", parameters, precondition, {}, {}, unit_cost);
        generator_ = std::make_unique<mimir::planners::LiftedSchemaSuccessorGenerator>(action_schema_, problem);
    }


    std::vector<std::pair<mimir::formalism::AtomList, std::vector<std::pair<std::string, mimir::formalism::Object>>>>
    LiteralGrounder::ground(const mimir::formalism::State& state)
    {
        const auto matches = generator_->get_applicable_actions(state);

        std::vector<std::pair<mimir::formalism::AtomList, std::vector<std::pair<std::string, mimir::formalism::Object>>>> instantiations_and_bindings;

        for (const auto& match : matches)
        {
            const auto& arguments = match->get_arguments();

            mimir::formalism::AtomList instantiation;
            std::vector<std::pair<std::string, mimir::formalism::Object>> binding;

            for (const auto& literal : match->get_precondition())
            {
                instantiation.emplace_back(literal->atom);
            }

            for (std::size_t index = 0; index < action_schema_->parameters.size(); ++index)
            {
                const auto& parameter = action_schema_->parameters.at(index);
                const auto& object = arguments.at(index);
                binding.emplace_back(parameter->name, object);
            }

            instantiations_and_bindings.emplace_back(std::make_pair(std::move(instantiation), std::move(binding)));
        }

        return instantiations_and_bindings;
    }
}  // namespace mimir::planners