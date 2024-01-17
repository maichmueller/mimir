
#ifndef LITERAL_GROUNDER_HPP
#define LITERAL_GROUNDER_HPP

#include "lifted_schema_successor_generator.hpp"

#include <mimir/formalism/declarations.hpp>

namespace mimir::planners
{
    /// @brief A class that enables enumerating all ground atoms (and bindings) of a conjunction of literals that are true in the given state.
    class LiteralGrounder
    {
      private:
        mimir::formalism::ActionSchema action_schema_;
        std::unique_ptr<mimir::planners::LiftedSchemaSuccessorGenerator> generator_;

      public:
        LiteralGrounder(const mimir::formalism::ProblemDescription& problem, const mimir::formalism::AtomList& atom_list);

        std::vector<std::pair<mimir::formalism::AtomList, std::vector<std::pair<std::string, mimir::formalism::Object>>>>
        ground(const mimir::formalism::State& state);
    };

}  // namespace mimir::planners

#endif  // LITERAL_GROUNDER_HPP
