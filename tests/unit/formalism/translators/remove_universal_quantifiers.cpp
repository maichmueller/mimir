#include "mimir/formalism/parser.hpp"
#include "mimir/formalism/translators.hpp"

#include <gtest/gtest.h>
#include <loki/parser.hpp>

namespace mimir::tests
{

TEST(MimirTests, FormalismTranslatorsRemoveUniversalQuantifiers)
{
    const auto domain_file = fs::path(std::string(DATA_DIR) + "miconic-fulladl/domain.pddl");
    const auto problem_file = fs::path(std::string(DATA_DIR) + "miconic-fulladl/problem.pddl");

    auto domain_parser = loki::DomainParser(domain_file);
    auto problem_parser = loki::ProblemParser(problem_file, domain_parser);

    auto parser = PDDLParser(domain_file, problem_file);
    auto domain = parser.get_domain();
    auto problem = parser.get_problem();

    std::cout << "\nInput domain and problem" << std::endl;
    std::cout << *domain << std::endl;
    std::cout << *problem << std::endl;

    auto remove_types_translator = RemoveTypesTranslator(parser.get_factories());
    auto translated_problem = remove_types_translator.run(*problem);
    auto translated_domain = translated_problem->get_domain();

    auto to_nnf_translator = ToNNFTranslator(parser.get_factories());
    translated_problem = to_nnf_translator.run(*translated_problem);
    translated_domain = translated_problem->get_domain();

    auto remove_universal_quantifiers_translator = RemoveUniversalQuantifiersTranslator(parser.get_factories(), to_nnf_translator);
    translated_problem = remove_universal_quantifiers_translator.run(*translated_problem);
    translated_domain = translated_problem->get_domain();

    std::cout << "\nTranslated domain and problem" << std::endl;
    std::cout << *translated_problem->get_domain() << std::endl;
    std::cout << *translated_problem << std::endl;
}

}