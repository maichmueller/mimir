
#include "init_declarations.hpp"
#include "mimir/mimir.hpp"
#include "utils.hpp"
#include "variants.hpp"
#include "opaque_types.hpp"

#include <pybind11/pybind11.h>
#include <range/v3/all.hpp>

namespace py = pybind11;

using namespace mimir;
using namespace mimir::pymimir;


void init_pddl_repositories(py::module& m)
{

    auto&& pddl_repositories = class_<PDDLRepositories, std::shared_ptr<PDDLRepositories>>(m, "PDDLRepositories");  //
    pddl_repositories
        .def("get_ground_atoms",
             [](const py::object& py_repo)  // we need an object handle to keep the factory alive for each atom in the list
             {
                 const auto& repo = py::cast<const PDDLRepositories&>(py_repo);
                 const auto& static_atom_factory = repo.get_pddl_type_to_factory<GroundAtomFactory<Static>>();
                 const auto& fluent_atom_factory = repo.get_factory<GroundAtomFactory<Fluent>>();
                 const auto& derived_atom_factory = repo.get_factory<GroundAtomFactory<Derived>>();

                 py::list all_atoms(static_atom_factory.size() + fluent_atom_factory.size() + derived_atom_factory.size());
                 size_t i = 0;
                 insert_into_list(py_repo, all_atoms, static_atom_factory, i);
                 insert_into_list(py_repo, all_atoms, fluent_atom_factory, i);
                 insert_into_list(py_repo, all_atoms, derived_atom_factory, i);
                 return all_atoms;
             })
        .def("get_static_ground_atom", &PDDLRepositories::get_ground_atom<Static>, py::return_value_policy::reference_internal)
        .def("get_fluent_ground_atom", &PDDLRepositories::get_ground_atom<Fluent>, py::return_value_policy::reference_internal)
        .def("get_derived_ground_atom", &PDDLRepositories::get_ground_atom<Derived>, py::return_value_policy::reference_internal)
        .def("get_static_ground_atoms_from_indices",
             py::overload_cast<const std::vector<size_t>&>(&PDDLRepositories::get_ground_atoms_from_indices<Static, std::vector<size_t>>, py::const_),
             py::keep_alive<0, 1>())
        .def("get_fluent_ground_atoms_from_indices",
             py::overload_cast<const std::vector<size_t>&>(&PDDLRepositories::get_ground_atoms_from_indices<Fluent, std::vector<size_t>>, py::const_),
             py::keep_alive<0, 1>())
        .def("get_derived_ground_atoms_from_indices",
             py::overload_cast<const std::vector<size_t>&>(&PDDLRepositories::get_ground_atoms_from_indices<Derived, std::vector<size_t>>, py::const_),
             py::keep_alive<0, 1>())
        .def("get_object", &PDDLRepositories::get_object, py::return_value_policy::reference_internal);

    auto bind_ground_atoms_range = [&]<typename Tag>(std::string func_name, Tag)
    {
        pddl_repositories.def(
            func_name.data(),
            [=](const PDDLRepositories& factory) { return ranges::to<GroundAtomList<Tag>>(factory.get_factory<GroundAtomFactory<Tag>>()); },
            py::keep_alive<0, 1>());
    };
    bind_ground_atoms_range("get_static_ground_atoms", Static {});
    bind_ground_atoms_range("get_fluent_ground_atoms", Fluent {});
    bind_ground_atoms_range("get_derived_ground_atoms", Derived {});
}